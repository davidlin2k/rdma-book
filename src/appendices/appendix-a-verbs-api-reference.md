# Appendix A: Verbs API Reference

This appendix provides an alphabetical reference of the most commonly used libibverbs functions. Each entry includes the C prototype, a description of the function's purpose and behavior, a parameter table, return value semantics, and key notes. The signatures correspond to the `libibverbs` API as shipped with `rdma-core`.

<div class="warning">

The verbs API is a low-level interface. Incorrect use -- such as posting work requests to a QP in the wrong state, or dereferencing freed memory regions -- can cause silent data corruption, kernel panics, or hardware hangs. Always validate return codes and follow the state-machine rules described in the main text.

</div>

---

## ibv_ack_async_event

```c
void ibv_ack_async_event(struct ibv_async_event *event);
```

**Description**

Acknowledges an asynchronous event previously obtained via `ibv_get_async_event()`. Every event retrieved must be acknowledged exactly once; failure to do so will prevent `ibv_destroy_qp()`, `ibv_destroy_cq()`, and other teardown functions from completing, because the kernel tracks outstanding unacknowledged events.

The call is non-blocking and does not return an error.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `event` | `struct ibv_async_event *` | Pointer to the event structure that was filled in by `ibv_get_async_event()`. |

**Return Value**

None (void).

**Key Notes**

- Failing to acknowledge events will cause resource destruction calls to block indefinitely.
- The `event` pointer must remain valid until after the acknowledgment call returns.
- Events should be acknowledged in a timely manner, even if the application does not act on them.

---

## ibv_alloc_mw

```c
struct ibv_mw *ibv_alloc_mw(struct ibv_pd *pd, enum ibv_mw_type type);
```

**Description**

Allocates a Memory Window (MW) associated with the given Protection Domain. Memory Windows provide a mechanism for granting and revoking remote access to memory regions at a finer granularity than Memory Regions, without the overhead of re-registration.

There are two types of Memory Windows: Type 1 (`IBV_MW_TYPE_1`) binds are posted via a special verb call, while Type 2 (`IBV_MW_TYPE_2`) binds are posted as work requests on a QP, enabling more efficient bind operations that can be pipelined with data transfer operations.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pd` | `struct ibv_pd *` | Protection Domain to associate the MW with. |
| `type` | `enum ibv_mw_type` | MW type: `IBV_MW_TYPE_1` or `IBV_MW_TYPE_2`. |

**Return Value**

Returns a pointer to the newly allocated `struct ibv_mw` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- Not all devices support Memory Windows. Check `ibv_query_device()` capabilities first.
- Type 2 MWs are generally preferred for performance-critical paths because their bind operations go through the data path (QP) rather than the control path.
- The allocated MW has no associated memory region until it is bound via `ibv_bind_mw()` or a Type 2 bind WR.

---

## ibv_alloc_pd

```c
struct ibv_pd *ibv_alloc_pd(struct ibv_context *context);
```

**Description**

Allocates a Protection Domain (PD) for the specified RDMA device context. A Protection Domain is a fundamental security construct in the verbs API: it groups Memory Regions, Queue Pairs, Address Handles, and other resources into a common protection scope. Only resources belonging to the same PD may interact with each other, preventing unauthorized cross-application memory access.

Every RDMA application must allocate at least one PD. Most applications use a single PD, but multi-tenant or security-sensitive designs may use multiple PDs to isolate different trust domains.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context obtained from `ibv_open_device()`. |

**Return Value**

Returns a pointer to the newly allocated `struct ibv_pd` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- PD allocation is a control-path operation and should not be performed on the data path.
- All resources associated with a PD must be destroyed before the PD itself can be deallocated.
- PDs are cheap to allocate; there is no significant per-PD hardware cost on most adapters.

---

## ibv_bind_mw

```c
int ibv_bind_mw(struct ibv_qp *qp, struct ibv_mw *mw,
                struct ibv_mw_bind *mw_bind);
```

**Description**

Binds a Type 1 Memory Window to a Memory Region, enabling remote access to a specific sub-range of the MR through the MW's R_Key. The bind operation is posted through the specified QP. After a successful bind, the remote side can use the MW's R_Key to access the bound memory range.

This function is only used for Type 1 MWs. Type 2 MW binds are posted as send work requests (`IBV_WR_BIND_MW`) via `ibv_post_send()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `qp` | `struct ibv_qp *` | QP through which the bind operation is posted. Must be in the RTS state. |
| `mw` | `struct ibv_mw *` | The Memory Window to bind. Must be Type 1. |
| `mw_bind` | `struct ibv_mw_bind *` | Bind attributes including the target MR, address range, and access flags. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- The QP used for the bind must be in the RTS (Ready to Send) state.
- The bound address range must fall within the MR's registered range.
- The access flags granted to the MW must be a subset of those granted to the underlying MR.
- After the bind completes, the `mw->rkey` field is updated with the new R_Key.

---

## ibv_close_device

```c
int ibv_close_device(struct ibv_context *context);
```

**Description**

Closes an RDMA device context that was previously opened with `ibv_open_device()`. This releases the file descriptor and any kernel resources associated with the context. All resources created under this context (PDs, MRs, QPs, CQs, etc.) must be destroyed before closing the device; otherwise, the behavior is undefined and may result in resource leaks or errors.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context to close, previously returned by `ibv_open_device()`. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- Closing a context with outstanding resources is a programming error. Always destroy all child resources first.
- After calling `ibv_close_device()`, the `context` pointer is invalid and must not be used.
- This function does not free the device list; `ibv_free_device_list()` must be called separately.

---

## ibv_create_ah

```c
struct ibv_ah *ibv_create_ah(struct ibv_pd *pd,
                             struct ibv_ah_attr *attr);
```

**Description**

Creates an Address Handle (AH), which encapsulates the routing information needed to send a packet to a remote destination. Address Handles are required for Unreliable Datagram (UD) QPs, where each send work request must specify the destination AH. They encode L2/L3 addressing information such as the destination LID (InfiniBand) or GID (RoCE), the service level, the port number, and the GRH (Global Route Header) fields.

For InfiniBand, the AH typically contains the DLID (Destination Local ID) and optionally a GRH for inter-subnet routing. For RoCE, a GRH is always required because routing uses GIDs mapped to IP addresses.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pd` | `struct ibv_pd *` | Protection Domain to associate the AH with. |
| `attr` | `struct ibv_ah_attr *` | Address Handle attributes including destination LID/GID, service level, port number, and GRH fields. |

**Return Value**

Returns a pointer to the newly created `struct ibv_ah` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- For RoCE, the `is_global` field in `attr` must be set to 1, and the GRH fields must be populated with a valid DGID.
- The AH must belong to the same PD as the QP that uses it.
- AH creation is a control-path operation; for high-throughput UD applications, pre-create AHs rather than creating them per-message.
- Use `ibv_init_ah_from_wc()` or `rdma_create_ah()` as convenient helpers when responding to received UD messages.

---

## ibv_create_cq

```c
struct ibv_cq *ibv_create_cq(struct ibv_context *context, int cqe,
                             void *cq_context,
                             struct ibv_comp_channel *channel,
                             int comp_vector);
```

**Description**

Creates a Completion Queue (CQ) that receives completion notifications for work requests posted to associated Queue Pairs. The CQ is the mechanism by which the application learns that a send or receive operation has completed, and whether it succeeded or failed.

The actual number of CQ entries allocated may be greater than the requested `cqe` parameter, as the hardware may round up to a power of two or a device-specific granularity. The application should check the `cq->cqe` field after creation to determine the actual capacity.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context for the CQ. |
| `cqe` | `int` | Minimum number of Completion Queue Entries (CQEs) the CQ should hold. |
| `cq_context` | `void *` | User-defined context pointer, returned with completion events. May be `NULL`. |
| `channel` | `struct ibv_comp_channel *` | Completion channel for event-driven notification. May be `NULL` for polling-only use. |
| `comp_vector` | `int` | Completion vector for MSI-X interrupt steering. Use 0 if unsure. |

**Return Value**

Returns a pointer to the newly created `struct ibv_cq` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- The CQ must be large enough to hold completions for all associated QPs. If the CQ overflows, an asynchronous CQ error event is generated, and the CQ becomes unusable.
- A single CQ can be shared by the send and receive sides of multiple QPs.
- For event-driven operation, pair with `ibv_req_notify_cq()` and a completion channel. For polling, pass `NULL` as the channel.
- The `comp_vector` parameter distributes interrupt load across CPU cores; set it to `core_id % context->num_comp_vectors`.

---

## ibv_create_qp

```c
struct ibv_qp *ibv_create_qp(struct ibv_pd *pd,
                              struct ibv_qp_init_attr *qp_init_attr);
```

**Description**

Creates a Queue Pair (QP), which is the fundamental data-transfer endpoint in RDMA. A QP consists of a Send Queue (SQ) and a Receive Queue (RQ), each backed by a Completion Queue. The QP type determines which transport service is used: Reliable Connected (RC), Unreliable Connected (UC), Unreliable Datagram (UD), or Extended Reliable Connected (XRC).

The newly created QP is in the RESET state. Before it can be used for data transfer, it must be transitioned through the INIT, RTR (Ready to Receive), and RTS (Ready to Send) states using `ibv_modify_qp()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pd` | `struct ibv_pd *` | Protection Domain for the QP. |
| `qp_init_attr` | `struct ibv_qp_init_attr *` | Initialization attributes including QP type, send/recv CQs, SRQ (optional), and queue capacities. |

**Return Value**

Returns a pointer to the newly created `struct ibv_qp` on success, or `NULL` on failure with `errno` set. On success, `qp_init_attr->cap` is updated with the actual capabilities allocated by the hardware (which may exceed the requested values).

**Key Notes**

- The maximum number of outstanding send and receive work requests is device-dependent. Check via `ibv_query_device()`.
- The send and receive CQs may be the same CQ or different CQs.
- If an SRQ is specified, the QP's receive queue is not used; receives are posted to the SRQ instead.
- For UD QPs, each send WR requires an Address Handle specifying the destination.
- Consider using `ibv_create_qp_ex()` for extended attributes such as QP creation flags, source QPN, or inline receive support.

---

## ibv_create_srq

```c
struct ibv_srq *ibv_create_srq(struct ibv_pd *pd,
                               struct ibv_srq_init_attr *srq_init_attr);
```

**Description**

Creates a Shared Receive Queue (SRQ), which allows multiple QPs to share a common pool of receive buffers. This is particularly useful in server applications with many connections, where provisioning individual receive buffers per QP would waste memory. With an SRQ, a smaller total number of receive buffers can serve all attached QPs.

The SRQ has a configurable low-watermark (`srq_limit`). When the number of outstanding receive work requests in the SRQ drops to this limit, an `IBV_EVENT_SRQ_LIMIT_REACHED` asynchronous event is generated, prompting the application to replenish the SRQ.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pd` | `struct ibv_pd *` | Protection Domain for the SRQ. |
| `srq_init_attr` | `struct ibv_srq_init_attr *` | Initialization attributes including maximum WRs, maximum scatter/gather entries per WR, and SRQ limit. |

**Return Value**

Returns a pointer to the newly created `struct ibv_srq` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- QPs are associated with an SRQ at QP creation time via `qp_init_attr->srq`.
- When a QP uses an SRQ, the QP's own RQ is unused; `ibv_post_recv()` must not be called on the QP -- use `ibv_post_srq_recv()` instead.
- The SRQ limit event is edge-triggered: it fires once when the threshold is crossed. Reset it with `ibv_modify_srq()` to re-arm.
- SRQ is not supported with all QP types. Check device capabilities.

---

## ibv_dealloc_mw

```c
int ibv_dealloc_mw(struct ibv_mw *mw);
```

**Description**

Deallocates a Memory Window previously allocated with `ibv_alloc_mw()`. If the MW is currently bound, the bind is implicitly invalidated. After deallocation, any remote access using the MW's R_Key will fail.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `mw` | `struct ibv_mw *` | The Memory Window to deallocate. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- It is the application's responsibility to ensure that no in-flight remote operations reference the MW's R_Key at the time of deallocation.
- After this call, the `mw` pointer is invalid.

---

## ibv_dealloc_pd

```c
int ibv_dealloc_pd(struct ibv_pd *pd);
```

**Description**

Deallocates a Protection Domain previously allocated with `ibv_alloc_pd()`. The PD must have no associated resources (MRs, QPs, AHs, MWs, SRQs) remaining; if any child resources still exist, the call will fail.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pd` | `struct ibv_pd *` | The Protection Domain to deallocate. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- All resources created under this PD must be destroyed first.
- After this call, the `pd` pointer is invalid.
- Attempting to deallocate a PD with outstanding resources returns `EBUSY`.

---

## ibv_dereg_mr

```c
int ibv_dereg_mr(struct ibv_mr *mr);
```

**Description**

Deregisters a Memory Region previously registered with `ibv_reg_mr()`. This releases the memory pinning and the HCA's translation table entries. After deregistration, any work requests referencing the MR's lkey or rkey will fail.

The application must ensure that no in-flight work requests reference this MR before deregistering it. Violating this rule results in undefined behavior, potentially including data corruption or hardware errors.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `mr` | `struct ibv_mr *` | The Memory Region to deregister. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- Deregistration unpins the underlying pages. If the memory was allocated with `malloc()`, it may be swapped out after deregistration.
- Do not free the underlying memory buffer before deregistering the MR.
- All Memory Windows bound to this MR must be unbound or deallocated first.

---

## ibv_destroy_ah

```c
int ibv_destroy_ah(struct ibv_ah *ah);
```

**Description**

Destroys an Address Handle previously created with `ibv_create_ah()`. The AH must not be referenced by any pending send work requests on any QP at the time of destruction.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `ah` | `struct ibv_ah *` | The Address Handle to destroy. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- Ensure no in-flight UD send work requests reference this AH before destroying it.
- After this call, the `ah` pointer is invalid.

---

## ibv_destroy_cq

```c
int ibv_destroy_cq(struct ibv_cq *cq);
```

**Description**

Destroys a Completion Queue previously created with `ibv_create_cq()`. All QPs associated with this CQ (either as send CQ or receive CQ) must be destroyed first. All asynchronous events associated with the CQ must have been acknowledged.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `cq` | `struct ibv_cq *` | The CQ to destroy. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- Destroying a CQ that still has associated QPs returns `EBUSY`.
- All unacknowledged async events for this CQ must be acknowledged before destruction, or the call blocks.
- Outstanding CQEs in the CQ at destruction time are silently discarded.

---

## ibv_destroy_qp

```c
int ibv_destroy_qp(struct ibv_qp *qp);
```

**Description**

Destroys a Queue Pair previously created with `ibv_create_qp()`. All outstanding work requests on the QP are flushed, and their completions are discarded. The QP is removed from any associated CQs and SRQ. All unacknowledged async events for the QP must have been acknowledged.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `qp` | `struct ibv_qp *` | The QP to destroy. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- After destruction, any CQEs that were generated by the QP but not yet polled may still appear in the CQ; they should be ignored (check the QP number).
- All unacknowledged async events must be acknowledged first, or the call blocks.
- Destroying a QP does not destroy its associated CQs or SRQ.

---

## ibv_destroy_srq

```c
int ibv_destroy_srq(struct ibv_srq *srq);
```

**Description**

Destroys a Shared Receive Queue previously created with `ibv_create_srq()`. All QPs associated with this SRQ must be destroyed first. Outstanding receive work requests on the SRQ are flushed.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `srq` | `struct ibv_srq *` | The SRQ to destroy. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- All QPs using this SRQ must be destroyed before the SRQ can be destroyed.
- Unacknowledged async events for the SRQ must be acknowledged first.

---

## ibv_free_device_list

```c
void ibv_free_device_list(struct ibv_device **list);
```

**Description**

Frees the array of RDMA devices returned by `ibv_get_device_list()`. This must be called after the application has finished examining the device list and has opened all desired device contexts. The function frees the array itself but does not close any device contexts opened via `ibv_open_device()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `list` | `struct ibv_device **` | The device list array to free, as returned by `ibv_get_device_list()`. |

**Return Value**

None (void).

**Key Notes**

- Calling `ibv_free_device_list()` does not invalidate device contexts obtained from `ibv_open_device()`.
- The individual `ibv_device` pointers in the list become invalid after this call. If you need the device name, copy it before freeing the list.
- Must be called exactly once per `ibv_get_device_list()` call to avoid memory leaks.

---

## ibv_get_async_event

```c
int ibv_get_async_event(struct ibv_context *context,
                        struct ibv_async_event *event);
```

**Description**

Retrieves the next asynchronous event from the device context's event queue. Asynchronous events report exceptional conditions such as CQ overflows, QP errors (path migration, communication errors), port state changes, and SRQ limit events. By default, this function blocks until an event is available.

The event must be acknowledged via `ibv_ack_async_event()` after it has been processed.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context to read events from. |
| `event` | `struct ibv_async_event *` | Output parameter filled with the event type and associated resource (QP, CQ, SRQ, or port). |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The function blocks by default. To use non-blocking mode, set `O_NONBLOCK` on the async event file descriptor (`context->async_fd`) using `fcntl()`.
- Common event types include `IBV_EVENT_QP_FATAL`, `IBV_EVENT_CQ_ERR`, `IBV_EVENT_PORT_ACTIVE`, `IBV_EVENT_PORT_ERR`, and `IBV_EVENT_SRQ_LIMIT_REACHED`.
- Use `poll()` or `epoll()` on `context->async_fd` to multiplex async event monitoring with other I/O.
- Every event retrieved must be acknowledged exactly once with `ibv_ack_async_event()`.

---

## ibv_get_device_list

```c
struct ibv_device **ibv_get_device_list(int *num_devices);
```

**Description**

Returns a NULL-terminated array of available RDMA devices on the system. This is typically the first verbs function called by an application, used to discover and enumerate RDMA-capable adapters. The returned list must be freed with `ibv_free_device_list()` when no longer needed.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `num_devices` | `int *` | Output parameter set to the number of devices in the list. May be `NULL` if the count is not needed. |

**Return Value**

Returns a pointer to a NULL-terminated array of `ibv_device` pointers on success, or `NULL` on failure with `errno` set.

**Key Notes**

- The returned array and the `ibv_device` structures it contains are owned by the library. They must not be freed directly; use `ibv_free_device_list()` instead.
- Device contexts obtained from `ibv_open_device()` remain valid after the list is freed.
- If no RDMA devices are present, the function succeeds with `*num_devices` set to 0 and an empty (but non-NULL) list.
- Call `ibv_get_device_name()` or `ibv_get_device_guid()` to identify specific devices.

---

## ibv_get_device_name

```c
const char *ibv_get_device_name(struct ibv_device *device);
```

**Description**

Returns the kernel device name string for the given RDMA device (e.g., `"mlx5_0"`, `"rxe0"`). This name corresponds to the directory name under `/sys/class/infiniband/` and is used for identification and debugging purposes.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `device` | `struct ibv_device *` | Device pointer from the list returned by `ibv_get_device_list()`. |

**Return Value**

Returns a pointer to a null-terminated string containing the device name. The string is owned by the library and must not be freed by the caller.

**Key Notes**

- The returned string is valid as long as the device list has not been freed.
- Copy the name if you need it after calling `ibv_free_device_list()`.

---

## ibv_modify_qp

```c
int ibv_modify_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                  int attr_mask);
```

**Description**

Modifies the attributes of a Queue Pair, most importantly transitioning it through the required state machine: RESET -> INIT -> RTR -> RTS. Each state transition requires a specific set of attributes to be provided, indicated by the `attr_mask` bitmask. The required attributes vary by QP transport type (RC, UC, UD).

This function is also used to modify QP attributes in the RTS state, such as the retry count, RNR retry count, or the path MTU. Some attributes can only be set during specific transitions.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `qp` | `struct ibv_qp *` | The QP to modify. |
| `attr` | `struct ibv_qp_attr *` | Structure containing the new attribute values. Only fields corresponding to set bits in `attr_mask` are used. |
| `attr_mask` | `int` | Bitmask indicating which attributes in `attr` are being set. Composed of `enum ibv_qp_attr_mask` values ORed together. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- The required `attr_mask` bits for each state transition are QP-type-dependent. For RC QPs transitioning INIT -> RTR, the minimum set is: `IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER`.
- Setting invalid attributes for the current transition returns `EINVAL`.
- For RC QPs, the RTR -> RTS transition requires the local QP's PSN (`sq_psn`), timeout, retry count, RNR retry, and maximum initiator depth.
- UD QPs only require RESET -> INIT -> RTR -> RTS with port number and Q_Key.

<div class="note">

**State transition cheat sheet for RC QPs:**

| Transition | Key Attributes |
|-----------|---------------|
| RESET -> INIT | `port_num`, `pkey_index`, `access_flags` |
| INIT -> RTR | `path_mtu`, `dest_qp_num`, `rq_psn`, `ah_attr`, `max_dest_rd_atomic`, `min_rnr_timer` |
| RTR -> RTS | `sq_psn`, `timeout`, `retry_cnt`, `rnr_retry`, `max_rd_atomic` |

</div>

---

## ibv_open_device

```c
struct ibv_context *ibv_open_device(struct ibv_device *device);
```

**Description**

Opens an RDMA device and returns a context handle used for all subsequent operations on that device. The context encapsulates the file descriptor to the kernel RDMA subsystem and the user-space driver's device-specific state. This is the entry point for creating all RDMA resources (PDs, MRs, QPs, CQs).

The device pointer is obtained from the array returned by `ibv_get_device_list()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `device` | `struct ibv_device *` | Pointer to a device from the list returned by `ibv_get_device_list()`. |

**Return Value**

Returns a pointer to the `struct ibv_context` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- Multiple contexts can be opened for the same device, but this is rarely useful and wastes resources.
- The device list can be freed after opening the context; the context remains valid.
- The context must be closed with `ibv_close_device()` when no longer needed.
- On Linux, opening a device opens `/dev/infiniband/uverbsN` and mmap's the doorbell pages.

---

## ibv_poll_cq

```c
int ibv_poll_cq(struct ibv_cq *cq, int num_entries,
                struct ibv_wc *wc);
```

**Description**

Polls the Completion Queue for completed work requests. This is a non-blocking, lightweight operation that reads CQEs directly from user-space memory (the CQ buffer mapped via mmap), making it extremely fast -- typically on the order of tens of nanoseconds. It is the primary mechanism for determining when RDMA operations have completed in high-performance applications.

Each returned work completion (`ibv_wc`) contains the status of the operation, the work request ID (`wr_id`), the number of bytes transferred (for receive completions), the opcode, and other metadata.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `cq` | `struct ibv_cq *` | The CQ to poll. |
| `num_entries` | `int` | Maximum number of completions to return. |
| `wc` | `struct ibv_wc *` | Array of at least `num_entries` work completion structures to fill in. |

**Return Value**

Returns the number of completions retrieved (0 to `num_entries`) on success, or a negative value on failure (CQ in error state).

**Key Notes**

- A return value of 0 means no completions are available; this is not an error.
- A negative return value indicates the CQ is in an error state (overrun) and cannot be used further.
- Always check `wc->status == IBV_WC_SUCCESS` for each completion. A non-success status indicates the operation failed, and the QP has likely transitioned to the Error state.
- For send operations, a successful completion means the send buffer can be reused. It does NOT mean the data has been received by the remote side (unless the operation was an RDMA Write with Immediate or the QP uses reliable transport).
- `ibv_poll_cq()` is **not** thread-safe on the same CQ. Use external locking if multiple threads poll the same CQ.
- Polled CQEs are consumed and removed from the CQ.

---

## ibv_post_recv

```c
int ibv_post_recv(struct ibv_qp *qp, struct ibv_recv_wr *wr,
                  struct ibv_recv_wr **bad_wr);
```

**Description**

Posts one or more receive work requests to the receive queue of a QP. Each receive WR describes a set of memory buffers (scatter/gather list) where incoming data will be placed. Receive WRs must be posted before the remote side sends data; otherwise, the receive will fail with a Receiver Not Ready (RNR) error at the sender.

Receive work requests can be chained via the `next` pointer to post multiple WRs in a single call, reducing doorbell overhead.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `qp` | `struct ibv_qp *` | The QP to post receive WRs to. The QP must be in INIT, RTR, or RTS state. |
| `wr` | `struct ibv_recv_wr *` | Linked list of receive work requests. |
| `bad_wr` | `struct ibv_recv_wr **` | Output: set to point to the first WR that failed to post, if any. |

**Return Value**

Returns 0 on success (all WRs posted), or `errno` on failure. On failure, `*bad_wr` points to the first WR that could not be posted.

**Key Notes**

- Receive buffers must be registered as MRs with local write access (`IBV_ACCESS_LOCAL_WRITE`).
- For RC and UC QPs, the total buffer size must be at least as large as the incoming message. For UD QPs, add 40 bytes for the GRH that is prepended to every received message.
- Do not call `ibv_post_recv()` on a QP that is associated with an SRQ. Use `ibv_post_srq_recv()` instead.
- The `wr_id` field is application-defined and returned in the work completion; use it to identify which buffer received data.

---

## ibv_post_send

```c
int ibv_post_send(struct ibv_qp *qp, struct ibv_send_wr *wr,
                  struct ibv_send_wr **bad_wr);
```

**Description**

Posts one or more send work requests to the send queue of a QP. The WR type determines the RDMA operation: Send, Send with Immediate, RDMA Write, RDMA Write with Immediate, RDMA Read, or Atomic (Compare-and-Swap, Fetch-and-Add). This is the primary data-path function for initiating RDMA operations.

Work requests can be chained via the `next` pointer to post multiple WRs in a single doorbell ring, amortizing the MMIO cost.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `qp` | `struct ibv_qp *` | The QP to post send WRs to. Must be in the RTS state. |
| `wr` | `struct ibv_send_wr *` | Linked list of send work requests. |
| `bad_wr` | `struct ibv_send_wr **` | Output: set to point to the first WR that failed to post, if any. |

**Return Value**

Returns 0 on success (all WRs posted), or `errno` on failure. On failure, `*bad_wr` points to the first WR that could not be posted.

**Key Notes**

- The QP must be in the RTS state. Posting to a QP in any other state returns `EINVAL`.
- For RDMA Read and Write operations, `wr->wr.rdma.remote_addr` and `wr->wr.rdma.rkey` must specify the remote memory location.
- For atomic operations, `wr->wr.atomic.remote_addr`, `wr->wr.atomic.compare_add`, and (for CAS) `wr->wr.atomic.swap` must be set. The remote address must be 8-byte aligned.
- Use the `IBV_SEND_INLINE` flag for small messages to avoid the overhead of a DMA read from the send buffer. The data is copied into the WQE itself.
- Use the `IBV_SEND_SIGNALED` flag to request a CQE for this WR. If the QP was created with `sq_sig_all = 1`, all WRs generate completions regardless of this flag.
- The `IBV_SEND_FENCE` flag ensures all prior RDMA Read operations have completed before this WR is executed.
- Inline data size is limited; check `max_inline_data` from QP creation.

---

## ibv_post_srq_recv

```c
int ibv_post_srq_recv(struct ibv_srq *srq,
                      struct ibv_recv_wr *recv_wr,
                      struct ibv_recv_wr **bad_recv_wr);
```

**Description**

Posts one or more receive work requests to a Shared Receive Queue. This is the SRQ equivalent of `ibv_post_recv()`. Buffers posted to the SRQ are available to any QP associated with that SRQ. When a message arrives on any of those QPs, one of the SRQ's receive buffers is consumed.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `srq` | `struct ibv_srq *` | The SRQ to post receive WRs to. |
| `recv_wr` | `struct ibv_recv_wr *` | Linked list of receive work requests. |
| `bad_recv_wr` | `struct ibv_recv_wr **` | Output: set to point to the first WR that failed to post, if any. |

**Return Value**

Returns 0 on success, or `errno` on failure.

**Key Notes**

- SRQ receive buffers must be registered as MRs with local write access.
- The `wr_id` is particularly important with SRQs since the completion could arrive on any QP's CQ.
- Monitor the SRQ fill level and replenish before it empties to avoid RNR errors.

---

## ibv_query_device

```c
int ibv_query_device(struct ibv_context *context,
                     struct ibv_device_attr *device_attr);
```

**Description**

Queries the RDMA device for its fixed attributes and capabilities. The returned structure contains hardware limits such as the maximum number of QPs, CQs, MRs, the maximum message size, the maximum number of scatter/gather entries, atomic capabilities, and more.

This function should be called early in application initialization to discover device limits and adapt resource allocation accordingly.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context to query. |
| `device_attr` | `struct ibv_device_attr *` | Output structure filled with device attributes. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- Key fields include `max_qp`, `max_qp_wr`, `max_cq`, `max_cqe`, `max_mr`, `max_mr_size`, `max_sge`, `max_qp_rd_atom` (max outstanding RDMA Read/Atomic as initiator), and `max_res_rd_atom` (total responder resources).
- The `atomic_cap` field indicates the atomicity guarantee: `IBV_ATOMIC_NONE`, `IBV_ATOMIC_HCA` (atomic within the HCA), or `IBV_ATOMIC_GLOB` (globally atomic across all HCAs).
- For extended attributes (e.g., ODP capabilities, RSS, timestamp support), use `ibv_query_device_ex()`.
- The `max_inline_data` capability is **not** reported here; it is determined at QP creation time.

---

## ibv_query_device_ex

```c
int ibv_query_device_ex(struct ibv_context *context,
                        const struct ibv_query_device_ex_input *input,
                        struct ibv_device_attr_ex *attr);
```

**Description**

Extended version of `ibv_query_device()` that returns additional device capabilities beyond those in the base `ibv_device_attr` structure. This includes On-Demand Paging (ODP) capabilities, RSS (Receive Side Scaling) support, timestamp resolution, completion timestamp support, TM (Tag Matching) capabilities, and other advanced features.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context to query. |
| `input` | `const struct ibv_query_device_ex_input *` | Input parameters. Currently reserved; pass `NULL` or a zeroed structure. |
| `attr` | `struct ibv_device_attr_ex *` | Output structure filled with base and extended device attributes. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- The `attr->orig_attr` field contains the same base attributes as `ibv_query_device()`.
- The `odp_caps` field describes ODP support: which operations support on-demand paging and whether implicit ODP is available.
- Check `rss_caps` to determine if the device supports RSS and the maximum number of indirection table entries.
- The `comp_mask` field in `input` is reserved for future extensions.

---

## ibv_query_gid

```c
int ibv_query_gid(struct ibv_context *context, uint8_t port_num,
                  int index, union ibv_gid *gid);
```

**Description**

Queries a specific GID (Global Identifier) table entry for a port. GIDs are 128-bit identifiers used for global routing, analogous to IPv6 addresses. For InfiniBand, the GID is formed from the subnet prefix and the port GUID. For RoCE, the GID corresponds to the IPv4 or IPv6 address assigned to the network interface.

Each port has a GID table with multiple entries. Index 0 is the default GID based on the port GUID (InfiniBand) or the link-local IPv6 address (RoCE).

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context. |
| `port_num` | `uint8_t` | Physical port number (1-based). |
| `index` | `int` | GID table index to query. |
| `gid` | `union ibv_gid *` | Output parameter filled with the 128-bit GID value. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- For RoCEv2, each IP address configured on the network interface creates a GID table entry. Use the GID index that corresponds to the desired source IP.
- Query `ibv_query_port()` first to learn the GID table size (`port_attr.gid_tbl_len`).
- Use `ibv_query_gid_type()` (if available) to determine if a GID is RoCEv1 or RoCEv2.
- The GID is needed when filling in the `ibv_ah_attr` for creating Address Handles or when transitioning RC QPs.

---

## ibv_query_port

```c
int ibv_query_port(struct ibv_context *context, uint8_t port_num,
                   struct ibv_port_attr *port_attr);
```

**Description**

Queries the attributes of a specific physical port on the RDMA device. The returned information includes the port state, maximum MTU, active MTU, LID, SM LID, link layer type, GID table length, and more. This function is essential for determining whether a port is active and what its capabilities are.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `context` | `struct ibv_context *` | Device context. |
| `port_num` | `uint8_t` | Physical port number (1-based). |
| `port_attr` | `struct ibv_port_attr *` | Output structure filled with port attributes. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- Check `port_attr->state` for `IBV_PORT_ACTIVE` before using the port. Other states include `IBV_PORT_DOWN`, `IBV_PORT_INIT`, and `IBV_PORT_ARMED`.
- The `link_layer` field distinguishes InfiniBand (`IBV_LINK_LAYER_INFINIBAND`) from Ethernet (`IBV_LINK_LAYER_ETHERNET`) ports. This is critical for determining whether to use LID-based or GID-based addressing.
- `active_mtu` indicates the maximum MTU negotiated with the link partner. Use this (not `max_mtu`) when setting `path_mtu` in QP transitions.
- Port numbers are 1-based. A dual-port HCA has ports 1 and 2.

---

## ibv_query_qp

```c
int ibv_query_qp(struct ibv_qp *qp, struct ibv_qp_attr *attr,
                 int attr_mask, struct ibv_qp_init_attr *init_attr);
```

**Description**

Queries the current attributes of a Queue Pair. This is primarily useful for debugging and for verifying the QP's state after a series of `ibv_modify_qp()` calls. The `attr_mask` parameter specifies which attributes to query.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `qp` | `struct ibv_qp *` | The QP to query. |
| `attr` | `struct ibv_qp_attr *` | Output structure filled with current QP attributes. |
| `attr_mask` | `int` | Bitmask of attributes to query. Use `IBV_QP_STATE` to query only the state, or a combination of flags. |
| `init_attr` | `struct ibv_qp_init_attr *` | Output structure filled with the QP's initial creation attributes. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- This is a slow control-path operation; do not use it in the data path.
- Not all attributes are queryable on all devices. Some attributes may be returned as zero if the hardware does not support querying them.
- Useful for debugging QP state issues: if a QP unexpectedly enters the Error state, query it to inspect the current state.

---

## ibv_reg_mr

```c
struct ibv_mr *ibv_reg_mr(struct ibv_pd *pd, void *addr,
                          size_t length, int access);
```

**Description**

Registers a contiguous region of user-space memory as a Memory Region (MR) for RDMA operations. Registration pins the physical pages in memory (preventing them from being swapped out), creates translation entries in the HCA's Memory Translation Table (MTT), and returns local and remote keys (L_Key and R_Key) that are used in work requests to reference the memory.

Memory registration is one of the most expensive operations in the verbs API, often taking hundreds of microseconds or more for large regions. Applications should register memory once during initialization and reuse the MR throughout the program's lifetime.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pd` | `struct ibv_pd *` | Protection Domain to associate the MR with. |
| `addr` | `void *` | Starting virtual address of the memory region. |
| `length` | `size_t` | Length of the memory region in bytes. |
| `access` | `int` | Access flags, ORed together from `enum ibv_access_flags`. |

**Access Flags**

| Flag | Value | Description |
|------|-------|-------------|
| `IBV_ACCESS_LOCAL_WRITE` | 1 | Allow local writes (required for receive buffers). |
| `IBV_ACCESS_REMOTE_WRITE` | 2 | Allow remote RDMA Write. Requires `LOCAL_WRITE`. |
| `IBV_ACCESS_REMOTE_READ` | 4 | Allow remote RDMA Read. |
| `IBV_ACCESS_REMOTE_ATOMIC` | 8 | Allow remote atomic operations. Requires `LOCAL_WRITE`. |
| `IBV_ACCESS_MW_BIND` | 16 | Allow Memory Window binding. |
| `IBV_ACCESS_ON_DEMAND` | 32 | Use On-Demand Paging (ODP) instead of pinning. |
| `IBV_ACCESS_HUGETLB` | 64 | Memory is backed by huge pages. |

**Return Value**

Returns a pointer to the newly registered `struct ibv_mr` on success, or `NULL` on failure with `errno` set. The `mr->lkey` and `mr->rkey` fields contain the keys to use in work requests.

**Key Notes**

- The memory at `[addr, addr+length)` must be allocated before registration. Common sources include `malloc()`, `mmap()`, `posix_memalign()`, and huge pages.
- Registration pins pages in physical memory. Registering very large regions may fail if the system runs out of pinnable memory. Check `/proc/sys/vm/max_map_count` and `ulimit -l`.
- `IBV_ACCESS_REMOTE_WRITE` and `IBV_ACCESS_REMOTE_ATOMIC` implicitly require `IBV_ACCESS_LOCAL_WRITE`.
- For performance, align the buffer to a page boundary and use huge pages for large registrations.
- The R_Key should be shared with remote peers only if remote access is intended. Treat R_Keys as security-sensitive values.
- Use `ibv_reg_mr_iova()` or `ibv_reg_mr_iova2()` for registering memory with a specific IOVA (I/O Virtual Address).

---

## ibv_rereg_mr

```c
int ibv_rereg_mr(struct ibv_mr *mr, int flags,
                 struct ibv_pd *pd, void *addr,
                 size_t length, int access);
```

**Description**

Re-registers a Memory Region by changing its Protection Domain, address range, or access flags without fully deregistering and re-registering. This can be more efficient than a deregister/register cycle because it may allow the HCA to update its translation tables in place rather than tearing them down and rebuilding them.

The `flags` parameter indicates which attributes are being changed. Not all combinations are supported by all devices.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `mr` | `struct ibv_mr *` | The MR to re-register. |
| `flags` | `int` | Bitmask of `IBV_REREG_MR_CHANGE_TRANSLATION`, `IBV_REREG_MR_CHANGE_PD`, `IBV_REREG_MR_CHANGE_ACCESS`. |
| `pd` | `struct ibv_pd *` | New PD (if changing PD), or the current PD. |
| `addr` | `void *` | New starting address (if changing translation). |
| `length` | `size_t` | New length (if changing translation). |
| `access` | `int` | New access flags (if changing access). |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- Not all devices support re-registration. If unsupported, use the deregister/register sequence.
- The MR's lkey and rkey may change after re-registration. Always update any cached keys.
- No work requests referencing the MR may be in flight during re-registration.
- If re-registration fails, the MR is left in an undefined state and must be deregistered.

---

## ibv_req_notify_cq

```c
int ibv_req_notify_cq(struct ibv_cq *cq, int solicited_only);
```

**Description**

Arms the CQ to deliver a completion notification event on the associated completion channel when the next CQE is added. This is used in event-driven (interrupt-based) completion processing as an alternative to busy-polling. After the notification fires, the CQ must be re-armed to receive subsequent notifications.

The typical event-driven flow is: (1) arm the CQ, (2) poll all available CQEs, (3) wait on the completion channel for the next event, (4) acknowledge the event, (5) repeat from step 1.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `cq` | `struct ibv_cq *` | The CQ to arm for notification. |
| `solicited_only` | `int` | If non-zero, only generate a notification for "solicited" completions (send WRs with `IBV_SEND_SOLICITED` flag). If zero, notify on any completion. |

**Return Value**

Returns 0 on success, or the value of `errno` on failure.

**Key Notes**

- The notification is **one-shot**: after one event fires, the CQ must be re-armed.
- To avoid missing completions, always re-arm the CQ **before** polling for completions (arm-then-poll pattern). If you poll-then-arm, a CQE could arrive between the last poll and the arm, and you would not be notified.
- The `solicited_only` mode is useful for reducing interrupt frequency: only messages explicitly marked by the sender as "solicited" will trigger the notification.
- Completion events are delivered via the `ibv_comp_channel` associated with the CQ at creation time. Use `ibv_get_cq_event()` and `ibv_ack_cq_events()` to receive and acknowledge them.

---

## Summary: Resource Hierarchy

The following diagram summarizes the ownership and dependency relationships among verbs resources. A resource cannot be destroyed until all its children are destroyed first.

```
ibv_context (device)
├── ibv_pd (protection domain)
│   ├── ibv_mr (memory region)
│   ├── ibv_mw (memory window)
│   ├── ibv_qp (queue pair)
│   ├── ibv_srq (shared receive queue)
│   └── ibv_ah (address handle)
├── ibv_cq (completion queue)
├── ibv_comp_channel (completion event channel)
└── ibv_xrcd (XRC domain)
```

<div class="note">

**Thread Safety**: In general, the verbs API allows concurrent calls on **different** objects from multiple threads without external locking. However, concurrent calls on the **same** object (e.g., two threads posting to the same QP, or two threads polling the same CQ) require external synchronization. The exception is `ibv_post_send()` and `ibv_post_recv()`, which are thread-safe with respect to each other on the same QP (you can post sends and receives concurrently), but two concurrent `ibv_post_send()` calls on the same QP are **not** safe.

</div>
