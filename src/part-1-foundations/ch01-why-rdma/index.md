# Chapter 1: Why RDMA?

Every network packet your application sends today takes a journey through layers of abstraction that were designed decades ago, when CPUs were slow and networks were unreliable. The packet crosses the boundary between user space and kernel space---twice. It gets copied from your application's buffer into kernel memory, then again into the NIC's transmit ring. The CPU computes checksums, manages protocol state machines, and fields interrupts when data arrives. Each of these steps made perfect sense in 1983 when BSD sockets were introduced. On modern hardware, they represent the single largest obstacle to extracting the performance that the network is physically capable of delivering.

RDMA---Remote Direct Memory Access---eliminates these obstacles. It lets an application place data directly into the memory of a remote machine without involving either operating system kernel in the data path. The result is latency measured in single-digit microseconds rather than tens, throughput that saturates 100 Gb/s and 400 Gb/s links with minimal CPU consumption, and a programming model that, once understood, enables a class of distributed systems that were previously impractical.

But RDMA is not a drop-in replacement for sockets. It demands a different mental model, different hardware, and a different approach to application design. Understanding *why* RDMA exists---what problems it solves and what costs it imposes---is the essential foundation for everything that follows in this book.

## What This Chapter Covers

This chapter builds your intuition from the ground up:

**Section 1.1 -- Traditional Networking: The Socket Model** dissects the conventional TCP/IP networking path, quantifying exactly where time and CPU cycles are spent. You will see why a `send()` call that appears instantaneous to the programmer actually triggers thousands of instructions of kernel work before a single byte reaches the wire.

**Section 1.2 -- DMA Fundamentals** introduces Direct Memory Access---the hardware mechanism that allows peripherals to read and write system memory without CPU intervention. Understanding local DMA is a prerequisite for understanding how RDMA extends this concept across a network.

**Section 1.3 -- The RDMA Concept** presents the three pillars of RDMA: kernel bypass, zero-copy data transfer, and CPU offload. This section shows how these pillars combine to deliver microsecond-scale latency and near-zero CPU overhead, and quantifies the performance gap with concrete numbers.

**Section 1.4 -- Kernel Bypass and Zero-Copy in Depth** takes a closer look at the mechanisms that make RDMA possible, and compares RDMA to other high-performance networking approaches such as DPDK, io_uring, and XDP/eBPF. Each of these technologies addresses part of the problem; only RDMA addresses all of it for network-to-network data movement.

**Section 1.5 -- When (and When Not) to Use RDMA** provides a practical decision framework. RDMA is not universally the right answer. This section identifies the workloads and environments where RDMA delivers transformative benefits, the situations where it adds complexity without proportional gain, and the common misconceptions that lead engineers astray.

## Prerequisites

This chapter assumes familiarity with basic operating system concepts (user space vs. kernel space, virtual memory, interrupts) and a working understanding of TCP/IP networking. No prior RDMA experience is required---that is what the rest of this book is for.

By the end of this chapter, you will understand the precise technical reasons why RDMA exists, what it costs, and whether your workload is likely to benefit from it. That understanding will make every subsequent chapter---from transport protocols to queue pair management to real application design---more concrete and more actionable.
