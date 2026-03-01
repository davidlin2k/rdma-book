# Chapter 11: Advanced Programming

The preceding chapters established the fundamental RDMA programming patterns: creating resources, establishing connections, transferring data, and handling completions. These patterns are sufficient for straightforward applications, but production RDMA systems face challenges that demand more sophisticated techniques. This chapter addresses those challenges head-on.

As RDMA deployments scale from a handful of nodes to thousands, resource consumption becomes a critical concern. A naive implementation that creates a dedicated queue pair for every peer and posts a fixed number of receive buffers per queue pair quickly exhausts memory. If a node communicates with 1,000 peers and each QP requires 64 receive buffers of 4 KB each, the receive buffer memory alone reaches 256 MB -- before accounting for send buffers, completion queues, or QP control structures. The advanced resource sharing mechanisms covered in this chapter directly address this scalability bottleneck.

## What This Chapter Covers

Section 11.1 introduces **Shared Receive Queues (SRQ)**, which decouple receive buffers from individual queue pairs. Instead of posting receive buffers to each QP independently, an SRQ provides a shared pool that any associated QP can draw from. This reduces memory consumption from O(N x M) to O(M), where N is the number of QPs and M is the number of receive buffers.

Section 11.2 examines the **XRC Transport**, designed for multi-process applications where multiple processes on the same node communicate with the same set of remote peers. XRC allows processes to share queue pair infrastructure, dramatically reducing the number of QPs required in scenarios such as MPI with multiple ranks per node.

Section 11.3 covers the **Dynamically Connected Transport (DCT)**, a vendor-specific extension from Mellanox/NVIDIA that provides on-demand connection establishment. Rather than pre-establishing connections to every potential peer, DCT creates connections transparently when the first message is sent, achieving O(1) QP resource usage per node regardless of cluster size.

Section 11.4 explains **Inline Data**, a simple but effective optimization for small messages. By copying message data directly into the work queue element, inline data eliminates one PCIe DMA read transaction, reducing latency for messages below a device-specific threshold (typically 64 to 256 bytes).

Section 11.5 tackles **Multi-Threaded RDMA**, covering thread safety guarantees in libibverbs, per-thread resource partitioning strategies, and NUMA-aware thread placement. Getting multi-threading right is essential for saturating modern high-bandwidth NICs.

Section 11.6 presents **Zero-Copy Design Patterns**, the architectural techniques that allow applications to fully realize RDMA's promise of eliminating unnecessary data copies. We cover pre-registered buffer pools, scatter-gather optimization, double buffering for pipelined communication, and application-level flow control.

## Code Organization

This chapter references code in `src/code/ch11-srq-example/`, which provides a complete SRQ implementation demonstrating shared receive buffers across multiple queue pairs with SRQ limit event handling. The remaining sections include extensive code fragments that illustrate each technique in context.

By the end of this chapter, you will have the tools to design RDMA applications that scale efficiently, use system resources judiciously, and extract maximum performance from the hardware.
