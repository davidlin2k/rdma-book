# 9.2 RC RDMA Write

RDMA Write is the first of the two one-sided operations in the RDMA programming model. Unlike Send/Receive, an RDMA Write transfers data directly from the initiator's local memory into a specified location in the target's remote memory, without any involvement from the target's CPU. The target does not post a receive buffer, does not consume a receive work request, and does not receive a completion queue entry (unless Write-with-Immediate is used). The target's RNIC handles the incoming write autonomously using information embedded in the packet headers.

This one-sided nature makes RDMA Write an excellent building block for high-performance systems where the writer knows the remote memory layout in advance, such as replicated storage, distributed shared memory, and RDMA-based key-value stores.

The full source code for this section is in `src/code/ch09-rdma-write/`.

## How RDMA Write Differs from Send/Receive

| Aspect | Send/Receive | RDMA Write |
|--------|-------------|------------|
| Receiver involvement | Must pre-post receive buffers | None (CPU not involved) |
| Receiver completion | CQE generated on receive | No CQE (unless Write-with-Immediate) |
| Remote memory info needed | No | Yes (remote address + R_Key) |
| QP access flags | None needed | `IBV_ACCESS_REMOTE_WRITE` on target |
| Target CPU notification | Implicit (CQE) | Requires separate mechanism |

The key tradeoff: RDMA Write achieves lower latency and higher throughput because the remote CPU is not in the critical path, but the initiator must know exactly where to write in the remote address space, and the remote side has no built-in mechanism to know that a write has completed.

## Exchanging Memory Region Information

In addition to the QP metadata exchange described in Section 9.1, RDMA Write requires the target to share its memory region information with the initiator. The initiator needs:

- **Remote virtual address**: The starting address of the remote buffer, as seen by the remote RNIC.
- **Remote key (R_Key)**: The authorization token that the remote RNIC uses to verify that the initiator is allowed to access this memory region.

```c
struct mr_info {
    uint64_t addr;
    uint32_t rkey;
    uint32_t length;
};
```

The server (target) populates this structure after registering its memory region:

```c
struct ibv_mr *mr = ibv_reg_mr(pd, buf, BUF_SIZE,
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_WRITE);

struct mr_info local_mr_info = {
    .addr   = (uint64_t)(uintptr_t)mr->addr,
    .rkey   = mr->rkey,
    .length = BUF_SIZE,
};
```

This information is exchanged over the same TCP side channel used for QP metadata:

```c
int exchange_mr_info(int sockfd, struct mr_info *local, struct mr_info *remote)
{
    if (write(sockfd, local, sizeof(*local)) != sizeof(*local))
        return -1;
    if (read(sockfd, remote, sizeof(*remote)) != sizeof(*remote))
        return -1;
    return 0;
}
```

<div class="warning">

The R_Key is a security-critical value. Anyone who possesses a valid R_Key and remote address can read from or write to the corresponding memory region (depending on the access flags). In production systems, R_Keys should be treated with the same care as encryption keys. Do not log them, do not transmit them over unencrypted channels in security-sensitive environments, and deregister memory regions promptly when they are no longer needed.

</div>

## QP Access Flags for RDMA Write

The target QP must be configured to allow remote write access during the INIT state transition:

```c
struct ibv_qp_attr attr = {
    .qp_state        = IBV_QPS_INIT,
    .pkey_index      = 0,
    .port_num        = ib_port,
    .qp_access_flags = IBV_ACCESS_REMOTE_WRITE,
};
```

Additionally, the memory region on the target must be registered with `IBV_ACCESS_REMOTE_WRITE`. If either the QP or the MR lacks the appropriate access flag, the RDMA Write will fail with a remote access error (`IBV_WC_REM_ACCESS_ERR`).

The MR must also include `IBV_ACCESS_LOCAL_WRITE` because the RNIC needs to write incoming data into the buffer. This is a hardware requirement: any MR that will be written to, whether locally or remotely, must have `IBV_ACCESS_LOCAL_WRITE` set.

## Posting an RDMA Write

The RDMA Write work request includes the remote address and key in addition to the local scatter-gather list:

```c
struct ibv_sge sge = {
    .addr   = (uintptr_t)local_buf,
    .length = data_len,
    .lkey   = local_mr->lkey,
};

struct ibv_send_wr wr = {
    .wr_id      = 0,
    .sg_list    = &sge,
    .num_sge    = 1,
    .opcode     = IBV_WR_RDMA_WRITE,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.rdma = {
        .remote_addr = remote_mr_info.addr,
        .rkey        = remote_mr_info.rkey,
    },
};

struct ibv_send_wr *bad_wr;
int ret = ibv_post_send(qp, &wr, &bad_wr);
```

The opcode `IBV_WR_RDMA_WRITE` tells the RNIC to perform a one-sided write. The `.wr.rdma` sub-structure provides the remote address and key. The local SGE describes where to read the data from in local memory.

## Writing to Specific Offsets

The `remote_addr` field does not have to point to the beginning of the remote MR. You can write to any offset within the registered region:

```c
/* Write 256 bytes to offset 1024 in the remote buffer */
wr.wr.rdma.remote_addr = remote_mr_info.addr + 1024;
sge.length = 256;
```

This capability is essential for building data structures in remote memory. For example, a remote ring buffer can be implemented by writing to successive offsets and wrapping around at the end of the buffer.

<div class="tip">

When writing to offsets within a remote MR, ensure that `remote_addr + length` does not exceed the end of the remote MR. The RNIC will reject writes that exceed the MR boundaries, resulting in a remote access error.

</div>

## Signaling Completion to the Remote Side

The most significant challenge with RDMA Write is that the target receives no notification when a write completes. The target's CPU is completely uninvolved. This creates a fundamental question: how does the target know that new data is available?

Several patterns address this problem:

### Pattern 1: Write-with-Immediate Data

RDMA Write-with-Immediate (`IBV_WR_RDMA_WRITE_WITH_IMM`) combines an RDMA Write with a 32-bit immediate value. The data is written to the remote buffer just like a regular RDMA Write, but additionally, a receive completion is generated on the target's completion queue, containing the 32-bit immediate value. The target must have a receive buffer posted (the receive buffer itself is not written to; it is consumed solely to generate the completion).

```c
struct ibv_send_wr wr = {
    .wr_id      = 0,
    .sg_list    = &sge,
    .num_sge    = 1,
    .opcode     = IBV_WR_RDMA_WRITE_WITH_IMM,
    .send_flags = IBV_SEND_SIGNALED,
    .imm_data   = htonl(data_length),  /* 32-bit immediate value */
    .wr.rdma = {
        .remote_addr = remote_addr,
        .rkey        = remote_rkey,
    },
};
```

On the target side, the completion entry for a Write-with-Immediate has:
- `wc.opcode = IBV_WC_RECV_RDMA_WITH_IMM`
- `wc.imm_data` containing the 32-bit immediate value (in network byte order)
- `wc.byte_len = 0` (the receive buffer is not used for data)

This pattern is elegant because it provides both the data transfer and the notification in a single operation. The 32-bit immediate value can encode metadata such as the data length, a sequence number, or a buffer index.

<div class="note">

Write-with-Immediate consumes a receive work request on the target, just like a regular Send. The target must have receive buffers posted. If no receive buffer is available, the sender will get an RNR retry or a fatal error, depending on the `rnr_retry` setting.

</div>

### Pattern 2: Polling a Shared Flag

The initiator writes data to the remote buffer and then writes a flag value to a known location in the remote buffer (using a second RDMA Write). The target polls this flag location:

```c
/* Initiator: write data, then write flag */
post_rdma_write(qp, data_buf, data_len, remote_data_addr, rkey);
poll_completion(cq);  /* Wait for data write to complete */

/* Write a "data ready" flag to a known offset */
uint32_t flag = 1;
post_rdma_write(qp, &flag, sizeof(flag), remote_flag_addr, rkey);
poll_completion(cq);

/* Target: poll the flag */
volatile uint32_t *flag_ptr = (volatile uint32_t *)(buf + FLAG_OFFSET);
while (*flag_ptr == 0) {
    /* busy poll */
}
/* Data is now available in buf */
```

This pattern avoids the need for receive buffers on the target but requires careful ordering. The data write **must** complete before the flag write is issued. Since RDMA guarantees in-order delivery on an RC QP, waiting for the data write completion before posting the flag write ensures that the target will see the data when it observes the flag change.

<div class="warning">

The flag must be declared `volatile` to prevent the compiler from optimizing away the polling loop. Additionally, on architectures with weak memory ordering (such as ARM), a memory barrier may be needed between reading the flag and reading the data. On x86, loads are not reordered with other loads, so a compiler barrier (`asm volatile("" ::: "memory")`) suffices.

</div>

### Pattern 3: Send After Write

The initiator performs the RDMA Write and then sends a small notification message via Send:

```c
/* Initiator: write data, then send notification */
post_rdma_write(qp, data_buf, data_len, remote_data_addr, rkey);
poll_completion(cq);

/* Send a notification */
post_send(qp, &notification, sizeof(notification), mr);
poll_completion(cq);
```

The target posts a receive buffer for the notification message. When the receive completes, the target knows that the preceding RDMA Write has completed (because RC guarantees in-order delivery) and can safely read the data from its buffer.

This pattern is more robust than flag polling because the notification arrives through the standard completion queue mechanism, but it consumes receive work requests on the target.

## Complete Example Walkthrough

Our example implements a simple one-shot RDMA Write:

1. **Server** registers a buffer with `IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE`.
2. Both sides exchange QP info and MR info over TCP.
3. Both sides transition QPs to RTS.
4. **Client** fills its local buffer with a message and posts an RDMA Write to the server's buffer.
5. Client waits for the write completion, then sends a small "done" message via Send.
6. **Server** waits for the "done" message (via receive completion), then prints the contents of its buffer to verify that the data arrived.

```c
if (is_server) {
    /* Initialize buffer to zeros */
    memset(buf, 0, BUF_SIZE);

    /* Post receive for the "done" notification */
    post_receive(qp, notify_buf, sizeof(notify_buf), notify_mr);

    /* Wait for client's notification that write is complete */
    poll_completion(cq);

    /* Print the buffer contents - written by the client */
    printf("Server buffer: %s\n", buf);
} else {
    /* Fill the buffer with a message */
    snprintf(buf, BUF_SIZE, "Hello from RDMA Write client!");

    /* RDMA Write to server's buffer */
    post_rdma_write(qp, buf, strlen(buf) + 1,
                    remote_mr_info.addr, remote_mr_info.rkey);
    poll_completion(cq);

    /* Send notification that write is done */
    post_send(qp, "done", 5, notify_mr);
    poll_completion(cq);
}
```

## Inline Data Optimization

For small writes (typically up to 256 bytes, depending on the hardware), RDMA Write can use **inline data**. When the `IBV_SEND_INLINE` flag is set, the RNIC reads the data directly from the work request rather than performing a DMA read from the registered buffer. This eliminates one DMA operation and can reduce latency for small messages:

```c
struct ibv_send_wr wr = {
    .opcode     = IBV_WR_RDMA_WRITE,
    .send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE,
    .sg_list    = &sge,
    .num_sge    = 1,
    .wr.rdma = {
        .remote_addr = remote_addr,
        .rkey        = remote_rkey,
    },
};
```

When using inline data, the send SGE does not need to reference a registered memory region. The `lkey` field is ignored. This can be useful when writing data from stack variables or other non-registered memory.

<div class="note">

The maximum inline data size must be specified when creating the QP, via `qp_init_attr.cap.max_inline_data`. The driver may round this up to the nearest supported value. Check `ibv_query_device()` to find the device's maximum supported inline data size.

</div>

## Performance Considerations

RDMA Write typically achieves the best latency and bandwidth of all RDMA operations because:

1. **No receive posting overhead on the target**: The target RNIC handles writes autonomously.
2. **No completion queue overhead on the target**: Unless Write-with-Immediate is used, no CQE is generated on the target.
3. **Single-packet path**: For writes that fit within the path MTU, the entire operation is a single request-ACK exchange.

For large writes that exceed the path MTU, the RNIC automatically segments the data into multiple packets. This segmentation is transparent to the application. The completion on the initiator side is generated only after all packets have been acknowledged.

Typical RDMA Write latencies on modern hardware:
- **Small messages (< 256 bytes)**: 1.0 - 1.5 microseconds
- **Medium messages (4 KB)**: 2 - 3 microseconds
- **Large messages (1 MB)**: Dominated by bandwidth; ~25 microseconds at 100 Gbps

## Key Takeaways

1. **RDMA Write is one-sided**: The target CPU is not involved in the data transfer. This is both a strength (low latency, high throughput) and a challenge (no built-in notification).
2. **Remote memory metadata must be exchanged**: The initiator needs the remote address and R_Key before it can write.
3. **Access flags are required on both the QP and the MR**: The target's QP must have `IBV_ACCESS_REMOTE_WRITE` set during the INIT transition, and the MR must be registered with both `IBV_ACCESS_LOCAL_WRITE` and `IBV_ACCESS_REMOTE_WRITE`.
4. **Notification requires a separate mechanism**: Choose from Write-with-Immediate, flag polling, Send-after-Write, or application-level protocols depending on your latency and complexity requirements.
5. **Inline data optimizes small writes**: For messages up to ~256 bytes, inline data eliminates a DMA read and reduces latency.

The complete source code is in `src/code/ch09-rdma-write/rdma_write.c`.
