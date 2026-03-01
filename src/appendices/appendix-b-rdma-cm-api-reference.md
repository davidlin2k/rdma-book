# Appendix B: RDMA-CM API Reference

This appendix provides an alphabetical reference of the most commonly used RDMA Communication Manager (`librdmacm`) functions. The RDMA-CM provides a connection management abstraction over the verbs API, handling address resolution, route resolution, and connection establishment for RC, UC, and UD QPs. It supports both InfiniBand and RoCE (including RoCEv2) transports, and provides a socket-like programming model familiar to TCP/IP developers.

<div class="note">

The RDMA-CM can operate in two modes: **asynchronous** (using event channels) and **synchronous** (blocking calls). This reference covers the asynchronous model, which is the recommended approach for production applications. Additionally, the `librdmacm` library provides a set of simplified "active-side" helper functions (prefixed `rdma_post_*`, `rdma_reg_*`) for common operations.

</div>

---

## rdma_accept

```c
int rdma_accept(struct rdma_cm_id *id,
                struct rdma_conn_param *conn_param);
```

**Description**

Accepts an incoming connection request on a listening RDMA-CM ID. This is called on the server side after receiving an `RDMA_CM_EVENT_CONNECT_REQUEST` event. The `conn_param` structure specifies QP parameters for the connection such as the initiator depth (maximum outstanding RDMA Reads as responder), responder resources (maximum outstanding RDMA Reads as initiator), retry count, and RNR retry count.

If a QP was created for this `rdma_cm_id` via `rdma_create_qp()`, the CM automatically transitions it to the RTS state as part of the accept process.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID from the `RDMA_CM_EVENT_CONNECT_REQUEST` event. |
| `conn_param` | `struct rdma_conn_param *` | Connection parameters. May be `NULL` for default values. |

**Key `rdma_conn_param` Fields**

| Field | Description |
|-------|-------------|
| `private_data` | Pointer to private data sent to the peer (up to 196 bytes for RC). |
| `private_data_len` | Length of private data in bytes. |
| `responder_resources` | Maximum number of outstanding RDMA Read requests this side can handle as a responder. |
| `initiator_depth` | Maximum number of outstanding RDMA Read requests this side can initiate. |
| `rnr_retry_count` | Number of RNR retries (7 = infinite). |
| `retry_count` | Number of retries for timeouts. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The `id` in the connect request event is a **new** CM ID, different from the listening ID.
- After `rdma_accept()` succeeds, the server should wait for an `RDMA_CM_EVENT_ESTABLISHED` event to confirm the connection is fully established.
- The `responder_resources` should not exceed the device's `max_qp_rd_atom` capability.
- Private data can be used to exchange application-level metadata (e.g., memory region keys, buffer addresses) during connection setup.

---

## rdma_ack_cm_event

```c
int rdma_ack_cm_event(struct rdma_cm_event *event);
```

**Description**

Acknowledges and frees an RDMA-CM event previously obtained via `rdma_get_cm_event()`. Every event must be acknowledged exactly once. The event structure and any data it references (such as private data) become invalid after this call.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `event` | `struct rdma_cm_event *` | The event to acknowledge, as returned by `rdma_get_cm_event()`. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- Failing to acknowledge events will cause memory leaks and may prevent subsequent operations from completing.
- Copy any private data from the event before acknowledging it, as the data pointer becomes invalid.
- For `RDMA_CM_EVENT_CONNECT_REQUEST` events, the new CM ID remains valid after acknowledgment; only the event structure itself is freed.

---

## rdma_bind_addr

```c
int rdma_bind_addr(struct rdma_cm_id *id, struct sockaddr *addr);
```

**Description**

Binds an RDMA-CM ID to a local address. This is analogous to `bind()` in the sockets API. The bind operation associates the CM ID with a specific local IP address and port, and also resolves the local address to a specific RDMA device. After binding, the CM ID's `verbs` and `pd` fields are set (if the CM ID was created with a PD).

For servers, binding is required before calling `rdma_listen()`. For clients, binding is optional; if not called, the address is resolved implicitly during `rdma_resolve_addr()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID to bind. |
| `addr` | `struct sockaddr *` | Local address to bind to. Use `INADDR_ANY` (for IPv4) or `in6addr_any` (for IPv6) to bind to all interfaces. Set the port to 0 to let the system choose a port. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- After a successful bind, `id->verbs` is set to the device context associated with the bound address. This context can be used for creating PDs, CQs, and other verbs resources.
- For RoCE, the address determines which network interface (and thus which GID) is used.
- Binding to `INADDR_ANY` with a specific port is the typical server pattern.
- The port number is in network byte order (use `htons()`).

---

## rdma_connect

```c
int rdma_connect(struct rdma_cm_id *id,
                 struct rdma_conn_param *conn_param);
```

**Description**

Initiates a connection request to a remote server. This is called on the client side after address and route resolution have completed. The function sends a connection request (REQ) message to the remote side. If a QP was created via `rdma_create_qp()`, the CM automatically manages the QP state transitions.

The connection process is asynchronous: `rdma_connect()` initiates the request, and the result is delivered as an `RDMA_CM_EVENT_ESTABLISHED` (success) or `RDMA_CM_EVENT_REJECTED` / `RDMA_CM_EVENT_UNREACHABLE` (failure) event.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID with resolved address and route. |
| `conn_param` | `struct rdma_conn_param *` | Connection parameters including private data, initiator depth, and responder resources. May be `NULL` for defaults. |

**Return Value**

Returns 0 on success (request sent), or -1 on failure with `errno` set.

**Key Notes**

- Before calling `rdma_connect()`, both `rdma_resolve_addr()` and `rdma_resolve_route()` must have completed successfully.
- Private data (up to 56 bytes for IB RC, 196 bytes for RDMA-CM over IP) can be included in the connection request for exchanging application metadata.
- The `initiator_depth` and `responder_resources` must not exceed the device capabilities.
- If using RDMA-CM managed QPs, the QP is automatically transitioned to RTR and RTS.

---

## rdma_create_event_channel

```c
struct rdma_event_channel *rdma_create_event_channel(void);
```

**Description**

Creates an event channel for receiving asynchronous RDMA-CM events. The event channel is the communication mechanism between the kernel CM agent and the user-space application. It contains a file descriptor that can be used with `poll()`, `epoll()`, or `select()` for non-blocking event processing.

Every CM ID must be associated with an event channel at creation time. Multiple CM IDs can share a single event channel.

**Parameters**

None.

**Return Value**

Returns a pointer to a new `struct rdma_event_channel` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- The event channel's file descriptor is `channel->fd`. Use it with `epoll()` for scalable event multiplexing.
- For non-blocking operation, set `O_NONBLOCK` on the channel's file descriptor using `fcntl()`.
- Destroy the channel with `rdma_destroy_event_channel()` after all associated CM IDs are destroyed.
- A typical application creates one event channel and uses it for all CM IDs.

---

## rdma_create_id

```c
int rdma_create_id(struct rdma_event_channel *channel,
                   struct rdma_cm_id **id,
                   void *context,
                   enum rdma_port_space ps);
```

**Description**

Creates a new RDMA-CM identifier, which represents a communication endpoint analogous to a socket. The CM ID is used throughout the connection lifecycle: address resolution, route resolution, connection establishment, and data transfer. It can also hold a QP (created via `rdma_create_qp()`) and manages its state transitions automatically.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `channel` | `struct rdma_event_channel *` | Event channel for delivering CM events for this ID. |
| `id` | `struct rdma_cm_id **` | Output: pointer to the newly created CM ID. |
| `context` | `void *` | User-defined context pointer stored in `id->context`. |
| `ps` | `enum rdma_port_space` | Port space: `RDMA_PS_TCP` (reliable, connection-oriented) or `RDMA_PS_UDP` (unreliable datagram). |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- Use `RDMA_PS_TCP` for RC QPs (most common) and `RDMA_PS_UDP` for UD QPs.
- The CM ID starts without an associated device. The device is resolved during `rdma_bind_addr()` or `rdma_resolve_addr()`.
- The `context` pointer is application-defined and useful for correlating events with application state.
- Destroy with `rdma_destroy_id()` when no longer needed.

---

## rdma_create_qp

```c
int rdma_create_qp(struct rdma_cm_id *id, struct ibv_pd *pd,
                   struct ibv_qp_init_attr *qp_init_attr);
```

**Description**

Creates a QP associated with the given RDMA-CM ID. This is a convenience wrapper around `ibv_create_qp()` that additionally associates the QP with the CM ID, enabling the CM to automatically manage QP state transitions during connection setup. After this call, `id->qp` points to the created QP.

If `pd` is `NULL`, the CM uses a default PD allocated on the CM ID's device.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID to associate the QP with. Must have a resolved device (after bind or address resolution). |
| `pd` | `struct ibv_pd *` | Protection Domain for the QP. May be `NULL` for the default PD. |
| `qp_init_attr` | `struct ibv_qp_init_attr *` | QP initialization attributes (type, CQs, capacities). |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- After this call, `id->qp` is set to the newly created QP.
- The CQs must be created on the same device as the CM ID's context (`id->verbs`).
- When using CM-managed QPs, do not call `ibv_modify_qp()` directly; the CM handles state transitions during `rdma_connect()` and `rdma_accept()`.
- Destroy with `rdma_destroy_qp()` before destroying the CM ID.

---

## rdma_destroy_event_channel

```c
void rdma_destroy_event_channel(struct rdma_event_channel *channel);
```

**Description**

Destroys an event channel previously created with `rdma_create_event_channel()`. All CM IDs associated with this channel must be destroyed before the channel can be safely destroyed.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `channel` | `struct rdma_event_channel *` | The event channel to destroy. |

**Return Value**

None (void).

**Key Notes**

- All CM IDs using this channel must be destroyed first.
- All unprocessed events on the channel are discarded.

---

## rdma_destroy_id

```c
int rdma_destroy_id(struct rdma_cm_id *id);
```

**Description**

Destroys an RDMA-CM identifier and releases all associated resources. If the CM ID has an active connection, it is disconnected. If a QP was created via `rdma_create_qp()`, it should be destroyed first with `rdma_destroy_qp()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID to destroy. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- Destroying a connected CM ID will trigger a disconnect.
- Destroy any CM-managed QP with `rdma_destroy_qp()` before destroying the CM ID.
- For listening CM IDs, all accepted connection CM IDs must be destroyed first.

---

## rdma_destroy_qp

```c
void rdma_destroy_qp(struct rdma_cm_id *id);
```

**Description**

Destroys the QP associated with a CM ID, previously created via `rdma_create_qp()`. This is a wrapper around `ibv_destroy_qp()` that also clears the `id->qp` pointer. Call this before `rdma_destroy_id()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID whose QP should be destroyed. |

**Return Value**

None (void).

**Key Notes**

- Only use this function for QPs created via `rdma_create_qp()`.
- After this call, `id->qp` is set to `NULL`.
- The associated CQs are not destroyed; they must be destroyed separately.

---

## rdma_disconnect

```c
int rdma_disconnect(struct rdma_cm_id *id);
```

**Description**

Initiates a graceful disconnect on an established connection. This sends a DREQ (Disconnect Request) message to the remote side. The remote side receives an `RDMA_CM_EVENT_DISCONNECTED` event. The local side also receives an `RDMA_CM_EVENT_DISCONNECTED` event after the remote acknowledges the disconnect.

The QP is transitioned to the Error state, causing all outstanding work requests to be flushed with error completions.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID of the established connection to disconnect. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- After disconnecting, poll the CQ to drain any flushed completions before destroying the QP.
- Both sides of the connection should call `rdma_disconnect()` for a clean shutdown.
- After disconnect, the CM ID can be reused for a new connection by calling `rdma_resolve_addr()` again (client side) or it can be destroyed.

---

## rdma_freeaddrinfo

```c
void rdma_freeaddrinfo(struct rdma_addrinfo *res);
```

**Description**

Frees the `rdma_addrinfo` linked list returned by `rdma_getaddrinfo()`. This is analogous to `freeaddrinfo()` in the sockets API.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `res` | `struct rdma_addrinfo *` | The address info list to free. |

**Return Value**

None (void).

**Key Notes**

- Must be called once for each successful `rdma_getaddrinfo()` call to avoid memory leaks.

---

## rdma_get_cm_event

```c
int rdma_get_cm_event(struct rdma_event_channel *channel,
                      struct rdma_cm_event **event);
```

**Description**

Retrieves the next pending CM event from the event channel. By default, this call blocks until an event is available. The returned event must be acknowledged via `rdma_ack_cm_event()` after processing.

Events report connection lifecycle state changes such as address resolution completion, route resolution completion, connection requests, connection establishment, disconnection, and errors.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `channel` | `struct rdma_event_channel *` | The event channel to read from. |
| `event` | `struct rdma_cm_event **` | Output: pointer to the received event structure. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key CM Event Types**

| Event | Description |
|-------|-------------|
| `RDMA_CM_EVENT_ADDR_RESOLVED` | Address resolution completed successfully. |
| `RDMA_CM_EVENT_ADDR_ERROR` | Address resolution failed. |
| `RDMA_CM_EVENT_ROUTE_RESOLVED` | Route resolution completed successfully. |
| `RDMA_CM_EVENT_ROUTE_ERROR` | Route resolution failed. |
| `RDMA_CM_EVENT_CONNECT_REQUEST` | Incoming connection request (server side). |
| `RDMA_CM_EVENT_CONNECT_RESPONSE` | Connection response received. |
| `RDMA_CM_EVENT_CONNECT_ERROR` | Connection attempt failed. |
| `RDMA_CM_EVENT_ESTABLISHED` | Connection fully established. |
| `RDMA_CM_EVENT_DISCONNECTED` | Connection disconnected by peer. |
| `RDMA_CM_EVENT_REJECTED` | Connection request rejected. |
| `RDMA_CM_EVENT_DEVICE_REMOVAL` | RDMA device removed. |
| `RDMA_CM_EVENT_MULTICAST_JOIN` | Multicast group join completed. |
| `RDMA_CM_EVENT_MULTICAST_ERROR` | Multicast error. |
| `RDMA_CM_EVENT_ADDR_CHANGE` | Address change detected. |
| `RDMA_CM_EVENT_TIMEWAIT_EXIT` | QP timewait period ended (safe to reuse QPN). |

**Key Notes**

- For non-blocking operation, set `O_NONBLOCK` on `channel->fd` and use `poll()` or `epoll()`.
- The `event->id` field identifies which CM ID generated the event.
- For `RDMA_CM_EVENT_CONNECT_REQUEST`, `event->id` is a **new** CM ID representing the incoming connection (distinct from the listening CM ID).
- The `event->param.conn` field contains connection parameters including private data from the peer.
- Always acknowledge events with `rdma_ack_cm_event()` after processing.

---

## rdma_getaddrinfo

```c
int rdma_getaddrinfo(const char *node, const char *service,
                     const struct rdma_addrinfo *hints,
                     struct rdma_addrinfo **res);
```

**Description**

Resolves a hostname and service name to RDMA address information, analogous to `getaddrinfo()` in the sockets API. The returned `rdma_addrinfo` structure contains the source and destination addresses suitable for use with `rdma_resolve_addr()` or `rdma_bind_addr()`.

This function provides a transport-independent way to obtain addressing information, supporting InfiniBand, RoCE, and iWARP.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `node` | `const char *` | Hostname, IP address, or NULL (for passive/server side). |
| `service` | `const char *` | Service name or port number as a string. |
| `hints` | `const struct rdma_addrinfo *` | Optional hints to filter results (address family, port space, flags). |
| `res` | `struct rdma_addrinfo **` | Output: linked list of resolved address info structures. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- Set `hints->ai_flags = RAI_PASSIVE` for server-side (listening) address resolution.
- Set `hints->ai_port_space` to `RDMA_PS_TCP` or `RDMA_PS_UDP`.
- The returned `res->ai_src_addr` and `res->ai_dst_addr` can be used directly with CM functions.
- Free the result with `rdma_freeaddrinfo()`.

---

## rdma_join_multicast

```c
int rdma_join_multicast(struct rdma_cm_id *id,
                        struct sockaddr *addr, void *context);
```

**Description**

Joins a multicast group on the specified CM ID. This is used with UD QPs for multicast communication. The join operation is asynchronous; a successful join is indicated by an `RDMA_CM_EVENT_MULTICAST_JOIN` event, which includes the multicast parameters (MLID, MGID) needed for creating Address Handles.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID (UD type) to join the multicast group on. |
| `addr` | `struct sockaddr *` | Multicast group address (IPv4/IPv6 multicast address or IB MGID). |
| `context` | `void *` | User-defined context returned with the join event. |

**Return Value**

Returns 0 on success (join request sent), or -1 on failure with `errno` set.

**Key Notes**

- The CM ID must be created with `RDMA_PS_UDP` port space for multicast.
- After receiving the `RDMA_CM_EVENT_MULTICAST_JOIN` event, create an AH from the event's multicast parameters for sending to the group.
- Use `rdma_leave_multicast()` to leave the group.
- For InfiniBand, the multicast group is managed by the Subnet Manager.

---

## rdma_leave_multicast

```c
int rdma_leave_multicast(struct rdma_cm_id *id,
                         struct sockaddr *addr);
```

**Description**

Leaves a multicast group previously joined via `rdma_join_multicast()`. The CM ID will no longer receive multicast traffic for the specified group after this call.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID to leave the multicast group on. |
| `addr` | `struct sockaddr *` | The multicast group address, matching the one used in `rdma_join_multicast()`. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The `addr` must match the address used when joining.
- Destroy any Address Handles associated with the multicast group after leaving.

---

## rdma_listen

```c
int rdma_listen(struct rdma_cm_id *id, int backlog);
```

**Description**

Places the CM ID in the listening state, ready to accept incoming connection requests. This is analogous to `listen()` in the sockets API. The CM ID must be bound to a local address via `rdma_bind_addr()` before calling this function.

Incoming connection requests arrive as `RDMA_CM_EVENT_CONNECT_REQUEST` events on the event channel.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID to place in the listening state. Must be bound to a local address. |
| `backlog` | `int` | Maximum number of pending connection requests. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The `backlog` parameter works similarly to the TCP listen backlog.
- Each incoming connection request creates a new CM ID (delivered in the event's `id` field), which must be separately accepted or rejected and eventually destroyed.
- The listening CM ID itself does not carry data; it only receives connection requests.

---

## rdma_post_read

```c
int rdma_post_read(struct rdma_cm_id *id, void *context,
                   void *addr, size_t length,
                   struct ibv_mr *mr, int flags,
                   uint64_t remote_addr, uint32_t rkey);
```

**Description**

Posts an RDMA Read work request on the QP associated with the CM ID. This is a simplified wrapper around `ibv_post_send()` with opcode `IBV_WR_RDMA_READ`. The operation reads data from the remote memory region specified by `remote_addr` and `rkey` into the local buffer at `addr`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | CM ID with an established connection and associated QP. |
| `context` | `void *` | User-defined context returned in the work completion's `wr_id`. |
| `addr` | `void *` | Local buffer address to read data into. |
| `length` | `size_t` | Number of bytes to read. |
| `mr` | `struct ibv_mr *` | MR covering the local buffer. Must have local write access. |
| `flags` | `int` | Send flags (e.g., `IBV_SEND_SIGNALED`). |
| `remote_addr` | `uint64_t` | Remote virtual address to read from. |
| `rkey` | `uint32_t` | Remote key for the remote memory region. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The local buffer MR must have `IBV_ACCESS_LOCAL_WRITE` permission.
- The remote MR must have been registered with `IBV_ACCESS_REMOTE_READ`.
- RDMA Read is only supported on RC and XRC QPs.

---

## rdma_post_recv

```c
int rdma_post_recv(struct rdma_cm_id *id, void *context,
                   void *addr, size_t length,
                   struct ibv_mr *mr);
```

**Description**

Posts a receive work request on the QP associated with the CM ID. This is a simplified wrapper around `ibv_post_recv()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | CM ID with an associated QP. |
| `context` | `void *` | User-defined context returned in the work completion's `wr_id`. |
| `addr` | `void *` | Local buffer address for incoming data. |
| `length` | `size_t` | Buffer size in bytes. |
| `mr` | `struct ibv_mr *` | MR covering the local buffer. Must have local write access. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- Receive buffers must be posted before the remote side sends data.
- The buffer must be large enough to hold the incoming message.

---

## rdma_post_send

```c
int rdma_post_send(struct rdma_cm_id *id, void *context,
                   void *addr, size_t length,
                   struct ibv_mr *mr, int flags);
```

**Description**

Posts a send work request on the QP associated with the CM ID. This is a simplified wrapper around `ibv_post_send()` with opcode `IBV_WR_SEND`. The data at the specified address is sent to the remote side, which must have a matching receive buffer posted.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | CM ID with an established connection and associated QP. |
| `context` | `void *` | User-defined context returned in the work completion's `wr_id`. |
| `addr` | `void *` | Local buffer address containing data to send. |
| `length` | `size_t` | Number of bytes to send. |
| `mr` | `struct ibv_mr *` | MR covering the local buffer. May be `NULL` if using inline data. |
| `flags` | `int` | Send flags (e.g., `IBV_SEND_SIGNALED`, `IBV_SEND_INLINE`). |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The remote side must have a receive buffer posted before this send completes.
- Use `IBV_SEND_INLINE` for small messages to avoid DMA overhead.
- A successful send completion means the local buffer can be reused.

---

## rdma_post_write

```c
int rdma_post_write(struct rdma_cm_id *id, void *context,
                    void *addr, size_t length,
                    struct ibv_mr *mr, int flags,
                    uint64_t remote_addr, uint32_t rkey);
```

**Description**

Posts an RDMA Write work request on the QP associated with the CM ID. This is a simplified wrapper around `ibv_post_send()` with opcode `IBV_WR_RDMA_WRITE`. The operation writes data from the local buffer to the remote memory region specified by `remote_addr` and `rkey`, without involving the remote CPU.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | CM ID with an established connection and associated QP. |
| `context` | `void *` | User-defined context returned in the work completion's `wr_id`. |
| `addr` | `void *` | Local buffer address containing data to write. |
| `length` | `size_t` | Number of bytes to write. |
| `mr` | `struct ibv_mr *` | MR covering the local buffer. |
| `flags` | `int` | Send flags. |
| `remote_addr` | `uint64_t` | Remote virtual address to write to. |
| `rkey` | `uint32_t` | Remote key for the remote memory region. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The remote MR must have been registered with `IBV_ACCESS_REMOTE_WRITE`.
- RDMA Write does not consume a receive buffer at the remote side (unlike Send).
- RDMA Write is "one-sided": the remote CPU is not notified. Use RDMA Write with Immediate if notification is needed.

---

## rdma_reg_msgs

```c
struct ibv_mr *rdma_reg_msgs(struct rdma_cm_id *id,
                             void *addr, size_t length);
```

**Description**

Registers a memory region for use with send and receive operations. This is a convenience wrapper around `ibv_reg_mr()` that registers the memory with `IBV_ACCESS_LOCAL_WRITE` permission -- sufficient for send and receive buffers but not for remote RDMA access.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | CM ID whose device context is used for registration. |
| `addr` | `void *` | Starting address of the memory region. |
| `length` | `size_t` | Length in bytes. |

**Return Value**

Returns a pointer to the registered `struct ibv_mr` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- The returned MR is suitable for `rdma_post_send()` and `rdma_post_recv()` but NOT for `rdma_post_read()` or `rdma_post_write()`.
- Deregister with `ibv_dereg_mr()` (there is no `rdma_dereg_*` function).

---

## rdma_reg_read

```c
struct ibv_mr *rdma_reg_read(struct rdma_cm_id *id,
                             void *addr, size_t length);
```

**Description**

Registers a memory region for use as a remote RDMA Read target. This is a convenience wrapper around `ibv_reg_mr()` with `IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ` permissions.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | CM ID whose device context is used for registration. |
| `addr` | `void *` | Starting address of the memory region. |
| `length` | `size_t` | Length in bytes. |

**Return Value**

Returns a pointer to the registered `struct ibv_mr` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- Share the returned `mr->rkey` and the buffer address with the remote peer so it can issue RDMA Reads.
- Deregister with `ibv_dereg_mr()`.

---

## rdma_reg_write

```c
struct ibv_mr *rdma_reg_write(struct rdma_cm_id *id,
                              void *addr, size_t length);
```

**Description**

Registers a memory region for use as a remote RDMA Write target. This is a convenience wrapper around `ibv_reg_mr()` with `IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE` permissions.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | CM ID whose device context is used for registration. |
| `addr` | `void *` | Starting address of the memory region. |
| `length` | `size_t` | Length in bytes. |

**Return Value**

Returns a pointer to the registered `struct ibv_mr` on success, or `NULL` on failure with `errno` set.

**Key Notes**

- Share the returned `mr->rkey` and the buffer address with the remote peer so it can issue RDMA Writes.
- Deregister with `ibv_dereg_mr()`.

---

## rdma_reject

```c
int rdma_reject(struct rdma_cm_id *id,
                const void *private_data,
                uint8_t private_data_len);
```

**Description**

Rejects an incoming connection request. This is called on the server side as an alternative to `rdma_accept()` when the server chooses not to accept the connection (e.g., due to resource limits, authentication failure, or application-level policy).

Optional private data can be included in the rejection, allowing the server to communicate a reason to the client.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID from the `RDMA_CM_EVENT_CONNECT_REQUEST` event. |
| `private_data` | `const void *` | Optional private data to send with the rejection. May be `NULL`. |
| `private_data_len` | `uint8_t` | Length of private data in bytes. |

**Return Value**

Returns 0 on success, or -1 on failure with `errno` set.

**Key Notes**

- The client receives an `RDMA_CM_EVENT_REJECTED` event with the private data (if any).
- The CM ID from the connect request event should be destroyed after rejection.
- The private data can convey an application-specific rejection reason.

---

## rdma_resolve_addr

```c
int rdma_resolve_addr(struct rdma_cm_id *id,
                      struct sockaddr *src_addr,
                      struct sockaddr *dst_addr,
                      int timeout_ms);
```

**Description**

Resolves a destination IP address to an RDMA device and port. This is the first step in the client-side connection establishment process. The resolution is asynchronous: the function returns immediately, and the result is delivered as an `RDMA_CM_EVENT_ADDR_RESOLVED` or `RDMA_CM_EVENT_ADDR_ERROR` event.

After successful address resolution, the CM ID's `verbs` field is set to the device context, and the application can create verbs resources (PD, CQ, QP).

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID for which to resolve the address. |
| `src_addr` | `struct sockaddr *` | Source address to bind to. May be `NULL` for automatic selection. |
| `dst_addr` | `struct sockaddr *` | Destination address to resolve. |
| `timeout_ms` | `int` | Timeout in milliseconds for the resolution. |

**Return Value**

Returns 0 on success (resolution initiated), or -1 on failure with `errno` set.

**Key Notes**

- After `RDMA_CM_EVENT_ADDR_RESOLVED`, call `rdma_resolve_route()` to resolve the path.
- For RoCE, address resolution involves ARP/ND to resolve the destination MAC address.
- If `src_addr` is `NULL`, the system selects the source address based on the routing table.
- Typical timeout values are 1000-5000 ms.

---

## rdma_resolve_route

```c
int rdma_resolve_route(struct rdma_cm_id *id, int timeout_ms);
```

**Description**

Resolves the route (path) to the destination after address resolution has completed. For InfiniBand, this queries the Subnet Manager for the path record. For RoCE, the route is determined from the local routing table. The resolution is asynchronous, with the result delivered as an `RDMA_CM_EVENT_ROUTE_RESOLVED` or `RDMA_CM_EVENT_ROUTE_ERROR` event.

After successful route resolution, the CM ID contains all the information needed to establish a connection, and the application should proceed to create a QP and call `rdma_connect()`.

**Parameters**

| Parameter | Type | Description |
|-----------|------|-------------|
| `id` | `struct rdma_cm_id *` | The CM ID with a resolved address. |
| `timeout_ms` | `int` | Timeout in milliseconds for the resolution. |

**Return Value**

Returns 0 on success (resolution initiated), or -1 on failure with `errno` set.

**Key Notes**

- Must be called after `RDMA_CM_EVENT_ADDR_RESOLVED` is received.
- After `RDMA_CM_EVENT_ROUTE_RESOLVED`, the CM ID's `route` field contains the path information.
- For InfiniBand, route resolution contacts the SA (Subnet Administrator). For RoCE, it is resolved locally and returns quickly.

---

## Connection Lifecycle Summary

The following sequence diagram illustrates the typical RDMA-CM connection lifecycle for an RC connection:

```
Client                                  Server
  |                                       |
  |  rdma_create_event_channel()          |  rdma_create_event_channel()
  |  rdma_create_id()                     |  rdma_create_id()
  |                                       |  rdma_bind_addr()
  |                                       |  rdma_listen()
  |                                       |
  |  rdma_resolve_addr() ------>          |
  |  [ADDR_RESOLVED event]               |
  |                                       |
  |  rdma_resolve_route() ----->          |
  |  [ROUTE_RESOLVED event]              |
  |                                       |
  |  ibv_alloc_pd()                       |
  |  ibv_create_cq()                      |
  |  rdma_create_qp()                     |
  |                                       |
  |  rdma_connect() -------REQ---------> |
  |                                       |  [CONNECT_REQUEST event]
  |                                       |  ibv_alloc_pd()
  |                                       |  ibv_create_cq()
  |                                       |  rdma_create_qp()
  |                                       |  rdma_accept()
  |  <--------------REP---------------   |
  |                                       |
  |  [ESTABLISHED event]                  |  [ESTABLISHED event]
  |                                       |
  |  --- data transfer (verbs API) ---    |
  |                                       |
  |  rdma_disconnect() ---DREQ---------> |
  |                                       |  [DISCONNECTED event]
  |                                       |  rdma_disconnect()
  |  <--------------DREP--------------   |
  |  [DISCONNECTED event]                |
  |                                       |
  |  rdma_destroy_qp()                   |  rdma_destroy_qp()
  |  rdma_destroy_id()                    |  rdma_destroy_id()
  |  rdma_destroy_event_channel()         |  rdma_destroy_event_channel()
```

<div class="tip">

**Simplified API**: For quick prototyping, the `rdma_post_*` and `rdma_reg_*` helper functions provide a simpler interface that avoids direct interaction with scatter/gather lists and work request structures. However, for production code, using the verbs API directly (through the CM-managed QP) provides more control over batching, signaling, and inline data.

</div>
