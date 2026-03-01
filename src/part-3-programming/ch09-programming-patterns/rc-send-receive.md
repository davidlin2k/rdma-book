# 9.1 RC Send/Receive

RC (Reliable Connected) Send/Receive is the most fundamental RDMA communication pattern and the natural starting point for any RDMA programmer. It closely resembles traditional socket-based messaging: one side sends a message and the other side receives it. However, unlike sockets, RDMA Send/Receive requires the receiver to pre-post receive buffers before any message can arrive, and both sides must establish a connection by manually transitioning their queue pairs through a precise state machine.

This section builds a complete RC ping-pong application step by step. We will cover the out-of-band metadata exchange, the QP state transitions, the send and receive posting mechanics, and completion handling. The full source code is available in `src/code/ch09-rc-pingpong/`.

## The RC Send/Receive Model

In the RC Send/Receive model, communication is **two-sided**: both the sender and receiver are active participants. The sender posts a Send work request to its Send Queue, and the receiver must have a Receive work request already posted on its Receive Queue before the message arrives. If no receive buffer is available when a message arrives, the QP transitions to an error state.

Key characteristics of RC Send/Receive:

- **Reliable delivery**: The hardware guarantees in-order, exactly-once delivery. Lost packets are retransmitted automatically by the RNIC.
- **Connected**: Each RC QP is connected to exactly one remote QP. The connection is established by exchanging metadata and configuring both QPs to know about each other.
- **Message boundaries preserved**: Unlike TCP streams, each Send operation produces exactly one completion on the receive side. A 64-byte send results in a single receive completion with 64 bytes.
- **Receiver must pre-post buffers**: This is the most important difference from sockets. The receive side must post receive work requests before the sender transmits. There is no kernel buffer to absorb unexpected messages.

## Architecture of the Ping-Pong Example

Our example implements both client and server roles in a single binary, selected by a command-line flag. The communication flow is:

1. **Server** starts and listens on a TCP port for metadata exchange.
2. **Client** connects to the server via TCP.
3. Both sides independently create RDMA resources: protection domain, completion queue, queue pair, and memory region.
4. Both sides exchange QP metadata (QPN, LID, GID, PSN) over the TCP connection.
5. Both sides transition their QPs through RESET → INIT → RTR → RTS.
6. The server posts a receive buffer, and the client sends the first message.
7. They alternate: receive, then send, for N iterations.
8. Latency statistics are printed and resources are cleaned up.

## Out-of-Band Metadata Exchange

Before two RDMA queue pairs can communicate, they must know about each other. Each side needs the remote side's:

- **QP Number (QPN)**: Identifies the specific queue pair on the remote node.
- **LID (Local Identifier)**: The InfiniBand subnet-layer address of the remote port (used with InfiniBand networks; not used with RoCE).
- **GID (Global Identifier)**: A 128-bit identifier for the remote port, required for RoCE and useful for cross-subnet InfiniBand routing.
- **PSN (Packet Sequence Number)**: The starting sequence number that the remote QP will use. Both sides must agree on each other's starting PSN.

We exchange this information over a simple TCP socket connection. This is the standard approach in RDMA programming; production systems often use more sophisticated mechanisms (such as RDMA CM), but the TCP-based exchange makes the underlying protocol transparent.

```c
struct qp_info {
    uint32_t qp_num;
    uint32_t lid;
    uint32_t psn;
    union ibv_gid gid;
};
```

The exchange function serializes this structure, sends it to the peer, and receives the peer's structure in return:

```c
int exchange_qp_info(int sockfd, struct qp_info *local, struct qp_info *remote)
{
    if (write(sockfd, local, sizeof(*local)) != sizeof(*local))
        return -1;
    if (read(sockfd, remote, sizeof(*remote)) != sizeof(*remote))
        return -1;
    return 0;
}
```

<div class="warning">

This simplified exchange assumes both peers run on machines with the same endianness. Production code should convert multi-byte fields to network byte order with `htonl()`/`ntohl()` before transmission. The GID, being a byte array, does not require conversion.

</div>

## Creating RDMA Resources

Before exchanging metadata, each side must create its RDMA resources. The sequence follows a strict dependency order:

```c
/* 1. Open the RDMA device */
struct ibv_context *ctx = open_device(dev_name);

/* 2. Allocate a Protection Domain */
struct ibv_pd *pd = ibv_alloc_pd(ctx);

/* 3. Create a Completion Queue */
struct ibv_cq *cq = ibv_create_cq(ctx, CQ_DEPTH, NULL, NULL, 0);

/* 4. Create a Queue Pair */
struct ibv_qp_init_attr qp_init_attr = {
    .send_cq = cq,
    .recv_cq = cq,
    .qp_type = IBV_QPT_RC,
    .cap = {
        .max_send_wr  = 16,
        .max_recv_wr  = 16,
        .max_send_sge = 1,
        .max_recv_sge = 1,
    },
};
struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);

/* 5. Register a Memory Region */
char *buf = malloc(MSG_SIZE);
struct ibv_mr *mr = ibv_reg_mr(pd, buf, MSG_SIZE,
                                IBV_ACCESS_LOCAL_WRITE);
```

We use a single CQ for both send and receive completions. This is common for simple applications. More complex applications may use separate CQs or even separate CQs per thread.

The memory region is registered with `IBV_ACCESS_LOCAL_WRITE` because the RNIC needs to write incoming data into this buffer (for receive operations). Send operations only read from the buffer, so they do not require additional access flags.

<div class="note">

The `max_send_wr` and `max_recv_wr` values in `qp_init_attr.cap` may be adjusted by the driver to the nearest supported value. After `ibv_create_qp()` returns, check the `cap` field to see the actual values the hardware will use.

</div>

## QP State Transitions

An RC queue pair must be transitioned through four states before it can send or receive data: **RESET → INIT → RTR → RTS**. Each transition is performed with `ibv_modify_qp()`, and each requires specific attributes to be set.

### RESET → INIT

The INIT state configures the QP's port and access flags. After this transition, the QP can accept receive work requests (but cannot yet receive incoming packets from the network).

```c
struct ibv_qp_attr attr = {
    .qp_state        = IBV_QPS_INIT,
    .pkey_index      = 0,
    .port_num        = ib_port,
    .qp_access_flags = 0,  /* No remote access needed for Send/Recv */
};
int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX |
            IBV_QP_PORT  | IBV_QP_ACCESS_FLAGS;
ibv_modify_qp(qp, &attr, flags);
```

For Send/Receive, we do not need any remote access flags. The `qp_access_flags` field controls whether the remote side can perform RDMA Read or Write operations on this QP. Since we are only using Send/Receive, we set it to zero.

### INIT → RTR (Ready to Receive)

The RTR transition is where we configure the QP with information about the **remote** side. This is why we need to exchange metadata before this step.

```c
struct ibv_qp_attr attr = {
    .qp_state               = IBV_QPS_RTR,
    .path_mtu               = IBV_MTU_1024,
    .dest_qp_num            = remote_info.qp_num,
    .rq_psn                 = remote_info.psn,
    .max_dest_rd_atomic     = 0,
    .min_rnr_timer          = 12,
    .ah_attr = {
        .is_global     = 1,
        .grh = {
            .dgid      = remote_info.gid,
            .sgid_index = gid_index,
            .hop_limit  = 1,
        },
        .dlid          = remote_info.lid,
        .sl            = 0,
        .src_path_bits = 0,
        .port_num      = ib_port,
    },
};
int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
            IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
            IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
ibv_modify_qp(qp, &attr, flags);
```

Several fields deserve explanation:

- **`path_mtu`**: The maximum transfer unit for the path between the two QPs. Use `ibv_query_port()` to discover the active MTU, or set a conservative value like `IBV_MTU_1024`.
- **`dest_qp_num`**: The remote QP number we obtained during the metadata exchange.
- **`rq_psn`**: The packet sequence number that the remote side will start sending with. This must match the remote side's `sq_psn` (set in the RTS transition).
- **`max_dest_rd_atomic`**: The number of outstanding RDMA Read/Atomic operations the remote side can have in flight targeting this QP. For Send/Receive only, set to 0.
- **`min_rnr_timer`**: How long the sender should wait before retrying when the receiver has no posted receive buffer. Value 12 corresponds to approximately 0.01 ms.
- **`ah_attr`**: The address handle attributes describing the path to the remote QP. For RoCE, set `is_global = 1` and fill in the GRH. For InfiniBand within a subnet, you can set `is_global = 0` and use only `dlid`.

<div class="tip">

For RoCE v2 networks, the GID index typically corresponds to a RoCE v2 GID entry. You can list available GIDs with `show_gids` or by reading `/sys/class/infiniband/<dev>/ports/<port>/gids/<index>`. GID index 0 is often an IB-format GID; the RoCE v2 GID is usually at index 1 or 3, depending on the driver and configuration.

</div>

### RTR → RTS (Ready to Send)

The RTS transition configures the sending side of the QP:

```c
struct ibv_qp_attr attr = {
    .qp_state      = IBV_QPS_RTS,
    .sq_psn        = local_info.psn,
    .timeout       = 14,
    .retry_cnt     = 7,
    .rnr_retry     = 7,
    .max_rd_atomic = 0,
};
int flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
            IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
            IBV_QP_MAX_QP_RD_ATOMIC;
ibv_modify_qp(qp, &attr, flags);
```

- **`sq_psn`**: The starting sequence number for packets sent by this QP. The remote side's `rq_psn` (set during RTR) must match this value.
- **`timeout`**: The local ACK timeout. Value 14 corresponds to about 67 ms. If an ACK is not received within this time, the packet is retransmitted.
- **`retry_cnt`**: Maximum number of retransmissions for a given packet before the QP enters an error state. 7 is the maximum.
- **`rnr_retry`**: Maximum number of RNR (Receiver Not Ready) retries. 7 means infinite retries.
- **`max_rd_atomic`**: Number of outstanding RDMA Read/Atomic operations this QP can initiate. Set to 0 for Send/Receive only.

After the RTS transition, the QP is fully operational and can both send and receive messages.

## Posting Send and Receive Work Requests

### Posting a Receive

The receiver must post a receive work request **before** the sender transmits. This is the most common source of errors in RDMA programming.

```c
struct ibv_sge sge = {
    .addr   = (uintptr_t)buf,
    .length = MSG_SIZE,
    .lkey   = mr->lkey,
};
struct ibv_recv_wr recv_wr = {
    .wr_id   = 0,
    .sg_list = &sge,
    .num_sge = 1,
};
struct ibv_recv_wr *bad_wr;
int ret = ibv_post_recv(qp, &recv_wr, &bad_wr);
```

The `wr_id` field is an opaque 64-bit value that the application can use to identify this work request when its completion arrives. In our ping-pong example, we use sequential identifiers.

### Posting a Send

```c
struct ibv_sge sge = {
    .addr   = (uintptr_t)buf,
    .length = MSG_SIZE,
    .lkey   = mr->lkey,
};
struct ibv_send_wr send_wr = {
    .wr_id      = 0,
    .sg_list    = &sge,
    .num_sge    = 1,
    .opcode     = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
};
struct ibv_send_wr *bad_wr;
int ret = ibv_post_send(qp, &send_wr, &bad_wr);
```

The `IBV_SEND_SIGNALED` flag requests that a completion queue entry (CQE) be generated when this send completes. Without this flag (and if the QP was created with `sq_sig_all = 0`), the send completes silently and no CQE is generated. This is called **selective signaling** and is used to reduce CQ overhead in high-throughput applications.

<div class="warning">

Even with selective signaling, you must request a signaled completion periodically. The send queue has a finite depth, and without completions, the application cannot know when work requests have been retired and their slots can be reused. A common pattern is to signal every Nth send operation.

</div>

## Polling for Completions

After posting a send or receive, we poll the completion queue to determine when the operation finishes:

```c
struct ibv_wc wc;
int ne;
do {
    ne = ibv_poll_cq(cq, 1, &wc);
} while (ne == 0);

if (ne < 0) {
    fprintf(stderr, "ibv_poll_cq() failed\n");
    return -1;
}

if (wc.status != IBV_WC_SUCCESS) {
    fprintf(stderr, "Completion error: %s (%d)\n",
            ibv_wc_status_str(wc.status), wc.status);
    return -1;
}
```

The completion entry contains:
- `wc.status`: Whether the operation succeeded (`IBV_WC_SUCCESS`) or an error code.
- `wc.wr_id`: The `wr_id` from the original work request, allowing the application to correlate completions with requests.
- `wc.opcode`: The type of completion (`IBV_WC_SEND`, `IBV_WC_RECV`, etc.).
- `wc.byte_len`: For receive completions, the actual number of bytes received.

## The Ping-Pong Loop

With all the setup complete, the ping-pong loop is straightforward:

```c
for (int i = 0; i < num_iters; i++) {
    if (is_server) {
        /* Server: receive then send */
        post_receive(qp, buf, MSG_SIZE, mr);
        poll_completion(cq);          /* Wait for receive */

        post_send(qp, buf, MSG_SIZE, mr);
        poll_completion(cq);          /* Wait for send */
    } else {
        /* Client: send then receive */
        post_receive(qp, buf, MSG_SIZE, mr);  /* Pre-post before send */
        post_send(qp, buf, MSG_SIZE, mr);
        poll_completion(cq);          /* Wait for send */
        poll_completion(cq);          /* Wait for receive */
    }
}
```

Note that the client pre-posts a receive buffer before sending. This is essential because the server will reply immediately after receiving the client's message, and the receive buffer must already be posted when the reply arrives.

<div class="note">

In the actual implementation, the client posts its receive buffer before sending the first message of each iteration. This ensures the receive buffer is always available before the server's reply arrives. Failure to do this can result in an RNR (Receiver Not Ready) error, which causes the sender to retry or, if retries are exhausted, a fatal QP error.

</div>

## Resource Cleanup

RDMA resources must be destroyed in the reverse order of creation. Destroying a resource that is still referenced by another resource results in an error:

```c
ibv_destroy_qp(qp);
ibv_dereg_mr(mr);
ibv_destroy_cq(cq);
ibv_dealloc_pd(pd);
ibv_close_device(ctx);
free(buf);
```

The QP must be destroyed before the CQ it references, the MR must be deregistered before the PD it belongs to, and so on.

## Running the Example

Build the example:

```bash
cd src/code/ch09-rc-pingpong
make
```

Run the server on one machine:

```bash
./rc_pingpong -s
```

Run the client on another machine (or the same machine with SoftRoCE):

```bash
./rc_pingpong -c <server_ip>
```

You should see output like:

```
Exchanged QP info:
  Local:  QPN=0x1a2b, LID=0x0001, PSN=0x00abcd
  Remote: QPN=0x3c4d, LID=0x0002, PSN=0x00ef01
QP state: RESET -> INIT -> RTR -> RTS
Ping-pong: 1000 iterations, 64 bytes
Average latency: 3.42 us
```

## Key Takeaways

1. **RC Send/Receive is two-sided**: Both sender and receiver are active participants. The receiver must pre-post buffers.
2. **QP metadata exchange is out-of-band**: You need a side channel (TCP, RDMA CM, etc.) to exchange QPN, LID/GID, and PSN before communication can begin.
3. **State transitions are mandatory**: QPs must progress through RESET → INIT → RTR → RTS, with specific attributes set at each step.
4. **RTR configures the receive side with remote info**: The remote QP number, PSN, and path are all set during the RTR transition.
5. **RTS configures the send side**: The local PSN, timeouts, and retry counts are set during the RTS transition.
6. **Always check completion status**: A successful `ibv_post_send()` or `ibv_post_recv()` only means the work request was accepted by the hardware. The actual operation may still fail, and you discover this through the completion queue.

The complete source code for this example is in `src/code/ch09-rc-pingpong/rc_pingpong.c`, with shared helpers in `src/code/common/`.
