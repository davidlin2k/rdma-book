# Summary

[Cover](cover.md) 
[Disclaimer] (disclaimer.md)
[Preface](preface.md)
[Introduction](introduction.md)

---

# Part I: Foundations

- [Why RDMA?](part-1-foundations/ch01-why-rdma/index.md)
  - [Traditional Networking: The Socket Model](part-1-foundations/ch01-why-rdma/socket-model.md)
  - [DMA Fundamentals](part-1-foundations/ch01-why-rdma/dma-fundamentals.md)
  - [The RDMA Concept](part-1-foundations/ch01-why-rdma/rdma-concept.md)
  - [Kernel Bypass and Zero-Copy in Depth](part-1-foundations/ch01-why-rdma/kernel-bypass-zero-copy.md)
  - [When (and When Not) to Use RDMA](part-1-foundations/ch01-why-rdma/when-to-use-rdma.md)
- [History and Ecosystem](part-1-foundations/ch02-history-and-ecosystem/index.md)
  - [InfiniBand Origins](part-1-foundations/ch02-history-and-ecosystem/infiniband-origins.md)
  - [iWARP](part-1-foundations/ch02-history-and-ecosystem/iwarp.md)
  - [RoCE and RoCEv2](part-1-foundations/ch02-history-and-ecosystem/roce.md)
  - [OFED and rdma-core](part-1-foundations/ch02-history-and-ecosystem/ofed-rdma-core.md)
  - [Industry Landscape](part-1-foundations/ch02-history-and-ecosystem/industry-landscape.md)
- [Transport Protocols](part-1-foundations/ch03-transport-protocols/index.md)
  - [InfiniBand Architecture](part-1-foundations/ch03-transport-protocols/infiniband-architecture.md)
  - [RoCE v1 and v2](part-1-foundations/ch03-transport-protocols/roce-v1-v2.md)
  - [iWARP Protocol Stack](part-1-foundations/ch03-transport-protocols/iwarp-protocol-stack.md)
  - [Packet Formats](part-1-foundations/ch03-transport-protocols/packet-formats.md)
  - [Protocol Comparison](part-1-foundations/ch03-transport-protocols/protocol-comparison.md)

---

# Part II: Architecture

- [Core Abstractions](part-2-architecture/ch04-core-abstractions/index.md)
  - [The Verbs Abstraction Layer](part-2-architecture/ch04-core-abstractions/verbs-abstraction.md)
  - [Queue Pairs (QP)](part-2-architecture/ch04-core-abstractions/queue-pairs.md)
  - [Completion Queues (CQ)](part-2-architecture/ch04-core-abstractions/completion-queues.md)
  - [Memory Regions (MR)](part-2-architecture/ch04-core-abstractions/memory-regions.md)
  - [Protection Domains (PD)](part-2-architecture/ch04-core-abstractions/protection-domains.md)
  - [Address Handles (AH)](part-2-architecture/ch04-core-abstractions/address-handles.md)
- [RDMA Operations](part-2-architecture/ch05-rdma-operations/index.md)
  - [Send/Receive (Two-Sided)](part-2-architecture/ch05-rdma-operations/send-receive.md)
  - [RDMA Write (One-Sided)](part-2-architecture/ch05-rdma-operations/rdma-write.md)
  - [RDMA Read (One-Sided)](part-2-architecture/ch05-rdma-operations/rdma-read.md)
  - [Atomic Operations](part-2-architecture/ch05-rdma-operations/atomic-operations.md)
  - [Immediate Data](part-2-architecture/ch05-rdma-operations/immediate-data.md)
  - [Operation Semantics and Ordering](part-2-architecture/ch05-rdma-operations/semantics-and-ordering.md)
- [Memory Management](part-2-architecture/ch06-memory-management/index.md)
  - [Memory Registration Deep Dive](part-2-architecture/ch06-memory-management/registration-deep-dive.md)
  - [Memory Pinning](part-2-architecture/ch06-memory-management/memory-pinning.md)
  - [MR Types](part-2-architecture/ch06-memory-management/mr-types.md)
  - [Memory Windows](part-2-architecture/ch06-memory-management/memory-windows.md)
  - [On-Demand Paging](part-2-architecture/ch06-memory-management/on-demand-paging.md)
- [Connection Management](part-2-architecture/ch07-connection-management/index.md)
  - [QP State Machine](part-2-architecture/ch07-connection-management/qp-state-machine.md)
  - [Communication Manager (CM)](part-2-architecture/ch07-connection-management/communication-manager.md)
  - [RDMA_CM](part-2-architecture/ch07-connection-management/rdma-cm.md)
  - [Connection Patterns](part-2-architecture/ch07-connection-management/connection-patterns.md)

---

# Part III: Programming

- [Getting Started](part-3-programming/ch08-getting-started/index.md)
  - [Environment Setup](part-3-programming/ch08-getting-started/environment-setup.md)
  - [libibverbs API Overview](part-3-programming/ch08-getting-started/libibverbs-overview.md)
  - [Your First RDMA Program](part-3-programming/ch08-getting-started/first-rdma-program.md)
  - [Device and Port Discovery](part-3-programming/ch08-getting-started/device-port-discovery.md)
  - [Building and Running](part-3-programming/ch08-getting-started/building-and-running.md)
- [Programming Patterns](part-3-programming/ch09-programming-patterns/index.md)
  - [RC Send/Receive](part-3-programming/ch09-programming-patterns/rc-send-receive.md)
  - [RC RDMA Write](part-3-programming/ch09-programming-patterns/rc-rdma-write.md)
  - [RC RDMA Read](part-3-programming/ch09-programming-patterns/rc-rdma-read.md)
  - [UD Messaging](part-3-programming/ch09-programming-patterns/ud-messaging.md)
  - [Completion Handling](part-3-programming/ch09-programming-patterns/completion-handling.md)
  - [Error Handling](part-3-programming/ch09-programming-patterns/error-handling.md)
- [RDMA_CM Programming](part-3-programming/ch10-rdma-cm-programming/index.md)
  - [CM Event Loop](part-3-programming/ch10-rdma-cm-programming/cm-event-loop.md)
  - [Client-Server with RDMA_CM](part-3-programming/ch10-rdma-cm-programming/client-server.md)
  - [Multicast](part-3-programming/ch10-rdma-cm-programming/multicast.md)
- [Advanced Programming](part-3-programming/ch11-advanced-programming/index.md)
  - [Shared Receive Queues (SRQ)](part-3-programming/ch11-advanced-programming/shared-receive-queues.md)
  - [XRC Transport](part-3-programming/ch11-advanced-programming/xrc-transport.md)
  - [DCT Transport](part-3-programming/ch11-advanced-programming/dct-transport.md)
  - [Inline Data](part-3-programming/ch11-advanced-programming/inline-data.md)
  - [Multi-Threaded RDMA](part-3-programming/ch11-advanced-programming/multi-threaded-rdma.md)
  - [Zero-Copy Design Patterns](part-3-programming/ch11-advanced-programming/zero-copy-patterns.md)

---

# Part IV: Performance

- [Performance Engineering](part-4-performance/ch12-performance-engineering/index.md)
  - [Latency Analysis](part-4-performance/ch12-performance-engineering/latency-analysis.md)
  - [Throughput Optimization](part-4-performance/ch12-performance-engineering/throughput-optimization.md)
  - [PCIe Considerations](part-4-performance/ch12-performance-engineering/pcie-considerations.md)
  - [NUMA Awareness](part-4-performance/ch12-performance-engineering/numa-awareness.md)
  - [NIC Architecture Internals](part-4-performance/ch12-performance-engineering/nic-architecture.md)
  - [Benchmarking](part-4-performance/ch12-performance-engineering/benchmarking.md)
- [Congestion Control](part-4-performance/ch13-congestion-control/index.md)
  - [Lossless Ethernet Fundamentals](part-4-performance/ch13-congestion-control/lossless-ethernet.md)
  - [Priority Flow Control (PFC)](part-4-performance/ch13-congestion-control/pfc.md)
  - [ECN and DCQCN](part-4-performance/ch13-congestion-control/ecn-dcqcn.md)
  - [Fabric Design for RDMA](part-4-performance/ch13-congestion-control/fabric-design.md)

---

# Part V: Deployment

- [Applications](part-5-deployment/ch14-applications/index.md)
  - [Storage: NVMe-oF and iSER](part-5-deployment/ch14-applications/storage.md)
  - [High-Performance Computing: MPI and UCX](part-5-deployment/ch14-applications/hpc.md)
  - [Databases and Key-Value Stores](part-5-deployment/ch14-applications/databases.md)
  - [Distributed File Systems](part-5-deployment/ch14-applications/distributed-filesystems.md)
  - [Machine Learning and AI](part-5-deployment/ch14-applications/ml-and-ai.md)
- [Cloud and Virtualization](part-5-deployment/ch15-cloud-and-virtualization/index.md)
  - [SR-IOV for RDMA](part-5-deployment/ch15-cloud-and-virtualization/sriov.md)
  - [virtio and VDPA](part-5-deployment/ch15-cloud-and-virtualization/virtio-vdpa.md)
  - [Containers and Kubernetes](part-5-deployment/ch15-cloud-and-virtualization/containers-kubernetes.md)
  - [Cloud Provider Implementations](part-5-deployment/ch15-cloud-and-virtualization/cloud-providers.md)
- [Security](part-5-deployment/ch16-security/index.md)
  - [RDMA Security Model](part-5-deployment/ch16-security/security-model.md)
  - [Key and Access Control](part-5-deployment/ch16-security/key-access-control.md)
  - [Attack Surfaces and Mitigations](part-5-deployment/ch16-security/attack-surfaces.md)
  - [Secure Deployment Practices](part-5-deployment/ch16-security/secure-deployment.md)
- [Troubleshooting](part-5-deployment/ch17-troubleshooting/index.md)
  - [Diagnostic Tools](part-5-deployment/ch17-troubleshooting/diagnostic-tools.md)
  - [Hardware Counters and Monitoring](part-5-deployment/ch17-troubleshooting/hardware-counters.md)
  - [Common Failure Modes](part-5-deployment/ch17-troubleshooting/common-failures.md)
  - [Debugging Methodology](part-5-deployment/ch17-troubleshooting/debugging-methodology.md)

---

# Part VI: Future

- [Future Directions](part-6-future/ch18-future-directions/index.md)
  - [CXL and Memory-Centric Computing](part-6-future/ch18-future-directions/cxl.md)
  - [SmartNICs and DPUs](part-6-future/ch18-future-directions/smartnics-dpus.md)
  - [GPUDirect RDMA](part-6-future/ch18-future-directions/gpudirect.md)
  - [Computational Storage and Beyond](part-6-future/ch18-future-directions/computational-storage.md)

---

# Appendices

- [Appendix A: Verbs API Reference](appendices/appendix-a-verbs-api-reference.md)
- [Appendix B: RDMA_CM API Reference](appendices/appendix-b-rdma-cm-api-reference.md)
- [Appendix C: Glossary](appendices/appendix-c-glossary.md)
- [Appendix D: Further Reading](appendices/appendix-d-further-reading.md)
- [Appendix E: Lab Setup Guide](appendices/appendix-e-lab-setup.md)
