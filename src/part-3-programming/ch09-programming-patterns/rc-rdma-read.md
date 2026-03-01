# 9.3 RC RDMA Read

RDMA Read is the complement of RDMA Write: the initiator reads data from a specified location in the target's remote memory, pulling it into local memory. Like RDMA Write, it is a one-sided operation. The target's CPU is not involved. No receive buffer is consumed on either side, and no completion is generated on the target. The target's RNIC responds to the read request autonomously, fetching the requested data from its registered memory and sending it back to the initiator.

RDMA Read is the foundation for building remote data structure traversal patterns. An application can inspect a remote hash table, walk a remote linked list, or read a remote log, all without the remote CPU performing any work. This section develops the RDMA Read pattern and introduces the metadata-read-then-data-read idiom that appears frequently in distributed systems.

The full source code is in `src/code/ch09-rdma-read/`.

## How RDMA Read Works

When the initiator posts an RDMA Read work request, the following sequence occurs at the hardware level:

1. The initiator's RNIC sends an RDMA Read Request packet to the target, containing the remote address, R_Key, and requested length.
2. The target's RNIC receives the request, validates the R_Key and address against the registered memory region, performs a DMA read from the target's memory, and sends the data back in one or more RDMA Read Response packets.
3. The initiator's RNIC receives the response data and performs a DMA write into the initiator's local buffer.
4. A completion queue entry is generated on the **initiator's** CQ.

No CQE is generated on the target. No receive buffer is consumed on either side. The target's CPU has no awareness that a read occurred.

## RDMA Read vs. RDMA Write

| Aspect | RDMA Write | RDMA Read |
|--------|-----------|-----------|
| Data direction | Initiator → Target | Target → Initiator |
| Packets on wire | Request (data) + ACK | Request + Response (data) |
| Latency | Lower (one round-trip) | Higher (request + response) |
| `max_rd_atomic` relevant | No | Yes |
| Target QP access flag | `IBV_ACCESS_REMOTE_WRITE` | `IBV_ACCESS_REMOTE_READ` |
| Target MR access flag | `IBV_ACCESS_REMOTE_WRITE` | `IBV_ACCESS_REMOTE_READ` |

RDMA Read has inherently higher latency than RDMA Write for small messages because it requires a full round trip: the request must travel to the target, the target must fetch the data from memory, and the response must travel back. For small messages, this roughly doubles the latency compared to RDMA Write. For large messages, the difference is less pronounced because the data transfer time dominates.

## Setting Up for RDMA Read

### QP Access Flags

The target QP must allow remote read access:

```c
struct ibv_qp_attr attr = {
    .qp_state        = IBV_QPS_INIT,
    .pkey_index      = 0,
    .port_num        = ib_port,
    .qp_access_flags = IBV_ACCESS_REMOTE_READ,
};
```

If the application needs both RDMA Read and Write, combine the flags:

```c
.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE,
```

### Memory Region Access Flags

The target's MR must include `IBV_ACCESS_REMOTE_READ`:

```c
struct ibv_mr *mr = ibv_reg_mr(pd, buf, BUF_SIZE,
                                IBV_ACCESS_LOCAL_WRITE |
                                IBV_ACCESS_REMOTE_READ);
```

The initiator's MR needs `IBV_ACCESS_LOCAL_WRITE` because the RNIC will write the incoming read response data into the local buffer.

### max_rd_atomic and max_dest_rd_atomic

RDMA Read requests are special because they consume resources on the target's RNIC. The target must maintain state for each outstanding read request until the response is fully sent. The number of outstanding RDMA Read (and Atomic) operations is limited by two parameters:

- **`max_rd_atomic`** (set during RTS): The maximum number of outstanding RDMA Read/Atomic operations that this QP can **initiate**.
- **`max_dest_rd_atomic`** (set during RTR): The maximum number of outstanding RDMA Read/Atomic operations that this QP can **respond to** (i.e., that the remote side can have in flight).

These values must be coordinated: the initiator's `max_rd_atomic` must not exceed the target's `max_dest_rd_atomic`. A typical setting is:

```c
/* During RTR transition */
attr.max_dest_rd_atomic = 4;

/* During RTS transition */
attr.max_rd_atomic = 4;
```

<div class="warning">

If `max_rd_atomic` and `max_dest_rd_atomic` are set to 0, RDMA Read operations will fail. This is a common mistake when reusing Send/Receive setup code for RDMA Read applications. Always check that these values are non-zero when using RDMA Read.

</div>

## Posting an RDMA Read

The RDMA Read work request looks similar to an RDMA Write, but the data flows in the opposite direction:

```c
struct ibv_sge sge = {
    .addr   = (uintptr_t)local_buf,   /* Where to store read data */
    .length = read_len,
    .lkey   = local_mr->lkey,
};

struct ibv_send_wr wr = {
    .wr_id      = 0,
    .sg_list    = &sge,
    .num_sge    = 1,
    .opcode     = IBV_WR_RDMA_READ,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.rdma = {
        .remote_addr = remote_mr_info.addr,
        .rkey        = remote_mr_info.rkey,
    },
};

struct ibv_send_wr *bad_wr;
int ret = ibv_post_send(qp, &wr, &bad_wr);
```

Note that RDMA Read is posted to the **Send Queue**, even though data flows from remote to local. This is because RDMA Read is an initiator-driven operation: the initiator decides what to read and when.

The SGE describes the **local destination buffer** where the read data will be placed. After the completion, this buffer contains the data that was at the specified remote address.

## Reading from Specific Offsets

Like RDMA Write, you can read from any offset within the remote MR:

```c
/* Read 128 bytes starting at offset 512 in the remote buffer */
wr.wr.rdma.remote_addr = remote_mr_info.addr + 512;
sge.length = 128;
```

This is the basis for remote data structure access. If you know the layout of a data structure in remote memory, you can read individual fields by computing their offsets.

## The Metadata-Read-Then-Data-Read Pattern

One of the most powerful RDMA Read patterns involves two phases: first, read a small metadata header from a known location in remote memory to discover the layout or availability of data, then use that metadata to issue a second, targeted read for the actual data.

Consider a remote node that maintains a shared data structure:

```c
/* Remote side's data structure (in registered memory) */
struct remote_data {
    uint32_t magic;       /* Validity marker */
    uint32_t data_len;    /* Length of payload */
    uint64_t sequence;    /* Sequence number */
    char     payload[];   /* Variable-length payload */
};
```

The initiator first reads the header to learn the data length and sequence number:

```c
/* Phase 1: Read the header (16 bytes) */
struct remote_data header;
struct ibv_sge sge = {
    .addr   = (uintptr_t)&header,
    .length = sizeof(uint32_t) * 2 + sizeof(uint64_t),  /* 16 bytes */
    .lkey   = local_mr->lkey,
};

struct ibv_send_wr wr = {
    .opcode     = IBV_WR_RDMA_READ,
    .send_flags = IBV_SEND_SIGNALED,
    .sg_list    = &sge,
    .num_sge    = 1,
    .wr.rdma = {
        .remote_addr = remote_data_addr,
        .rkey        = remote_rkey,
    },
};

ibv_post_send(qp, &wr, &bad_wr);
poll_completion(cq);

/* Inspect the header */
if (header.magic != EXPECTED_MAGIC) {
    /* Data not ready or corrupted */
    return -1;
}

/* Phase 2: Read the payload based on header info */
uint64_t payload_addr = remote_data_addr +
    offsetof(struct remote_data, payload);

sge.addr   = (uintptr_t)local_payload_buf;
sge.length = header.data_len;

wr.wr.rdma.remote_addr = payload_addr;

ibv_post_send(qp, &wr, &bad_wr);
poll_completion(cq);

/* local_payload_buf now contains the remote payload */
printf("Read %u bytes, sequence %lu\n",
       header.data_len, header.sequence);
```

This two-phase approach is more efficient than reading the entire maximum-sized buffer because:
1. The header read is small (16 bytes) and completes quickly.
2. The data read transfers exactly the needed number of bytes.
3. The header can indicate whether data is available at all, avoiding unnecessary large reads.

<div class="note">

Consistency is a concern with the metadata-read-then-data-read pattern. If the remote side updates the data structure concurrently with the reads, the initiator might read an inconsistent state (e.g., a new header with an old payload, or vice versa). Techniques to address this include:

- **Sequence numbers**: Include a sequence number at both the beginning and end of the data. The reader checks that both match.
- **Write-once semantics**: The remote side writes to a new location each time, and updates the header atomically to point to the new data.
- **RDMA Atomic operations**: Use Compare-and-Swap to coordinate access.

</div>

## Remote Data Structure Traversal

RDMA Read enables traversal of remote data structures without any involvement from the remote CPU. Consider a remote linked list:

```c
struct remote_node {
    uint64_t next_addr;   /* Remote address of next node (0 = end) */
    uint32_t key;
    uint32_t value_len;
    char     value[];
};
```

The initiator can walk this list by repeatedly reading nodes:

```c
uint64_t current_addr = remote_list_head_addr;

while (current_addr != 0) {
    /* Read the node header */
    struct remote_node node;
    rdma_read(qp, &node, sizeof(node), current_addr, remote_rkey);
    poll_completion(cq);

    printf("Node at 0x%lx: key=%u, value_len=%u\n",
           current_addr, node.key, node.value_len);

    if (node.key == target_key) {
        /* Found it - read the value */
        char *value = malloc(node.value_len);
        uint64_t value_addr = current_addr +
            offsetof(struct remote_node, value);
        rdma_read(qp, value, node.value_len, value_addr, remote_rkey);
        poll_completion(cq);
        /* Process value... */
        free(value);
        break;
    }

    current_addr = node.next_addr;
}
```

Each node read is an independent RDMA Read operation. For a list of length N, this requires N round trips, which can be expensive. Several optimizations are possible:

- **Prefetching**: Post multiple RDMA Read operations simultaneously to overlap network latency. Read the next node while processing the current one.
- **Batching**: If the list is stored contiguously in memory, read a chunk of multiple nodes at once.
- **B-tree structures**: Use tree structures instead of linked lists to reduce the number of hops. A B-tree with fanout F requires only log_F(N) RDMA Read round trips.

## Complete Example Walkthrough

Our example implements a straightforward remote memory read:

1. **Server** fills its buffer with a message, registers the MR with `IBV_ACCESS_REMOTE_READ`, and shares the MR info.
2. **Client** receives the MR info, issues an RDMA Read to pull the data from the server's buffer, and prints the result.

```c
if (is_server) {
    /* Fill the buffer with data */
    snprintf(buf, BUF_SIZE, "This data lives on the server. "
             "The client will read it via RDMA Read.");

    printf("Server buffer initialized: \"%s\"\n", buf);

    /* Exchange MR info so client knows where to read */
    exchange_mr_info(sockfd, &local_mr_info, &remote_mr_info);

    /* Wait for client to signal it has finished reading */
    post_receive(qp, notify_buf, sizeof(notify_buf), notify_mr);
    poll_completion(cq);
    printf("Client has finished reading.\n");
} else {
    /* Exchange MR info to learn server's buffer location */
    exchange_mr_info(sockfd, &local_mr_info, &remote_mr_info);

    /* Clear local buffer */
    memset(buf, 0, BUF_SIZE);

    /* RDMA Read from server's buffer into local buffer */
    post_rdma_read(qp, buf, BUF_SIZE,
                   remote_mr_info.addr, remote_mr_info.rkey);
    poll_completion(cq);

    printf("Client read from server: \"%s\"\n", buf);

    /* Notify server that we are done */
    post_send(qp, "done", 5, notify_mr);
    poll_completion(cq);
}
```

## Ordering and Consistency

RDMA Read operations on an RC QP are completed in the order they are posted. If you post Read A followed by Read B, Read A's completion will arrive before Read B's completion. However, the data for Read A is not guaranteed to be visible in local memory until Read A's completion has been polled.

<div class="warning">

Do not access the local buffer before polling the RDMA Read completion. The RNIC writes data into the local buffer asynchronously, and the data is only guaranteed to be consistent after the CQE has been polled. Accessing the buffer before polling the completion can result in reading partially written or stale data.

</div>

When performing RDMA Read against a remote buffer that the remote side is concurrently modifying, RDMA provides no atomicity guarantees for regions larger than 8 bytes. An RDMA Read of a 1 KB region may see some bytes from the old value and some from the new value if the remote side writes to the region concurrently. Applications that require consistency must use higher-level synchronization mechanisms.

## Performance Characteristics

RDMA Read latency is inherently higher than RDMA Write latency because it requires a full round trip:

1. Request travels from initiator to target.
2. Target RNIC fetches data from memory.
3. Response travels from target back to initiator.
4. Initiator RNIC writes data to local memory.

For small messages, this adds roughly 0.5 - 1.0 microsecond compared to RDMA Write. The exact overhead depends on network latency and the target RNIC's processing time.

For large reads, the target RNIC must read the data from local memory and segment it into multiple response packets. This can create a bandwidth bottleneck at the target if many initiators are reading from the same target simultaneously. Unlike RDMA Write, where each writer generates its own packets, RDMA Read concentrates the response generation burden on the target's RNIC.

| Message size | Typical RDMA Read latency | Typical RDMA Write latency |
|-------------|--------------------------|---------------------------|
| 8 bytes     | 1.8 - 2.5 us            | 1.0 - 1.5 us             |
| 4 KB        | 3 - 4 us                | 2 - 3 us                 |
| 1 MB         | ~30 us at 100 Gbps      | ~25 us at 100 Gbps       |

## When to Use RDMA Read vs. Write

Choose **RDMA Read** when:
- The data resides on the remote node and you want to pull it on demand.
- You do not control when the remote side produces data.
- You need to inspect remote data structures (hash tables, trees, logs).
- The remote side should not be burdened with pushing data.

Choose **RDMA Write** when:
- You have data to deliver to a specific remote location.
- The remote side has allocated a buffer and shared its address.
- You want the lowest possible latency.
- You want to minimize the load on the remote RNIC.

In many systems, RDMA Write is used for data transfer and RDMA Read is used for metadata inspection or control-plane queries.

## Key Takeaways

1. **RDMA Read is one-sided and initiator-driven**: The target CPU is not involved. The initiator decides what to read and when.
2. **Higher latency than RDMA Write**: The additional round trip adds 0.5 - 1.0 us for small messages.
3. **`max_rd_atomic` and `max_dest_rd_atomic` must be non-zero**: These QP parameters limit the number of outstanding RDMA Read operations and must be configured during QP state transitions.
4. **Metadata-read-then-data-read is a core pattern**: First read a small header to learn the data layout, then read the actual data. This avoids over-reading and enables dynamic data access.
5. **No remote-side consistency guarantees**: If the remote side modifies data concurrently, the reader may see partial updates. Use sequence numbers or atomic operations for synchronization.

The complete source code is in `src/code/ch09-rdma-read/rdma_read.c`.
