# 10.3 Multicast

RDMA multicast enables one-to-many communication where a single send operation delivers a message to all members of a multicast group. This is fundamentally different from the point-to-point patterns we have explored so far. Multicast is supported natively on InfiniBand and available on RoCE through IP multicast mapping. It operates exclusively over UD (Unreliable Datagram) transport, which means messages are limited to a single MTU and delivery is best-effort.

RDMA_CM provides a clean interface for multicast group management through the same event-driven model used for connection management. Rather than dealing with raw multicast GIDs, subnet manager queries, and address handle construction, the application joins a group by IP address and receives events when the join completes.

## Multicast Concepts

An RDMA multicast group is identified by a **multicast GID** on InfiniBand or by a standard **IP multicast address** when using RDMA_CM. The CM maps IP multicast addresses to the appropriate underlying multicast GIDs transparently.

Key characteristics of RDMA multicast:

- **UD transport only**: Multicast uses Unreliable Datagram QPs. The maximum message size is a single MTU (typically 4096 bytes on InfiniBand).
- **Best-effort delivery**: There is no acknowledgment or retransmission. Messages may be lost, duplicated, or delivered out of order.
- **Join required for receive**: A node must join the multicast group to receive messages. On InfiniBand, the Subnet Manager configures the switch multicast forwarding tables.
- **Join not required for send**: Any UD QP with the appropriate address handle can send to a multicast group, though joining is the standard way to obtain the address handle.
- **40-byte GRH**: As with all UD receives, incoming multicast messages are prepended with a 40-byte Global Route Header that the application must account for in its receive buffer sizing.

## Joining a Multicast Group

The RDMA_CM multicast workflow begins with creating a UD-type `rdma_cm_id` and joining a multicast group:

```c
/* Create event channel and CM ID for UD */
struct rdma_event_channel *channel = rdma_create_event_channel();
struct rdma_cm_id *id;
rdma_create_id(channel, &id, NULL, RDMA_PS_UDP);

/* Bind to a local address to select the RDMA device */
struct sockaddr_in local = {
    .sin_family = AF_INET,
    .sin_addr.s_addr = INADDR_ANY
};
rdma_bind_addr(id, (struct sockaddr *)&local);

/* Now create PD, CQ, and QP (must be UD type) */
struct ibv_pd *pd = ibv_alloc_pd(id->verbs);
struct ibv_cq *cq = ibv_create_cq(id->verbs, 64, NULL, NULL, 0);

struct ibv_qp_init_attr qp_attr = {
    .send_cq = cq,
    .recv_cq = cq,
    .qp_type = IBV_QPT_UD,
    .cap = {
        .max_send_wr = 16,
        .max_recv_wr = 64,
        .max_send_sge = 1,
        .max_recv_sge = 1
    }
};
rdma_create_qp(id, pd, &qp_attr);

/* Post receive buffers before joining (messages may arrive immediately) */
for (int i = 0; i < 32; i++) {
    post_receive_ud(id, recv_bufs[i], BUF_SIZE + 40);  /* +40 for GRH */
}

/* Join the multicast group */
struct sockaddr_in mcast_addr = {
    .sin_family = AF_INET,
};
inet_pton(AF_INET, "239.192.1.1", &mcast_addr.sin_addr);
rdma_join_multicast(id, (struct sockaddr *)&mcast_addr, NULL);
```

The `rdma_join_multicast()` call is asynchronous. When the join completes, the event loop receives `RDMA_CM_EVENT_MULTICAST_JOIN`:

```c
case RDMA_CM_EVENT_MULTICAST_JOIN: {
    /* The event provides the address handle for sending to the group */
    struct ibv_ah_attr ah_attr;
    memcpy(&ah_attr, &event->param.ud.ah_attr, sizeof(ah_attr));

    /* Create the address handle for sending */
    struct ibv_ah *ah = ibv_create_ah(pd, &ah_attr);

    /* Save the remote QPN and QKey for sending */
    uint32_t remote_qpn = event->param.ud.qp_num;
    uint32_t remote_qkey = event->param.ud.qkey;

    printf("Joined multicast group, QPN=%u, QKey=0x%x\n",
           remote_qpn, remote_qkey);
    break;
}
```

The event provides the **address handle attributes**, **QP number**, and **QKey** needed to send to the multicast group. On InfiniBand, the Subnet Manager allocates a multicast LID and configures switch forwarding during the join process. RDMA_CM handles all of this transparently.

## Sending to a Multicast Group

Sending to a multicast group uses the standard UD send mechanism with the address handle obtained from the join event:

```c
struct ibv_sge sge = {
    .addr = (uint64_t)(uintptr_t)send_buf,
    .length = message_len,
    .lkey = send_mr->lkey
};

struct ibv_send_wr wr = {
    .wr_id = 0,
    .sg_list = &sge,
    .num_sge = 1,
    .opcode = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
    .wr.ud = {
        .ah = mcast_ah,
        .remote_qpn = mcast_qpn,
        .remote_qkey = mcast_qkey
    }
};

struct ibv_send_wr *bad_wr;
ibv_post_send(id->qp, &wr, &bad_wr);
```

The message is delivered to all members of the multicast group, including the sender if it has joined the group and posted receive buffers. This loopback behavior can be useful for confirming that the message was sent, but applications that do not want to process their own messages should filter by source information in the GRH.

## Receiving Multicast Messages

Receiving multicast messages is identical to receiving any UD message. The 40-byte Global Route Header is prepended to the payload:

```c
/* Poll for completions */
struct ibv_wc wc;
int n = ibv_poll_cq(cq, 1, &wc);
if (n > 0 && wc.status == IBV_WC_SUCCESS) {
    if (wc.opcode == IBV_WC_RECV) {
        /* Skip the 40-byte GRH to get to the actual payload */
        char *payload = recv_bufs[wc.wr_id] + 40;
        uint32_t payload_len = wc.byte_len - 40;

        printf("Received multicast message: %.*s\n",
               payload_len, payload);

        /* Re-post the receive buffer */
        post_receive_ud(id, recv_bufs[wc.wr_id], BUF_SIZE + 40);
    }
}
```

<div class="warning">

**Buffer Sizing**: Every UD receive buffer must be at least MTU + 40 bytes to accommodate the GRH. For a 4096-byte MTU, allocate at least 4136 bytes per receive buffer. Posting a buffer that is too small results in a local length error.

</div>

## Leaving a Multicast Group

To leave a multicast group:

```c
rdma_leave_multicast(id, (struct sockaddr *)&mcast_addr);
```

This is also asynchronous, but there is no corresponding leave event. After calling `rdma_leave_multicast()`, the node will stop receiving messages for that group (once the switch forwarding tables are updated). You should destroy the address handle after leaving:

```c
ibv_destroy_ah(mcast_ah);
```

## Multiple Groups

A single UD QP can be a member of multiple multicast groups simultaneously. Each `rdma_join_multicast()` call produces its own `RDMA_CM_EVENT_MULTICAST_JOIN` event with a distinct address handle. The application uses different address handles to send to different groups, and receives messages from all joined groups on the same QP.

```c
/* Join two groups on the same ID */
rdma_join_multicast(id, (struct sockaddr *)&group1_addr, context1);
rdma_join_multicast(id, (struct sockaddr *)&group2_addr, context2);

/* In event handler, distinguish by the context pointer */
case RDMA_CM_EVENT_MULTICAST_JOIN:
    if (event->id->context == context1) {
        /* Group 1 join complete */
    } else {
        /* Group 2 join complete */
    }
    break;
```

## InfiniBand vs. RoCE Multicast

On **InfiniBand**, multicast is a first-class feature of the fabric. The Subnet Manager manages multicast groups, allocates multicast LIDs, and programs switch forwarding tables. Join and leave operations involve SA (Subnet Administrator) queries. This provides reliable group membership management and efficient hardware-level multicast forwarding.

On **RoCE**, multicast maps to IP multicast. The RDMA device uses IGMP to join the corresponding IP multicast group, and the Ethernet switches handle multicast forwarding. This works well in practice but depends on proper IGMP snooping configuration on the switches. Without IGMP snooping, multicast traffic floods all switch ports, which can cause significant performance degradation.

<div class="tip">

**Tip**: When using RoCE multicast in a data center, ensure that IGMP snooping is enabled on all switches in the path. Also configure an IGMP querier (typically on the router or a designated switch) to maintain group membership state.

</div>

On **iWARP**, multicast is generally not supported, as iWARP operates over TCP which is inherently point-to-point.

## Use Cases

RDMA multicast is particularly valuable in several scenarios:

**Distributed notifications**: A coordinator broadcasts state changes (configuration updates, leader elections, membership changes) to all nodes simultaneously. The unreliable nature is acceptable because the application can implement its own acknowledgment protocol on top.

**Group communication in HPC**: Collective operations such as broadcast and barrier can leverage hardware multicast for optimal performance. MPI implementations use RDMA multicast when available to accelerate small-message broadcasts.

**Service discovery**: Nodes can announce their presence and capabilities by sending to a well-known multicast group. New nodes join the group and immediately receive announcements from all existing nodes.

**Market data distribution**: Financial applications use multicast to distribute price updates to many consumers with minimal latency. The single-MTU message size is usually sufficient for individual price ticks.

**Replicated state machines**: Protocols like Paxos and Raft can use multicast for the proposal broadcast phase, falling back to reliable point-to-point communication for the commit phase.

## Limitations and Considerations

While RDMA multicast is powerful, several limitations must be considered:

1. **Unreliable delivery**: Messages can be lost with no notification to the sender. Applications requiring reliability must implement acknowledgments and retransmission at the application level.

2. **Single-MTU messages**: The UD transport limits each message to a single MTU. There is no fragmentation and reassembly. For larger messages, the application must implement its own segmentation.

3. **No RDMA Write/Read**: Multicast supports only send/receive operations. One-sided RDMA operations require a connected (RC) QP and are inherently point-to-point.

4. **Scalability of group membership**: On InfiniBand, the number of multicast groups and the size of each group consume switch TCAM resources. Very large deployments may need to be mindful of these limits.

5. **Ordering**: There are no ordering guarantees between messages from different senders. Messages from the same sender are typically delivered in order (within a single path), but this is not guaranteed by the specification.

Despite these limitations, multicast fills an important niche in RDMA programming. When you need to efficiently distribute small messages to many recipients with minimal latency, RDMA multicast offers performance that is difficult to achieve with point-to-point alternatives. Combined with RDMA_CM's clean API for group management, it is straightforward to integrate into applications that already use the CM event loop for connection management.
