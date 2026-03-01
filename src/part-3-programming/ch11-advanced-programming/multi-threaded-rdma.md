# 11.5 Multi-Threaded RDMA

Modern RDMA NICs can deliver 200 Gbps or more of bandwidth and tens of millions of messages per second. Saturating this capacity from a single CPU core is often impossible -- a single core simply cannot post work requests and process completions fast enough. Multi-threaded RDMA programming is therefore essential for achieving full hardware utilization.

However, RDMA's low-level nature means that thread safety is not automatic. The libibverbs API has specific thread safety guarantees and limitations that the programmer must understand. Getting multi-threaded RDMA wrong can result in data corruption, missed completions, or -- more subtly -- performance far below single-threaded levels due to lock contention.

## Thread Safety in libibverbs

The libibverbs library provides the following thread safety guarantees:

### Thread-Safe Operations

The following operations are safe to call concurrently from multiple threads without external synchronization:

- **`ibv_post_send()` on the same QP**: Multiple threads can post send work requests to the same QP concurrently. The library serializes these internally (typically using a spinlock). This is guaranteed by the verbs specification.

- **`ibv_post_recv()` on the same QP**: Similarly, multiple threads can post receive work requests concurrently.

- **`ibv_post_srq_recv()` on the same SRQ**: Thread-safe for concurrent posts.

- **Operations on different objects**: Any operations on different QPs, CQs, MRs, or PDs can proceed concurrently without any synchronization.

### Non-Thread-Safe Operations

The following operations require external synchronization:

- **`ibv_poll_cq()` on the same CQ from multiple threads**: This is NOT thread-safe. Two threads polling the same CQ concurrently can both see the same completion, leading to double processing. The application must serialize access to a CQ.

- **`ibv_req_notify_cq()` on the same CQ**: Not safe to call concurrently with `ibv_poll_cq()` on the same CQ.

- **Resource creation/destruction**: Operations like `ibv_create_qp()`, `ibv_destroy_qp()`, `ibv_reg_mr()`, and `ibv_dereg_mr()` should not be called concurrently on related objects (e.g., creating a QP while destroying the PD it belongs to).

<div class="warning">

**Critical**: The most common multi-threading bug in RDMA applications is polling the same CQ from multiple threads. Even though `ibv_post_send()` is thread-safe for the same QP, the associated CQ is NOT safe for concurrent polling. Every CQ must have exactly one polling thread, or access must be serialized with a lock.

</div>

## Pattern 1: CQ-Per-Thread

The simplest and highest-performance pattern assigns each thread its own completion queue. Multiple threads can share a QP (since `ibv_post_send()` is thread-safe), but each thread's work requests generate completions on its private CQ.

This requires creating separate send CQs per thread:

```c
#define NUM_THREADS 4

struct thread_ctx {
    struct ibv_cq *send_cq;    /* Private CQ for this thread */
    struct ibv_qp *qp;         /* Shared QP (or per-thread) */
    int thread_id;
};

/* Create a shared QP with per-thread send CQs is not directly
   possible -- a QP has one send CQ. So this pattern requires
   separate QPs if you want separate CQs. See Pattern 2. */
```

In practice, the CQ-per-thread pattern naturally leads to the QP-per-thread pattern (Pattern 2), because a QP is bound to a single send CQ at creation time.

## Pattern 2: QP-Per-Thread

The most practical high-performance pattern gives each thread its own QP and CQ. This eliminates all contention:

```c
struct thread_ctx {
    struct ibv_cq *cq;         /* Private CQ */
    struct ibv_qp *qp;         /* Private QP */
    struct ibv_mr *mr;         /* Can be shared or private */
    void *send_buf;
    void *recv_buf;
    int thread_id;
};

void *worker_thread(void *arg) {
    struct thread_ctx *ctx = (struct thread_ctx *)arg;

    while (running) {
        /* Post sends -- no locking needed, private QP */
        ibv_post_send(ctx->qp, &send_wr, &bad_wr);

        /* Poll completions -- no locking needed, private CQ */
        struct ibv_wc wc[16];
        int n = ibv_poll_cq(ctx->cq, 16, wc);
        for (int i = 0; i < n; i++) {
            handle_completion(&wc[i]);
        }
    }
    return NULL;
}
```

This pattern provides the best performance because:
- No lock contention on `ibv_post_send()` (private QP).
- No lock contention on `ibv_poll_cq()` (private CQ).
- Each thread's working set (QP context, CQ, buffers) is cache-local.

The trade-off is resource consumption: each thread needs its own QP connected to the remote peer, which means more QP state in the NIC. For moderate thread counts (4-16 threads) on modern hardware, this is not a concern.

### Connection Setup for QP-Per-Thread

When using QP-per-thread with RC transport, each thread's QP needs a separate connection to the remote peer. This can be done by:

1. Establishing connections sequentially during initialization.
2. Using RDMA_CM to manage multiple connections to the same peer.
3. Having each thread connect independently.

```c
/* Initialize: create per-thread QPs and connect them all */
for (int t = 0; t < NUM_THREADS; t++) {
    threads[t].cq = ibv_create_cq(dev_ctx, CQ_SIZE, NULL, NULL, 0);

    struct ibv_qp_init_attr attr = {
        .send_cq = threads[t].cq,
        .recv_cq = threads[t].cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 128,
            .max_recv_wr = 128,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };
    threads[t].qp = ibv_create_qp(pd, &attr);

    /* Connect this QP to the remote peer's corresponding QP */
    connect_qp(threads[t].qp, remote_qp_info[t]);
}
```

## Pattern 3: Shared QP with Synchronization

When the number of connections must be minimized (e.g., due to QP resource limits or connection setup costs), threads can share a QP with explicit synchronization:

```c
struct shared_qp_ctx {
    struct ibv_qp *qp;
    struct ibv_cq *cq;
    pthread_spinlock_t cq_lock;  /* Protects CQ polling */
    /* ibv_post_send is thread-safe, no lock needed for posting */
};

void *worker_thread(void *arg) {
    struct shared_qp_ctx *ctx = (struct shared_qp_ctx *)arg;

    while (running) {
        /* Post sends -- thread-safe, no lock needed */
        ibv_post_send(ctx->qp, &send_wr, &bad_wr);

        /* Poll completions -- MUST lock */
        struct ibv_wc wc[16];
        pthread_spin_lock(&ctx->cq_lock);
        int n = ibv_poll_cq(ctx->cq, 16, wc);
        pthread_spin_unlock(&ctx->cq_lock);

        for (int i = 0; i < n; i++) {
            handle_completion(&wc[i]);
        }
    }
    return NULL;
}
```

This pattern has lower resource usage but introduces contention on the CQ lock. The contention can be mitigated by:

- **Batching**: Each thread polls multiple completions while holding the lock, amortizing the lock overhead.
- **Dedicated completion thread**: One thread is responsible for polling the CQ and dispatching completions to worker threads through lock-free queues.

### Dedicated Completion Thread

```c
void *completion_thread(void *arg) {
    struct shared_qp_ctx *ctx = (struct shared_qp_ctx *)arg;

    while (running) {
        struct ibv_wc wc[64];
        int n = ibv_poll_cq(ctx->cq, 64, wc);

        for (int i = 0; i < n; i++) {
            /* Route completion to the appropriate worker thread
               based on wr_id encoding */
            int thread_id = wc[i].wr_id >> 48;  /* Upper bits = thread */
            enqueue_completion(&worker_queues[thread_id], &wc[i]);
        }
    }
    return NULL;
}
```

Encoding the thread ID in the upper bits of `wr_id` is a common trick that allows the completion thread to route completions without a lookup table.

## Lock-Free Polling Patterns

For the highest performance, avoid locks entirely by using atomic operations:

```c
/* Atomic CQ ownership: only one thread polls at a time */
static atomic_flag cq_polling = ATOMIC_FLAG_INIT;

void try_poll_cq(struct ibv_cq *cq) {
    /* Try to acquire polling rights */
    if (!atomic_flag_test_and_set(&cq_polling)) {
        /* We own the CQ -- poll it */
        struct ibv_wc wc[64];
        int n = ibv_poll_cq(cq, 64, wc);

        atomic_flag_clear(&cq_polling);

        for (int i = 0; i < n; i++) {
            handle_completion(&wc[i]);
        }
    }
    /* If we didn't get the flag, skip -- another thread is polling */
}
```

This pattern avoids blocking but means completions may be delayed if a thread cannot acquire polling rights. It works well when threads have other work to do between polls.

## NUMA-Aware Thread Placement

RDMA NICs are physically connected to a specific PCIe root complex, which is associated with a specific NUMA node. Threads that access the NIC from the local NUMA node avoid cross-socket memory accesses, which can save 100-200 nanoseconds per operation.

### Determining NIC NUMA Affinity

```c
#include <infiniband/verbs.h>

/* Query the NUMA node of the RDMA device */
const char *dev_name = ibv_get_device_name(device);
char path[256];
snprintf(path, sizeof(path),
         "/sys/class/infiniband/%s/device/numa_node", dev_name);

FILE *f = fopen(path, "r");
int numa_node;
fscanf(f, "%d", &numa_node);
fclose(f);

printf("Device %s is on NUMA node %d\n", dev_name, numa_node);
```

### Pinning Threads to NUMA-Local Cores

```c
#include <sched.h>
#include <numa.h>

void pin_thread_to_numa_node(int numa_node) {
    struct bitmask *cpumask = numa_allocate_cpumask();
    numa_node_to_cpus(numa_node, cpumask);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int i = 0; i < numa_num_configured_cpus(); i++) {
        if (numa_bitmask_isbitset(cpumask, i)) {
            CPU_SET(i, &cpuset);
        }
    }

    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
    numa_free_cpumask(cpumask);
}
```

### NUMA-Aware Memory Allocation

Equally important is allocating RDMA buffers on the correct NUMA node:

```c
#include <numaif.h>

/* Allocate memory on the same NUMA node as the NIC */
void *buf = numa_alloc_onnode(BUF_SIZE, nic_numa_node);
struct ibv_mr *mr = ibv_reg_mr(pd, buf, BUF_SIZE,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
```

Cross-NUMA memory access for RDMA buffers is particularly expensive because every DMA transaction crosses the inter-socket link.

<div class="tip">

**Tip**: On systems with two NUMA nodes and two NICs, the ideal configuration is to assign each NIC to threads running on its local NUMA node, with all RDMA buffers allocated on that node. This eliminates all cross-NUMA traffic for RDMA operations.

</div>

## Best Practices Summary

1. **Default to QP-per-thread**: Unless resource constraints prevent it, give each thread its own QP and CQ. This eliminates all contention.

2. **Never share a CQ without synchronization**: This is the most common source of bugs. If you must share, use a spinlock or a dedicated completion thread.

3. **Pin threads to NUMA-local cores**: Determine which NUMA node the NIC is on and pin worker threads to cores on that node.

4. **Allocate buffers NUMA-locally**: Use `numa_alloc_onnode()` or equivalent to place RDMA buffers near the NIC.

5. **Encode thread identity in wr_id**: When threads share a CQ, encode the thread ID in `wr_id` so completions can be routed efficiently.

6. **Avoid false sharing**: Ensure per-thread data structures are on separate cache lines (64 bytes on x86). Use `__attribute__((aligned(64)))` or padding.

```c
struct __attribute__((aligned(64))) thread_ctx {
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    uint64_t sends_posted;
    uint64_t completions;
    char _pad[0];  /* Ensure next thread_ctx starts on new cache line */
};
```

7. **Scale threads to match NIC capacity**: Profile to find the sweet spot. More threads than needed adds context-switch overhead without increasing throughput. Typically, 4-8 threads can saturate a 200 Gbps NIC for large messages, while small-message workloads may need more.

Multi-threaded RDMA programming requires careful attention to resource partitioning, synchronization, and hardware topology. The QP-per-thread pattern with NUMA-aware placement provides the best performance-to-complexity ratio and is the recommended starting point for high-performance applications.
