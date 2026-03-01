# 9.6 Error Handling

RDMA error handling is more complex than traditional socket error handling because errors can surface through multiple independent channels: synchronous return codes from verb calls, asynchronous completion queue errors, and asynchronous events from the RNIC. A robust RDMA application must monitor all three channels and respond appropriately to each type of failure.

This section catalogs the error codes, explains their root causes, and develops patterns for error recovery, async event handling, and systematic debugging.

## Error Surfaces in RDMA

RDMA errors appear through three mechanisms:

1. **Synchronous verb errors**: Functions like `ibv_post_send()`, `ibv_modify_qp()`, and `ibv_reg_mr()` return non-zero on failure. These are programming errors or resource exhaustion, detected immediately.

2. **Completion queue errors (CQE errors)**: A work request is accepted by `ibv_post_send()` or `ibv_post_recv()` but fails during execution. The failure is reported through the completion queue with a non-success status code. These are the most common runtime errors.

3. **Async events**: The RNIC reports significant state changes, such as a port going down, a QP entering an error state, or a catastrophic hardware failure. These are delivered through the async event channel.

## CQE Error Codes

When `ibv_poll_cq()` returns a completion with `wc.status != IBV_WC_SUCCESS`, the status code indicates what went wrong. Here is a comprehensive catalog of error codes, their meanings, and common causes.

### IBV_WC_LOC_LEN_ERR (Local Length Error)

**What it means**: The receive buffer was too small for the incoming message, or a send specified a length that exceeds the QP's maximum message size.

**Common causes**:
- UD receive buffer does not include space for the 40-byte GRH.
- Receive SGE length is smaller than the sent message.
- Send length exceeds the path MTU for UD.

**Fix**: Ensure receive buffers are large enough. For UD, add 40 bytes for the GRH. Check the path MTU before sending.

```c
if (wc.status == IBV_WC_LOC_LEN_ERR) {
    fprintf(stderr, "Local length error on wr_id %lu. "
            "Check buffer sizes and MTU.\n", wc.wr_id);
}
```

### IBV_WC_LOC_QP_OP_ERR (Local QP Operation Error)

**What it means**: An internal QP error occurred. The operation violated a constraint that the QP cannot handle.

**Common causes**:
- Posting an RDMA Read when `max_rd_atomic` is set to 0.
- Posting a work request with too many SGEs (exceeding `max_send_sge` or `max_recv_sge`).
- Posting to a QP that is in an error state.
- Using an invalid opcode for the QP type (e.g., RDMA Write on a UD QP).

**Fix**: Review the QP configuration and ensure the work request parameters are compatible. Check that the QP is in the RTS state.

### IBV_WC_LOC_PROT_ERR (Local Protection Error)

**What it means**: The local memory region does not have the required access permissions, or the L_Key in the SGE does not match a valid registered MR.

**Common causes**:
- Using an L_Key from a deregistered MR.
- The SGE address range falls outside the registered MR boundaries.
- The MR does not have `IBV_ACCESS_LOCAL_WRITE` set for a receive operation.
- Using the wrong PD (the MR's PD does not match the QP's PD).

**Fix**: Verify that the L_Key is valid, the address range is within the MR, and the MR has appropriate access flags.

### IBV_WC_MW_BIND_ERR (Memory Window Bind Error)

**What it means**: A Memory Window bind operation failed.

**Common causes**:
- Binding to an MR that does not allow MW binding.
- The MW type does not match the bind operation.

**Fix**: This error is relatively rare; it applies only to applications using Memory Windows.

### IBV_WC_REM_ACCESS_ERR (Remote Access Error)

**What it means**: The remote RNIC rejected the RDMA Read or Write because the R_Key is invalid, the remote address is out of range, or the MR does not have the required access permissions.

**Common causes**:
- Using a stale R_Key (the remote MR was deregistered or re-registered).
- The remote address + length exceeds the remote MR boundaries.
- The remote MR does not have `IBV_ACCESS_REMOTE_READ` or `IBV_ACCESS_REMOTE_WRITE` as needed.
- The remote QP does not have the corresponding access flag set.

**Fix**: Verify R_Key validity, check address arithmetic, and ensure access flags are set on both the remote QP and MR.

```c
if (wc.status == IBV_WC_REM_ACCESS_ERR) {
    fprintf(stderr, "Remote access error: check R_Key (0x%x) "
            "and remote address (0x%lx)\n", rkey, remote_addr);
}
```

### IBV_WC_REM_INV_REQ_ERR (Remote Invalid Request Error)

**What it means**: The remote RNIC received a request it could not process.

**Common causes**:
- The remote QP is not in the RTS state.
- The request type is not supported by the remote QP.
- A protocol violation occurred.

**Fix**: Ensure both sides complete QP state transitions before sending messages. Check QP type compatibility.

### IBV_WC_REM_OP_ERR (Remote Operation Error)

**What it means**: The remote side encountered an internal error while processing the request.

**Common causes**:
- The remote QP encountered a local error (e.g., local protection error) while responding.
- Hardware error on the remote RNIC.

**Fix**: Check the remote side's logs and error counters. This often indicates a problem on the remote machine rather than the local machine.

### IBV_WC_RETRY_EXC_ERR (Transport Retry Counter Exceeded)

**What it means**: The local RNIC retransmitted a packet the maximum number of times (`retry_cnt`) without receiving an acknowledgment.

**Common causes**:
- The remote machine is down or unreachable.
- Network connectivity lost (cable unplugged, switch failure).
- The remote QP is not in the RTS state.
- Incorrect routing information (wrong LID, GID, or path).
- Firewall blocking RDMA traffic (common with RoCE).

**Fix**: This is one of the most common RDMA errors. Verify network connectivity, check that the remote side has completed QP setup, and verify routing information.

```c
if (wc.status == IBV_WC_RETRY_EXC_ERR) {
    fprintf(stderr, "Retry exceeded: remote side may be down "
            "or unreachable. Check:\n"
            "  - Remote QP is in RTS state\n"
            "  - Network connectivity\n"
            "  - LID/GID/routing info\n");
}
```

<div class="warning">

`IBV_WC_RETRY_EXC_ERR` is the most common error encountered during RDMA development. When you see this error, do not immediately suspect a bug in your code. First, verify basic network connectivity and confirm that the remote side has completed its QP setup before the local side starts sending.

</div>

### IBV_WC_RNR_RETRY_EXC_ERR (RNR Retry Counter Exceeded)

**What it means**: The remote side did not have a receive buffer posted, and the sender exhausted its RNR retry limit (`rnr_retry`).

**Common causes**:
- The receiver did not pre-post receive buffers before the sender started sending.
- The receiver is processing received messages too slowly, and its receive queue drained.
- The `rnr_retry` count is set too low (set to 7 for infinite retries during development).

**Fix**: Ensure receive buffers are posted before sending begins. Increase the receive queue depth. Set `rnr_retry = 7` during development.

### IBV_WC_WR_FLUSH_ERR (Work Request Flushed)

**What it means**: The work request was flushed because the QP transitioned to an error state. This is not an independent error; it is a consequence of a prior error that caused the QP to enter the error state.

**Common causes**:
- A previous work request on this QP failed, causing the QP to enter the SQE (Send Queue Error) or ERR state.
- The application explicitly transitioned the QP to the error state.
- All subsequent work requests in the queue are flushed with this status.

**Fix**: Identify and fix the root cause error that caused the QP state transition. The flush errors are secondary symptoms.

<div class="note">

When a QP enters the error state, **all** outstanding work requests on both the send queue and receive queue are flushed with `IBV_WC_WR_FLUSH_ERR`. This means you will see a burst of flush errors after any single error. Do not be alarmed by the quantity; focus on the first non-flush error to find the root cause.

</div>

### IBV_WC_GENERAL_ERR (General Error)

**What it means**: A catch-all for errors not covered by other codes.

**Common causes**: Hardware-specific errors, driver bugs, or unusual failure modes. Check `wc.vendor_err` for additional information.

## QP Error State and Recovery

When a CQE error occurs on an RC QP, the QP transitions to one of two error states:

- **SQE (Send Queue Error)**: A send operation failed. The QP can be recovered by transitioning back to RTS via `ibv_modify_qp()` (RESET → INIT → RTR → RTS sequence).
- **ERR (Error)**: A more severe error. The QP must be transitioned through RESET → INIT → RTR → RTS to recover.

### Recovering a QP

```c
int recover_qp(struct ibv_qp *qp, int ib_port,
               struct qp_info *remote_info, int gid_index)
{
    /* Step 1: Transition to RESET */
    struct ibv_qp_attr attr = { .qp_state = IBV_QPS_RESET };
    int ret = ibv_modify_qp(qp, &attr, IBV_QP_STATE);
    if (ret) {
        fprintf(stderr, "Failed to reset QP: %s\n", strerror(ret));
        return -1;
    }

    /* Step 2: RESET -> INIT */
    ret = modify_qp_to_init(qp, ib_port);
    if (ret) return -1;

    /* Step 3: INIT -> RTR (need remote info again) */
    ret = modify_qp_to_rtr(qp, ib_port, remote_info, gid_index);
    if (ret) return -1;

    /* Step 4: RTR -> RTS */
    ret = modify_qp_to_rts(qp);
    if (ret) return -1;

    printf("QP recovered successfully\n");
    return 0;
}
```

<div class="warning">

QP recovery resets the packet sequence numbers. The remote side must also be aware of the recovery and reset its QP, or the two QPs will be out of sync. In practice, QP recovery is rarely done in production; it is usually simpler to destroy the failed QP and create a new one, re-establishing the connection from scratch.

</div>

### Draining Flush Errors

When a QP enters the error state, you must drain all flush completions before the QP can be destroyed:

```c
void drain_cq(struct ibv_cq *cq)
{
    struct ibv_wc wc;
    int ne;
    do {
        ne = ibv_poll_cq(cq, 1, &wc);
        if (ne > 0 && wc.status == IBV_WC_WR_FLUSH_ERR) {
            /* Expected: flush error from QP in error state */
            continue;
        }
    } while (ne > 0);
}
```

## Async Events

Asynchronous events report significant state changes that are not tied to a specific work request. They are delivered through the device's async event file descriptor and must be retrieved with `ibv_get_async_event()`.

### Event Types

**QP Events**:
- `IBV_EVENT_QP_FATAL`: QP entered an unrecoverable error state. The QP must be destroyed.
- `IBV_EVENT_QP_REQ_ERR`: A request error occurred on the QP (protocol violation).
- `IBV_EVENT_QP_ACCESS_ERR`: An access violation occurred on the QP.
- `IBV_EVENT_QP_LAST_WQE_REACHED`: The last WQE on an SRQ-attached QP was consumed.
- `IBV_EVENT_COMM_EST`: Communication established. The QP received a message and transitioned from RTR to a state where it can process messages. Informational only.
- `IBV_EVENT_SQ_DRAINED`: The send queue has been drained (all outstanding sends completed). Generated after requesting SQ drain via QP modify.
- `IBV_EVENT_PATH_MIG`: A path migration occurred (used with automatic path migration).
- `IBV_EVENT_PATH_MIG_ERR`: Path migration failed.

**CQ Events**:
- `IBV_EVENT_CQ_ERR`: CQ overrun. The CQ is full and a completion was lost. This is fatal; the CQ and all associated QPs must be destroyed.

**Port Events**:
- `IBV_EVENT_PORT_ACTIVE`: A port became active (link up).
- `IBV_EVENT_PORT_ERR`: A port encountered an error (link down).
- `IBV_EVENT_LID_CHANGE`: The port's LID changed (IB subnet reconfiguration).
- `IBV_EVENT_PKEY_CHANGE`: The port's P_Key table changed.
- `IBV_EVENT_GID_CHANGE`: The port's GID table changed (RoCE address change).
- `IBV_EVENT_SM_CHANGE`: The subnet manager changed (IB).
- `IBV_EVENT_CLIENT_REREGISTER`: SM requests clients to re-register (IB).

**Device Events**:
- `IBV_EVENT_DEVICE_FATAL`: Catastrophic device error. All resources are invalid. The application must close the device and restart.

### Async Event Handling Thread

A dedicated thread should monitor async events throughout the application's lifetime:

```c
#include <pthread.h>

struct async_event_ctx {
    struct ibv_context *ctx;
    volatile int       running;
};

void *async_event_thread(void *arg)
{
    struct async_event_ctx *aectx = (struct async_event_ctx *)arg;
    struct ibv_async_event event;

    while (aectx->running) {
        /* ibv_get_async_event() blocks until an event is available */
        if (ibv_get_async_event(aectx->ctx, &event)) {
            if (!aectx->running)
                break;  /* Shutdown requested */
            fprintf(stderr, "Failed to get async event\n");
            continue;
        }

        switch (event.event_type) {
        /* QP events */
        case IBV_EVENT_QP_FATAL:
            fprintf(stderr, "FATAL: QP %u entered error state\n",
                    event.element.qp->qp_num);
            /* Trigger QP recovery or shutdown */
            break;

        case IBV_EVENT_QP_REQ_ERR:
            fprintf(stderr, "QP %u request error\n",
                    event.element.qp->qp_num);
            break;

        case IBV_EVENT_QP_ACCESS_ERR:
            fprintf(stderr, "QP %u access error\n",
                    event.element.qp->qp_num);
            break;

        case IBV_EVENT_COMM_EST:
            fprintf(stderr, "QP %u communication established\n",
                    event.element.qp->qp_num);
            break;

        /* CQ events */
        case IBV_EVENT_CQ_ERR:
            fprintf(stderr, "FATAL: CQ overrun\n");
            /* CQ and associated QPs must be destroyed */
            break;

        /* Port events */
        case IBV_EVENT_PORT_ACTIVE:
            fprintf(stderr, "Port %d is active\n",
                    event.element.port_num);
            break;

        case IBV_EVENT_PORT_ERR:
            fprintf(stderr, "Port %d error (link down)\n",
                    event.element.port_num);
            /* Pause operations, wait for port recovery */
            break;

        case IBV_EVENT_GID_CHANGE:
            fprintf(stderr, "Port %d GID table changed\n",
                    event.element.port_num);
            /* May need to re-resolve paths */
            break;

        /* Device events */
        case IBV_EVENT_DEVICE_FATAL:
            fprintf(stderr, "FATAL: Device error. Must restart.\n");
            aectx->running = 0;
            break;

        default:
            fprintf(stderr, "Async event: %d\n", event.event_type);
        }

        /* MUST acknowledge every event */
        ibv_ack_async_event(&event);
    }

    return NULL;
}
```

<div class="warning">

Every async event retrieved with `ibv_get_async_event()` **must** be acknowledged with `ibv_ack_async_event()`. Failing to acknowledge events will cause `ibv_close_device()` to hang, because the close operation waits for all events to be acknowledged.

</div>

### Using epoll with Async Events

The async event file descriptor can be integrated with `epoll` for event-driven architectures:

```c
int setup_async_epoll(struct ibv_context *ctx, int epfd)
{
    /* Make the async FD non-blocking */
    int flags = fcntl(ctx->async_fd, F_GETFL);
    fcntl(ctx->async_fd, F_SETFL, flags | O_NONBLOCK);

    struct epoll_event ev = {
        .events  = EPOLLIN,
        .data.fd = ctx->async_fd,
    };
    return epoll_ctl(epfd, EPOLL_CTL_ADD, ctx->async_fd, &ev);
}
```

## Debugging Strategies

### 1. Enable Verbose Debug Output

Set environment variables to enable driver-level debugging:

```bash
# Enable verbs debug output
export IBV_FORK_SAFE=1
export MLX5_DEBUG_MASK=0xFFFF    # Mellanox/NVIDIA driver debug
export RDMAV_HUGEPAGES_SAFE=1

# For rdma-core debug
export RDMAV_DEBUG=1
```

### 2. Check Hardware Counters

RDMA hardware maintains detailed error counters accessible through sysfs:

```bash
# Port-level counters
cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_errors
cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_xmit_discards
cat /sys/class/infiniband/mlx5_0/ports/1/counters/local_link_integrity_errors

# Extended counters (if supported)
perfquery -x mlx5_0 1
```

The `rdma` tool provides a convenient interface:

```bash
rdma statistic show
rdma res show qp
rdma res show cq
rdma res show mr
```

### 3. Use ibv_devinfo and ibv_devices

```bash
# List all RDMA devices
ibv_devices

# Show detailed device info
ibv_devinfo -d mlx5_0

# Check port state
ibv_devinfo -d mlx5_0 -i 1
```

### 4. Packet Capture

For RoCE, standard packet capture tools work:

```bash
# Capture RDMA traffic (RoCE uses UDP port 4791)
tcpdump -i eth0 udp port 4791 -w rdma_capture.pcap
```

For InfiniBand, use `ibdump` (NVIDIA/Mellanox tool):

```bash
ibdump -d mlx5_0 -i 1 -o ib_capture.pcap
```

### 5. Systematic Error Checking Macro

Wrap every verb call with error checking:

```c
#define CHECK_ERRNO(call, msg) do {           \
    if ((call)) {                              \
        fprintf(stderr, "%s:%d: %s: %s\n",    \
                __FILE__, __LINE__, msg,       \
                strerror(errno));              \
        exit(EXIT_FAILURE);                    \
    }                                          \
} while (0)

#define CHECK_NULL(ptr, msg) do {              \
    if (!(ptr)) {                              \
        fprintf(stderr, "%s:%d: %s: %s\n",    \
                __FILE__, __LINE__, msg,       \
                strerror(errno));              \
        exit(EXIT_FAILURE);                    \
    }                                          \
} while (0)

/* Usage */
CHECK_NULL(pd = ibv_alloc_pd(ctx), "ibv_alloc_pd failed");
CHECK_ERRNO(ibv_modify_qp(qp, &attr, flags), "modify QP to INIT");
```

## Common Mistakes and Their Symptoms

### Mistake 1: Not Pre-Posting Receive Buffers

**Symptom**: `IBV_WC_RNR_RETRY_EXC_ERR` on the sender. The receiver's QP may also enter an error state.

**Fix**: Always post receive buffers before the remote side can send. In the ping-pong pattern, post the receive before sending the message that will trigger the remote side's reply.

### Mistake 2: Wrong Access Flags on MR or QP

**Symptom**: `IBV_WC_REM_ACCESS_ERR` for RDMA Read/Write. `IBV_WC_LOC_PROT_ERR` for local access issues.

**Fix**: Use `IBV_ACCESS_REMOTE_WRITE` on both QP and MR for RDMA Write. Use `IBV_ACCESS_REMOTE_READ` for RDMA Read. Always include `IBV_ACCESS_LOCAL_WRITE` for any MR that the RNIC writes to.

### Mistake 3: Mismatched QP Parameters

**Symptom**: `IBV_WC_RETRY_EXC_ERR`. Packets are sent but the remote side does not recognize them.

**Fix**: Ensure `sq_psn` on one side matches `rq_psn` on the other. Ensure `dest_qp_num` is correct. Double-check GID and LID values.

### Mistake 4: Not Accounting for GRH in UD Receive

**Symptom**: `IBV_WC_LOC_LEN_ERR` on UD receives.

**Fix**: Add 40 bytes to every UD receive buffer for the GRH.

### Mistake 5: Forgetting to Arm CQ for Event Notification

**Symptom**: `ibv_get_cq_event()` blocks forever, even though completions are arriving.

**Fix**: Call `ibv_req_notify_cq()` before blocking on `ibv_get_cq_event()`. Remember to re-arm after each event.

### Mistake 6: CQ Overflow

**Symptom**: `IBV_EVENT_CQ_ERR` async event. Completions are lost. QPs associated with the CQ may enter error state.

**Fix**: Size the CQ large enough to hold all possible outstanding completions. Poll completions promptly.

### Mistake 7: Using Stale MR Keys After Re-registration

**Symptom**: `IBV_WC_REM_ACCESS_ERR` or `IBV_WC_LOC_PROT_ERR`.

**Fix**: When an MR is deregistered and re-registered, the L_Key and R_Key change. Update all references to the old keys.

### Mistake 8: Destroying Resources in Wrong Order

**Symptom**: `ibv_destroy_qp()`, `ibv_dereg_mr()`, or `ibv_destroy_cq()` returns `EBUSY`.

**Fix**: Destroy resources in reverse order of creation: QP first, then MR, then CQ, then PD, then close the device. Drain all outstanding completions before destroying the CQ.

## Diagnostic Checklist

When an RDMA operation fails, work through this checklist:

1. **Check the completion status code** and look up its meaning in the table above.
2. **Check QP state**: Use `ibv_query_qp()` to verify the QP is in RTS.
3. **Check MR validity**: Ensure the MR is still registered and the L_Key/R_Key are current.
4. **Check access flags**: Verify both QP and MR have the required access permissions.
5. **Check address arithmetic**: For RDMA Read/Write, verify the remote address and length are within the MR boundaries.
6. **Check network connectivity**: Use `ibv_devinfo` to verify port state, and `ibping` or `rping` to verify basic RDMA connectivity.
7. **Check hardware counters**: Look for packet errors, link integrity errors, or buffer overruns.
8. **Check the remote side**: The error may originate from the remote machine. Check remote logs and counters.
9. **Reproduce with a simple test**: Use `ib_send_bw`, `ib_read_bw`, or `rping` to verify that basic RDMA operations work between the two machines.

## Key Takeaways

1. **Errors surface through three channels**: Synchronous verb returns, CQE status codes, and async events. Monitor all three.
2. **`IBV_WC_RETRY_EXC_ERR` is the most common error**: It usually indicates network or QP setup problems, not application bugs.
3. **Flush errors are secondary symptoms**: When a QP enters the error state, all outstanding WRs are flushed. Find the first non-flush error.
4. **Async events require a dedicated thread**: Start the async event thread early and keep it running throughout the application's lifetime.
5. **Every async event must be acknowledged**: Failing to call `ibv_ack_async_event()` will cause `ibv_close_device()` to hang.
6. **Hardware counters are your best diagnostic tool**: They reveal errors that may not be visible through the software API.
7. **QP recovery is possible but rarely practical**: Destroying and recreating the QP is usually simpler than recovering it.
