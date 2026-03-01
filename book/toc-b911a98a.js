// Populate the sidebar
//
// This is a script, and not included directly in the page, to control the total size of the book.
// The TOC contains an entry for each page, so if each page includes a copy of the TOC,
// the total size of the page becomes O(n**2).
class MDBookSidebarScrollbox extends HTMLElement {
    constructor() {
        super();
    }
    connectedCallback() {
        this.innerHTML = '<ol class="chapter"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="cover.html">Cover</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="disclaimer.html">Disclaimer</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="preface.html">Preface</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="introduction.html">Introduction</a></span></li><li class="chapter-item "><li class="spacer"></li></li><li class="chapter-item "><li class="part-title">Part I: Foundations</li></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch01-why-rdma/index.html"><strong aria-hidden="true">1.</strong> Why RDMA?</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch01-why-rdma/socket-model.html"><strong aria-hidden="true">1.1.</strong> Traditional Networking: The Socket Model</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch01-why-rdma/dma-fundamentals.html"><strong aria-hidden="true">1.2.</strong> DMA Fundamentals</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch01-why-rdma/rdma-concept.html"><strong aria-hidden="true">1.3.</strong> The RDMA Concept</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch01-why-rdma/kernel-bypass-zero-copy.html"><strong aria-hidden="true">1.4.</strong> Kernel Bypass and Zero-Copy in Depth</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch01-why-rdma/when-to-use-rdma.html"><strong aria-hidden="true">1.5.</strong> When (and When Not) to Use RDMA</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch02-history-and-ecosystem/index.html"><strong aria-hidden="true">2.</strong> History and Ecosystem</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch02-history-and-ecosystem/infiniband-origins.html"><strong aria-hidden="true">2.1.</strong> InfiniBand Origins</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch02-history-and-ecosystem/iwarp.html"><strong aria-hidden="true">2.2.</strong> iWARP</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch02-history-and-ecosystem/roce.html"><strong aria-hidden="true">2.3.</strong> RoCE and RoCEv2</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch02-history-and-ecosystem/ofed-rdma-core.html"><strong aria-hidden="true">2.4.</strong> OFED and rdma-core</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch02-history-and-ecosystem/industry-landscape.html"><strong aria-hidden="true">2.5.</strong> Industry Landscape</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch03-transport-protocols/index.html"><strong aria-hidden="true">3.</strong> Transport Protocols</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch03-transport-protocols/infiniband-architecture.html"><strong aria-hidden="true">3.1.</strong> InfiniBand Architecture</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch03-transport-protocols/roce-v1-v2.html"><strong aria-hidden="true">3.2.</strong> RoCE v1 and v2</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch03-transport-protocols/iwarp-protocol-stack.html"><strong aria-hidden="true">3.3.</strong> iWARP Protocol Stack</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch03-transport-protocols/packet-formats.html"><strong aria-hidden="true">3.4.</strong> Packet Formats</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-1-foundations/ch03-transport-protocols/protocol-comparison.html"><strong aria-hidden="true">3.5.</strong> Protocol Comparison</a></span></li></ol><li class="chapter-item "><li class="spacer"></li></li><li class="chapter-item "><li class="part-title">Part II: Architecture</li></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch04-core-abstractions/index.html"><strong aria-hidden="true">4.</strong> Core Abstractions</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch04-core-abstractions/verbs-abstraction.html"><strong aria-hidden="true">4.1.</strong> The Verbs Abstraction Layer</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch04-core-abstractions/queue-pairs.html"><strong aria-hidden="true">4.2.</strong> Queue Pairs (QP)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch04-core-abstractions/completion-queues.html"><strong aria-hidden="true">4.3.</strong> Completion Queues (CQ)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch04-core-abstractions/memory-regions.html"><strong aria-hidden="true">4.4.</strong> Memory Regions (MR)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch04-core-abstractions/protection-domains.html"><strong aria-hidden="true">4.5.</strong> Protection Domains (PD)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch04-core-abstractions/address-handles.html"><strong aria-hidden="true">4.6.</strong> Address Handles (AH)</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch05-rdma-operations/index.html"><strong aria-hidden="true">5.</strong> RDMA Operations</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch05-rdma-operations/send-receive.html"><strong aria-hidden="true">5.1.</strong> Send/Receive (Two-Sided)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch05-rdma-operations/rdma-write.html"><strong aria-hidden="true">5.2.</strong> RDMA Write (One-Sided)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch05-rdma-operations/rdma-read.html"><strong aria-hidden="true">5.3.</strong> RDMA Read (One-Sided)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch05-rdma-operations/atomic-operations.html"><strong aria-hidden="true">5.4.</strong> Atomic Operations</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch05-rdma-operations/immediate-data.html"><strong aria-hidden="true">5.5.</strong> Immediate Data</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch05-rdma-operations/semantics-and-ordering.html"><strong aria-hidden="true">5.6.</strong> Operation Semantics and Ordering</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch06-memory-management/index.html"><strong aria-hidden="true">6.</strong> Memory Management</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch06-memory-management/registration-deep-dive.html"><strong aria-hidden="true">6.1.</strong> Memory Registration Deep Dive</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch06-memory-management/memory-pinning.html"><strong aria-hidden="true">6.2.</strong> Memory Pinning</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch06-memory-management/mr-types.html"><strong aria-hidden="true">6.3.</strong> MR Types</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch06-memory-management/memory-windows.html"><strong aria-hidden="true">6.4.</strong> Memory Windows</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch06-memory-management/on-demand-paging.html"><strong aria-hidden="true">6.5.</strong> On-Demand Paging</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch07-connection-management/index.html"><strong aria-hidden="true">7.</strong> Connection Management</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch07-connection-management/qp-state-machine.html"><strong aria-hidden="true">7.1.</strong> QP State Machine</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch07-connection-management/communication-manager.html"><strong aria-hidden="true">7.2.</strong> Communication Manager (CM)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch07-connection-management/rdma-cm.html"><strong aria-hidden="true">7.3.</strong> RDMA_CM</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-2-architecture/ch07-connection-management/connection-patterns.html"><strong aria-hidden="true">7.4.</strong> Connection Patterns</a></span></li></ol><li class="chapter-item "><li class="spacer"></li></li><li class="chapter-item "><li class="part-title">Part III: Programming</li></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch08-getting-started/index.html"><strong aria-hidden="true">8.</strong> Getting Started</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch08-getting-started/environment-setup.html"><strong aria-hidden="true">8.1.</strong> Environment Setup</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch08-getting-started/libibverbs-overview.html"><strong aria-hidden="true">8.2.</strong> libibverbs API Overview</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch08-getting-started/first-rdma-program.html"><strong aria-hidden="true">8.3.</strong> Your First RDMA Program</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch08-getting-started/device-port-discovery.html"><strong aria-hidden="true">8.4.</strong> Device and Port Discovery</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch08-getting-started/building-and-running.html"><strong aria-hidden="true">8.5.</strong> Building and Running</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch09-programming-patterns/index.html"><strong aria-hidden="true">9.</strong> Programming Patterns</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch09-programming-patterns/rc-send-receive.html"><strong aria-hidden="true">9.1.</strong> RC Send/Receive</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch09-programming-patterns/rc-rdma-write.html"><strong aria-hidden="true">9.2.</strong> RC RDMA Write</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch09-programming-patterns/rc-rdma-read.html"><strong aria-hidden="true">9.3.</strong> RC RDMA Read</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch09-programming-patterns/ud-messaging.html"><strong aria-hidden="true">9.4.</strong> UD Messaging</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch09-programming-patterns/completion-handling.html"><strong aria-hidden="true">9.5.</strong> Completion Handling</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch09-programming-patterns/error-handling.html"><strong aria-hidden="true">9.6.</strong> Error Handling</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch10-rdma-cm-programming/index.html"><strong aria-hidden="true">10.</strong> RDMA_CM Programming</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch10-rdma-cm-programming/cm-event-loop.html"><strong aria-hidden="true">10.1.</strong> CM Event Loop</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch10-rdma-cm-programming/client-server.html"><strong aria-hidden="true">10.2.</strong> Client-Server with RDMA_CM</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch10-rdma-cm-programming/multicast.html"><strong aria-hidden="true">10.3.</strong> Multicast</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch11-advanced-programming/index.html"><strong aria-hidden="true">11.</strong> Advanced Programming</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch11-advanced-programming/shared-receive-queues.html"><strong aria-hidden="true">11.1.</strong> Shared Receive Queues (SRQ)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch11-advanced-programming/xrc-transport.html"><strong aria-hidden="true">11.2.</strong> XRC Transport</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch11-advanced-programming/dct-transport.html"><strong aria-hidden="true">11.3.</strong> DCT Transport</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch11-advanced-programming/inline-data.html"><strong aria-hidden="true">11.4.</strong> Inline Data</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch11-advanced-programming/multi-threaded-rdma.html"><strong aria-hidden="true">11.5.</strong> Multi-Threaded RDMA</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-3-programming/ch11-advanced-programming/zero-copy-patterns.html"><strong aria-hidden="true">11.6.</strong> Zero-Copy Design Patterns</a></span></li></ol><li class="chapter-item "><li class="spacer"></li></li><li class="chapter-item "><li class="part-title">Part IV: Performance</li></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch12-performance-engineering/index.html"><strong aria-hidden="true">12.</strong> Performance Engineering</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch12-performance-engineering/latency-analysis.html"><strong aria-hidden="true">12.1.</strong> Latency Analysis</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch12-performance-engineering/throughput-optimization.html"><strong aria-hidden="true">12.2.</strong> Throughput Optimization</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch12-performance-engineering/pcie-considerations.html"><strong aria-hidden="true">12.3.</strong> PCIe Considerations</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch12-performance-engineering/numa-awareness.html"><strong aria-hidden="true">12.4.</strong> NUMA Awareness</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch12-performance-engineering/nic-architecture.html"><strong aria-hidden="true">12.5.</strong> NIC Architecture Internals</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch12-performance-engineering/benchmarking.html"><strong aria-hidden="true">12.6.</strong> Benchmarking</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch13-congestion-control/index.html"><strong aria-hidden="true">13.</strong> Congestion Control</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch13-congestion-control/lossless-ethernet.html"><strong aria-hidden="true">13.1.</strong> Lossless Ethernet Fundamentals</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch13-congestion-control/pfc.html"><strong aria-hidden="true">13.2.</strong> Priority Flow Control (PFC)</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch13-congestion-control/ecn-dcqcn.html"><strong aria-hidden="true">13.3.</strong> ECN and DCQCN</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-4-performance/ch13-congestion-control/fabric-design.html"><strong aria-hidden="true">13.4.</strong> Fabric Design for RDMA</a></span></li></ol><li class="chapter-item "><li class="spacer"></li></li><li class="chapter-item "><li class="part-title">Part V: Deployment</li></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch14-applications/index.html"><strong aria-hidden="true">14.</strong> Applications</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch14-applications/storage.html"><strong aria-hidden="true">14.1.</strong> Storage: NVMe-oF and iSER</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch14-applications/hpc.html"><strong aria-hidden="true">14.2.</strong> High-Performance Computing: MPI and UCX</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch14-applications/databases.html"><strong aria-hidden="true">14.3.</strong> Databases and Key-Value Stores</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch14-applications/distributed-filesystems.html"><strong aria-hidden="true">14.4.</strong> Distributed File Systems</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch14-applications/ml-and-ai.html"><strong aria-hidden="true">14.5.</strong> Machine Learning and AI</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch15-cloud-and-virtualization/index.html"><strong aria-hidden="true">15.</strong> Cloud and Virtualization</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch15-cloud-and-virtualization/sriov.html"><strong aria-hidden="true">15.1.</strong> SR-IOV for RDMA</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch15-cloud-and-virtualization/virtio-vdpa.html"><strong aria-hidden="true">15.2.</strong> virtio and VDPA</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch15-cloud-and-virtualization/containers-kubernetes.html"><strong aria-hidden="true">15.3.</strong> Containers and Kubernetes</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch15-cloud-and-virtualization/cloud-providers.html"><strong aria-hidden="true">15.4.</strong> Cloud Provider Implementations</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch16-security/index.html"><strong aria-hidden="true">16.</strong> Security</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch16-security/security-model.html"><strong aria-hidden="true">16.1.</strong> RDMA Security Model</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch16-security/key-access-control.html"><strong aria-hidden="true">16.2.</strong> Key and Access Control</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch16-security/attack-surfaces.html"><strong aria-hidden="true">16.3.</strong> Attack Surfaces and Mitigations</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch16-security/secure-deployment.html"><strong aria-hidden="true">16.4.</strong> Secure Deployment Practices</a></span></li></ol><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch17-troubleshooting/index.html"><strong aria-hidden="true">17.</strong> Troubleshooting</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch17-troubleshooting/diagnostic-tools.html"><strong aria-hidden="true">17.1.</strong> Diagnostic Tools</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch17-troubleshooting/hardware-counters.html"><strong aria-hidden="true">17.2.</strong> Hardware Counters and Monitoring</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch17-troubleshooting/common-failures.html"><strong aria-hidden="true">17.3.</strong> Common Failure Modes</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-5-deployment/ch17-troubleshooting/debugging-methodology.html"><strong aria-hidden="true">17.4.</strong> Debugging Methodology</a></span></li></ol><li class="chapter-item "><li class="spacer"></li></li><li class="chapter-item "><li class="part-title">Part VI: Future</li></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-6-future/ch18-future-directions/index.html"><strong aria-hidden="true">18.</strong> Future Directions</a><a class="chapter-fold-toggle"><div>❱</div></a></span><ol class="section"><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-6-future/ch18-future-directions/cxl.html"><strong aria-hidden="true">18.1.</strong> CXL and Memory-Centric Computing</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-6-future/ch18-future-directions/smartnics-dpus.html"><strong aria-hidden="true">18.2.</strong> SmartNICs and DPUs</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-6-future/ch18-future-directions/gpudirect.html"><strong aria-hidden="true">18.3.</strong> GPUDirect RDMA</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="part-6-future/ch18-future-directions/computational-storage.html"><strong aria-hidden="true">18.4.</strong> Computational Storage and Beyond</a></span></li></ol><li class="chapter-item "><li class="spacer"></li></li><li class="chapter-item "><li class="part-title">Appendices</li></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="appendices/appendix-a-verbs-api-reference.html"><strong aria-hidden="true">19.</strong> Appendix A: Verbs API Reference</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="appendices/appendix-b-rdma-cm-api-reference.html"><strong aria-hidden="true">20.</strong> Appendix B: RDMA_CM API Reference</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="appendices/appendix-c-glossary.html"><strong aria-hidden="true">21.</strong> Appendix C: Glossary</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="appendices/appendix-d-further-reading.html"><strong aria-hidden="true">22.</strong> Appendix D: Further Reading</a></span></li><li class="chapter-item "><span class="chapter-link-wrapper"><a href="appendices/appendix-e-lab-setup.html"><strong aria-hidden="true">23.</strong> Appendix E: Lab Setup Guide</a></span></li></ol>';
        // Set the current, active page, and reveal it if it's hidden
        let current_page = document.location.href.toString().split('#')[0].split('?')[0];
        if (current_page.endsWith('/')) {
            current_page += 'index.html';
        }
        const links = Array.prototype.slice.call(this.querySelectorAll('a'));
        const l = links.length;
        for (let i = 0; i < l; ++i) {
            const link = links[i];
            const href = link.getAttribute('href');
            if (href && !href.startsWith('#') && !/^(?:[a-z+]+:)?\/\//.test(href)) {
                link.href = path_to_root + href;
            }
            // The 'index' page is supposed to alias the first chapter in the book.
            if (link.href === current_page
                || i === 0
                && path_to_root === ''
                && current_page.endsWith('/index.html')) {
                link.classList.add('active');
                let parent = link.parentElement;
                while (parent) {
                    if (parent.tagName === 'LI' && parent.classList.contains('chapter-item')) {
                        parent.classList.add('expanded');
                    }
                    parent = parent.parentElement;
                }
            }
        }
        // Track and set sidebar scroll position
        this.addEventListener('click', e => {
            if (e.target.tagName === 'A') {
                const clientRect = e.target.getBoundingClientRect();
                const sidebarRect = this.getBoundingClientRect();
                sessionStorage.setItem('sidebar-scroll-offset', clientRect.top - sidebarRect.top);
            }
        }, { passive: true });
        const sidebarScrollOffset = sessionStorage.getItem('sidebar-scroll-offset');
        sessionStorage.removeItem('sidebar-scroll-offset');
        if (sidebarScrollOffset !== null) {
            // preserve sidebar scroll position when navigating via links within sidebar
            const activeSection = this.querySelector('.active');
            if (activeSection) {
                const clientRect = activeSection.getBoundingClientRect();
                const sidebarRect = this.getBoundingClientRect();
                const currentOffset = clientRect.top - sidebarRect.top;
                this.scrollTop += currentOffset - parseFloat(sidebarScrollOffset);
            }
        } else {
            // scroll sidebar to current active section when navigating via
            // 'next/previous chapter' buttons
            const activeSection = document.querySelector('#mdbook-sidebar .active');
            if (activeSection) {
                activeSection.scrollIntoView({ block: 'center' });
            }
        }
        // Toggle buttons
        const sidebarAnchorToggles = document.querySelectorAll('.chapter-fold-toggle');
        function toggleSection(ev) {
            ev.currentTarget.parentElement.parentElement.classList.toggle('expanded');
        }
        Array.from(sidebarAnchorToggles).forEach(el => {
            el.addEventListener('click', toggleSection);
        });
    }
}
window.customElements.define('mdbook-sidebar-scrollbox', MDBookSidebarScrollbox);


// ---------------------------------------------------------------------------
// Support for dynamically adding headers to the sidebar.

(function() {
    // This is used to detect which direction the page has scrolled since the
    // last scroll event.
    let lastKnownScrollPosition = 0;
    // This is the threshold in px from the top of the screen where it will
    // consider a header the "current" header when scrolling down.
    const defaultDownThreshold = 150;
    // Same as defaultDownThreshold, except when scrolling up.
    const defaultUpThreshold = 300;
    // The threshold is a virtual horizontal line on the screen where it
    // considers the "current" header to be above the line. The threshold is
    // modified dynamically to handle headers that are near the bottom of the
    // screen, and to slightly offset the behavior when scrolling up vs down.
    let threshold = defaultDownThreshold;
    // This is used to disable updates while scrolling. This is needed when
    // clicking the header in the sidebar, which triggers a scroll event. It
    // is somewhat finicky to detect when the scroll has finished, so this
    // uses a relatively dumb system of disabling scroll updates for a short
    // time after the click.
    let disableScroll = false;
    // Array of header elements on the page.
    let headers;
    // Array of li elements that are initially collapsed headers in the sidebar.
    // I'm not sure why eslint seems to have a false positive here.
    // eslint-disable-next-line prefer-const
    let headerToggles = [];
    // This is a debugging tool for the threshold which you can enable in the console.
    let thresholdDebug = false;

    // Updates the threshold based on the scroll position.
    function updateThreshold() {
        const scrollTop = window.pageYOffset || document.documentElement.scrollTop;
        const windowHeight = window.innerHeight;
        const documentHeight = document.documentElement.scrollHeight;

        // The number of pixels below the viewport, at most documentHeight.
        // This is used to push the threshold down to the bottom of the page
        // as the user scrolls towards the bottom.
        const pixelsBelow = Math.max(0, documentHeight - (scrollTop + windowHeight));
        // The number of pixels above the viewport, at least defaultDownThreshold.
        // Similar to pixelsBelow, this is used to push the threshold back towards
        // the top when reaching the top of the page.
        const pixelsAbove = Math.max(0, defaultDownThreshold - scrollTop);
        // How much the threshold should be offset once it gets close to the
        // bottom of the page.
        const bottomAdd = Math.max(0, windowHeight - pixelsBelow - defaultDownThreshold);
        let adjustedBottomAdd = bottomAdd;

        // Adjusts bottomAdd for a small document. The calculation above
        // assumes the document is at least twice the windowheight in size. If
        // it is less than that, then bottomAdd needs to be shrunk
        // proportional to the difference in size.
        if (documentHeight < windowHeight * 2) {
            const maxPixelsBelow = documentHeight - windowHeight;
            const t = 1 - pixelsBelow / Math.max(1, maxPixelsBelow);
            const clamp = Math.max(0, Math.min(1, t));
            adjustedBottomAdd *= clamp;
        }

        let scrollingDown = true;
        if (scrollTop < lastKnownScrollPosition) {
            scrollingDown = false;
        }

        if (scrollingDown) {
            // When scrolling down, move the threshold up towards the default
            // downwards threshold position. If near the bottom of the page,
            // adjustedBottomAdd will offset the threshold towards the bottom
            // of the page.
            const amountScrolledDown = scrollTop - lastKnownScrollPosition;
            const adjustedDefault = defaultDownThreshold + adjustedBottomAdd;
            threshold = Math.max(adjustedDefault, threshold - amountScrolledDown);
        } else {
            // When scrolling up, move the threshold down towards the default
            // upwards threshold position. If near the bottom of the page,
            // quickly transition the threshold back up where it normally
            // belongs.
            const amountScrolledUp = lastKnownScrollPosition - scrollTop;
            const adjustedDefault = defaultUpThreshold - pixelsAbove
                + Math.max(0, adjustedBottomAdd - defaultDownThreshold);
            threshold = Math.min(adjustedDefault, threshold + amountScrolledUp);
        }

        if (documentHeight <= windowHeight) {
            threshold = 0;
        }

        if (thresholdDebug) {
            const id = 'mdbook-threshold-debug-data';
            let data = document.getElementById(id);
            if (data === null) {
                data = document.createElement('div');
                data.id = id;
                data.style.cssText = `
                    position: fixed;
                    top: 50px;
                    right: 10px;
                    background-color: 0xeeeeee;
                    z-index: 9999;
                    pointer-events: none;
                `;
                document.body.appendChild(data);
            }
            data.innerHTML = `
                <table>
                  <tr><td>documentHeight</td><td>${documentHeight.toFixed(1)}</td></tr>
                  <tr><td>windowHeight</td><td>${windowHeight.toFixed(1)}</td></tr>
                  <tr><td>scrollTop</td><td>${scrollTop.toFixed(1)}</td></tr>
                  <tr><td>pixelsAbove</td><td>${pixelsAbove.toFixed(1)}</td></tr>
                  <tr><td>pixelsBelow</td><td>${pixelsBelow.toFixed(1)}</td></tr>
                  <tr><td>bottomAdd</td><td>${bottomAdd.toFixed(1)}</td></tr>
                  <tr><td>adjustedBottomAdd</td><td>${adjustedBottomAdd.toFixed(1)}</td></tr>
                  <tr><td>scrollingDown</td><td>${scrollingDown}</td></tr>
                  <tr><td>threshold</td><td>${threshold.toFixed(1)}</td></tr>
                </table>
            `;
            drawDebugLine();
        }

        lastKnownScrollPosition = scrollTop;
    }

    function drawDebugLine() {
        if (!document.body) {
            return;
        }
        const id = 'mdbook-threshold-debug-line';
        const existingLine = document.getElementById(id);
        if (existingLine) {
            existingLine.remove();
        }
        const line = document.createElement('div');
        line.id = id;
        line.style.cssText = `
            position: fixed;
            top: ${threshold}px;
            left: 0;
            width: 100vw;
            height: 2px;
            background-color: red;
            z-index: 9999;
            pointer-events: none;
        `;
        document.body.appendChild(line);
    }

    function mdbookEnableThresholdDebug() {
        thresholdDebug = true;
        updateThreshold();
        drawDebugLine();
    }

    window.mdbookEnableThresholdDebug = mdbookEnableThresholdDebug;

    // Updates which headers in the sidebar should be expanded. If the current
    // header is inside a collapsed group, then it, and all its parents should
    // be expanded.
    function updateHeaderExpanded(currentA) {
        // Add expanded to all header-item li ancestors.
        let current = currentA.parentElement;
        while (current) {
            if (current.tagName === 'LI' && current.classList.contains('header-item')) {
                current.classList.add('expanded');
            }
            current = current.parentElement;
        }
    }

    // Updates which header is marked as the "current" header in the sidebar.
    // This is done with a virtual Y threshold, where headers at or below
    // that line will be considered the current one.
    function updateCurrentHeader() {
        if (!headers || !headers.length) {
            return;
        }

        // Reset the classes, which will be rebuilt below.
        const els = document.getElementsByClassName('current-header');
        for (const el of els) {
            el.classList.remove('current-header');
        }
        for (const toggle of headerToggles) {
            toggle.classList.remove('expanded');
        }

        // Find the last header that is above the threshold.
        let lastHeader = null;
        for (const header of headers) {
            const rect = header.getBoundingClientRect();
            if (rect.top <= threshold) {
                lastHeader = header;
            } else {
                break;
            }
        }
        if (lastHeader === null) {
            lastHeader = headers[0];
            const rect = lastHeader.getBoundingClientRect();
            const windowHeight = window.innerHeight;
            if (rect.top >= windowHeight) {
                return;
            }
        }

        // Get the anchor in the summary.
        const href = '#' + lastHeader.id;
        const a = [...document.querySelectorAll('.header-in-summary')]
            .find(element => element.getAttribute('href') === href);
        if (!a) {
            return;
        }

        a.classList.add('current-header');

        updateHeaderExpanded(a);
    }

    // Updates which header is "current" based on the threshold line.
    function reloadCurrentHeader() {
        if (disableScroll) {
            return;
        }
        updateThreshold();
        updateCurrentHeader();
    }


    // When clicking on a header in the sidebar, this adjusts the threshold so
    // that it is located next to the header. This is so that header becomes
    // "current".
    function headerThresholdClick(event) {
        // See disableScroll description why this is done.
        disableScroll = true;
        setTimeout(() => {
            disableScroll = false;
        }, 100);
        // requestAnimationFrame is used to delay the update of the "current"
        // header until after the scroll is done, and the header is in the new
        // position.
        requestAnimationFrame(() => {
            requestAnimationFrame(() => {
                // Closest is needed because if it has child elements like <code>.
                const a = event.target.closest('a');
                const href = a.getAttribute('href');
                const targetId = href.substring(1);
                const targetElement = document.getElementById(targetId);
                if (targetElement) {
                    threshold = targetElement.getBoundingClientRect().bottom;
                    updateCurrentHeader();
                }
            });
        });
    }

    // Takes the nodes from the given head and copies them over to the
    // destination, along with some filtering.
    function filterHeader(source, dest) {
        const clone = source.cloneNode(true);
        clone.querySelectorAll('mark').forEach(mark => {
            mark.replaceWith(...mark.childNodes);
        });
        dest.append(...clone.childNodes);
    }

    // Scans page for headers and adds them to the sidebar.
    document.addEventListener('DOMContentLoaded', function() {
        const activeSection = document.querySelector('#mdbook-sidebar .active');
        if (activeSection === null) {
            return;
        }

        const main = document.getElementsByTagName('main')[0];
        headers = Array.from(main.querySelectorAll('h2, h3, h4, h5, h6'))
            .filter(h => h.id !== '' && h.children.length && h.children[0].tagName === 'A');

        if (headers.length === 0) {
            return;
        }

        // Build a tree of headers in the sidebar.

        const stack = [];

        const firstLevel = parseInt(headers[0].tagName.charAt(1));
        for (let i = 1; i < firstLevel; i++) {
            const ol = document.createElement('ol');
            ol.classList.add('section');
            if (stack.length > 0) {
                stack[stack.length - 1].ol.appendChild(ol);
            }
            stack.push({level: i + 1, ol: ol});
        }

        // The level where it will start folding deeply nested headers.
        const foldLevel = 3;

        for (let i = 0; i < headers.length; i++) {
            const header = headers[i];
            const level = parseInt(header.tagName.charAt(1));

            const currentLevel = stack[stack.length - 1].level;
            if (level > currentLevel) {
                // Begin nesting to this level.
                for (let nextLevel = currentLevel + 1; nextLevel <= level; nextLevel++) {
                    const ol = document.createElement('ol');
                    ol.classList.add('section');
                    const last = stack[stack.length - 1];
                    const lastChild = last.ol.lastChild;
                    // Handle the case where jumping more than one nesting
                    // level, which doesn't have a list item to place this new
                    // list inside of.
                    if (lastChild) {
                        lastChild.appendChild(ol);
                    } else {
                        last.ol.appendChild(ol);
                    }
                    stack.push({level: nextLevel, ol: ol});
                }
            } else if (level < currentLevel) {
                while (stack.length > 1 && stack[stack.length - 1].level > level) {
                    stack.pop();
                }
            }

            const li = document.createElement('li');
            li.classList.add('header-item');
            li.classList.add('expanded');
            if (level < foldLevel) {
                li.classList.add('expanded');
            }
            const span = document.createElement('span');
            span.classList.add('chapter-link-wrapper');
            const a = document.createElement('a');
            span.appendChild(a);
            a.href = '#' + header.id;
            a.classList.add('header-in-summary');
            filterHeader(header.children[0], a);
            a.addEventListener('click', headerThresholdClick);
            const nextHeader = headers[i + 1];
            if (nextHeader !== undefined) {
                const nextLevel = parseInt(nextHeader.tagName.charAt(1));
                if (nextLevel > level && level >= foldLevel) {
                    const toggle = document.createElement('a');
                    toggle.classList.add('chapter-fold-toggle');
                    toggle.classList.add('header-toggle');
                    toggle.addEventListener('click', () => {
                        li.classList.toggle('expanded');
                    });
                    const toggleDiv = document.createElement('div');
                    toggleDiv.textContent = '❱';
                    toggle.appendChild(toggleDiv);
                    span.appendChild(toggle);
                    headerToggles.push(li);
                }
            }
            li.appendChild(span);

            const currentParent = stack[stack.length - 1];
            currentParent.ol.appendChild(li);
        }

        const onThisPage = document.createElement('div');
        onThisPage.classList.add('on-this-page');
        onThisPage.append(stack[0].ol);
        const activeItemSpan = activeSection.parentElement;
        activeItemSpan.after(onThisPage);
    });

    document.addEventListener('DOMContentLoaded', reloadCurrentHeader);
    document.addEventListener('scroll', reloadCurrentHeader, { passive: true });
})();

