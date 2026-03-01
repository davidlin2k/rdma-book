# Chapter 9: Programming Patterns

The previous chapters introduced the RDMA programming model, its core abstractions, and the verbs API surface. With that foundation in place, this chapter shifts from theory to practice. Here we develop the fundamental programming patterns that underpin virtually every RDMA application, from simple benchmarks to production-scale distributed systems.

RDMA programming differs from conventional socket programming in several important ways. The application is responsible for managing queue pair state transitions, exchanging connection metadata out-of-band, registering memory regions, and handling completions explicitly. These responsibilities create a set of recurring patterns that, once mastered, make RDMA development systematic rather than ad hoc.

## What This Chapter Covers

We begin with **RC Send/Receive** (Section 9.1), the most intuitive RDMA pattern and the closest analog to traditional socket communication. A client sends a message and a server receives it, but unlike sockets, both sides must pre-post receive buffers and manage queue pair states through a well-defined state machine. We build a complete ping-pong application that exchanges QP metadata over a TCP side channel, transitions queue pairs through RESET, INIT, RTR, and RTS states, and measures round-trip latency.

Section 9.2 introduces **RC RDMA Write**, which shifts to a one-sided model. The initiator writes directly into a remote memory region without any involvement from the remote CPU. This pattern requires exchanging memory region metadata (remote keys and virtual addresses) in addition to QP information, and raises the question of how the remote side knows that a write has completed.

**RC RDMA Read** (Section 9.3) completes the one-sided operation set. We explore the metadata-read-then-data-read pattern, where an application first reads a small header from a remote node to discover what data is available, then issues a second read to fetch the actual payload. This pattern appears frequently in distributed data structures such as remote hash tables and B-trees.

Section 9.4 covers **UD Messaging**, the connectionless datagram transport. UD queue pairs require no connection setup, support one-to-many communication including multicast, but impose a single-MTU message size limit and require the receiver to account for the 40-byte Global Route Header prepended to every incoming message.

**Completion Handling** (Section 9.5) is where performance tuning meets application architecture. We develop four distinct completion handling patterns, from busy polling for lowest latency to event-driven notification for lowest CPU usage, and a hybrid adaptive approach that combines the best of both. The choice of completion handling strategy often dominates application performance characteristics.

Finally, **Error Handling** (Section 9.6) addresses the failure modes that every production RDMA application must contend with. We catalog the completion queue error codes, explain their root causes, and develop patterns for async event handling, QP recovery, and systematic debugging.

## Code Organization

Each section references complete, compilable code examples in the `src/code/` directory. A shared helper library in `src/code/common/` provides TCP-based metadata exchange, error-checking macros, and utility functions that all examples build upon. The code targets the `rdma-core` userspace library and compiles with standard GCC on any Linux system with RDMA hardware or the SoftRoCE software provider.

By the end of this chapter, you will have a working toolkit of RDMA patterns that you can compose and adapt for real applications.
