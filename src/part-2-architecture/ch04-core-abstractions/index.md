# Chapter 4: Core Abstractions

Every networking technology has a programming model -- a set of concepts and interfaces through which applications interact with hardware. In conventional socket-based networking, that model is familiar: open a socket, bind it to an address, connect to a peer, and then read and write byte streams. The kernel mediates every step, copying data between user and kernel buffers, managing protocol state machines, and scheduling interrupts. The model is simple, but that simplicity comes at the cost of performance.

RDMA discards this model almost entirely. In its place, it introduces a set of **core abstractions** -- objects and operations that give applications direct, fine-grained control over network hardware. These abstractions were first defined in the InfiniBand Architecture Specification as a set of "verbs": abstract operations that describe *what* an application can do without prescribing *how* the hardware implements them. Over the past two decades, these abstractions have been refined, extended, and implemented across InfiniBand, RoCE, and iWARP fabrics. They now form the universal programming model for kernel-bypass networking.

This chapter is the conceptual heart of the book. Every chapter that follows -- whether it covers RDMA operations, memory management, connection setup, or performance tuning -- builds on the objects introduced here. Understanding these abstractions deeply is the difference between writing RDMA code that merely works and writing RDMA code that performs.

We begin with the **Verbs Abstraction Layer** itself: the software architecture that sits between your application and the network hardware, dispatching operations to hardware-specific providers while presenting a uniform API. We then examine the five fundamental objects that every RDMA application creates and manages:

- **Queue Pairs (QP)**: The fundamental unit of communication. A Queue Pair couples a Send Queue and a Receive Queue into a single bidirectional channel. Queue Pairs come in several types -- Reliable Connected, Unreliable Datagram, and others -- each with different semantics and performance characteristics.

- **Completion Queues (CQ)**: The mechanism by which hardware reports that work has been done. When a send completes or a receive arrives, the NIC writes a Completion Queue Entry that the application can poll or wait for.

- **Memory Regions (MR)**: The bridge between virtual memory and DMA-capable hardware. Registering a memory region pins physical pages in place and gives the NIC a translation table so it can read and write application memory directly, without any CPU involvement.

- **Protection Domains (PD)**: The isolation boundary. Every QP and MR belongs to a Protection Domain, and hardware enforces that objects in different domains cannot interact. This is the foundation of multi-tenant security in RDMA.

- **Address Handles (AH)**: The destination descriptor for datagram communication. Because Unreliable Datagram QPs are connectionless, each message must carry its own routing information, encapsulated in an Address Handle.

These five objects, together with the verbs that operate on them, constitute the RDMA programming model. They are not merely API constructs -- they correspond to real data structures inside the NIC hardware. When you create a Queue Pair, the NIC allocates memory for ring buffers. When you register a Memory Region, the NIC builds page table entries. When you post a work request, the NIC's DMA engine reads your descriptor directly from user-space memory. Understanding this hardware correspondence is essential for understanding performance.

We start with the layer that ties it all together: the Verbs abstraction.
