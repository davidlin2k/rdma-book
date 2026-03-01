# 9.4 UD Messaging

Unreliable Datagram (UD) is the second major transport type in RDMA, alongside the Reliable Connected (RC) transport used in the preceding sections. UD operates on a fundamentally different model: there is no connection between queue pairs, messages are not guaranteed to arrive, and a single UD QP can communicate with any number of remote UD QPs. These characteristics make UD well-suited for discovery protocols, membership services, multicast communication, and any scenario where the overhead of maintaining per-peer connections is prohibitive.

This section covers UD QP setup, the Global Route Header requirement, address handle management, multicast, and the MTU limitation that constrains UD message sizes. The full source code is in `src/code/ch09-ud-example/`.

## UD Transport Characteristics

UD differs from RC in several fundamental ways:

| Aspect | RC (Reliable Connected) | UD (Unreliable Datagram) |
|--------|------------------------|-------------------------|
| Connection | One-to-one, connection required | Connectionless, any-to-any |
| Reliability | Guaranteed delivery, retransmission | Best-effort, no retransmission |
| Ordering | In-order within a QP | No ordering guarantees |
| Message size | Up to 2 GB (segmented by HW) | Single MTU (typically 4 KB) |
| QP state machine | RESET→INIT→RTR→RTS | RESET→INIT→RTR→RTS (simpler) |
| RDMA Read/Write | Supported | Not supported |
| Operations | Send, Recv, RDMA Read, RDMA Write | Send, Recv only |
| Receive buffer | Exact size match | Must include 40-byte GRH |
| Scalability | One QP per peer | One QP for all peers |

The scalability advantage is significant. An application communicating with 10,000 peers over RC needs 10,000 queue pairs, each consuming RNIC memory for connection state. With UD, a single queue pair suffices for all peers.

## UD QP State Transitions

UD queue pairs go through the same state machine as RC (RESET → INIT → RTR → RTS), but the transitions require fewer attributes because there is no remote peer to configure.

### RESET → INIT

```c
struct ibv_qp_attr attr = {
    .qp_state   = IBV_QPS_INIT,
    .pkey_index = 0,
    .port_num   = ib_port,
    .qkey       = QKEY,    /* Q_Key for UD */
};
int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX |
            IBV_QP_PORT  | IBV_QP_QKEY;
ibv_modify_qp(qp, &attr, flags);
```

Note that instead of `qp_access_flags` (used with RC), UD uses a **Q_Key**. The Q_Key is a 32-bit value that provides a basic form of access control: incoming messages are accepted only if their Q_Key matches the QP's Q_Key (or if the sender uses a special privileged Q_Key). Both communicating sides must agree on the Q_Key value.

### INIT → RTR

```c
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_RTR,
};
ibv_modify_qp(qp, &attr, IBV_QP_STATE);
```

Unlike RC, the RTR transition for UD does not require any remote peer information. There is no `dest_qp_num`, no `rq_psn`, and no `ah_attr` to configure. The QP is ready to receive from any sender.

### RTR → RTS

```c
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_RTS,
    .sq_psn   = 0,
};
ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN);
```

The RTS transition sets the starting packet sequence number. For UD, the PSN is less significant than for RC because there is no reliability mechanism that uses it for retransmission.

<div class="note">

The simplicity of UD state transitions is a major advantage for applications that need to create and destroy QPs frequently, or that need to start communicating quickly without a connection setup handshake.

</div>

## The Global Route Header (GRH)

Every UD message received by the RNIC is prepended with a **40-byte Global Route Header (GRH)**. This header contains routing information such as the source and destination GIDs. The GRH is always present in received UD messages, regardless of whether the network is InfiniBand or RoCE, and regardless of whether the sender explicitly included a GRH.

This has a critical implication for buffer sizing: **every receive buffer posted for a UD QP must be at least 40 bytes larger than the maximum expected message size**.

```c
#define GRH_SIZE  40
#define MSG_SIZE  256
#define BUF_SIZE  (GRH_SIZE + MSG_SIZE)

char *recv_buf = malloc(BUF_SIZE);
struct ibv_mr *recv_mr = ibv_reg_mr(pd, recv_buf, BUF_SIZE,
                                     IBV_ACCESS_LOCAL_WRITE);

/* Post receive with the full buffer size including GRH space */
struct ibv_sge sge = {
    .addr   = (uintptr_t)recv_buf,
    .length = BUF_SIZE,
    .lkey   = recv_mr->lkey,
};

struct ibv_recv_wr wr = {
    .sg_list = &sge,
    .num_sge = 1,
};
struct ibv_recv_wr *bad_wr;
ibv_post_recv(qp, &wr, &bad_wr);
```

When a message arrives, the first 40 bytes of the receive buffer contain the GRH, and the actual message data starts at byte 40:

```c
/* After receiving a message */
struct ibv_grh *grh = (struct ibv_grh *)recv_buf;
char *msg_data = recv_buf + GRH_SIZE;

printf("Message from GID: %016lx:%016lx\n",
       be64toh(grh->sgid.global.subnet_prefix),
       be64toh(grh->sgid.global.interface_id));
printf("Message content: %s\n", msg_data);
```

<div class="warning">

Failing to account for the GRH is the most common UD programming mistake. If you post a receive buffer of exactly `MSG_SIZE` bytes, the incoming message (which is `GRH_SIZE + actual_payload` bytes) will either be truncated or cause a local length error (`IBV_WC_LOC_LEN_ERR`), depending on the implementation.

</div>

The GRH structure is defined in `infiniband/verbs.h`:

```c
struct ibv_grh {
    __be32          version_tclass_flow;  /* Version, traffic class, flow label */
    __be16          paylen;               /* Payload length */
    uint8_t         next_hdr;             /* Next header type */
    uint8_t         hop_limit;            /* Hop limit */
    union ibv_gid   sgid;                 /* Source GID */
    union ibv_gid   dgid;                 /* Destination GID */
};
```

## Address Handles

To send a UD message, the sender must create an **Address Handle (AH)** that describes the path to the destination. The AH is analogous to a socket address in traditional networking, but it is an RNIC-managed object rather than a simple data structure.

```c
struct ibv_ah_attr ah_attr = {
    .is_global = 1,
    .grh = {
        .dgid        = remote_gid,
        .sgid_index  = gid_index,
        .hop_limit   = 1,
        .traffic_class = 0,
    },
    .dlid       = remote_lid,
    .sl         = 0,
    .src_path_bits = 0,
    .port_num   = ib_port,
};

struct ibv_ah *ah = ibv_create_ah(pd, &ah_attr);
if (!ah) {
    fprintf(stderr, "Failed to create AH\n");
    return -1;
}
```

For RoCE networks, set `is_global = 1` and fill in the GRH fields. For InfiniBand within a single subnet, you can use `is_global = 0` and rely on the LID for routing.

The AH can be reused for multiple sends to the same destination. Creating an AH is a relatively expensive operation, so applications should cache AHs rather than creating and destroying them for each message.

## Posting a UD Send

A UD send work request includes the address handle and the remote QP number:

```c
struct ibv_sge sge = {
    .addr   = (uintptr_t)send_buf,
    .length = msg_len,
    .lkey   = send_mr->lkey,
};

struct ibv_send_wr wr = {
    .wr_id      = 0,
    .sg_list    = &sge,
    .num_sge    = 1,
    .opcode     = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.ud = {
        .ah          = ah,
        .remote_qpn  = remote_qpn,
        .remote_qkey = QKEY,
    },
};

struct ibv_send_wr *bad_wr;
ibv_post_send(qp, &wr, &bad_wr);
```

Note the `.wr.ud` sub-structure, which is specific to UD sends. The `remote_qpn` is the QP number of the destination QP, and `remote_qkey` must match the Q_Key configured on the destination QP.

<div class="note">

Unlike RC, where the remote QPN is configured once during the RTR transition, UD specifies the destination on a per-send basis. This is what makes UD connectionless: each message can be sent to a different destination simply by using a different AH and remote QPN.

</div>

## Metadata Exchange for UD

Since UD is connectionless, the metadata exchange is simpler than RC. Each side only needs to share:

- **QP Number**: So the sender knows which QP to target.
- **LID and/or GID**: So the sender can create an Address Handle.

There is no PSN exchange (UD does not do retransmission) and no need for remote MR information (UD does not support RDMA operations).

```c
struct ud_info {
    uint32_t      qp_num;
    uint32_t      lid;
    union ibv_gid gid;
    uint32_t      qkey;
};
```

In many UD applications, discovery happens through well-known addresses (such as multicast groups) rather than explicit out-of-band exchange.

## The 4 KB MTU Limitation

UD messages are limited to a single MTU. Unlike RC, where the RNIC automatically segments large messages into multiple packets, UD sends each message as a single packet. The maximum UD message size is determined by the active MTU of the port:

| MTU enum | Bytes | Typical use |
|----------|-------|-------------|
| `IBV_MTU_256`  | 256  | Rarely used |
| `IBV_MTU_512`  | 512  | Rarely used |
| `IBV_MTU_1024` | 1024 | Common for IB |
| `IBV_MTU_2048` | 2048 | |
| `IBV_MTU_4096` | 4096 | Common for RoCE |

You can query the active MTU with `ibv_query_port()`:

```c
struct ibv_port_attr port_attr;
ibv_query_port(ctx, ib_port, &port_attr);
uint32_t mtu = 128 << port_attr.active_mtu;  /* Convert enum to bytes */
printf("Active MTU: %u bytes\n", mtu);
```

Applications that need to send messages larger than the MTU must implement their own fragmentation and reassembly on top of UD. This is similar to how UDP applications handle messages larger than the IP MTU in traditional networking.

<div class="warning">

Posting a UD send with a message size exceeding the MTU will succeed at the `ibv_post_send()` call (the verb layer does not check message sizes), but the operation will fail with a completion error. Always validate message sizes against the active MTU before sending.

</div>

## Multicast with UD

UD supports multicast: a single send can be delivered to all members of a multicast group. This is the only RDMA transport that supports multicast.

### Joining a Multicast Group

To receive multicast messages, a QP must join the multicast group:

```c
/* Define the multicast GID (must be a valid multicast GID) */
union ibv_gid mgid;
/* FF12::1234:5678 as a multicast GID */
memset(&mgid, 0, sizeof(mgid));
mgid.raw[0] = 0xFF;
mgid.raw[1] = 0x12;
mgid.raw[14] = 0x12;
mgid.raw[15] = 0x34;

/* For IB: use the SA (Subnet Administrator) to join */
/* For RoCE: attach directly */
int ret = ibv_attach_mcast(qp, &mgid, 0);  /* 0 = MLID for RoCE */
if (ret) {
    fprintf(stderr, "Failed to join multicast group: %s\n",
            strerror(ret));
    return -1;
}
```

### Sending to a Multicast Group

To send a multicast message, create an AH with the multicast GID as the destination:

```c
struct ibv_ah_attr ah_attr = {
    .is_global = 1,
    .grh = {
        .dgid       = mgid,
        .sgid_index = gid_index,
        .hop_limit  = 1,
    },
    .dlid      = MULTICAST_MLID,  /* Multicast LID (IB) or 0 (RoCE) */
    .sl        = 0,
    .port_num  = ib_port,
};

struct ibv_ah *mcast_ah = ibv_create_ah(pd, &ah_attr);

/* Send to the multicast group */
struct ibv_send_wr wr = {
    .opcode  = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
    .sg_list = &sge,
    .num_sge = 1,
    .wr.ud = {
        .ah          = mcast_ah,
        .remote_qpn  = 0xFFFFFF,  /* Multicast pseudo-QPN */
        .remote_qkey = QKEY,
    },
};
ibv_post_send(qp, &wr, &bad_wr);
```

The `remote_qpn` for multicast is always `0xFFFFFF`. The RNIC uses the multicast GID in the AH to route the packet to all group members.

<div class="tip">

When using multicast with RoCE, the multicast GID is mapped to an IP multicast address by the network stack. Ensure that your network switches have IGMP snooping configured correctly, or multicast traffic may be flooded to all ports.

</div>

### Leaving a Multicast Group

```c
ibv_detach_mcast(qp, &mgid, 0);
```

## Complete Example Walkthrough

Our example implements a sender-receiver pair using UD:

1. **Receiver** creates a UD QP, transitions it to RTS, and shares its QP number and GID over TCP.
2. **Sender** creates a UD QP, transitions it to RTS, creates an AH for the receiver, and sends a series of messages.
3. **Receiver** posts receive buffers (accounting for the GRH), polls for completions, and prints received messages.

```c
if (is_receiver) {
    /* Post receive buffers (with GRH space) */
    for (int i = 0; i < NUM_RECV_BUFS; i++) {
        post_ud_receive(qp, recv_bufs[i], BUF_SIZE, recv_mr);
    }

    /* Receive messages */
    for (int i = 0; i < NUM_MESSAGES; i++) {
        poll_completion(cq);

        /* Skip the 40-byte GRH to get to the message */
        char *msg = recv_bufs[i] + GRH_SIZE;
        printf("Received[%d]: %s\n", i, msg);

        /* Re-post the receive buffer */
        post_ud_receive(qp, recv_bufs[i], BUF_SIZE, recv_mr);
    }
} else {
    /* Create address handle for the receiver */
    struct ibv_ah *ah = create_ah(pd, remote_info.lid,
                                   remote_info.gid, ib_port);

    /* Send messages */
    for (int i = 0; i < NUM_MESSAGES; i++) {
        snprintf(send_buf, MSG_SIZE, "UD message #%d", i);
        post_ud_send(qp, send_buf, strlen(send_buf) + 1,
                     send_mr, ah, remote_info.qp_num, QKEY);
        poll_completion(cq);
    }

    ibv_destroy_ah(ah);
}
```

## Unreliability Considerations

UD provides no delivery guarantees. Messages can be:
- **Lost**: Due to network congestion, buffer overflows, or transient errors.
- **Duplicated**: Although rare, the same packet could be delivered twice.
- **Reordered**: Messages may arrive in a different order than sent.

Applications must handle these cases. Common strategies include:
- **Sequence numbers**: Include a sequence number in each message. The receiver detects gaps (lost messages) and duplicates.
- **Application-level acknowledgments**: The receiver sends an ACK for each message (or batch of messages) it receives. The sender retransmits unacknowledged messages.
- **Idempotent operations**: Design operations so that receiving a duplicate message has no adverse effect.

For many use cases (discovery, heartbeats, metrics), occasional message loss is acceptable and the simplicity of UD outweighs the lack of reliability.

## Performance Characteristics

UD has several performance advantages over RC:
- **Lower connection overhead**: No per-peer connection state in the RNIC.
- **Faster setup**: Simpler state transitions, no remote peer configuration.
- **Better scalability**: A single QP handles all peers.

However, UD also has disadvantages:
- **No hardware segmentation**: Messages limited to one MTU.
- **No reliability**: Application must handle loss and reordering.
- **GRH overhead**: 40 bytes consumed per received message.
- **No RDMA operations**: Only Send/Receive is supported.

## Key Takeaways

1. **UD is connectionless**: A single QP can communicate with any number of peers. No connection setup is required.
2. **Always account for the 40-byte GRH**: Receive buffers must be at least 40 bytes larger than the maximum message size. The actual message data starts at offset 40 in the receive buffer.
3. **Address Handles describe the destination**: Create an AH for each destination and reuse it across sends. AH creation is expensive; cache them.
4. **Messages are limited to one MTU**: Typically 4096 bytes on RoCE or 1024-4096 bytes on InfiniBand. Applications needing larger messages must implement fragmentation.
5. **No delivery guarantees**: Messages can be lost, duplicated, or reordered. Design accordingly.
6. **Q_Key provides basic access control**: Both sides must agree on the Q_Key value. It is set during the INIT transition and specified in each send work request.

The complete source code is in `src/code/ch09-ud-example/ud_send.c`.
