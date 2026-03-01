# 10.1 CM Event Loop

The RDMA Communication Manager operates through an asynchronous, event-driven model. Rather than calling blocking functions that return success or failure, the application submits requests -- resolve this address, listen for connections, connect to a peer -- and then receives events that report the outcome. This design maps naturally onto modern server architectures and allows a single thread to manage thousands of connections.

## Event Channels

All CM events flow through an **event channel**, created with `rdma_create_event_channel()`:

```c
#include <rdma/rdma_cma.h>

struct rdma_event_channel *channel;
channel = rdma_create_event_channel();
if (!channel) {
    perror("rdma_create_event_channel");
    return 1;
}
```

An event channel is essentially a file descriptor wrapper. The underlying file descriptor is accessible as `channel->fd`, which means you can integrate it with `poll()`, `epoll()`, or `select()` for non-blocking operation. By default, the channel operates in blocking mode -- calls to retrieve events will block until an event is available.

When you are finished with a channel, destroy it with `rdma_destroy_event_channel()`. This must happen after all `rdma_cm_id` objects associated with the channel have been destroyed.

## Retrieving Events

The primary event retrieval function is `rdma_get_cm_event()`:

```c
struct rdma_cm_event *event;
int ret = rdma_get_cm_event(channel, &event);
if (ret) {
    perror("rdma_get_cm_event");
    return 1;
}

printf("Event: %s, status: %d\n",
       rdma_event_str(event->event), event->status);
```

This call blocks (unless the channel is set to non-blocking mode) until the next event arrives. The returned `rdma_cm_event` structure contains:

- **`event`**: The event type (an `enum rdma_cm_event_type` value).
- **`status`**: Zero on success, or a negative error code.
- **`id`**: The `rdma_cm_id` associated with this event.
- **`param`**: A union containing event-specific parameters. For connection events, `param.conn` provides private data, QP information, and responder resources. For UD events, `param.ud` provides the QP number and QKey.

## Event Types

The following table summarizes the CM event types that an application will encounter. Understanding when each event occurs is essential for writing a correct event loop.

| Event | Trigger | Typical Handler Action |
|-------|---------|----------------------|
| `RDMA_CM_EVENT_ADDR_RESOLVED` | `rdma_resolve_addr()` completed | Call `rdma_resolve_route()` |
| `RDMA_CM_EVENT_ADDR_ERROR` | Address resolution failed | Report error, retry or abort |
| `RDMA_CM_EVENT_ROUTE_RESOLVED` | `rdma_resolve_route()` completed | Create QP, call `rdma_connect()` |
| `RDMA_CM_EVENT_ROUTE_ERROR` | Route resolution failed | Report error, retry or abort |
| `RDMA_CM_EVENT_CONNECT_REQUEST` | Incoming connection on listening ID | Create QP, call `rdma_accept()` or `rdma_reject()` |
| `RDMA_CM_EVENT_CONNECT_RESPONSE` | Server responded (client side) | Transition to established |
| `RDMA_CM_EVENT_ESTABLISHED` | Connection fully established | Begin data transfer |
| `RDMA_CM_EVENT_CONNECT_ERROR` | Connection attempt failed | Clean up resources |
| `RDMA_CM_EVENT_DISCONNECTED` | Peer disconnected | Clean up, potentially reconnect |
| `RDMA_CM_EVENT_DEVICE_REMOVAL` | RDMA device removed | Emergency cleanup |
| `RDMA_CM_EVENT_TIMEWAIT_EXIT` | QP exited timewait state | Safe to reuse QP number |
| `RDMA_CM_EVENT_MULTICAST_JOIN` | Multicast join completed | Begin multicast communication |
| `RDMA_CM_EVENT_MULTICAST_ERROR` | Multicast join failed | Report error |
| `RDMA_CM_EVENT_ADDR_CHANGE` | Network address changed | Re-resolve address |
| `RDMA_CM_EVENT_REJECTED` | Connection rejected by peer | Handle rejection reason |

## Event Acknowledgment

Every event retrieved with `rdma_get_cm_event()` **must** be acknowledged with `rdma_ack_cm_event()`:

```c
rdma_ack_cm_event(event);
```

This is not optional. Failing to acknowledge events causes the event channel to accumulate unacknowledged events, eventually stalling the CM. The acknowledgment serves two purposes: it frees the memory associated with the event structure, and it signals to the CM that the application has processed the event and is ready to proceed.

<div class="warning">

**Critical Rule**: You must call `rdma_ack_cm_event()` exactly once for every event returned by `rdma_get_cm_event()`. Acknowledging the same event twice or failing to acknowledge an event are both programming errors. If you need to save information from the event (such as private data or connection parameters), copy it before calling `rdma_ack_cm_event()`, because the event structure becomes invalid after acknowledgment.

</div>

There is one subtlety with `RDMA_CM_EVENT_CONNECT_REQUEST` events. The event contains a newly created `rdma_cm_id` (in `event->id`) that represents the incoming connection. You must acknowledge the event, but the new `rdma_cm_id` remains valid and is your handle to the new connection. Do not confuse the event lifetime with the connection ID lifetime.

## Complete Event Loop Pattern

The canonical CM event loop follows this structure:

```c
static int handle_event(struct rdma_cm_event *event)
{
    switch (event->event) {
    case RDMA_CM_EVENT_ADDR_RESOLVED:
        return on_addr_resolved(event->id);

    case RDMA_CM_EVENT_ROUTE_RESOLVED:
        return on_route_resolved(event->id);

    case RDMA_CM_EVENT_CONNECT_REQUEST:
        return on_connect_request(event->id, &event->param.conn);

    case RDMA_CM_EVENT_ESTABLISHED:
        return on_established(event->id);

    case RDMA_CM_EVENT_DISCONNECTED:
        return on_disconnected(event->id);

    default:
        fprintf(stderr, "Unexpected event: %s\n",
                rdma_event_str(event->event));
        return -1;
    }
}

static int run_event_loop(struct rdma_event_channel *channel)
{
    struct rdma_cm_event *event;
    int ret = 0;

    while (rdma_get_cm_event(channel, &event) == 0) {
        /* Copy any data we need before ack */
        struct rdma_cm_event event_copy = *event;

        rdma_ack_cm_event(event);

        ret = handle_event(&event_copy);
        if (ret)
            break;
    }

    return ret;
}
```

Notice the pattern of copying the event before acknowledgment. This is a common idiom because some handlers need to act on the event data (such as the `rdma_cm_id` pointer or private data) after the event has been freed. Since `event->id` is a pointer to a persistent object (the connection ID), copying the event structure preserves this pointer safely. However, if you need the private data itself (pointed to by `event->param.conn.private_data`), you must perform a deep copy of that buffer before acknowledging.

## Non-Blocking Event Loop with epoll

For production servers managing many connections, blocking on `rdma_get_cm_event()` in a dedicated thread is often not ideal. Instead, you can integrate the CM event channel with `epoll`:

```c
#include <sys/epoll.h>
#include <fcntl.h>

/* Set the event channel to non-blocking mode */
int flags = fcntl(channel->fd, F_GETFL);
fcntl(channel->fd, F_SETFL, flags | O_NONBLOCK);

/* Add the CM channel fd to an epoll instance */
int epfd = epoll_create1(0);
struct epoll_event ev = {
    .events = EPOLLIN,
    .data.fd = channel->fd
};
epoll_ctl(epfd, EPOLL_CTL_ADD, channel->fd, &ev);

/* Main event loop -- can also monitor CQ fds, timers, etc. */
while (running) {
    struct epoll_event events[16];
    int nfds = epoll_wait(epfd, events, 16, 1000 /* ms timeout */);

    for (int i = 0; i < nfds; i++) {
        if (events[i].data.fd == channel->fd) {
            struct rdma_cm_event *cm_event;
            while (rdma_get_cm_event(channel, &cm_event) == 0) {
                struct rdma_cm_event copy = *cm_event;
                rdma_ack_cm_event(cm_event);
                handle_event(&copy);
            }
        }
        /* Handle other fds (CQ channels, signals, etc.) */
    }
}
```

This pattern allows you to multiplex CM events with completion channel events, timer file descriptors, and signal handling all in a single event loop. The inner `while` loop drains all pending CM events after `epoll` signals readiness, which is important because multiple events may have been queued between `epoll_wait` calls.

<div class="tip">

**Tip**: In a high-connection-rate server, you can also use the CM event channel's fd with `io_uring` for even lower overhead event dispatching. The fd behaves like any other readable file descriptor.

</div>

## Error Events and Recovery

Not all events indicate success. When an asynchronous operation fails, the CM delivers an error event rather than the expected success event. For example, if `rdma_resolve_addr()` cannot resolve the destination, you receive `RDMA_CM_EVENT_ADDR_ERROR` instead of `RDMA_CM_EVENT_ADDR_RESOLVED`. The event's `status` field contains a negative errno value indicating the specific failure.

Connection failures (`RDMA_CM_EVENT_CONNECT_ERROR`, `RDMA_CM_EVENT_REJECTED`, `RDMA_CM_EVENT_UNREACHABLE`) require the application to clean up the associated `rdma_cm_id`. For rejected connections, the event's private data may contain a rejection reason from the peer, which is useful for debugging.

The `RDMA_CM_EVENT_DISCONNECTED` event deserves special attention. It is delivered when the remote peer disconnects, either gracefully (by calling `rdma_disconnect()`) or because of a failure. Upon receiving this event, the local QP transitions to the error state, and any outstanding work requests complete with a flush error. The application should destroy the QP and the `rdma_cm_id`, or attempt reconnection if appropriate.

```c
case RDMA_CM_EVENT_DISCONNECTED:
    fprintf(stderr, "Peer disconnected\n");
    rdma_destroy_qp(event->id);
    rdma_destroy_id(event->id);
    return 0;
```

## Event Channel Lifecycle

A typical application creates one event channel and associates multiple `rdma_cm_id` objects with it. However, you can also create multiple channels for different categories of connections. For instance, a server might use one channel for the listening ID and a separate channel for accepted connections, allowing different threads to handle new connections versus established-connection events.

The lifecycle is straightforward:

1. Create the channel with `rdma_create_event_channel()`.
2. Create `rdma_cm_id` objects on the channel with `rdma_create_id()`.
3. Run the event loop, processing and acknowledging events.
4. Destroy all `rdma_cm_id` objects.
5. Destroy the channel with `rdma_destroy_event_channel()`.

Destroying a channel while events are outstanding or while `rdma_cm_id` objects still reference it results in undefined behavior. Always clean up in the correct order.

The event loop is the heartbeat of every RDMA_CM application. Whether you use a simple blocking loop for a single-connection client or a sophisticated `epoll`-based dispatcher for a multi-thousand-connection server, the fundamental rhythm is the same: wait for an event, copy what you need, acknowledge it, and handle it. With this foundation in place, we can build complete client-server applications in the next section.
