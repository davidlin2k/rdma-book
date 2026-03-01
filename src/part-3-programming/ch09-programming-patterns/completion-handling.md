# 9.5 Completion Handling

Completion handling is where RDMA application architecture meets performance engineering. Every RDMA operation, whether send, receive, RDMA write, or RDMA read, produces a completion event that the application must consume. How the application waits for and processes these completions determines its latency, throughput, and CPU utilization characteristics.

This section develops four completion handling patterns, from the simplest busy-polling approach to a sophisticated hybrid strategy. Each pattern occupies a different point in the latency-vs-CPU tradeoff space, and the right choice depends on the application's requirements.

## The Completion Queue Interface

Before examining the patterns, let us review the completion queue API. The fundamental operation is `ibv_poll_cq()`:

```c
int ibv_poll_cq(struct ibv_cq *cq, int num_entries, struct ibv_wc *wc);
```

This function polls the CQ for up to `num_entries` completions. It returns:
- **> 0**: The number of completions retrieved (stored in the `wc` array).
- **0**: No completions available.
- **< 0**: An error occurred.

`ibv_poll_cq()` is a non-blocking, lock-free operation. It directly reads the CQ memory that is shared between the RNIC and the application. There is no system call, no context switch, and no kernel involvement. This is what makes RDMA completion handling so fast and why busy polling achieves sub-microsecond latency.

The work completion structure contains the result of each operation:

```c
struct ibv_wc {
    uint64_t        wr_id;       /* From the original work request */
    enum ibv_wc_status status;   /* Success or error code */
    enum ibv_wc_opcode opcode;   /* Send, Recv, RDMA Read, etc. */
    uint32_t        vendor_err;  /* Vendor-specific error info */
    uint32_t        byte_len;    /* Bytes transferred (recv only) */
    uint32_t        imm_data;    /* Immediate data (if present) */
    uint32_t        qp_num;      /* QP that generated this CQE */
    uint32_t        src_qp;      /* Source QP (UD only) */
    unsigned int    wc_flags;    /* Flags (e.g., IBV_WC_WITH_IMM) */
    /* ... additional fields ... */
};
```

## Pattern 1: Busy Polling

Busy polling is the simplest and lowest-latency completion handling strategy. The application continuously polls the CQ in a tight loop until a completion arrives:

```c
int poll_completion_busy(struct ibv_cq *cq, struct ibv_wc *wc)
{
    int ne;
    do {
        ne = ibv_poll_cq(cq, 1, wc);
        if (ne < 0) {
            fprintf(stderr, "ibv_poll_cq() failed: %d\n", ne);
            return -1;
        }
    } while (ne == 0);

    if (wc->status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Completion error: %s (wr_id=%lu)\n",
                ibv_wc_status_str(wc->status), wc->wr_id);
        return -1;
    }
    return 0;
}
```

**Characteristics**:
- **Latency**: Lowest possible. The completion is detected within one loop iteration (~tens of nanoseconds) after it is written to the CQ by the RNIC.
- **CPU usage**: 100% on the polling core. The core is never idle, even when no completions are arriving.
- **Complexity**: Minimal. No event channels, no notifications, no system calls.
- **When to use**: Latency-critical applications where a dedicated CPU core is acceptable. Common in high-frequency trading, low-latency storage, and benchmarks.

<div class="tip">

On modern CPUs, busy polling in a tight loop can cause power management issues and may actually increase latency due to thermal throttling. Consider inserting a `_mm_pause()` intrinsic (x86) or equivalent in the polling loop to hint to the CPU that this is a spin-wait loop. This reduces power consumption and can improve cross-hyperthread performance.

```c
#include <immintrin.h>

do {
    ne = ibv_poll_cq(cq, 1, wc);
    if (ne == 0)
        _mm_pause();
} while (ne == 0);
```

</div>

## Pattern 2: Event-Driven Completion

Event-driven completion uses the kernel's event notification mechanism to avoid consuming CPU while waiting. The application blocks until the RNIC signals that a new completion is available.

The event-driven API involves three functions:

1. **`ibv_req_notify_cq(cq, solicited_only)`**: Arm the CQ for notification. The RNIC will generate an event the next time a completion is added to the CQ.
2. **`ibv_get_cq_event(channel, &cq, &cq_context)`**: Block until an event arrives on the completion event channel.
3. **`ibv_ack_cq_events(cq, num_events)`**: Acknowledge received events. Events must be acknowledged before the CQ can be destroyed.

### Setting Up the Event Channel

The event channel must be created before the CQ and passed to `ibv_create_cq()`:

```c
/* Create the completion event channel */
struct ibv_comp_channel *channel = ibv_create_comp_channel(ctx);
if (!channel) {
    fprintf(stderr, "Failed to create completion channel\n");
    return -1;
}

/* Create CQ with the event channel */
struct ibv_cq *cq = ibv_create_cq(ctx, CQ_DEPTH, NULL, channel, 0);
if (!cq) {
    fprintf(stderr, "Failed to create CQ\n");
    return -1;
}

/* Arm the CQ for the first notification */
ibv_req_notify_cq(cq, 0);  /* 0 = notify on all completions */
```

### Blocking Wait for Completion

```c
int poll_completion_blocking(struct ibv_cq *cq,
                              struct ibv_comp_channel *channel,
                              struct ibv_wc *wc)
{
    struct ibv_cq *ev_cq;
    void *ev_ctx;

    /* Block until the RNIC signals a new completion */
    if (ibv_get_cq_event(channel, &ev_cq, &ev_ctx)) {
        fprintf(stderr, "ibv_get_cq_event() failed\n");
        return -1;
    }

    /* Acknowledge the event */
    ibv_ack_cq_events(ev_cq, 1);

    /* Re-arm the CQ for the next notification */
    if (ibv_req_notify_cq(ev_cq, 0)) {
        fprintf(stderr, "ibv_req_notify_cq() failed\n");
        return -1;
    }

    /* Now poll for all available completions */
    int ne;
    do {
        ne = ibv_poll_cq(ev_cq, 1, wc);
        if (ne < 0) {
            fprintf(stderr, "ibv_poll_cq() failed\n");
            return -1;
        }
        if (ne > 0 && wc->status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Completion error: %s\n",
                    ibv_wc_status_str(wc->status));
            return -1;
        }
    } while (ne > 0);  /* Drain all completions */

    return 0;
}
```

<div class="warning">

There is a race condition between `ibv_req_notify_cq()` and `ibv_poll_cq()`. Completions that arrive after the notification is armed but before the CQ is polled will generate an event but will be consumed by the subsequent poll. Completions that arrive after the poll but before the re-arm will NOT generate an event and will be missed if the application does not poll again.

The correct sequence is:
1. Call `ibv_req_notify_cq()` to arm the CQ.
2. Call `ibv_poll_cq()` to drain any completions that arrived before (or during) the arm.
3. If completions were found, process them and go to step 1.
4. If no completions were found, call `ibv_get_cq_event()` to block.

This ensures no completions are missed.

</div>

### Using epoll for Multiple CQs

In applications with multiple completion queues (e.g., one per QP or one per thread), you can use `epoll` to wait on multiple completion event channels simultaneously:

```c
#include <sys/epoll.h>

int setup_epoll(struct ibv_comp_channel **channels, int num_channels)
{
    int epfd = epoll_create1(0);
    if (epfd < 0)
        return -1;

    for (int i = 0; i < num_channels; i++) {
        /* Set the channel FD to non-blocking */
        int flags = fcntl(channels[i]->fd, F_GETFL);
        fcntl(channels[i]->fd, F_SETFL, flags | O_NONBLOCK);

        struct epoll_event ev = {
            .events  = EPOLLIN,
            .data.fd = channels[i]->fd,
        };
        epoll_ctl(epfd, EPOLL_CTL_ADD, channels[i]->fd, &ev);
    }
    return epfd;
}

void event_loop(int epfd, struct ibv_comp_channel **channels,
                struct ibv_cq **cqs, int num_cqs)
{
    struct epoll_event events[MAX_EVENTS];

    while (running) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);

        for (int i = 0; i < nfds; i++) {
            /* Find which channel triggered */
            struct ibv_comp_channel *ch = find_channel(events[i].data.fd,
                                                        channels,
                                                        num_cqs);
            struct ibv_cq *ev_cq;
            void *ev_ctx;

            /* Consume the event */
            while (ibv_get_cq_event(ch, &ev_cq, &ev_ctx) == 0) {
                ibv_ack_cq_events(ev_cq, 1);
                ibv_req_notify_cq(ev_cq, 0);

                /* Poll and process completions */
                struct ibv_wc wc;
                while (ibv_poll_cq(ev_cq, 1, &wc) > 0) {
                    process_completion(&wc);
                }
            }
        }
    }
}
```

**Characteristics**:
- **Latency**: Higher than busy polling. The event notification path involves an interrupt from the RNIC, kernel processing, and a system call return. Typical added latency: 5-15 microseconds.
- **CPU usage**: Near zero when idle. The thread blocks in `epoll_wait()` and consumes no CPU until a completion arrives.
- **Complexity**: Moderate. Requires event channels, careful re-arming, and handling the arm-poll race condition.
- **When to use**: Applications where CPU efficiency matters more than latency. Background tasks, infrequent operations, or systems where CPU cores are shared with other workloads.

## Pattern 3: Hybrid Adaptive Polling

The hybrid approach combines busy polling and event-driven notification. The application polls aggressively for a configurable number of iterations; if no completion arrives within that window, it switches to event-driven mode and blocks:

```c
int poll_completion_adaptive(struct ibv_cq *cq,
                              struct ibv_comp_channel *channel,
                              struct ibv_wc *wc,
                              int max_poll_iters)
{
    int ne;

    /* Phase 1: Busy poll for up to max_poll_iters iterations */
    for (int i = 0; i < max_poll_iters; i++) {
        ne = ibv_poll_cq(cq, 1, wc);
        if (ne > 0)
            goto found;
        if (ne < 0) {
            fprintf(stderr, "ibv_poll_cq() failed\n");
            return -1;
        }
    }

    /* Phase 2: No completion found after polling; switch to events */
    if (ibv_req_notify_cq(cq, 0)) {
        fprintf(stderr, "ibv_req_notify_cq() failed\n");
        return -1;
    }

    /* Check once more in case a completion arrived during arming */
    ne = ibv_poll_cq(cq, 1, wc);
    if (ne > 0)
        goto found;
    if (ne < 0)
        return -1;

    /* Block for event */
    struct ibv_cq *ev_cq;
    void *ev_ctx;
    if (ibv_get_cq_event(channel, &ev_cq, &ev_ctx)) {
        fprintf(stderr, "ibv_get_cq_event() failed\n");
        return -1;
    }
    ibv_ack_cq_events(ev_cq, 1);

    /* Poll after event */
    ne = ibv_poll_cq(cq, 1, wc);
    if (ne <= 0) {
        fprintf(stderr, "Expected completion after event\n");
        return -1;
    }

found:
    if (wc->status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Completion error: %s\n",
                ibv_wc_status_str(wc->status));
        return -1;
    }
    return 0;
}
```

The `max_poll_iters` parameter controls the tradeoff. A larger value favors latency (more time spent polling before blocking); a smaller value favors CPU efficiency (quicker fallback to event-driven mode).

Some advanced implementations adapt the polling window dynamically based on recent completion patterns. If completions have been arriving frequently, increase the polling window. If completions have been infrequent, decrease it:

```c
/* Simple adaptive window: double on hit, halve on miss */
static int poll_window = INITIAL_POLL_WINDOW;

int poll_adaptive(struct ibv_cq *cq, struct ibv_comp_channel *channel,
                  struct ibv_wc *wc)
{
    for (int i = 0; i < poll_window; i++) {
        int ne = ibv_poll_cq(cq, 1, wc);
        if (ne > 0) {
            /* Completion found during polling - increase window */
            poll_window = MIN(poll_window * 2, MAX_POLL_WINDOW);
            return (wc->status == IBV_WC_SUCCESS) ? 0 : -1;
        }
    }

    /* No completion found - decrease window and block */
    poll_window = MAX(poll_window / 2, MIN_POLL_WINDOW);
    return block_for_completion(cq, channel, wc);
}
```

**Characteristics**:
- **Latency**: Near busy-poll latency when completions arrive quickly; falls back to event-driven latency during idle periods.
- **CPU usage**: High during active periods, near zero during idle periods.
- **Complexity**: Higher. Must handle the arm-poll race condition and tune the polling window.
- **When to use**: Applications with variable load patterns. Achieves low latency during bursts while conserving CPU during quiet periods.

## Pattern 4: Batched Polling

Batched polling retrieves multiple completions in a single `ibv_poll_cq()` call. This amortizes the per-poll overhead across multiple completions and is essential for high-throughput applications:

```c
#define BATCH_SIZE 32

int process_completions_batched(struct ibv_cq *cq)
{
    struct ibv_wc wc_batch[BATCH_SIZE];
    int total_processed = 0;

    int ne;
    do {
        ne = ibv_poll_cq(cq, BATCH_SIZE, wc_batch);
        if (ne < 0) {
            fprintf(stderr, "ibv_poll_cq() failed\n");
            return -1;
        }

        for (int i = 0; i < ne; i++) {
            if (wc_batch[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Completion error on wr_id %lu: %s\n",
                        wc_batch[i].wr_id,
                        ibv_wc_status_str(wc_batch[i].status));
                return -1;
            }

            /* Process each completion */
            switch (wc_batch[i].opcode) {
            case IBV_WC_SEND:
                handle_send_completion(&wc_batch[i]);
                break;
            case IBV_WC_RECV:
                handle_recv_completion(&wc_batch[i]);
                break;
            case IBV_WC_RDMA_WRITE:
                handle_write_completion(&wc_batch[i]);
                break;
            case IBV_WC_RDMA_READ:
                handle_read_completion(&wc_batch[i]);
                break;
            default:
                fprintf(stderr, "Unexpected opcode: %d\n",
                        wc_batch[i].opcode);
            }
            total_processed++;
        }
    } while (ne == BATCH_SIZE);  /* Continue if batch was full */

    return total_processed;
}
```

The key insight is that `ibv_poll_cq()` with `num_entries > 1` is barely more expensive than polling for a single entry. The function reads CQEs from a memory-mapped ring buffer, and reading 32 entries is only marginally slower than reading 1. By processing completions in batches, the per-completion overhead drops significantly.

<div class="note">

The `do { ... } while (ne == BATCH_SIZE)` pattern ensures that all available completions are drained. If a batch returns exactly `BATCH_SIZE` completions, there may be more available, so we poll again. If a batch returns fewer, we have drained the CQ.

</div>

### Batched Polling with Replenishment

In high-throughput applications, receive completions must be promptly replenished with new receive work requests. A common pattern is to post new receives in batches after processing a batch of completions:

```c
int process_and_replenish(struct ibv_cq *cq, struct ibv_qp *qp,
                           struct recv_buf_pool *pool)
{
    struct ibv_wc wc_batch[BATCH_SIZE];
    int ne = ibv_poll_cq(cq, BATCH_SIZE, wc_batch);

    int recv_count = 0;
    for (int i = 0; i < ne; i++) {
        if (wc_batch[i].opcode == IBV_WC_RECV) {
            process_received_data(&wc_batch[i], pool);
            recv_count++;
        }
    }

    /* Replenish receive buffers in a batch */
    if (recv_count > 0) {
        post_recv_batch(qp, pool, recv_count);
    }

    return ne;
}
```

## Comparison Table

| Pattern | Latency | CPU Usage | Complexity | Best For |
|---------|---------|-----------|------------|----------|
| Busy polling | ~50 ns | 100% | Low | Latency-critical, dedicated cores |
| Event-driven | ~10 us | ~0% idle | Moderate | CPU-constrained, background ops |
| Hybrid adaptive | ~50 ns to ~10 us | Variable | High | Variable workloads |
| Batched polling | ~50 ns (amortized) | 100% | Low-Moderate | High-throughput |

## Best Practices for High-Performance Completion Handling

### 1. Use Dedicated Polling Threads

Pin completion polling threads to specific CPU cores using `pthread_setaffinity_np()` or `taskset`. This avoids cache pollution from context switches and ensures consistent polling performance.

```c
void pin_to_core(int core_id)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset);
}
```

### 2. Use Selective Signaling

Not every send needs a completion. Use `IBV_SEND_SIGNALED` only on every Nth send to reduce the number of CQEs the polling thread must process:

```c
/* Signal every 16th send */
send_wr.send_flags = (send_count % 16 == 0) ? IBV_SEND_SIGNALED : 0;
```

<div class="warning">

When using selective signaling, you must still ensure that signaled completions are polled before the send queue wraps around. If the SQ has depth 256 and you signal every 16th send, you must poll before posting 256 sends after the last signaled one. Otherwise, the SQ fills up and `ibv_post_send()` fails.

</div>

### 3. Use Shared CQs Judiciously

A single CQ shared by multiple QPs allows a single poll to retrieve completions from all QPs. However, shared CQs can become contention points in multi-threaded applications. The guideline is:
- **Single thread, multiple QPs**: Share a CQ.
- **Multiple threads**: Give each thread its own CQ.

### 4. Size the CQ Appropriately

The CQ must be large enough to hold all completions that can be generated before the application drains them. If the CQ overflows, the QP transitions to an error state. A safe sizing rule:

```
CQ depth >= (max_send_wr * num_send_QPs) + (max_recv_wr * num_recv_QPs)
```

### 5. Batch CQ Event Acknowledgments

Instead of acknowledging each CQ event individually, accumulate events and acknowledge them in bulk:

```c
int unacked_events = 0;

/* In the event loop */
while (ibv_get_cq_event(channel, &ev_cq, &ev_ctx) == 0) {
    unacked_events++;
    /* ... process completions ... */

    /* Acknowledge in batches */
    if (unacked_events >= 100) {
        ibv_ack_cq_events(ev_cq, unacked_events);
        unacked_events = 0;
    }
}

/* Final acknowledgment before cleanup */
if (unacked_events > 0)
    ibv_ack_cq_events(ev_cq, unacked_events);
```

`ibv_ack_cq_events()` takes a `num_events` parameter that can acknowledge multiple events at once, avoiding the overhead of one system call per event.

### 6. Consider CQ Moderation

Some RNICs support **CQ moderation** (also called interrupt coalescing), which delays the notification event until either N completions have accumulated or a timeout expires:

```c
struct ibv_modify_cq_attr cq_attr = {
    .attr_mask = IBV_CQ_ATTR_MODERATE,
    .moderate = {
        .cq_count = 64,     /* Generate event after 64 CQEs */
        .cq_period = 100,   /* Or after 100 microseconds */
    },
};
ibv_modify_cq(cq, &cq_attr);
```

CQ moderation reduces interrupt rate at the cost of higher latency for individual completions. It is useful for high-throughput workloads where per-completion latency is less important than aggregate throughput.

<div class="note">

CQ moderation is not universally supported. Check `ibv_query_device_ex()` for the `IBV_DEVICE_CQ_MODERATION` capability flag before using it.

</div>

## Key Takeaways

1. **`ibv_poll_cq()` is lock-free and fast**: It reads directly from RNIC-shared memory with no system calls. This is why RDMA achieves microsecond latencies.
2. **Busy polling gives the best latency but wastes CPU**: Use it only when latency is paramount and dedicated cores are available.
3. **Event-driven completion saves CPU but adds latency**: The interrupt and system call path adds 5-15 microseconds.
4. **Hybrid adaptive polling provides the best of both worlds**: Poll aggressively during active periods, block during idle periods.
5. **Batched polling improves throughput**: Always poll for multiple CQEs at once in throughput-oriented applications.
6. **Selective signaling reduces CQ pressure**: Signal only every Nth send to reduce the number of completions to process.
7. **The arm-poll race condition is subtle**: Always re-poll after arming the CQ to avoid missing completions.
