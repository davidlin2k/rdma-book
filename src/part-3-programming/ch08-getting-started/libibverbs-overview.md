# 8.2 libibverbs API Overview

Before diving into code, it is worth surveying the API landscape. The libibverbs library is the primary user-space interface to RDMA hardware on Linux. It is a C library with a procedural API organized around a small number of object types and a consistent set of conventions. This section provides a map that will help you navigate the API efficiently as you write increasingly complex programs.

## Header Files

Two header files cover the vast majority of RDMA programming:

```c
#include <infiniband/verbs.h>    /* Core verbs API: QP, CQ, MR, PD, etc. */
#include <rdma/rdma_cma.h>       /* RDMA Connection Manager API */
```

The `verbs.h` header declares all functions and data structures for resource management and data transfer. The `rdma_cma.h` header declares the connection management API, which handles address resolution, route resolution, and connection establishment. You can write complete RDMA programs using only `verbs.h` if you manage connections manually (by exchanging QP information out-of-band), but most production applications use both.

Additional headers you may encounter include:

```c
#include <infiniband/verbs_api.h>  /* Extended verbs (ibv_*_ex functions) */
#include <rdma/rdma_verbs.h>       /* Convenience wrappers over CM + verbs */
#include <infiniband/mlx5dv.h>     /* Mellanox/NVIDIA direct verbs (hardware-specific) */
```

## Naming Conventions

The API follows strict naming prefixes:

| Prefix | Scope | Examples |
|--------|-------|---------|
| `ibv_` | Core verbs functions and structures | `ibv_open_device()`, `ibv_create_qp()`, `struct ibv_qp` |
| `rdma_` | Connection Manager functions and structures | `rdma_create_id()`, `rdma_connect()`, `struct rdma_cm_event` |
| `IBV_` | Verbs constants and enumerations | `IBV_QPT_RC`, `IBV_WR_SEND`, `IBV_WC_SUCCESS` |
| `RDMA_` | CM constants and enumerations | `RDMA_CM_EVENT_ESTABLISHED`, `RDMA_PS_TCP` |

Function names follow the pattern `ibv_<verb>_<object>`: `ibv_create_qp`, `ibv_destroy_cq`, `ibv_reg_mr`, `ibv_post_send`. This makes the API highly discoverable once you know the pattern.

## The Object Lifecycle Pattern

Every RDMA object follows the same lifecycle: **create**, **use**, **destroy**. Objects are created with `ibv_create_*` or `ibv_alloc_*` functions, used for their intended purpose, and then freed with the corresponding `ibv_destroy_*` or `ibv_dealloc_*` function. The creation and destruction functions are always paired:

| Create | Destroy |
|--------|---------|
| `ibv_open_device()` | `ibv_close_device()` |
| `ibv_alloc_pd()` | `ibv_dealloc_pd()` |
| `ibv_create_cq()` | `ibv_destroy_cq()` |
| `ibv_reg_mr()` | `ibv_dereg_mr()` |
| `ibv_create_qp()` | `ibv_destroy_qp()` |

<div class="warning">

Resources must be destroyed in the reverse order of creation. A Protection Domain cannot be deallocated while Queue Pairs or Memory Regions still reference it. A Completion Queue cannot be destroyed while Queue Pairs are still attached to it. Violating the ordering will cause the destroy call to fail with `EBUSY`. The correct teardown order for a typical program is: QP, MR, CQ, PD, device context.

</div>

## Key Data Structures

The core of the API revolves around a handful of opaque and semi-opaque structures. Understanding what each one represents and how they relate to each other is essential.

### `struct ibv_context`

The device context, returned by `ibv_open_device()`. It represents an open handle to a specific RDMA device. All subsequent resource allocation calls take a context (or a resource derived from one) as a parameter. You can open the same device multiple times to get multiple independent contexts.

### `struct ibv_pd`

A Protection Domain, allocated with `ibv_alloc_pd()`. It is the security boundary for RDMA resources. Queue Pairs and Memory Regions must belong to the same PD to interact. Most applications create a single PD and use it for all resources.

### `struct ibv_cq`

A Completion Queue, created with `ibv_create_cq()`. It receives completion notifications for send and receive operations posted to any QP that references it. You poll it with `ibv_poll_cq()` to retrieve `ibv_wc` entries. A single CQ can be shared by multiple QPs, or you can create separate send and receive CQs.

### `struct ibv_qp`

A Queue Pair, created with `ibv_create_qp()`. It contains a Send Queue and a Receive Queue. You post work requests to these queues, and the hardware processes them. The QP type (RC, UC, UD, etc.) determines the transport semantics. After creation, the QP must be transitioned through the state machine (RST -> INIT -> RTR -> RTS) before it can send data.

### `struct ibv_mr`

A Memory Region, registered with `ibv_reg_mr()`. It represents a contiguous block of virtual memory that has been pinned and registered with the RDMA device. Registration returns a local key (`lkey`) used by the local side when posting work requests, and a remote key (`rkey`) used by the remote side for RDMA Read and Write operations.

### `struct ibv_send_wr` --- The Send Work Request

This structure describes a single operation to be posted to the Send Queue. It is arguably the most complex structure in the API:

```c
struct ibv_send_wr {
    uint64_t                wr_id;       /* User-defined ID, returned in WC */
    struct ibv_send_wr     *next;        /* Linked list for batch posting */
    struct ibv_sge         *sg_list;     /* Scatter/gather list */
    int                     num_sge;     /* Number of SGE entries */
    enum ibv_wr_opcode      opcode;      /* SEND, RDMA_WRITE, RDMA_READ, etc. */
    unsigned int            send_flags;  /* IBV_SEND_SIGNALED, IBV_SEND_INLINE, etc. */

    union {
        struct {                         /* For RDMA Read/Write */
            uint64_t        remote_addr; /* Remote virtual address */
            uint32_t        rkey;        /* Remote memory region key */
        } rdma;
        struct {                         /* For Atomic operations */
            uint64_t        remote_addr;
            uint64_t        compare_add;
            uint64_t        swap;
            uint32_t        rkey;
        } atomic;
        struct {                         /* For UD Send */
            struct ibv_ah  *ah;          /* Address Handle */
            uint32_t        remote_qpn;
            uint32_t        remote_qkey;
        } ud;
    } wr;

    uint32_t                imm_data;    /* Immediate data (network byte order) */
};
```

The `wr_id` field is critical: it is a 64-bit value that you set when posting the request, and it is returned verbatim in the work completion. You can use it to identify which request completed, or as a pointer to application-level state (by casting a pointer to `uint64_t`).

The `sg_list` points to an array of scatter/gather elements, each of which references a region of registered memory:

```c
struct ibv_sge {
    uint64_t addr;    /* Virtual address of the buffer */
    uint32_t length;  /* Length in bytes */
    uint32_t lkey;    /* Local key from ibv_reg_mr() */
};
```

### `struct ibv_recv_wr` --- The Receive Work Request

Simpler than the send WR, because receive operations are always passive:

```c
struct ibv_recv_wr {
    uint64_t                wr_id;       /* User-defined ID */
    struct ibv_recv_wr     *next;        /* Linked list */
    struct ibv_sge         *sg_list;     /* Where to place incoming data */
    int                     num_sge;     /* Number of SGE entries */
};
```

You post receive WRs before the remote side sends data. If a Send arrives and there is no posted receive buffer, the QP enters an error state (for RC transport).

### `struct ibv_wc` --- The Work Completion

When the hardware completes a work request, it writes a work completion entry to the CQ. You retrieve these by polling:

```c
struct ibv_wc {
    uint64_t                wr_id;       /* From the original WR */
    enum ibv_wc_status      status;      /* IBV_WC_SUCCESS or error code */
    enum ibv_wc_opcode      opcode;      /* IBV_WC_SEND, IBV_WC_RECV, etc. */
    uint32_t                vendor_err;  /* Hardware-specific error code */
    uint32_t                byte_len;    /* Bytes transferred (recv only) */
    uint32_t                imm_data;    /* Immediate data (if present) */
    uint32_t                qp_num;      /* QP that generated this completion */
    uint32_t                src_qp;      /* Source QP (UD only) */
    unsigned int            wc_flags;    /* IBV_WC_WITH_IMM, IBV_WC_GRH, etc. */
    uint16_t                pkey_index;
    uint16_t                slid;        /* Source LID (IB only) */
    uint8_t                 sl;          /* Service Level */
    uint8_t                 dlid_path_bits;
};
```

The most important fields are `status`, `wr_id`, and `byte_len`. Always check `status` first: if it is anything other than `IBV_WC_SUCCESS`, the other fields (except `wr_id` and `qp_num`) may be invalid.

## Error Handling Conventions

The libibverbs API uses two error reporting patterns:

**Functions that return pointers** return `NULL` on error and set `errno`:

```c
struct ibv_pd *pd = ibv_alloc_pd(ctx);
if (!pd) {
    fprintf(stderr, "ibv_alloc_pd failed: %s\n", strerror(errno));
    /* handle error */
}
```

**Functions that return integers** return 0 on success and a positive `errno` value on failure (or -1 with `errno` set, depending on the function):

```c
int ret = ibv_destroy_qp(qp);
if (ret) {
    fprintf(stderr, "ibv_destroy_qp failed: %s\n", strerror(ret));
}
```

The posting functions (`ibv_post_send` and `ibv_post_recv`) have a special convention. They return 0 on success and set a "bad work request" output parameter on failure:

```c
struct ibv_send_wr *bad_wr;
int ret = ibv_post_send(qp, &send_wr, &bad_wr);
if (ret) {
    fprintf(stderr, "ibv_post_send failed: %s\n", strerror(ret));
    /* bad_wr points to the first WR that failed */
}
```

This is useful when posting a linked list of work requests: `bad_wr` tells you exactly which one in the chain could not be posted. All WRs before `bad_wr` were successfully posted; `bad_wr` and all subsequent WRs were not.

## Thread Safety

The libibverbs library provides the following thread safety guarantees:

- **Posting operations** (`ibv_post_send`, `ibv_post_recv`) are thread-safe with respect to the same QP. Multiple threads can post to the same QP concurrently without external locking.
- **Polling** (`ibv_poll_cq`) is thread-safe with respect to the same CQ. Multiple threads can poll the same CQ concurrently.
- **Resource creation and destruction** functions are thread-safe at the library level, but you must ensure that no thread is using a resource while another thread destroys it.
- **Modifying QP state** (`ibv_modify_qp`) is not safe to call concurrently with posting operations on the same QP. Transition the QP to RTS before starting to post.

<div class="tip">

In high-performance designs, a common pattern is to give each thread its own QP and CQ, eliminating all contention. If threads must share a CQ, the polling thread should use `ibv_poll_cq` in a tight loop (busy-polling) rather than using event-driven notification with `ibv_get_cq_event`, which involves a kernel call.

</div>

## Linking

To compile and link an RDMA program, you need to link against at least `libibverbs`. If you use the connection manager, add `librdmacm`:

```bash
# Verbs only
gcc -o my_program my_program.c -libverbs

# Verbs + Connection Manager
gcc -o my_program my_program.c -libverbs -lrdmacm

# With pkg-config (recommended)
gcc -o my_program my_program.c $(pkg-config --cflags --libs libibverbs)
gcc -o my_program my_program.c $(pkg-config --cflags --libs libibverbs librdmacm)
```

## ibv_post_send() and ibv_post_recv() Semantics

These two functions are the hot-path API---they are called for every data transfer operation, potentially millions of times per second. Understanding their exact semantics is critical:

```c
int ibv_post_send(struct ibv_qp *qp,
                  struct ibv_send_wr *wr,
                  struct ibv_send_wr **bad_wr);

int ibv_post_recv(struct ibv_qp *qp,
                  struct ibv_recv_wr *wr,
                  struct ibv_recv_wr **bad_wr);
```

Key semantics:

1. **Batch posting**: The `wr` parameter is the head of a linked list. You can post multiple WRs in a single call by chaining them through the `next` field. This amortizes the cost of the function call (and any MMIO doorbell ring) across multiple operations.

2. **Ordering**: WRs are processed in the order they appear in the linked list, and in the order of successive `ibv_post_send` calls. This ordering guarantee is per-QP, not global.

3. **Completion signaling**: For send operations, you can control whether a completion is generated by setting or omitting the `IBV_SEND_SIGNALED` flag. If the QP was created with `sq_sig_all = 1`, all send WRs generate completions regardless of the flag. For receive operations, a completion is always generated.

4. **Inline sends**: For small messages, setting `IBV_SEND_INLINE` copies the data directly into the WQE, bypassing the DMA read from the registered buffer. This can reduce latency for small messages (typically up to 64--256 bytes, depending on hardware). When using inline sends, the buffer does not need to be registered (no `lkey` is needed), and the buffer can be reused immediately after `ibv_post_send` returns.

5. **Return value**: Zero means all WRs in the chain were posted successfully. Non-zero means posting failed, and `*bad_wr` points to the first failing WR. WRs before it were posted; it and all subsequent WRs were not.

6. **Memory semantics**: After `ibv_post_send` returns successfully, ownership of the buffers referenced by the SGE list transfers to the hardware. You must not modify the data until the corresponding work completion is polled. After `ibv_post_recv` returns, the receive buffers are owned by the hardware until a receive completion arrives.

With this API map in hand, you are ready to write your first RDMA program.
