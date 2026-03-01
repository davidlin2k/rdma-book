# Chapter 10: RDMA_CM Programming

In the previous chapter, we built RDMA applications by manually managing every detail of the connection lifecycle: allocating protection domains, creating completion queues and queue pairs, exchanging QP metadata over a TCP side channel, and driving the QP state machine through RESET, INIT, RTR, and RTS transitions. This approach provides maximum control and is indispensable for understanding what happens beneath the surface. However, it is also verbose, error-prone, and couples application logic to transport-specific setup details. The RDMA Communication Manager (RDMA_CM) exists to solve exactly this problem.

RDMA_CM is a connection management library (`librdmacm`) that provides a socket-like abstraction over RDMA connections. It handles address resolution, route resolution, QP state transitions, and connection handshaking behind a clean, event-driven API. Where raw verbs programming required hundreds of lines of boilerplate to establish a single connection, RDMA_CM reduces this to a handful of calls that mirror the familiar `bind`, `listen`, `connect`, and `accept` pattern from TCP sockets.

## What This Chapter Covers

Section 10.1 introduces the **CM Event Loop**, the central mechanism through which RDMA_CM communicates with the application. Every significant connection state change -- address resolution completing, a new connection request arriving, a connection being established or torn down -- is delivered as an event through an event channel. We examine each event type, the event acknowledgment protocol, and how to integrate the event loop with `epoll` for non-blocking, scalable connection management.

Section 10.2 develops a complete **Client-Server application** using RDMA_CM. We walk through the server and client lifecycles side by side, compare them with the raw verbs approach from Chapter 9, and show how RDMA_CM eliminates the need for a separate TCP side channel, manual QP state transitions, and explicit GID/LID exchange. The accompanying code examples provide a fully annotated client and server that you can compile and run immediately.

Section 10.3 covers **Multicast**, a capability that is particularly natural with RDMA_CM. Joining a multicast group, sending messages to all group members, and receiving multicast traffic all operate through the same event-driven model. Multicast is invaluable for distributed notifications, membership protocols, and any scenario where one-to-many communication is needed.

## Code Organization

This chapter references code in `src/code/ch10-cm-client-server/`, which contains a complete RDMA_CM-based client and server. These examples build upon the same `rdma-core` library as the Chapter 9 examples but replace the manual connection management with RDMA_CM calls. The resulting code is shorter, clearer, and more portable across InfiniBand, RoCE, and iWARP transports -- one of the key advantages of the RDMA_CM abstraction.
