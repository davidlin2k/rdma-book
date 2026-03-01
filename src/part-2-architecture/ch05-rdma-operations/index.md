# Chapter 5: RDMA Operations

In Chapter 4, we built the stage: Queue Pairs, Completion Queues, Memory Regions, Protection Domains, and Address Handles. These are the objects that an RDMA application creates and manages. But objects alone do nothing. It is the **operations** posted to those objects that move data across the network, and the semantics of those operations that distinguish RDMA from every other networking paradigm.

Conventional networking offers a single abstraction: the byte stream (TCP) or the datagram (UDP). You write bytes into a socket; the kernel copies them into a buffer; the NIC DMAs them onto the wire; the remote kernel receives them, checksums them, reassembles them, and copies them into the receiving application's buffer. Every byte touches the CPU at least twice on each side.

RDMA offers five fundamentally different operation types, each with distinct semantics for who participates, who is notified, and where data lands in memory. Choosing the right operation for a given task is not an optimization -- it is a design decision that shapes the entire architecture of a distributed system. The five operations are:

1. **Send/Receive** -- the two-sided channel semantic. Both sides participate: the sender posts a Send work request, the receiver posts a Receive work request, and the NIC matches them. This is the closest analog to traditional messaging, but with zero-copy delivery and microsecond latency.

2. **RDMA Write** -- a one-sided operation in which the requester pushes data directly into a specified location in the remote node's memory. The remote CPU is not involved at all. No receive buffer is posted, no interrupt is raised, no completion is generated on the remote side. The data simply appears in memory.

3. **RDMA Read** -- the mirror of RDMA Write. The requester pulls data from a specified location in the remote node's memory into its own local buffer. Again, the remote CPU is oblivious.

4. **Atomic Operations** -- Compare-and-Swap and Fetch-and-Add, executed atomically on 8-byte values in remote memory. These operations enable distributed synchronization primitives -- locks, counters, consensus protocols -- without any involvement from the remote CPU.

5. **Immediate Data** -- a modifier that can be attached to Send or RDMA Write operations, piggy-backing a 32-bit value that generates a completion on the remote side. This hybrid mechanism bridges the gap between one-sided and two-sided semantics, enabling patterns like "write bulk data silently, then notify the receiver."

These operations are not merely API variations on the same underlying mechanism. They differ in their wire protocols, their completion semantics, their ordering guarantees, and their performance characteristics. An RDMA Write completes with a single round of DMA on each end. An RDMA Read requires a request-response exchange at the wire level, roughly doubling the latency. An Atomic operation serializes at the remote NIC, introducing contention that does not exist for Read or Write. Understanding these differences is essential for building systems that actually achieve the performance RDMA promises.

This chapter examines each operation in detail: the wire-level mechanics, the verbs API, the completion semantics, and the practical use cases. We conclude with a comprehensive treatment of operation ordering and signaling -- the rules that govern when operations are visible, when completions are generated, and how applications can control both. By the end, you will understand not just *how* to post each operation, but *when* and *why* to choose one over another.
