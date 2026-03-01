# 11.6 Zero-Copy Design Patterns

RDMA's fundamental promise is zero-copy data transfer: data moves directly between application buffers without intermediate copies in the kernel, network stack, or protocol processing layers. But achieving true zero-copy in practice requires careful application design. A naive RDMA application can easily introduce unnecessary copies -- marshalling data into send buffers, copying received data into application structures, or allocating and registering memory on every operation. This section presents the design patterns that eliminate these copies.

## Pre-Registered Buffer Pools

Memory registration is expensive. Calling `ibv_reg_mr()` involves pinning pages, programming IOMMU translations, and updating NIC page tables. On a typical system, registering a 4 KB buffer takes 5-15 microseconds -- orders of magnitude more than posting a send. Registering memory in the fast path is a performance killer.

The solution is to register a large memory region once during initialization and carve out buffers from it as needed:

```c
#define POOL_SIZE       (64 * 1024 * 1024)  /* 64 MB */
#define BUFFER_SIZE     4096
#define NUM_BUFFERS     (POOL_SIZE / BUFFER_SIZE)

struct buffer_pool {
    void *base;                   /* Pool base address */
    struct ibv_mr *mr;            /* Single MR covering entire pool */
    int free_list[NUM_BUFFERS];   /* Stack of free buffer indices */
    int free_count;               /* Number of free buffers */
    pthread_spinlock_t lock;      /* Protects free_list */
};

int buffer_pool_init(struct buffer_pool *pool, struct ibv_pd *pd)
{
    /* Allocate aligned memory */
    pool->base = aligned_alloc(4096, POOL_SIZE);
    if (!pool->base) return -1;

    /* Register entire pool as a single MR */
    pool->mr = ibv_reg_mr(pd, pool->base, POOL_SIZE,
        IBV_ACCESS_LOCAL_WRITE |
        IBV_ACCESS_REMOTE_WRITE |
        IBV_ACCESS_REMOTE_READ);
    if (!pool->mr) {
        free(pool->base);
        return -1;
    }

    /* Initialize free list */
    pool->free_count = NUM_BUFFERS;
    for (int i = 0; i < NUM_BUFFERS; i++) {
        pool->free_list[i] = i;
    }
    pthread_spin_init(&pool->lock, PTHREAD_PROCESS_PRIVATE);

    return 0;
}
```

### Buffer Allocation and Deallocation

Allocating a buffer from the pool is a simple stack pop:

```c
void *buffer_pool_alloc(struct buffer_pool *pool, uint32_t *lkey)
{
    pthread_spin_lock(&pool->lock);
    if (pool->free_count == 0) {
        pthread_spin_unlock(&pool->lock);
        return NULL;
    }
    int idx = pool->free_list[--pool->free_count];
    pthread_spin_unlock(&pool->lock);

    *lkey = pool->mr->lkey;  /* Same lkey for all buffers in pool */
    return (char *)pool->base + idx * BUFFER_SIZE;
}

void buffer_pool_free(struct buffer_pool *pool, void *buf)
{
    int idx = ((char *)buf - (char *)pool->base) / BUFFER_SIZE;

    pthread_spin_lock(&pool->lock);
    pool->free_list[pool->free_count++] = idx;
    pthread_spin_unlock(&pool->lock);
}
```

The key insight is that every buffer from the pool shares the same `lkey` (and the same `rkey` for remote access), because they all reside within the same registered memory region. This eliminates per-buffer registration overhead entirely.

### Lock-Free Pool with Ring Buffer

For higher performance, replace the locked free list with a lock-free ring buffer:

```c
struct lockfree_pool {
    void *base;
    struct ibv_mr *mr;
    _Atomic uint32_t head;           /* Consumer (alloc) pointer */
    _Atomic uint32_t tail;           /* Producer (free) pointer */
    uint32_t indices[NUM_BUFFERS];   /* Ring buffer of free indices */
};

void *lockfree_pool_alloc(struct lockfree_pool *pool) {
    uint32_t head = atomic_load_explicit(&pool->head,
                                          memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&pool->tail,
                                          memory_order_acquire);
    if (head == tail)
        return NULL;  /* Pool empty */

    uint32_t idx = pool->indices[head % NUM_BUFFERS];
    atomic_store_explicit(&pool->head, head + 1,
                          memory_order_release);
    return (char *)pool->base + idx * BUFFER_SIZE;
}
```

<div class="tip">

**Tip**: For single-producer, single-consumer scenarios (one thread allocates, one thread frees), this lock-free ring buffer is sufficient. For multi-producer, multi-consumer patterns, use a compare-and-swap loop or a per-thread pool with periodic rebalancing.

</div>

## Scatter-Gather Lists

RDMA natively supports scatter-gather I/O through the `ibv_sge` (Scatter-Gather Element) array in work requests. This allows sending data from multiple non-contiguous buffers in a single operation, or receiving into a buffer with a header/payload split:

```c
/* Send a message composed of a header and payload
   from different memory locations */
struct msg_header hdr = {
    .type = MSG_DATA,
    .seq_num = seq++,
    .payload_len = data_len
};

struct ibv_sge sge_list[2] = {
    {   /* Header from stack (using inline would be even better) */
        .addr = (uint64_t)(uintptr_t)&hdr,
        .length = sizeof(hdr),
        .lkey = hdr_mr->lkey
    },
    {   /* Payload from application buffer */
        .addr = (uint64_t)(uintptr_t)app_data,
        .length = data_len,
        .lkey = data_mr->lkey
    }
};

struct ibv_send_wr wr = {
    .sg_list = sge_list,
    .num_sge = 2,
    .opcode = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED
};
```

Without scatter-gather, the application would need to copy the header and payload into a contiguous buffer before sending. Scatter-gather eliminates this copy by allowing the NIC to gather data from multiple locations.

Similarly, on the receive side, scatter-gather can split incoming data:

```c
/* Receive with header/payload split */
struct ibv_sge recv_sge[2] = {
    {   /* Header goes here */
        .addr = (uint64_t)(uintptr_t)hdr_buf,
        .length = sizeof(struct msg_header),
        .lkey = hdr_mr->lkey
    },
    {   /* Payload goes here */
        .addr = (uint64_t)(uintptr_t)payload_buf,
        .length = MAX_PAYLOAD,
        .lkey = payload_mr->lkey
    }
};

struct ibv_recv_wr recv_wr = {
    .sg_list = recv_sge,
    .num_sge = 2
};
```

## Pipelining: Overlap Computation and Communication

True zero-copy extends beyond eliminating memory copies -- it also means eliminating idle time. Pipelining overlaps computation with communication so that the CPU processes one data chunk while the NIC transfers the next:

```c
/* Simple two-stage pipeline:
   While the NIC transfers chunk N+1,
   the CPU processes chunk N */

void pipeline_transfer(struct buffer_pool *pool,
                        struct ibv_qp *qp,
                        struct ibv_cq *cq,
                        void *data, size_t total_len)
{
    size_t chunk_size = BUFFER_SIZE;
    void *buf[2];
    buf[0] = buffer_pool_alloc(pool, &lkey);
    buf[1] = buffer_pool_alloc(pool, &lkey);

    /* Start first transfer */
    memcpy(buf[0], data, chunk_size);
    post_send(qp, buf[0], chunk_size, lkey);

    size_t offset = chunk_size;
    int current = 1;
    int previous = 0;

    while (offset < total_len) {
        /* Prepare next chunk while current one is in flight */
        size_t len = min(chunk_size, total_len - offset);
        memcpy(buf[current], data + offset, len);

        /* Wait for previous transfer to complete */
        poll_completion(cq);

        /* Start next transfer */
        post_send(qp, buf[current], len, lkey);

        offset += len;
        previous = current;
        current = 1 - current;  /* Toggle between 0 and 1 */
    }

    /* Wait for last transfer */
    poll_completion(cq);

    buffer_pool_free(pool, buf[0]);
    buffer_pool_free(pool, buf[1]);
}
```

## Double and Triple Buffering

The pipelining concept extends to double and triple buffering, where multiple buffers rotate through different stages:

```c
/*
 * Triple buffering for maximum overlap:
 *
 * Stage 1: Application fills buffer A with data
 * Stage 2: NIC transfers buffer B to remote peer
 * Stage 3: Remote peer processes buffer C
 *
 * All three stages execute concurrently.
 */

struct triple_buffer {
    void *bufs[3];
    int fill_idx;      /* Buffer being filled by application */
    int transfer_idx;  /* Buffer being transferred by NIC */
    int process_idx;   /* Buffer being processed by remote */
};

void triple_buffer_rotate(struct triple_buffer *tb,
                           struct ibv_qp *qp,
                           struct ibv_cq *cq)
{
    /* Wait for the transfer of the current transfer buffer */
    if (tb->transfer_idx >= 0) {
        poll_completion(cq);
    }

    /* Rotate: fill -> transfer -> process -> fill */
    int tmp = tb->process_idx;
    tb->process_idx = tb->transfer_idx;
    tb->transfer_idx = tb->fill_idx;
    tb->fill_idx = tmp;

    /* Start transferring the newly filled buffer */
    post_rdma_write(qp, tb->bufs[tb->transfer_idx],
                    BUFFER_SIZE, remote_addr, remote_rkey);
}
```

Double buffering is sufficient when only two stages overlap. Triple buffering adds a third stage and is useful when the remote side also needs time to process data before its buffer can be reused.

## Application-Level Flow Control

RDMA Write is a one-sided operation: the remote CPU is not involved and does not know when a write completes. This creates a flow control problem -- the sender can overwrite data that the receiver has not yet processed. Several patterns address this:

### Credit-Based Flow Control

The receiver grants "credits" to the sender, each credit representing one buffer that the sender may write into. The sender decrements its credit count with each write and replenishes it when the receiver sends acknowledgments:

```c
struct flow_control {
    _Atomic int32_t credits;     /* Available write slots */
    uint64_t remote_addrs[16];   /* Circular buffer of remote addrs */
    int next_slot;               /* Next slot to write into */
};

int send_with_flow_control(struct flow_control *fc,
                            struct ibv_qp *qp,
                            void *data, size_t len)
{
    /* Wait for a credit */
    while (atomic_load(&fc->credits) <= 0) {
        /* Spin or poll for credit replenishment */
        check_for_credit_updates(fc);
    }

    atomic_fetch_sub(&fc->credits, 1);

    /* Write to the next available remote slot */
    uint64_t remote_addr = fc->remote_addrs[fc->next_slot];
    post_rdma_write(qp, data, len, remote_addr, remote_rkey);

    fc->next_slot = (fc->next_slot + 1) % 16;
    return 0;
}
```

### Completion Notification via RDMA Write with Immediate

The sender can notify the receiver using RDMA Write with Immediate Data. The write delivers the data, and the immediate value generates a completion on the receiver's CQ, waking the receiver:

```c
/* Sender: write data AND notify receiver */
struct ibv_send_wr wr = {
    .opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
    .send_flags = IBV_SEND_SIGNALED,
    .imm_data = htonl(slot_index),  /* Tell receiver which slot */
    .wr.rdma = {
        .remote_addr = remote_slot_addr,
        .rkey = remote_rkey
    },
    .sg_list = &sge,
    .num_sge = 1
};
ibv_post_send(qp, &wr, &bad_wr);

/* Receiver: poll CQ to learn about incoming writes */
struct ibv_wc wc;
if (ibv_poll_cq(recv_cq, 1, &wc) > 0) {
    if (wc.opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
        int slot = ntohl(wc.imm_data);
        process_data(recv_slots[slot]);
        /* Grant credit back to sender */
        send_credit(credit_qp, slot);
    }
}
```

### Polling a Shared Flag

For simpler cases, the sender can write a "done" flag to a known location in the receiver's memory after writing the data:

```c
/* Sender: write data, then write flag */
post_rdma_write(qp, data, len, remote_data_addr, rkey);
/* Use a fence to ensure ordering */
uint32_t done = 1;
post_rdma_write_with_fence(qp, &done, sizeof(done),
                            remote_flag_addr, rkey);

/* Receiver: poll the flag */
volatile uint32_t *flag = (volatile uint32_t *)flag_addr;
while (*flag == 0) {
    _mm_pause();  /* Reduce power consumption while spinning */
}
*flag = 0;  /* Reset for next message */
process_data(data_buffer);
```

<div class="warning">

**Ordering**: RDMA does not guarantee that writes from the same QP are visible in order at the remote side unless you use the `IBV_SEND_FENCE` flag or a single WR with scatter-gather. Without fencing, the flag might be visible before the data, causing the receiver to read stale data.

</div>

## Slab Allocator for Variable-Size Messages

When messages vary in size, a fixed-size buffer pool wastes memory. A slab allocator creates pools for different size classes:

```c
struct slab_allocator {
    struct buffer_pool pools[NUM_SIZE_CLASSES];
    size_t size_classes[NUM_SIZE_CLASSES];
    /* e.g., 64, 256, 1024, 4096, 16384, 65536 */
};

void *slab_alloc(struct slab_allocator *slab, size_t size,
                 uint32_t *lkey)
{
    /* Find the smallest size class that fits */
    for (int i = 0; i < NUM_SIZE_CLASSES; i++) {
        if (size <= slab->size_classes[i]) {
            return buffer_pool_alloc(&slab->pools[i], lkey);
        }
    }
    return NULL;  /* Too large for any pool */
}
```

All buffers within each size class share the same MR (registered during initialization), so there is zero registration overhead regardless of allocation pattern.

## Design Principles Summary

The zero-copy patterns in this section follow several core principles:

1. **Register once, use many times**: Pre-register large memory regions and sub-allocate from them. Never register memory in the data path.

2. **Avoid data copies**: Use scatter-gather to compose messages from non-contiguous sources. Design data structures so that application buffers can be sent directly without marshalling.

3. **Overlap computation and communication**: Use pipelining and double/triple buffering to keep both the CPU and the NIC busy simultaneously.

4. **Design for the NIC**: Place buffers in NUMA-local memory, align to cache lines and page boundaries, and prefer large contiguous registrations over many small ones.

5. **Manage flow control explicitly**: RDMA provides no built-in flow control for one-sided operations. The application must implement credits, notifications, or polling to prevent buffer overruns.

These patterns form the foundation of high-performance RDMA application design. They appear in every production RDMA system, from distributed storage engines to HPC communication libraries to machine learning training frameworks. Mastering them is the key to building applications that fully realize RDMA's performance potential.
