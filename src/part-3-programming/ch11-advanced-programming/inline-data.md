# 11.4 Inline Data

## The Small Message Problem

When an application posts a send work request, the NIC processes it in two steps. First, the NIC reads the Work Queue Element (WQE) from host memory via a DMA read over PCIe. The WQE contains the operation type, flags, and a scatter-gather list pointing to the data buffers. Second, the NIC performs another DMA read to fetch the actual message data from the buffer described in the scatter-gather list. For a typical send of a small message, this means two PCIe round trips before the data even hits the wire.

For large messages, the overhead of two DMA reads is negligible relative to the data transfer time. But for small messages -- control packets, acknowledgments, metadata updates, RPC headers -- the second DMA read can represent a significant fraction of the total latency. If the message is only 32 bytes, spending a full PCIe round trip (roughly 200-500 nanoseconds) to fetch those 32 bytes is wasteful.

## The Inline Data Solution

The `IBV_SEND_INLINE` flag eliminates the second DMA read by copying the message data directly into the WQE itself. Instead of storing a pointer to the data buffer, the NIC finds the data embedded in the WQE. The WQE is fetched in a single DMA read, and the NIC has everything it needs to transmit the message.

```
Without inline:
  WQE (DMA read #1) → contains pointer to data
  Data (DMA read #2) → contains actual payload
  Total: 2 PCIe round trips

With inline:
  WQE (DMA read #1) → contains actual payload embedded
  Total: 1 PCIe round trip
```

This saves one PCIe transaction per send, which typically translates to 100-500 nanoseconds of latency reduction for small messages.

## Using Inline Data

### Requesting Inline Capacity

Inline data support must be requested at QP creation time through the `max_inline_data` field:

```c
struct ibv_qp_init_attr qp_attr = {
    .send_cq = cq,
    .recv_cq = cq,
    .qp_type = IBV_QPT_RC,
    .cap = {
        .max_send_wr = 64,
        .max_recv_wr = 64,
        .max_send_sge = 1,
        .max_recv_sge = 1,
        .max_inline_data = 128   /* Request 128 bytes inline capacity */
    }
};

struct ibv_qp *qp = ibv_create_qp(pd, &qp_attr);

/* Check what we actually got */
printf("Actual max_inline_data: %u\n", qp_attr.cap.max_inline_data);
```

The device may grant more or less inline capacity than requested. The actual value is returned in `qp_attr.cap.max_inline_data` after QP creation. Typical values range from 36 bytes (older HCAs) to 256 bytes or more (modern ConnectX devices). Some devices support up to 512 bytes or even 1 KB of inline data.

<div class="warning">

**Important**: Requesting a larger `max_inline_data` increases the WQE size, which means fewer WQEs fit in the send queue for a given `max_send_wr`. The device may silently reduce `max_send_wr` to accommodate larger WQEs. Always check the returned capabilities after QP creation.

</div>

### Sending with Inline Flag

To send data inline, set the `IBV_SEND_INLINE` flag on the send work request:

```c
char message[] = "Hello, RDMA!";

struct ibv_sge sge = {
    .addr = (uint64_t)(uintptr_t)message,
    .length = sizeof(message),
    .lkey = 0   /* lkey is ignored for inline sends! */
};

struct ibv_send_wr wr = {
    .wr_id = 1,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE
};

struct ibv_send_wr *bad_wr;
int ret = ibv_post_send(qp, &wr, &bad_wr);
```

There are two critical details in this code:

1. **The `lkey` is ignored**: Because the data is copied into the WQE by the CPU (not DMA-read by the NIC), the memory does not need to be registered. You can inline-send data from any memory, including stack buffers, without registering it as a memory region. This is a significant convenience for small messages.

2. **The data is copied during `ibv_post_send()`**: The library copies the data into the WQE as part of the post operation. After `ibv_post_send()` returns, the original buffer can be immediately modified or freed -- there is no need to wait for a completion. This is different from non-inline sends, where the buffer must remain untouched until the send completes.

<div class="tip">

**Tip**: The fact that inline sends do not require memory registration makes them ideal for sending small, transient data like RPC headers, control messages, or metadata. You can construct the message on the stack and send it inline without any memory management overhead.

</div>

### Inline with RDMA Write

Inline data is not limited to send operations. RDMA Write can also use inline data:

```c
struct ibv_send_wr wr = {
    .wr_id = 2,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_RDMA_WRITE,
    .send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE,
    .wr.rdma = {
        .remote_addr = remote_addr,
        .rkey = remote_rkey
    }
};
```

This is useful for writing small amounts of data (doorbell values, status flags, counters) to remote memory with minimal latency.

### Inline with RDMA Write with Immediate

Similarly, RDMA Write with Immediate can be combined with inline:

```c
struct ibv_send_wr wr = {
    .opcode = IBV_WR_RDMA_WRITE_WITH_IMM,
    .send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE,
    .imm_data = htonl(42),
    .wr.rdma = {
        .remote_addr = remote_addr,
        .rkey = remote_rkey
    },
    .sg_list = &sge,
    .num_sge = 1
};
```

## Performance Impact

The performance benefit of inline data is most pronounced for small messages and latency-sensitive operations. The following table illustrates typical latency improvements measured on ConnectX-6 hardware:

| Message Size | Non-Inline Latency | Inline Latency | Improvement |
|-------------|-------------------|----------------|-------------|
| 8 bytes | ~1.5 us | ~1.1 us | ~27% |
| 32 bytes | ~1.5 us | ~1.1 us | ~27% |
| 64 bytes | ~1.5 us | ~1.2 us | ~20% |
| 128 bytes | ~1.6 us | ~1.3 us | ~19% |
| 256 bytes | ~1.7 us | ~1.5 us | ~12% |
| 512 bytes (non-inline) | ~1.8 us | N/A | N/A |

The absolute improvement is roughly constant (one PCIe round trip), but the relative improvement decreases as message size grows because the data transfer time begins to dominate. Beyond the `max_inline_data` threshold, inline is not available and the standard two-DMA-read path is used.

For **throughput** workloads with many outstanding sends, the benefit is smaller because PCIe transactions are pipelined and the second DMA read can overlap with WQE processing. However, inline still helps by reducing PCIe bandwidth consumption, which can matter when the PCIe link is a bottleneck.

## When to Use Inline Data

Use inline data when:

- **Messages are smaller than `max_inline_data`**: There is essentially no downside. The latency improvement is free.
- **Latency matters more than throughput**: The one-PCIe-round-trip saving is most valuable in latency-sensitive paths.
- **Data is on the stack or in unregistered memory**: Inline eliminates the need for memory registration, simplifying the code path.
- **Sending control messages or metadata**: RPCs, acknowledgments, heartbeats, and similar small messages are ideal candidates.

Do not use inline data when:

- **Messages exceed `max_inline_data`**: The `ibv_post_send()` call will fail with an error.
- **You need maximum send queue depth**: Larger WQEs (due to embedded data) reduce the effective send queue capacity. If you need thousands of outstanding sends, the reduced queue depth may be limiting.

## Querying Device Capabilities

To determine the maximum inline data size supported by a device:

```c
struct ibv_device_attr dev_attr;
ibv_query_device(ctx, &dev_attr);

/* Note: max_inline_data is not in ibv_device_attr.
   It is determined at QP creation time. Request a large
   value and check what the device actually grants. */

struct ibv_qp_init_attr attr = {
    /* ... */
    .cap.max_inline_data = 512  /* Request generously */
};
struct ibv_qp *qp = ibv_create_qp(pd, &attr);
printf("Device supports up to %u bytes inline\n",
       attr.cap.max_inline_data);
```

<div class="tip">

**Tip**: There is no device-level query for maximum inline data size. The only reliable way to determine it is to create a QP with a large `max_inline_data` request and check the returned value. Some drivers (mlx5) support up to 512 bytes or more; others may be limited to 64 bytes. If you request more than the device supports, QP creation still succeeds but `max_inline_data` is capped at the device maximum.

</div>

## Combining Inline with Other Optimizations

Inline data combines well with other optimizations:

- **Unsignaled sends + inline**: For RPC-style workloads, send the request inline and unsignaled (signal every Nth send for completion tracking). This minimizes both latency and completion overhead.

- **Inline + doorbell batching**: When posting multiple sends, some drivers can batch the doorbell ring. Combining this with inline data maximizes the sends-per-doorbell ratio.

- **Inline for control path, regular for data path**: Use inline sends for small control messages (connection setup, flow control tokens, metadata) and regular sends for bulk data transfers. This naturally optimizes each path.

Inline data is one of the simplest RDMA optimizations to implement -- often just adding a single flag to existing code -- yet it provides measurable latency improvements for the small-message workloads that dominate many distributed systems.
