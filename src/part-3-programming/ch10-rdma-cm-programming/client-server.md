# 10.2 Client-Server with RDMA_CM

This section develops a complete RDMA client-server application using `librdmacm`. We will walk through the server and client lifecycles in detail, compare them with the raw verbs approach from Chapter 9, and provide fully annotated code that you can compile and run. By the end, you will see how RDMA_CM reduces connection setup from hundreds of lines of boilerplate to a clean, event-driven flow.

## Comparison with Raw Verbs

Recall the connection setup from Chapter 9. To establish a single RC connection using raw verbs, both sides needed to:

1. Open the device and allocate a protection domain.
2. Create completion queues and a queue pair.
3. Query the port for the local LID and GID.
4. Exchange QP numbers, LIDs, GIDs, and PSN values over a TCP socket.
5. Transition the QP from RESET to INIT (setting port number, access flags).
6. Transition from INIT to RTR (setting the remote QPN, LID, GID, PSN, MTU, and numerous other parameters).
7. Transition from RTR to RTS (setting timeout, retry count, RNR retry, and SQ PSN).

With RDMA_CM, steps 3 through 7 are eliminated entirely. The library resolves addresses, discovers routes, exchanges QP metadata, and drives the state machine internally. The application only needs to:

1. Create an `rdma_cm_id` on an event channel.
2. For the server: bind to an address and listen.
3. For the client: resolve address and route, then connect.
4. Create a QP (using `rdma_create_qp()` which associates it with the CM ID).
5. Accept or establish the connection through the event loop.

The following table summarizes the difference:

| Step | Raw Verbs | RDMA_CM |
|------|-----------|---------|
| Metadata exchange | Manual TCP socket | Automatic (CM protocol) |
| QP state: RESET->INIT | `ibv_modify_qp()` | Automatic |
| QP state: INIT->RTR | `ibv_modify_qp()` with 10+ params | Automatic |
| QP state: RTR->RTS | `ibv_modify_qp()` with 5+ params | Automatic |
| Transport portability | Must handle IB/RoCE/iWARP differences | Transparent |
| Side channel needed | Yes (TCP socket) | No |

## Server Lifecycle

The server follows a lifecycle that mirrors TCP servers: create, bind, listen, accept. Here is the sequence in detail.

### Step 1: Create the Event Channel and Listener ID

```c
struct rdma_event_channel *channel = rdma_create_event_channel();

struct rdma_cm_id *listener;
rdma_create_id(channel, &listener, NULL, RDMA_PS_TCP);
```

The `RDMA_PS_TCP` port space indicates reliable, connection-oriented service (analogous to RC transport). For datagram service, you would use `RDMA_PS_UDP`. The fourth parameter (`NULL` in this example) is a user-defined context pointer that will be accessible via `listener->context` in event handlers.

### Step 2: Bind to an Address

```c
struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(12345),
    .sin_addr.s_addr = INADDR_ANY
};
rdma_bind_addr(listener, (struct sockaddr *)&addr);
```

This binds the listener to a specific port on all available RDMA devices. If you bind to a specific IP address, RDMA_CM will automatically select the RDMA device whose port is associated with that address. This is one of the key advantages of RDMA_CM: device selection is driven by IP addressing rather than explicit device enumeration.

### Step 3: Listen for Connections

```c
rdma_listen(listener, 10);  /* backlog of 10 */
```

After this call, the server will begin accepting incoming connection requests. The backlog parameter works the same way as in TCP's `listen()`.

### Step 4: Handle Connection Requests

When a client connects, the event loop receives `RDMA_CM_EVENT_CONNECT_REQUEST`. The event carries a new `rdma_cm_id` representing the incoming connection:

```c
case RDMA_CM_EVENT_CONNECT_REQUEST: {
    struct rdma_cm_id *client_id = event->id;

    /* Allocate PD if not done already */
    struct ibv_pd *pd = ibv_alloc_pd(client_id->verbs);

    /* Create CQ */
    struct ibv_cq *cq = ibv_create_cq(client_id->verbs,
                                        16, NULL, NULL, 0);

    /* Create QP on the new connection ID */
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 16,
            .max_recv_wr = 16,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };
    rdma_create_qp(client_id, pd, &qp_attr);

    /* Post receive buffers before accepting */
    post_receive(client_id);

    /* Accept the connection */
    struct rdma_conn_param conn_param = {
        .responder_resources = 1,
        .initiator_depth = 1,
    };
    rdma_accept(client_id, &conn_param);
    break;
}
```

<div class="warning">

**Important**: Always post receive buffers *before* calling `rdma_accept()`. Once the connection is established, the remote side may begin sending immediately. If no receive buffers are posted, those sends will fail with a Receiver Not Ready (RNR) error.

</div>

Note that `rdma_create_qp()` replaces the raw `ibv_create_qp()`. The key difference is that `rdma_create_qp()` associates the QP with the `rdma_cm_id`, allowing the CM to manage its state transitions automatically. You must not call `ibv_modify_qp()` on a QP managed by RDMA_CM.

### Step 5: Connection Established

After the client confirms the connection, both sides receive `RDMA_CM_EVENT_ESTABLISHED`:

```c
case RDMA_CM_EVENT_ESTABLISHED:
    printf("Connection established with client\n");
    /* Begin data transfer operations */
    break;
```

## Client Lifecycle

The client lifecycle involves resolving the server's address, resolving the network route, and connecting.

### Step 1: Create Event Channel and CM ID

```c
struct rdma_event_channel *channel = rdma_create_event_channel();

struct rdma_cm_id *conn;
rdma_create_id(channel, &conn, NULL, RDMA_PS_TCP);
```

### Step 2: Resolve Address

```c
struct sockaddr_in addr = {
    .sin_family = AF_INET,
    .sin_port = htons(12345)
};
inet_pton(AF_INET, server_ip, &addr.sin_addr);

rdma_resolve_addr(conn, NULL, (struct sockaddr *)&addr, 2000 /* ms timeout */);
```

This is an asynchronous call. It resolves the IP address to an RDMA device and port. When resolution completes, the event loop receives `RDMA_CM_EVENT_ADDR_RESOLVED`.

### Step 3: Resolve Route

Upon receiving `RDMA_CM_EVENT_ADDR_RESOLVED`:

```c
case RDMA_CM_EVENT_ADDR_RESOLVED:
    rdma_resolve_route(event->id, 2000 /* ms timeout */);
    break;
```

Route resolution determines the path to the remote peer. On InfiniBand, this involves querying the Subnet Manager for path information. On RoCE and iWARP, the route is derived from IP routing tables. When complete, the event loop receives `RDMA_CM_EVENT_ROUTE_RESOLVED`.

### Step 4: Create QP and Connect

Upon receiving `RDMA_CM_EVENT_ROUTE_RESOLVED`:

```c
case RDMA_CM_EVENT_ROUTE_RESOLVED: {
    struct ibv_pd *pd = ibv_alloc_pd(event->id->verbs);
    struct ibv_cq *cq = ibv_create_cq(event->id->verbs,
                                        16, NULL, NULL, 0);

    struct ibv_qp_init_attr qp_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr = 16,
            .max_recv_wr = 16,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };
    rdma_create_qp(event->id, pd, &qp_attr);

    /* Post receive buffers before connecting */
    post_receive(event->id);

    struct rdma_conn_param conn_param = {
        .responder_resources = 1,
        .initiator_depth = 1,
    };
    rdma_connect(event->id, &conn_param);
    break;
}
```

## Private Data Exchange

One of the elegant features of RDMA_CM is the ability to exchange small amounts of **private data** during the connection handshake. The client can attach data to `rdma_connect()`, and the server can attach data to `rdma_accept()`, eliminating the need for a separate metadata exchange channel.

```c
/* Client side: send memory region info with connect request */
struct conn_metadata {
    uint64_t addr;
    uint32_t rkey;
    uint32_t size;
};

struct conn_metadata my_meta = {
    .addr = (uint64_t)(uintptr_t)mr->addr,
    .rkey = mr->rkey,
    .size = BUFFER_SIZE
};

struct rdma_conn_param param = {
    .responder_resources = 1,
    .initiator_depth = 1,
    .private_data = &my_meta,
    .private_data_len = sizeof(my_meta),
};
rdma_connect(conn, &param);
```

The server receives this data in the `RDMA_CM_EVENT_CONNECT_REQUEST` event:

```c
case RDMA_CM_EVENT_CONNECT_REQUEST: {
    struct conn_metadata *peer_meta =
        (struct conn_metadata *)event->param.conn.private_data;
    printf("Remote buffer: addr=0x%lx, rkey=0x%x, size=%u\n",
           peer_meta->addr, peer_meta->rkey, peer_meta->size);

    /* Copy before ack! */
    struct conn_metadata saved_meta = *peer_meta;
    /* ... */
}
```

<div class="tip">

**Tip**: The maximum private data size depends on the transport. For InfiniBand RC connections, the limit is 56 bytes for connect requests and 148 bytes for accept responses. For iWARP, the limits are larger (up to 512 bytes). Always check `RDMA_MAX_PRIVATE_DATA` or handle the transport-specific limits. For larger metadata, use a send/receive exchange after connection establishment.

</div>

## Automatic QP State Management

A key benefit of RDMA_CM is that you never directly modify QP state. The library handles the full state machine:

- **After `rdma_create_qp()`**: QP is in RESET state.
- **When processing CONNECT_REQUEST** (server side): CM transitions QP to INIT and RTR internally before delivering the event.
- **After `rdma_accept()` / `rdma_connect()`**: CM transitions QP to RTS.
- **On `RDMA_CM_EVENT_ESTABLISHED`**: QP is fully in RTS state, ready for data transfer.

This means all the transport-specific parameters (MTU, retry counts, RNR timers, PSN values, destination QPN) are negotiated and configured automatically. If you need to customize these parameters, you can do so through `rdma_conn_param` fields:

```c
struct rdma_conn_param param = {
    .responder_resources = 4,   /* max incoming RDMA reads */
    .initiator_depth = 4,       /* max outgoing RDMA reads */
    .retry_count = 7,           /* transport retries */
    .rnr_retry_count = 7,       /* RNR retries (7 = infinite) */
};
```

## Disconnection and Cleanup

Either side can initiate disconnection:

```c
rdma_disconnect(conn);
```

This triggers `RDMA_CM_EVENT_DISCONNECTED` on both sides. Cleanup follows the reverse of creation:

```c
case RDMA_CM_EVENT_DISCONNECTED:
    rdma_destroy_qp(event->id);
    ibv_dereg_mr(mr);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    rdma_destroy_id(event->id);
    break;
```

For the server's listening ID:

```c
rdma_destroy_id(listener);
rdma_destroy_event_channel(channel);
```

## Complete Code Walkthrough

The full client and server implementations are provided in `src/code/ch10-cm-client-server/`. Here is a summary of the code structure:

**`cm_server.c`** implements:
- Event channel and listener creation
- Binding to port and listening
- Handling CONNECT_REQUEST: allocate resources, create QP, post receives, accept
- Handling ESTABLISHED: perform a send/receive data exchange
- Handling DISCONNECTED: clean up resources
- Graceful shutdown

**`cm_client.c`** implements:
- Event channel and CM ID creation
- Address and route resolution through event handlers
- QP creation and connection on ROUTE_RESOLVED
- Data exchange on ESTABLISHED
- Disconnection and cleanup

Both programs exchange a simple message to demonstrate the complete lifecycle. The server waits for a message from the client, then sends a response. This mirrors the ping-pong pattern from Chapter 9 but with dramatically less setup code.

To build and run:

```bash
cd src/code/ch10-cm-client-server
make

# Terminal 1 (server):
./cm_server

# Terminal 2 (client):
./cm_client 192.168.1.1
```

The server binds to `0.0.0.0:12345` by default and the client connects to the specified IP address. On success, you will see the connection lifecycle events logged by both sides, followed by the exchanged message content.

<div class="tip">

**Tip**: RDMA_CM works transparently with SoftRoCE (`rxe`), making it possible to develop and test these examples on machines without physical RDMA hardware. Refer to Appendix E for SoftRoCE setup instructions.

</div>
