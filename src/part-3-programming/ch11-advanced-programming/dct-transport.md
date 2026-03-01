# 11.3 DCT Transport

## The Scalability Wall

Even with XRC, the number of QPs grows linearly with the number of remote nodes. In a 10,000-node cluster, each node still needs thousands of QPs -- one per remote node per process. The connection setup phase (establishing all-to-all connections at application startup) can take minutes, and the NIC memory consumed by QP contexts becomes a significant fraction of the device's resources.

**Dynamically Connected Transport (DCT)** takes a fundamentally different approach. Instead of pre-establishing connections to every potential peer, DCT creates connections on demand, transparently, when the first message is sent. The connection is cached for subsequent messages and can be evicted when no longer in use. This achieves O(1) QP resource usage per node regardless of cluster size.

DCT is a vendor-specific extension from Mellanox (now NVIDIA) and is available on ConnectX-3 and later HCAs. It is not part of the IBTA specification, so it requires the mlx5 device-specific API (`libmlx5`/`mlx5dv`).

## DC Architecture

DCT introduces two QP types:

### DC Initiator (DCI)

A **DCI** is the send-side entity. It is similar to an RC QP's send queue, but it is not permanently bound to a single remote peer. Instead, a DCI can be used to send to any remote DCT. The NIC handles the connection setup transparently: when a send WQE targets a new remote DCT, the hardware establishes the connection on the fly.

A node typically creates a small, fixed number of DCIs -- often one per CPU core or one per thread. This number does not grow with cluster size.

### DC Target (DCT)

A **DCT** is the receive-side entity. It accepts incoming connections from any DCI. Each process typically creates one or a few DCTs. The DCT is associated with an SRQ for receiving messages, similar to XRC.

The beauty of this architecture is that both DCI and DCT counts are O(1) per node:

```
Standard RC:  O(N) QPs per process, O(P × N) per node
XRC:          O(N) QPs per process, O(N) per node
DCT:          O(C) DCIs + O(1) DCTs per process = O(C) per node
              (where C = number of cores, independent of N)
```

## Creating DC QPs

DC QPs are created using the mlx5 device-specific interface. The standard `ibv_create_qp()` does not support DC types directly.

### Creating a DCI

```c
#include <infiniband/mlx5dv.h>

/* Create a DCI using mlx5dv */
struct ibv_qp_init_attr_ex attr_ex = {
    .comp_mask = IBV_QP_INIT_ATTR_PD |
                 IBV_QP_INIT_ATTR_SEND_OPS_FLAGS,
    .pd = pd,
    .send_cq = send_cq,
    .recv_cq = recv_cq,     /* Not used for DCI, but required */
    .qp_type = IBV_QPT_DRIVER,
    .cap = {
        .max_send_wr = 128,
        .max_send_sge = 1,
        .max_inline_data = 64
    },
    .send_ops_flags = MLX5DV_QP_EX_WITH_SEND
};

struct mlx5dv_qp_init_attr dv_attr = {
    .comp_mask = MLX5DV_QP_INIT_ATTR_MASK_DC,
    .dc_init_attr = {
        .dc_type = MLX5DV_DCTYPE_DCI,
        .dct_access_key = 0x12345678   /* Must match DCT */
    }
};

struct ibv_qp *dci = mlx5dv_create_qp(ctx, &attr_ex, &dv_attr);
```

### Creating a DCT

```c
struct ibv_qp_init_attr_ex attr_ex = {
    .comp_mask = IBV_QP_INIT_ATTR_PD,
    .pd = pd,
    .send_cq = send_cq,     /* Not used for DCT */
    .recv_cq = recv_cq,
    .srq = srq,             /* DCT uses SRQ for receives */
    .qp_type = IBV_QPT_DRIVER,
    .cap = {
        .max_recv_wr = 0,    /* Receives through SRQ */
        .max_recv_sge = 0
    }
};

struct mlx5dv_qp_init_attr dv_attr = {
    .comp_mask = MLX5DV_QP_INIT_ATTR_MASK_DC,
    .dc_init_attr = {
        .dc_type = MLX5DV_DCTYPE_DCT,
        .dct_access_key = 0x12345678,  /* Access key for authentication */
        .port = 1,
        .pkey_index = 0
    }
};

struct ibv_qp *dct = mlx5dv_create_qp(ctx, &attr_ex, &dv_attr);
```

### DCT Access Key

The **DCT access key** is a 64-bit value that serves as a simple authentication mechanism. A DCI can only connect to a DCT if it presents the correct access key. All cooperating processes in an application should use the same key.

## QP State Transitions

DC QPs follow a simplified state machine compared to RC:

**DCI transitions**: RESET -> INIT -> RTR -> RTS, but the transitions do not require specifying a remote QPN or address -- because the DCI is not bound to a specific peer.

```c
/* DCI: RESET -> INIT */
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_INIT,
    .pkey_index = 0,
    .port_num = 1
};
ibv_modify_qp(dci, &attr,
    IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT);

/* DCI: INIT -> RTR */
attr.qp_state = IBV_QPS_RTR;
attr.path_mtu = IBV_MTU_4096;
attr.ah_attr.port_num = 1;
ibv_modify_qp(dci, &attr,
    IBV_QP_STATE | IBV_QP_PATH_MTU | IBV_QP_AV);

/* DCI: RTR -> RTS */
attr.qp_state = IBV_QPS_RTS;
attr.timeout = 14;
attr.retry_cnt = 7;
attr.rnr_retry = 7;
attr.max_rd_atomic = 4;
ibv_modify_qp(dci, &attr,
    IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
    IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC);
```

**DCT transition**: The DCT only needs to go to RTR (it does not send):

```c
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_RTR,
    .min_rnr_timer = 12,
    .path_mtu = IBV_MTU_4096,
    .ah_attr = {
        .port_num = 1,
        .is_global = 1,
    }
};
ibv_modify_qp(dct, &attr,
    IBV_QP_STATE | IBV_QP_MIN_RNR_TIMER |
    IBV_QP_PATH_MTU | IBV_QP_AV);
```

## Sending with DCI

When posting a send WR on a DCI, the application specifies the target DCT's address and QP number in each WR. This is done using the mlx5 extended send interface:

```c
#include <infiniband/mlx5dv.h>

/* Use the mlx5dv_set_dc_addr helper */
struct ibv_qp_ex *qpx = ibv_qp_to_qp_ex(dci);
struct mlx5dv_qp_ex *mqpx = mlx5dv_qp_ex_from_ibv_qp_ex(qpx);

ibv_wr_start(qpx);
qpx->wr_id = wr_id;
qpx->wr_flags = IBV_SEND_SIGNALED;
ibv_wr_send(qpx);

/* Set the destination: address handle + remote DCT QPN + DC key */
mlx5dv_wr_set_dc_addr(mqpx, ah, remote_dct_qpn, dc_access_key);

ibv_wr_set_sge(qpx, lkey, (uint64_t)buf, len);

int ret = ibv_wr_complete(qpx);
```

Each send can target a different remote DCT, making the DCI a reusable, multi-destination send engine. The NIC handles connection caching internally: if a connection to the target DCT already exists in the hardware cache, it is reused. If not, the NIC establishes a new connection transparently.

## On-Demand Connection Behavior

The "dynamically connected" aspect of DCT means:

1. **First message to a new peer**: The NIC establishes a connection on the fly. This adds latency to the first message (typically a few microseconds extra for the connection handshake).

2. **Subsequent messages to the same peer**: The connection is cached in the NIC and reused. Latency is comparable to standard RC.

3. **Connection eviction**: When the NIC's connection cache is full, least-recently-used connections are evicted. The next message to an evicted peer triggers a new connection setup. The cache size is hardware-dependent (typically hundreds to thousands of entries on ConnectX-5 and later).

4. **Transparent to the application**: The application does not see connection events or need to manage connections. It simply posts sends with a target address, and the NIC handles the rest.

<div class="warning">

**Latency Consideration**: The first message to a new (or evicted) peer incurs additional connection setup latency. In latency-sensitive applications, consider "warming up" connections by sending a small message to each peer during initialization. For throughput-oriented workloads, the amortized overhead is negligible.

</div>

## RDMA Operations over DC

DCT supports the same RDMA operations as RC:

- **Send/Receive**: Standard two-sided messaging, with receives served from the DCT's SRQ.
- **RDMA Write**: One-sided write to remote memory. The remote side must have registered memory with appropriate access flags.
- **RDMA Read**: One-sided read from remote memory.
- **Atomics**: Compare-and-swap and fetch-and-add operations.

This makes DCT a drop-in replacement for RC in terms of functionality, with the added benefit of on-demand connections.

## Trade-offs

### Advantages

- **O(1) QPs**: Resource usage is independent of cluster size. A 10-node and a 10,000-node cluster require the same number of QPs per node.
- **No connection setup phase**: The application can start sending immediately without an all-to-all connection establishment phase.
- **Reduced NIC memory**: Fewer QP contexts means more NIC memory available for other purposes (caches, flow tables).

### Disadvantages

- **Vendor-specific**: DCT is only available on Mellanox/NVIDIA ConnectX hardware. Applications using DCT are not portable to other vendors.
- **First-message latency**: The on-demand connection adds latency to the first message to each peer (or after cache eviction).
- **API complexity**: The mlx5dv API is more complex than standard verbs, requiring device-specific headers and link flags.
- **No RDMA_CM integration**: DCT is not supported by `librdmacm`. Connection management is implicit in the hardware, and the application must manage DCT metadata exchange (QPN, GID, access key) through its own out-of-band mechanism.

## When to Use DCT

DCT is the right choice when:

- You are running on Mellanox/NVIDIA hardware (ConnectX-3 or later).
- The cluster is large (hundreds to thousands of nodes) and connection setup time or QP memory is a bottleneck.
- The communication pattern is sparse or unpredictable (not every node talks to every other node).
- You can accept vendor lock-in in exchange for superior scalability.

DCT is used in production by several MPI implementations (notably MVAPICH2 with its DC transport option) and by large-scale storage systems running on NVIDIA InfiniBand fabrics. It represents the most scalable RDMA transport available today, trading portability for radical resource efficiency.

<div class="tip">

**Tip**: NVIDIA's UCX (Unified Communication X) library supports DCT as a transport and provides a portable abstraction layer. If you want DCT's scalability benefits without directly coding against the mlx5dv API, consider using UCX as an intermediary.

</div>
