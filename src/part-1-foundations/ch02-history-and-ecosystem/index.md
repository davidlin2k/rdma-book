# Chapter 2: History and Ecosystem

RDMA did not emerge fully formed from a single standards body or a single company. It evolved through two decades of competing visions, corporate mergers, standards battles, and hard-won engineering compromises. Understanding this history is not merely academic---it explains *why* today's RDMA landscape looks the way it does, why three distinct transport technologies coexist, and why the software stack carries the architectural fingerprints of decisions made in the late 1990s.

The story begins with a crisis. By the mid-1990s, the PCI bus that connected processors to I/O devices inside servers was becoming a bottleneck. Two industry consortia formed independently to solve the same problem, collided, merged, and produced InfiniBand---a technology that would ultimately redefine high-performance computing even as it failed to achieve its original goal of replacing the internal I/O bus. That pivot from I/O interconnect to network fabric is the first act of the RDMA story, and it echoes through every design decision that followed.

The second and third acts involve bringing RDMA semantics to the dominant networking technology of the datacenter: Ethernet. iWARP attempted this by layering RDMA over TCP/IP, preserving compatibility with existing infrastructure at the cost of protocol complexity. RoCE took a different path, transplanting InfiniBand's efficient transport layer directly onto Ethernet frames---first at Layer 2, then, when the limitations of that approach became clear, encapsulated within UDP/IP headers for routability. Today, RoCE v2 dominates new deployments in cloud and enterprise datacenters, but all three transports remain relevant in specific niches.

Beneath these transports lies a shared software ecosystem: the OpenFabrics stack, now manifested as the `rdma-core` user-space package and the kernel RDMA subsystem. This software layer provides a unified programming interface---the Verbs API---that abstracts away transport differences and gives application developers a single model for zero-copy, kernel-bypass networking regardless of the underlying hardware.

## What This Chapter Covers

**Section 2.1 -- InfiniBand Origins** traces the birth of InfiniBand from the merger of Intel's NGIO and IBM's Future I/O initiatives, its initial ambitions, its pivot to HPC networking, and its eventual dominance in supercomputing.

**Section 2.2 -- iWARP** examines the IETF-standardized approach to running RDMA over TCP/IP, the protocol layers involved, and why iWARP found a narrower market than its proponents hoped.

**Section 2.3 -- RoCE and RoCE v2** covers the technology that brought InfiniBand-class RDMA performance to Ethernet networks, including the critical evolution from Layer 2 to routable UDP/IP encapsulation.

**Section 2.4 -- OFED and rdma-core** describes the software ecosystem: the OpenFabrics Alliance, the kernel RDMA subsystem, the user-space library stack, and the tools that tie everything together.

**Section 2.5 -- Industry Landscape** surveys the current market: the key hardware vendors, the impact of NVIDIA's Mellanox acquisition, cloud provider strategies, and where the technology is heading.

By the end of this chapter, you will have a clear map of the RDMA landscape---historically, technically, and commercially. That map will make the deep technical material in subsequent chapters far easier to navigate, because you will understand not just *how* each component works, but *why* it exists and what alternatives it displaced.
