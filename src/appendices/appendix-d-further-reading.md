# Appendix D: Further Reading

This appendix provides a curated collection of specifications, academic papers, books, and online resources for deepening your understanding of RDMA technology. Resources are organized by category and annotated with brief descriptions of their relevance.

---

## Specifications

These are the authoritative technical specifications that define RDMA protocols and behavior.

**InfiniBand Architecture Specification (IBTA)**
The definitive specification for InfiniBand, covering the physical layer, link layer, network layer, transport layer, and management framework. Volume 1 covers the core architecture; Volume 2 covers management. Essential reading for anyone implementing or debugging InfiniBand or RoCE systems. Available from the InfiniBand Trade Association (IBTA) website (membership required for full access).

**IBTA RoCE Annex**
The supplement to the InfiniBand Architecture Specification that defines RDMA over Converged Ethernet. It specifies RoCEv1 (raw Ethertype encapsulation) and RoCEv2 (UDP/IP encapsulation, destination port 4791). The RoCE Annex describes how InfiniBand transport headers are mapped onto Ethernet frames and the associated addressing changes (GID-based addressing instead of LID).

**IBTA Supplement: Dynamically Connected Transport (DCT)**
Defines the DCT transport type that enables scalable many-to-many RDMA communication with dynamic connection establishment. Relevant for understanding Mellanox/NVIDIA's DCT implementation.

**IEEE 802.1Qbb -- Priority-based Flow Control (PFC)**
The IEEE standard for priority-based flow control on Ethernet, which is the foundation of lossless Ethernet required by RoCE. Defines the PAUSE frame mechanism on a per-priority basis.

**IEEE 802.1Qaz -- Enhanced Transmission Selection (ETS)**
The IEEE standard for bandwidth allocation among traffic classes on Ethernet, used alongside PFC to configure quality of service in RoCE networks.

---

## RFCs (Request for Comments)

These IETF standards documents define the iWARP protocol suite and related RDMA specifications.

**RFC 5040 -- Remote Direct Memory Access Protocol Specification (RDMAP)**
Defines the RDMA Protocol (RDMAP) layer of the iWARP stack, which provides RDMA semantics (Send, RDMA Read, RDMA Write, Terminate) over the DDP layer. This is the top layer of the iWARP stack that maps most directly to the verbs API operations.

**RFC 5041 -- Direct Data Placement over Reliable Transports (DDP)**
Defines the Direct Data Placement (DDP) protocol, which enables zero-copy data transfer by allowing the receiving NIC to place incoming data directly into tagged or untagged application buffers. DDP operates over MPA and provides the data placement functionality that is the core of iWARP's performance advantage.

**RFC 5042 -- Direct Data Placement Protocol (DDP) / Remote Direct Memory Access Protocol (RDMAP) Security**
Addresses security considerations for DDP and RDMAP, including threats, trust models, and mitigation strategies for RDMA over IP networks.

**RFC 5043 -- Stream Control Transmission Protocol (SCTP) Direct Data Placement (DDP) Adaptation**
Defines how DDP can operate over SCTP as an alternative to TCP. While less commonly deployed than TCP-based iWARP, this specification provides insight into transport-independent RDMA design.

**RFC 5044 -- Marker PDU Aligned Framing for TCP Specification (MPA)**
Defines the MPA framing protocol that provides message boundaries over TCP for iWARP. MPA inserts markers and CRCs to enable the receiver to locate DDP message boundaries within TCP byte streams. Understanding MPA is essential for diagnosing iWARP performance issues related to TCP segment boundaries.

**RFC 6580 -- IANA Registrations for the Remote Direct Data Placement (RDDP) Protocols**
Defines the IANA registry entries for RDDP (which encompasses DDP and RDMAP), including well-known port numbers and protocol identifiers.

**RFC 7306 -- Remote Direct Memory Access (RDMA) Protocol Extensions**
Defines extensions to the RDMA protocols, including enhanced connection management and new RDMA operations.

**RFC 7580 -- RDMA Protocol Extensions**
Defines further extensions to RDMA protocol specifications, including updates to RDMAP and DDP for improved interoperability and new capabilities.

---

## Key Research Papers

The following papers represent landmark contributions to RDMA systems research, organized roughly by topic.

### RDMA System Design and Guidelines

**"Design Guidelines for High Performance RDMA Systems" -- Kalia, Kaminsky, Andersen (USENIX ATC 2016)**
A must-read paper that provides practical guidelines for designing high-performance RDMA applications. The authors systematically evaluate design choices including one-sided vs. two-sided operations, doorbell batching, selective signaling, inline data, and payload size effects. Their guidelines, derived from extensive microbenchmarks on ConnectX-3 and ConnectX-4 adapters, remain highly relevant.

**"FaSST: Fast, Scalable and Simple Distributed Transactions with Two-sided RDMA Datagram RPCs" -- Kalia, Kaminsky, Andersen (OSDI 2016)**
Demonstrates that two-sided UD operations can outperform one-sided RC operations for RPC workloads, challenging the conventional wisdom that one-sided operations are always superior. FaSST achieves high scalability by avoiding the O(n^2) QP problem of RC transport.

### Key-Value Stores and Storage

**"Pilaf: An RDMA-Based Key-Value Store" -- Mitchell, Gettings, Montgomery (USENIX ATC 2013)**
One of the first papers to demonstrate an RDMA-based key-value store, using RDMA Read for GET operations (bypassing the server CPU) and Send/Receive for PUTs. Pilaf introduced the "client-driven" RDMA design pattern where clients read server data structures directly via one-sided operations.

**"HERD: A Scalable RDMA-based Key-Value Store" -- Kalia, Kaminsky, Andersen (ACM SIGCOMM 2014)**
A highly influential paper showing that a design combining RDMA Write (client to server) and UD Send (server to client) outperforms designs based purely on RDMA Read. HERD demonstrated that server-centric designs with efficient request processing can beat client-driven one-sided designs by avoiding multiple round trips for hash table lookups.

**"FaRM: Fast Remote Memory" -- Dragojević, Narayanan, Hodson, Castro (NSDI 2014)**
Describes Microsoft's FaRM system for distributed computing in main memory, using RDMA for all inter-machine communication. FaRM achieves millions of distributed transactions per second using a combination of RDMA Read for data access and RDMA Write for commit protocols. The paper is essential reading for understanding RDMA-based distributed systems design.

**"FaRM: No Compromises" -- Dragojević, Narayanan, Nightingale, Renzelmann, Szekeres, Hodson, Castro (SOSP 2015)**
The follow-up to the original FaRM paper, presenting the complete transactional system with strict serializability, high availability, and unprecedented performance. Describes the four-phase commit protocol (lock, validate, commit backup, commit primary) implemented entirely over RDMA.

### RPC Frameworks

**"eRPC: Fault-tolerant Millisecond-scale RPCs" -- Kalia, Kaminsky, Andersen (NSDI 2019)**
Presents a general-purpose RPC framework that achieves near-optimal performance on both RDMA and lossy Ethernet. eRPC uses carefully optimized two-sided operations and demonstrates that a well-engineered RPC layer can match or exceed the performance of hand-tuned one-sided RDMA code while providing a much simpler programming model. The paper includes a thorough analysis of NIC architecture implications.

### Congestion Control

**"DCQCN: Congestion Control for Large-Scale RDMA Deployments" -- Zhu, Eran, Firestone, Guo, Lipshteyn, Liron, Paley, Raindel, Yahia, Zhang (ACM SIGCOMM 2015)**
The seminal paper on DCQCN (Data Center Quantized Congestion Notification), the congestion control algorithm used in production RoCEv2 networks at Microsoft Azure and other hyperscalers. DCQCN combines ECN marking at switches, CNP generation at receivers, and rate-based throttling at senders. This paper is essential for understanding RoCE congestion management.

**"TIMELY: RTT-based Congestion Control for the Datacenter" -- Mittal, Lam, Dukkipati, Blem, Wassel, Ghobadi, Vahdat, Wang, Wetherall, Zats (ACM SIGCOMM 2015)**
Proposes an alternative approach to datacenter congestion control using RTT (round-trip time) measurements rather than ECN. TIMELY demonstrates that precise RTT measurements from NIC hardware timestamps can detect congestion earlier and more accurately than ECN marking. Relevant for understanding the design space of RDMA congestion control.

**"RoGUE: RDMA over Generic Unconverged Ethernet" -- Shpiner, Zahavi, Vanunu, Kfir (technical report)**
Explores techniques for deploying RDMA over standard (lossy) Ethernet without PFC, using a combination of software retransmission, congestion control, and selective acknowledgments. RoGUE is relevant for understanding the challenges and solutions for RDMA in environments where lossless Ethernet is impractical.

**"IRN: Resilient RDMA" -- Mittal, Shpiner, Panda, Zahavi, Krishnamurthy, Ratnasamy, Shenker (ACM SIGCOMM 2018)**
Proposes modifications to the RDMA NIC to make RC transport resilient to packet loss without requiring PFC. IRN introduces selective retransmission (go-back-0 instead of go-back-N), BDP-based flow control, and improved ECN processing. This paper is important for understanding the limitations of current RDMA implementations on lossy networks and potential hardware solutions.

### Networking and Transport

**"RDMA over Commodity Ethernet at Scale" -- Guo, Wu, Deng, Chen, Arefin, Zhang, Chen, Ye, Tao, Xu, Cheng, Zhang (ACM SIGCOMM 2016)**
Describes Microsoft's experience deploying RoCEv2 at scale in Azure data centers. The paper covers practical challenges including PFC deadlocks, ECMP-induced reordering, and the PFC storm problem, along with solutions such as DSCP-based PFC, PFC watchdog, and DCQCN tuning. Essential reading for anyone deploying RoCE in production.

**"Revisiting Network Support for RDMA" -- Zhu, Kang, Firestone, de Bruijn, Lu, Peng, Zhang (ACM SIGCOMM 2023)**
A recent paper from Microsoft revisiting the network requirements for RDMA in light of a decade of production experience. Discusses the evolution from strict lossless requirements toward more flexible network configurations and proposes improvements to the RDMA transport stack.

---

## Books and Tutorials

**"RDMA Aware Networks Programming User Manual" -- Mellanox/NVIDIA**
The comprehensive programming guide for RDMA development with Mellanox/NVIDIA hardware. Covers the verbs API, RDMA-CM, device configuration, and performance optimization. Available from the NVIDIA networking documentation portal. This is the single most useful practical reference for RDMA programming.

**"Introduction to InfiniBand for End Users" -- IBTA**
An accessible introduction to InfiniBand architecture, targeted at users and system administrators rather than hardware designers. Covers basic concepts, topology, addressing, and management.

**rdma-core Documentation**
The official documentation for the rdma-core user-space RDMA library package, including man pages for all verbs and RDMA-CM functions, example programs, and provider-specific documentation. Available in the rdma-core source repository and installed on systems with the `rdma-core` package.

**Linux Kernel RDMA Documentation**
In-tree kernel documentation (`Documentation/infiniband/`) covering the kernel RDMA subsystem, including the ULP (Upper Layer Protocol) interface, the core verbs layer, hardware driver APIs, and sysfs interfaces. Essential for kernel developer and driver contributors.

**"Understanding InfiniBand and RDMA Technology" -- SNIA Tutorial**
A tutorial from the Storage Networking Industry Association providing an overview of InfiniBand and RDMA for storage professionals. Covers the basics of RDMA operations, connection management, and storage-specific applications (iSER, NVMe-oF).

---

## Online Resources

### Source Code and Repositories

**rdma-core GitHub Repository**
<https://github.com/linux-rdma/rdma-core>
The canonical source for user-space RDMA libraries, including `libibverbs`, `librdmacm`, provider drivers (mlx4, mlx5, rxe, siw, efa), and utility programs (`ibv_devinfo`, `ibv_rc_pingpong`, `ib_send_bw`, etc.). The `tests/` and `examples/` directories contain valuable reference code.

**Linux Kernel RDMA Subsystem**
<https://git.kernel.org/pub/scm/linux/kernel/git/rdma/rdma.git/>
The kernel RDMA subsystem tree, including drivers for hardware (mlx5, hfi1, qedr, bnxt_re) and software (rxe, siw) RDMA providers, the core verbs layer, the connection manager, and the MAD layer.

**perftest Repository**
<https://github.com/linux-rdma/perftest>
Micro-benchmark suite for RDMA performance testing, including `ib_send_bw`, `ib_send_lat`, `ib_write_bw`, `ib_write_lat`, `ib_read_bw`, `ib_read_lat`, and `ib_atomic_bw`. Essential for baseline performance measurements and hardware validation.

### Mailing Lists and Community

**Linux RDMA Mailing List**
<linux-rdma@vger.kernel.org> (archives at <https://lore.kernel.org/linux-rdma/>)
The primary discussion forum for Linux RDMA kernel and user-space development. Patch submissions, design discussions, and bug reports for rdma-core and the kernel RDMA subsystem are posted here. Subscribing to this list (or reading the archives) is the best way to stay current with RDMA development.

### Standards Bodies

**InfiniBand Trade Association (IBTA)**
<https://www.infinibandta.org/>
The industry consortium that develops and maintains the InfiniBand Architecture Specification, the RoCE specification, and related standards. The IBTA website provides access to specifications (some require membership), press releases, and member information.

**OpenFabrics Alliance (OFA)**
<https://www.openfabrics.org/>
The organization that maintains the OpenFabrics software stack (OFED) and hosts the annual OFA Workshop. The OFA website provides access to OFED releases, workshop presentations, and working group information. The annual workshop presentations are a valuable source of technical information on RDMA developments.

### Vendor Documentation

**NVIDIA RDMA Documentation**
<https://docs.nvidia.com/networking/>
Comprehensive documentation for NVIDIA (formerly Mellanox) networking products, including ConnectX adapter guides, BlueField DPU programming guides, OFED release notes, performance tuning guides, and firmware release notes.

**Intel RDMA Documentation**
Developer resources for Intel Ethernet adapters with iWARP or RoCE support, including the E810 series. Includes programming guides, performance tuning recommendations, and DPDK integration documentation.

**AWS Elastic Fabric Adapter (EFA) Documentation**
<https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/efa.html>
Documentation for Amazon's EFA, including supported instance types, setup guides, and programming model descriptions. Relevant for developers targeting cloud RDMA workloads.

### Tools and Frameworks

**UCX (Unified Communication X)**
<https://openucx.org/>
A communication framework that abstracts multiple transports (verbs, shared memory, TCP) behind a unified API. UCX is used by MPI implementations and NCCL for transport-agnostic RDMA communication. The UCX documentation and performance tuning guide are valuable for applications using this framework.

**libfabric (OFI)**
<https://ofiwg.github.io/libfabric/>
The OpenFabrics Interfaces (OFI) library, an alternative high-level API for fabric communication. libfabric provides providers for verbs, EFA, shared memory, and other transports. It is used by some HPC applications as an alternative to direct verbs programming.

---

## Recommended Reading Order

For readers new to RDMA, we suggest the following reading order for the external resources listed above:

1. **Start with** the NVIDIA "RDMA Aware Networks Programming User Manual" for practical API coverage.
2. **Read** Kalia et al., "Design Guidelines for High Performance RDMA Systems" for performance insights.
3. **Study** the HERD and FaRM papers for real-world RDMA system design patterns.
4. **Explore** the rdma-core source code, particularly the examples and tests directories.
5. **Reference** the IBTA specification for protocol-level details as needed.
6. **Read** the DCQCN and TIMELY papers if working with RoCE congestion control.
7. **Follow** the Linux RDMA mailing list to stay current with ongoing development.
