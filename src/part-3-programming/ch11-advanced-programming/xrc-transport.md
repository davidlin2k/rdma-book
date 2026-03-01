# 11.2 XRC Transport

## The Multi-Process QP Problem

Consider a cluster running MPI with four ranks per node and 256 nodes. In a standard RC (Reliable Connection) setup, each process needs a QP to every remote process. That means each process creates (256 - 1) x 4 = 1,020 QPs, and each node hosts 4 x 1,020 = 4,080 QPs. The total QP count across the cluster is over one million.

This is problematic for several reasons. Each QP consumes NIC memory for its context (typically 256 bytes to 1 KB per QP). The QP setup time during application initialization can take minutes. And the NIC's QP cache is finite -- when the working set of QPs exceeds the cache, QP context must be swapped to and from host memory, degrading performance.

The root cause of this explosion is that RC is process-to-process. If process A on node X wants to send to process B on node Y, it needs a dedicated QP -- even if process C on node X also has a QP to node Y. The QPs cannot be shared across processes because each QP is associated with a single protection domain, and protection domains cannot span processes.

## XRC: Cross-Process Resource Sharing

The **eXtended Reliable Connection (XRC)** transport type solves this problem by allowing QPs on the same node to share receive-side resources. With XRC, if four processes on node X all communicate with node Y, they can share a single receive-side connection to node Y instead of maintaining four separate connections.

The key insight is that on the receive side, what matters is delivering the message to the correct process. XRC achieves this by associating each target process with a **Shared Receive Queue (SRQ)** identified by a number (SRQN). The sender specifies which SRQ (and therefore which target process) should receive each message. This way, a single transport-level connection between two nodes can multiplex messages to different processes.

The QP reduction is significant:

```
Without XRC: Processes × Remote_Processes QPs per node
             4 × (255 × 4) = 4,080 QPs per node

With XRC:    Processes × Remote_Nodes QPs per node
             4 × 255 = 1,020 QPs per node (4× reduction)
```

In general, the reduction factor equals the number of processes per node.

## XRC Architecture

XRC introduces several new concepts:

### XRC Domain (XRCD)

An **XRC Domain** is a namespace for sharing XRC resources across processes on the same node. It is created with `ibv_open_xrcd()`:

```c
struct ibv_xrcd_init_attr xrcd_attr = {
    .comp_mask = IBV_XRCD_INIT_ATTR_FD |
                 IBV_XRCD_INIT_ATTR_OFLAGS,
    .fd = fd,           /* File descriptor for cross-process sharing */
    .oflags = O_CREAT   /* Create if does not exist */
};

struct ibv_xrcd *xrcd = ibv_open_xrcd(ctx, &xrcd_attr);
```

The file descriptor `fd` is a key mechanism: all processes that open the same file (typically in `/tmp` or a shared filesystem) and pass its fd to `ibv_open_xrcd()` share the same XRC domain. This is how cross-process sharing is achieved without shared memory or IPC.

```c
/* Both processes open the same file to share the XRCD */
int fd = open("/tmp/rdma_xrcd", O_CREAT | O_RDWR, 0666);
struct ibv_xrcd *xrcd = ibv_open_xrcd(ctx, &xrcd_attr);
```

### XRC SRQ

An **XRC SRQ** is a shared receive queue associated with an XRC domain. It serves as the receive endpoint for a specific process:

```c
struct ibv_srq_init_attr_ex srq_attr = {
    .comp_mask = IBV_SRQ_INIT_ATTR_TYPE |
                 IBV_SRQ_INIT_ATTR_XRCD |
                 IBV_SRQ_INIT_ATTR_CQ |
                 IBV_SRQ_INIT_ATTR_PD,
    .srq_type = IBV_SRQT_XRC,
    .xrcd = xrcd,
    .cq = recv_cq,
    .pd = pd,
    .attr = {
        .max_wr = 128,
        .max_sge = 1
    }
};

struct ibv_srq *xrc_srq = ibv_create_srq_ex(ctx, &srq_attr);
```

Each process creates its own XRC SRQ within the shared XRCD. The SRQ is identified by its SRQ number (SRQN), which is communicated to remote nodes so they can target messages to the correct process.

```c
/* Get the SRQ number for sharing with remote nodes */
uint32_t srqn = xrc_srq->xrc_srq_num;
```

### XRC INI QP (Initiator)

An **XRC INI QP** is the send-side QP. It is similar to an RC QP but can target different XRC SRQs on the remote node:

```c
struct ibv_qp_init_attr_ex qp_attr = {
    .comp_mask = IBV_QP_INIT_ATTR_PD,
    .pd = pd,
    .send_cq = send_cq,
    .qp_type = IBV_QPT_XRC_SEND,
    .cap = {
        .max_send_wr = 64,
        .max_send_sge = 1
    }
};

struct ibv_qp *ini_qp = ibv_create_qp_ex(ctx, &qp_attr);
```

### XRC TGT QP (Target)

An **XRC TGT QP** is the receive-side QP. It is associated with the XRC SRQ and handles the transport-level connection from remote initiators:

```c
struct ibv_qp_init_attr_ex qp_attr = {
    .comp_mask = IBV_QP_INIT_ATTR_PD |
                 IBV_QP_INIT_ATTR_XRCD,
    .pd = pd,
    .xrcd = xrcd,
    .qp_type = IBV_QPT_XRC_RECV,
    .cap = {
        .max_recv_wr = 0,   /* Receives go through XRC SRQ */
        .max_recv_sge = 0
    }
};

struct ibv_qp *tgt_qp = ibv_create_qp_ex(ctx, &qp_attr);
```

## Connection Setup

XRC connection setup is more involved than standard RC because both INI and TGT QPs must be configured, and the SRQ numbers must be exchanged. The typical flow is:

1. **Each process** creates an XRC domain (sharing via file descriptor), an XRC SRQ, and the necessary CQs.

2. **Metadata exchange**: All processes exchange their XRC SRQ numbers (SRQNs) with remote peers. This can be done through MPI's existing out-of-band channel or through a separate metadata service.

3. **INI QP creation**: When process A on node X wants to send to any process on node Y, it creates an XRC INI QP and connects it to node Y's TGT QP. If another process on node X already has a connection to node Y, the same underlying transport connection can be reused.

4. **Sending with SRQN**: When sending a message, the initiator specifies the remote SRQN to indicate which process should receive it:

```c
struct ibv_send_wr wr = {
    .opcode = IBV_WR_SEND,
    .send_flags = IBV_SEND_SIGNALED,
    .sg_list = &sge,
    .num_sge = 1,
    .qp_type.xrc.remote_srqn = target_srqn  /* Target process */
};

struct ibv_send_wr *bad_wr;
ibv_post_send(ini_qp, &wr, &bad_wr);
```

## QP State Transitions for XRC

XRC QPs follow the same state machine as RC QPs (RESET -> INIT -> RTR -> RTS for INI; RESET -> INIT -> RTR for TGT), but with additional parameters:

**INI QP (INIT -> RTR)**:
```c
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_RTR,
    .path_mtu = IBV_MTU_4096,
    .dest_qp_num = remote_tgt_qpn,
    .rq_psn = 0,
    .max_dest_rd_atomic = 4,
    .min_rnr_timer = 12,
    .ah_attr = { /* ... remote address ... */ }
};
ibv_modify_qp(ini_qp, &attr,
    IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
    IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
    IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
```

**TGT QP** transitions are similar but the TGT QP only goes to RTR (it does not send, so RTS is not needed).

## When XRC Makes Sense

XRC is most beneficial when:

- **Multiple processes per node**: The benefit scales linearly with the number of processes per node. With 8 processes per node, XRC provides an 8x QP reduction.
- **Large clusters**: The absolute QP savings are proportional to cluster size times processes per node.
- **MPI workloads**: MPI implementations (Open MPI, MVAPICH) support XRC natively as a transport option.
- **Memory-constrained NICs**: Older or lower-end NICs have smaller QP caches, making QP reduction more valuable.

XRC is less beneficial when:

- **Single process per node**: There is nothing to share -- XRC degenerates to regular RC.
- **Small clusters**: The absolute QP savings are minimal, and the setup complexity may not be justified.
- **Thread-based parallelism**: If the application uses threads rather than processes, standard SRQ (Section 11.1) provides buffer sharing within a single process without XRC's complexity.

<div class="tip">

**Tip**: If you are using MPI, you do not need to implement XRC yourself. Set the transport selection parameter in your MPI implementation (e.g., `--mca btl_openib_connect_udcm` in Open MPI or `MV2_USE_XRC=1` in MVAPICH2) to enable XRC automatically.

</div>

## XRC vs. Other Scalability Solutions

| Feature | Standard RC | RC + SRQ | XRC | DCT (Section 11.3) |
|---------|------------|----------|-----|-----|
| QPs per node | O(P x N) | O(P x N) | O(P x N / K) | O(1) |
| Receive buffers | O(P x N x M) | O(M) | O(M) | O(M) |
| Cross-process sharing | No | No | Yes | N/A |
| Connection setup | All-to-all | All-to-all | All-to-all | On-demand |
| Vendor support | Universal | Universal | Most IB HCAs | Mellanox/NVIDIA only |

Where P = processes per node, N = remote nodes, K = processes per node, M = buffer pool size.

XRC represents a pragmatic middle ground between the universality of standard RC and the radical scalability of DCT. It is well-supported by major MPI implementations and requires no vendor-specific APIs, making it a reliable choice for multi-process HPC applications running on InfiniBand fabrics.
