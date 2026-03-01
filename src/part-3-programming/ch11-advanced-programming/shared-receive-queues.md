# 11.1 Shared Receive Queues (SRQ)

## The Problem: Receive Buffer Explosion

In standard RDMA programming, every queue pair has its own receive queue. The application must post receive buffers to each QP independently, and those buffers cannot be shared. Consider a server that maintains RC connections to 1,000 clients. If each QP needs 64 posted receive buffers to avoid Receiver Not Ready (RNR) errors, and each buffer is 4 KB, the total receive buffer memory is:

```
1,000 QPs × 64 buffers × 4 KB = 256 MB
```

This is purely for receive buffers -- it does not include send buffers, completion queues, or QP control structures. Worse, this memory must be pinned (non-swappable), directly consuming physical memory. And the 64 buffers per QP is a conservative estimate; bursty workloads may require hundreds of buffers per QP to prevent RNR conditions.

The fundamental inefficiency is that receive buffers are statically partitioned across QPs. If QP A is idle while QP B receives a burst, QP A's buffers sit unused while QP B might run out. What we need is a shared pool.

## SRQ: The Solution

A **Shared Receive Queue (SRQ)** is a receive buffer pool that can be shared across multiple queue pairs. Instead of posting receive buffers to individual QPs, the application posts them to the SRQ. When a message arrives on any associated QP, it consumes a buffer from the shared pool.

The memory savings are dramatic:

```
Without SRQ: 1,000 QPs × 64 buffers × 4 KB = 256 MB
With SRQ:    1 SRQ  × 256 buffers × 4 KB =   1 MB
```

With SRQ, a pool of 256 buffers can comfortably serve 1,000 QPs because the peak concurrent receive rate across all QPs is far less than the sum of individual peaks. This is the classic statistical multiplexing advantage.

## Creating an SRQ

An SRQ is created with `ibv_create_srq()`:

```c
struct ibv_srq_init_attr srq_init_attr = {
    .attr = {
        .max_wr = 256,        /* Maximum WRs in the SRQ */
        .max_sge = 1,         /* Max scatter-gather entries per WR */
    }
};

struct ibv_srq *srq = ibv_create_srq(pd, &srq_init_attr);
if (!srq) {
    perror("ibv_create_srq");
    return 1;
}
```

The `max_wr` field determines the maximum number of receive work requests that can be posted to the SRQ simultaneously. The actual capacity may be rounded up by the device; query it with `ibv_query_srq()`:

```c
struct ibv_srq_attr srq_attr;
ibv_query_srq(srq, &srq_attr);
printf("SRQ max_wr: %u, current limit: %u\n",
       srq_attr.max_wr, srq_attr.srq_limit);
```

## Posting Receives to the SRQ

Posting receive work requests to an SRQ is nearly identical to posting to a regular QP receive queue, but uses `ibv_post_srq_recv()`:

```c
static int post_srq_receive(struct ibv_srq *srq,
                            struct ibv_mr *mr,
                            void *buf, size_t len,
                            uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr = (uint64_t)(uintptr_t)buf,
        .length = len,
        .lkey = mr->lkey
    };

    struct ibv_recv_wr wr = {
        .wr_id = wr_id,
        .sg_list = &sge,
        .num_sge = 1,
    };

    struct ibv_recv_wr *bad_wr;
    return ibv_post_srq_recv(srq, &wr, &bad_wr);
}

/* Pre-fill the SRQ with receive buffers */
for (int i = 0; i < NUM_SRQ_BUFFERS; i++) {
    post_srq_receive(srq, mr, &recv_pool[i * BUF_SIZE],
                     BUF_SIZE, i);
}
```

The `wr_id` field is critical for buffer management. When a completion arrives, the `wr_id` in the work completion tells the application which buffer was consumed. The application can then repost that buffer (or allocate a new one) back to the SRQ.

<div class="warning">

**Important**: Unlike regular QP receive queues, the SRQ is shared and can be drained by any associated QP. You must monitor the SRQ fill level and replenish buffers promptly. Running out of SRQ buffers causes RNR errors on *all* associated QPs, not just one.

</div>

## Creating QPs with SRQ

To associate a QP with an SRQ, set the `srq` field in `ibv_qp_init_attr`:

```c
struct ibv_qp_init_attr qp_attr = {
    .send_cq = cq,
    .recv_cq = cq,
    .srq = srq,              /* Associate with SRQ */
    .qp_type = IBV_QPT_RC,
    .cap = {
        .max_send_wr = 16,
        .max_recv_wr = 0,    /* No per-QP receive queue needed */
        .max_send_sge = 1,
        .max_recv_sge = 0
    }
};

struct ibv_qp *qp = ibv_create_qp(pd, &qp_attr);
```

Notice that `max_recv_wr` and `max_recv_sge` are set to zero in the QP attributes. When a QP is associated with an SRQ, it does not have its own receive queue -- all receives are served from the SRQ. You must not call `ibv_post_recv()` on a QP that uses an SRQ; all receives go through `ibv_post_srq_recv()` instead.

Multiple QPs can share the same SRQ, and they can also share or have separate completion queues. A common pattern is to use a shared SRQ with a shared CQ:

```c
/* Create one SRQ and one CQ shared by all QPs */
struct ibv_srq *srq = ibv_create_srq(pd, &srq_init_attr);
struct ibv_cq *cq = ibv_create_cq(ctx, 1024, NULL, NULL, 0);

/* Create 1000 QPs, all sharing the SRQ and CQ */
struct ibv_qp *qps[1000];
for (int i = 0; i < 1000; i++) {
    struct ibv_qp_init_attr attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .srq = srq,
        .qp_type = IBV_QPT_RC,
        .cap = { .max_send_wr = 16, .max_send_sge = 1 }
    };
    qps[i] = ibv_create_qp(pd, &attr);
}
```

## SRQ Limit Events

One of the most important SRQ features is the **SRQ limit notification**. You can configure a watermark so that when the number of available receive buffers drops below a threshold, the device generates an asynchronous event. This allows the application to replenish the SRQ before it runs out completely.

### Setting the Limit

The SRQ limit is set with `ibv_modify_srq()`:

```c
struct ibv_srq_attr attr = {
    .srq_limit = 32    /* Trigger event when <= 32 WRs remain */
};
ibv_modify_srq(srq, &attr, IBV_SRQ_LIMIT);
```

When the number of posted receive WRs in the SRQ drops to or below 32, the device generates an `IBV_EVENT_SRQ_LIMIT_REACHED` asynchronous event.

### Handling the Limit Event

Asynchronous events are retrieved with `ibv_get_async_event()`:

```c
void *srq_limit_thread(void *arg)
{
    struct ibv_context *ctx = (struct ibv_context *)arg;
    struct ibv_async_event event;

    while (ibv_get_async_event(ctx, &event) == 0) {
        if (event.event_type == IBV_EVENT_SRQ_LIMIT_REACHED) {
            printf("SRQ limit reached! Replenishing buffers...\n");

            /* Post more receive buffers to the SRQ */
            replenish_srq(srq);

            /* Re-arm the limit notification */
            struct ibv_srq_attr attr = { .srq_limit = 32 };
            ibv_modify_srq(srq, &attr, IBV_SRQ_LIMIT);
        }
        ibv_ack_async_event(&event);
    }
    return NULL;
}
```

<div class="warning">

**Critical**: The SRQ limit notification is **one-shot**. After the event fires, you must re-arm it by calling `ibv_modify_srq()` again with the desired limit. Failing to re-arm means you will not receive further notifications, and the SRQ may drain silently.

</div>

### Limit Value Guidelines

Choosing the right SRQ limit depends on your application's receive rate and replenishment latency:

- **Too high**: The event fires too frequently, causing unnecessary overhead.
- **Too low**: The SRQ may drain completely before the application can replenish it, causing RNR errors.

A good heuristic is to set the limit high enough that the application has time to post at least one batch of receive buffers before the SRQ empties. If the peak aggregate receive rate across all QPs is R messages per second and the replenishment latency is T seconds, set the limit to at least R x T, plus a safety margin.

## SRQ with RDMA_CM

SRQs integrate naturally with RDMA_CM. Create the SRQ on the device obtained from the CM ID, then pass it when creating QPs:

```c
case RDMA_CM_EVENT_CONNECT_REQUEST: {
    /* SRQ was created earlier on the same device */
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .srq = srq,
        .qp_type = IBV_QPT_RC,
        .cap = { .max_send_wr = 16, .max_send_sge = 1 }
    };
    rdma_create_qp(event->id, pd, &qp_attr);
    rdma_accept(event->id, &conn_param);
    break;
}
```

Since the SRQ provides receive buffers for all QPs, you do not need to post per-connection receives -- just keep the SRQ filled.

## Buffer Management Strategies

Managing the SRQ buffer pool efficiently is important for performance. Several strategies work well:

### Index-Based Pool

Allocate a large contiguous buffer and divide it into fixed-size slots. Use the slot index as the `wr_id`:

```c
#define NUM_BUFFERS 1024
#define BUF_SIZE    4096

char *pool = aligned_alloc(4096, NUM_BUFFERS * BUF_SIZE);
struct ibv_mr *pool_mr = ibv_reg_mr(pd, pool,
    NUM_BUFFERS * BUF_SIZE,
    IBV_ACCESS_LOCAL_WRITE);

/* Post all buffers to SRQ */
for (int i = 0; i < NUM_BUFFERS; i++) {
    post_srq_receive(srq, pool_mr,
                     pool + i * BUF_SIZE, BUF_SIZE, i);
}

/* On completion, identify buffer by wr_id */
void on_completion(struct ibv_wc *wc) {
    int idx = (int)wc->wr_id;
    char *buf = pool + idx * BUF_SIZE;
    /* Process message in buf, then repost */
    post_srq_receive(srq, pool_mr, buf, BUF_SIZE, idx);
}
```

### Identifying the Source QP

When using a shared CQ with SRQ, you need to know which QP a received message came from. The work completion's `qp_num` field provides this:

```c
void on_completion(struct ibv_wc *wc) {
    if (wc->opcode == IBV_WC_RECV) {
        printf("Received message on QP %u, buffer %lu\n",
               wc->qp_num, wc->wr_id);
        /* Look up connection context by QP number */
        struct conn_ctx *conn = find_conn_by_qpn(wc->qp_num);
        process_message(conn, get_buffer(wc->wr_id), wc->byte_len);
    }
}
```

Maintaining a hash map from QP numbers to connection contexts is a standard pattern with SRQ.

## Complete Example

A full SRQ example is provided in `src/code/ch11-srq-example/srq_example.c`. It demonstrates:

1. Creating an SRQ shared by multiple QPs (using loopback for simplicity).
2. Posting a shared pool of receive buffers.
3. Setting an SRQ limit and handling the limit event.
4. Sending messages on different QPs and receiving them through the shared SRQ.
5. Identifying which QP each received message arrived on.

## Performance Considerations

SRQ provides memory savings, but there are performance trade-offs to be aware of:

- **Atomic operations**: The SRQ is accessed concurrently by the NIC (consuming buffers) and the application (posting buffers). This requires internal synchronization, which may add a small amount of overhead compared to per-QP receive queues.

- **Cache effects**: With per-QP receive queues, each QP's buffers tend to be cache-hot. With SRQ, the next buffer consumed might not be in cache. In practice, this effect is small for typical buffer sizes.

- **Completion routing**: With SRQ, the application must determine which QP a message arrived on from the work completion. This lookup (typically a hash map) adds a small per-message cost.

Despite these trade-offs, SRQ is overwhelmingly beneficial for applications with many connections. The memory savings alone justify its use, and the performance overhead is negligible for most workloads. SRQ is used extensively in production systems including MPI implementations, distributed storage systems, and database interconnects.

<div class="tip">

**Tip**: Some RDMA-aware applications combine SRQ with a two-level buffer scheme. Small, frequently used buffers (e.g., 256 bytes for headers) are posted to the SRQ, while large data transfers use RDMA Write to pre-registered application buffers. This minimizes SRQ buffer memory while supporting large transfers efficiently.

</div>
