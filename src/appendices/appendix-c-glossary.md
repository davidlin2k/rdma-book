# Appendix C: Glossary

This glossary defines the key terms, acronyms, and concepts used throughout this book and in the broader RDMA ecosystem. Terms are arranged alphabetically.

---

**ACK (Acknowledgment)**
A transport-layer response confirming successful receipt of a packet or sequence of packets. In Reliable Connected (RC) transport, the responder sends ACKs to the requester, enabling retransmission of lost packets.

**AETH (ACK Extended Transport Header)**
An InfiniBand packet header that carries acknowledgment information, including the MSN (Message Sequence Number) and syndrome field indicating ACK, NAK, or RNR NAK status.

**AH (Address Handle)**
A verbs object that encapsulates the routing information needed to send a packet to a remote destination. AHs are required for Unreliable Datagram (UD) QPs, where each send work request specifies the destination via an AH. For InfiniBand, the AH contains the destination LID; for RoCE, it contains the destination GID.

**APM (Automatic Path Migration)**
An InfiniBand feature that allows a QP to automatically switch to an alternate path if the primary path fails. APM provides high availability without application intervention by pre-loading a backup path into the QP configuration.

**Base Transport Header**
See **BTH**.

**BlueFlame**
A Mellanox/NVIDIA hardware optimization that allows work queue entries (WQEs) to be written directly to the HCA via a write-combining MMIO region, reducing doorbell latency. Instead of writing the WQE to host memory and then ringing a doorbell, BlueFlame combines both steps into a single write-combining burst, saving a PCIe round trip.

**BTH (Base Transport Header)**
The core InfiniBand transport header present in every data packet. It contains the opcode, destination QP number (QPN), Packet Sequence Number (PSN), partition key, and other control fields essential for packet routing and transport-layer processing.

**CAS (Compare-and-Swap)**
An atomic RDMA operation that reads an 8-byte value from a remote memory location, compares it with a provided value, and if they match, replaces it with a new value. The original value is returned to the requester. CAS is used for building distributed locks, reference counts, and other synchronization primitives.

**CM (Communication Manager)**
The component responsible for establishing, maintaining, and tearing down RDMA connections. The CM handles the exchange of QP parameters (QPN, PSN, GID, etc.) between peers. In InfiniBand, the CM operates via MAD (Management Datagram) messages. The user-space API is provided by `librdmacm`.

**CNP (Congestion Notification Packet)**
A packet sent by an RDMA endpoint back to the source of congestion when it receives a packet marked with ECN (Explicit Congestion Notification). CNPs are used by the DCQCN congestion control algorithm in RoCEv2 to signal the sender to reduce its transmission rate.

**CQ (Completion Queue)**
A FIFO queue that holds Completion Queue Entries (CQEs) generated when work requests complete. Each CQE reports the status (success or error), the work request ID, the opcode, and the number of bytes transferred. CQs are the primary feedback mechanism from the HCA to the application.

**CQE (Completion Queue Entry)**
A single entry in a Completion Queue, representing the completion of one work request. A CQE contains the `wr_id`, status code, opcode, byte count, and other metadata. The application retrieves CQEs by polling the CQ via `ibv_poll_cq()`.

**DCQCN (Data Center Quantized Congestion Notification)**
A congestion control algorithm designed for RoCEv2 networks, combining ECN marking at switches with rate-based throttling at the sender. DCQCN uses three components: the switch marks packets with ECN when queues build up, the receiver sends CNPs back to the sender, and the sender reduces its rate in response. DCQCN is the most widely deployed congestion control mechanism for RoCE in data centers.

**DCT (Dynamically Connected Transport)**
A Mellanox/NVIDIA proprietary transport type that allows a single QP to communicate with many remote peers by dynamically establishing connections on demand. DCTs combine the scalability of UD (no per-peer QP) with the reliability of RC, addressing the O(n^2) QP scaling problem in large clusters.

**DDP (Direct Data Placement)**
A protocol layer in the iWARP stack (RFC 5041) that enables data to be placed directly into the application's buffer by the receiving NIC, without intermediate copies. DDP sits between MPA (below) and RDMAP (above) in the iWARP protocol stack.

**DMA (Direct Memory Access)**
A hardware mechanism that allows a peripheral device (such as an RDMA NIC) to read from or write to host memory without CPU involvement. DMA is fundamental to RDMA's zero-copy data transfer model: the HCA uses DMA to fetch send data and deposit receive data directly in application buffers.

**DMFS (Device-Managed Flow Steering)**
A hardware capability that allows the NIC to steer incoming packets to specific QPs or RQs based on flow rules (e.g., matching on IP addresses, ports, or VLAN tags). DMFS offloads flow classification to the HCA, reducing CPU overhead for multi-flow workloads.

**DPU (Data Processing Unit)**
A programmable network processor that combines RDMA NIC functionality with general-purpose compute cores, typically built on ARM or custom architectures. DPUs (such as NVIDIA BlueField) offload infrastructure tasks like storage, networking, and security from the host CPU, acting as a "smart NIC" with full programmability.

**DSCP (Differentiated Services Code Point)**
A 6-bit field in the IP header used to classify packets into quality-of-service (QoS) classes. In RoCEv2, the DSCP value maps to a Service Level (SL) and determines the traffic class and priority treatment at switches. Proper DSCP configuration is essential for PFC and ECN to function correctly.

**ECN (Explicit Congestion Notification)**
A mechanism defined in RFC 3168 that allows network switches to signal congestion to endpoints by marking packets (setting the CE -- Congestion Experienced -- bits in the IP header) rather than dropping them. ECN is a key enabler for lossless RDMA over Ethernet (RoCE), working with DCQCN to manage congestion without packet loss.

**ECMP (Equal-Cost Multi-Path)**
A routing strategy that distributes traffic across multiple equal-cost paths to a destination, increasing aggregate bandwidth and providing fault tolerance. In RDMA networks, ECMP can cause packet reordering, which is problematic for RC transport. Techniques like flowlet switching and adaptive routing mitigate this issue.

**EFA (Elastic Fabric Adapter)**
Amazon Web Services' custom RDMA-like network interface for EC2 instances. EFA provides a Scalable Reliable Datagram (SRD) transport that supports unordered delivery, enabling effective use of multi-path routing in cloud networks. EFA uses a verbs-compatible API through the `efa` provider in `rdma-core`.

**FAA (Fetch-and-Add)**
An atomic RDMA operation that reads an 8-byte value from a remote memory location, adds a specified value to it, and returns the original value. FAA is used for distributed counters, sequence number allocation, and other concurrent data structure operations.

**GID (Global Identifier)**
A 128-bit identifier for an RDMA port, analogous to an IPv6 address. For InfiniBand, the GID consists of a 64-bit subnet prefix and a 64-bit port GUID. For RoCE, the GID is derived from the interface's IPv4 or IPv6 address (mapped into the IPv6 format). GIDs are used in the Global Route Header (GRH) for inter-subnet routing and as the primary addressing mechanism in RoCE.

**GRH (Global Route Header)**
An InfiniBand packet header containing source and destination GIDs, traffic class, hop limit, and flow label. The GRH enables routing across subnets in InfiniBand and is always present in RoCE packets (mapped to the IP header in RoCEv2). In UD QPs, the GRH is prepended to received message data.

**GRH Padding**
The 40 bytes of Global Route Header data that is prepended to every message received on a UD QP. Receive buffers for UD QPs must be allocated with an additional 40 bytes to accommodate the GRH.

**HCA (Host Channel Adapter)**
The RDMA network interface card in an InfiniBand system. The HCA is a full-featured intelligent adapter that implements the InfiniBand transport protocol in hardware, manages QPs, performs DMA, and handles retransmission. In Ethernet-based RDMA, the equivalent device is called an RNIC.

**ICRC (Invariant CRC)**
A 32-bit CRC at the end of every InfiniBand and RoCE packet that covers the portions of the packet not modified by routers (i.e., everything except the LRH and certain GRH fields). The ICRC detects data corruption end-to-end, complementing the VCRC which provides link-level integrity.

**iWARP (Internet Wide Area RDMA Protocol)**
An RDMA transport protocol that runs over TCP/IP, enabling RDMA over standard Ethernet infrastructure without requiring lossless configuration. iWARP uses a stack of protocols: TCP for reliable transport, MPA for framing, DDP for direct data placement, and RDMAP for RDMA semantics. iWARP trades some performance for easier deployment compared to RoCE.

**IOMMU (I/O Memory Management Unit)**
A hardware unit that translates I/O virtual addresses used by devices (such as RDMA NICs) to physical memory addresses. The IOMMU provides memory protection and isolation for DMA operations. With IOMMU enabled, RDMA memory registration creates entries in both the HCA's translation tables and the IOMMU's page tables.

**L_Key (Local Key)**
A 32-bit key associated with a registered Memory Region that authorizes local access. The L_Key is included in scatter/gather list entries of work requests to identify the MR that covers the referenced memory buffer. The HCA uses the L_Key to look up the memory translation and verify access rights.

**LID (Local Identifier)**
A 16-bit address assigned by the Subnet Manager to each InfiniBand port within a subnet. LIDs are used for intra-subnet routing in InfiniBand. RoCE does not use LIDs; it uses GIDs (mapped to IP addresses) instead. A LID of 0 indicates the port has no LID assigned.

**LRH (Local Route Header)**
The first header in an InfiniBand link-level packet, containing the source and destination LIDs, Virtual Lane (VL), Service Level (SL), and packet length. The LRH is consumed by switches for intra-subnet routing and is not present in RoCE packets (replaced by the Ethernet header).

**MAD (Management Datagram)**
Special InfiniBand packets used for management operations such as Subnet Manager queries, performance monitoring, and diagnostic functions. MADs are sent via QP0 (Subnet Management) and QP1 (General Services) using UD transport.

**MPA (Marker PDU Aligned)**
A framing protocol in the iWARP stack (RFC 5044) that provides message boundaries over TCP's byte-stream abstraction. MPA inserts markers and CRCs to enable the receiver to locate DDP message boundaries even when TCP segments do not align with RDMA messages.

**MPS (Max Payload Size)**
See **MTU**.

**MR (Memory Region)**
A verbs object representing a contiguous region of virtual memory that has been registered with the RDMA subsystem. Registration pins the physical pages, creates translation table entries in the HCA, and produces an L_Key (for local access) and R_Key (for remote access). MRs are the fundamental mechanism for authorizing RDMA access to application memory.

**MTT (Memory Translation Table)**
An internal HCA data structure that maps virtual addresses in registered Memory Regions to physical page addresses. The MTT enables the HCA to perform DMA operations using application virtual addresses. Each MR registration consumes MTT entries proportional to the number of pages in the region.

**MTU (Maximum Transfer Unit)**
The maximum payload size of a single RDMA packet. For InfiniBand, common MTU values are 256, 512, 1024, 2048, and 4096 bytes. For RoCE, the MTU is constrained by the Ethernet MTU (typically 1500 bytes standard or 9000 bytes with jumbo frames). Messages larger than the MTU are automatically segmented by the HCA into multiple packets.

**MW (Memory Window)**
A verbs object that provides a mechanism for granting fine-grained remote access to a sub-range of an existing Memory Region. MWs allow dynamic binding and unbinding of remote access rights without the overhead of re-registering the MR. Type 1 MWs are bound via a special verb; Type 2 MWs are bound via a QP work request.

**NAK (Negative Acknowledgment)**
A transport-layer response indicating that a packet was received with an error or was unexpected. NAKs trigger retransmission by the sender. Types of NAKs include sequence error NAK, invalid request NAK, remote access error NAK, remote operational error NAK, and invalid RD request NAK.

**NCCL (NVIDIA Collective Communications Library)**
A library providing multi-GPU and multi-node collective communication primitives (all-reduce, all-gather, broadcast, etc.) optimized for NVIDIA GPUs. NCCL uses RDMA (via verbs or UCX) for inter-node communication and NVLink/NVSwitch for intra-node GPU-to-GPU transfers. It is the primary communication library for distributed deep learning training.

**NVMe-oF (NVMe over Fabrics)**
A storage protocol that extends the NVMe (Non-Volatile Memory Express) interface over network fabrics, including RDMA. NVMe-oF over RDMA enables remote access to NVMe storage devices with near-local latency, leveraging RDMA's zero-copy and kernel-bypass capabilities. Supported transports include RoCE, InfiniBand, and iWARP.

**ODP (On-Demand Paging)**
An RDMA feature that eliminates the need for memory pinning during registration. With ODP, registered pages are resolved on demand via page faults handled by the HCA in cooperation with the OS. This reduces memory registration overhead and allows RDMA over memory that may be swapped out, forked, or relocated. ODP is supported on Mellanox/NVIDIA ConnectX-4 and later.

**OFED (OpenFabrics Enterprise Distribution)**
A software stack providing RDMA drivers, libraries, and utilities for InfiniBand, RoCE, and iWARP. OFED includes the Linux kernel RDMA subsystem, `rdma-core` user-space libraries (`libibverbs`, `librdmacm`), and diagnostic tools. The distribution is maintained by the OpenFabrics Alliance (OFA). The inbox version in the Linux kernel is sometimes called "inbox OFED."

**PD (Protection Domain)**
A verbs object that groups RDMA resources (MRs, QPs, AHs, MWs, SRQs) into a common security scope. Only resources within the same PD can interact, preventing unauthorized cross-application memory access. Every RDMA application must allocate at least one PD.

**PFC (Priority Flow Control)**
An Ethernet-level flow control mechanism (IEEE 802.1Qbb) that prevents packet loss due to buffer overflow by pausing traffic on a per-priority basis. PFC is essential for RoCE deployments because the InfiniBand transport (used by RoCE) was designed for a lossless fabric and performs poorly with packet loss. PFC sends PAUSE frames when receive buffer occupancy exceeds a threshold.

**P_Key (Partition Key)**
A 16-bit key used in InfiniBand to enforce partition-based access control, similar to VLANs in Ethernet. Each QP is associated with a P_Key, and communication is only allowed between QPs with matching P_Keys. The high bit indicates full or limited membership. Default P_Key is `0xFFFF`.

**PSN (Packet Sequence Number)**
A 24-bit sequence number in the BTH that identifies each packet within a QP's message stream. The responder uses the PSN to detect missing, duplicate, or out-of-order packets and to request retransmission. PSN wraps around at 2^24. The starting PSN for each QP is set during the RTR transition.

**QoS (Quality of Service)**
Mechanisms for differentiating traffic classes and providing performance guarantees (bandwidth, latency) to different applications or traffic types. In InfiniBand, QoS is implemented via Service Levels (SLs) mapped to Virtual Lanes (VLs). In RoCE, QoS uses DSCP/PCP values mapped to switch priority queues.

**QP (Queue Pair)**
The fundamental communication endpoint in RDMA, consisting of a Send Queue (SQ) and a Receive Queue (RQ). Each QP has a transport type (RC, UC, UD, XRC) that determines its capabilities: reliability, connection semantics, and supported operations. QPs are identified by a 24-bit QP Number (QPN) and undergo a state machine (RESET -> INIT -> RTR -> RTS) before use.

**QPN (Queue Pair Number)**
A 24-bit identifier that uniquely identifies a QP within an RDMA device. The QPN is exchanged between peers during connection setup and is included in the BTH of every packet to identify the destination QP. QPN 0 is reserved for the Subnet Management QP; QPN 1 is reserved for the General Services QP.

**R_Key (Remote Key)**
A 32-bit key associated with a registered Memory Region that authorizes remote access (RDMA Read, Write, or Atomic operations). The R_Key is shared with remote peers out of band (e.g., via Send/Receive or connection private data) and must be included in remote work requests. Possession of an R_Key grants access; it should be treated as a capability and shared only with trusted peers.

**RC (Reliable Connected)**
The most commonly used RDMA transport type, providing reliable, in-order delivery between a pair of connected QPs. RC supports all RDMA operations (Send, RDMA Read, RDMA Write, Atomics) and handles retransmission transparently. The main limitation is that each RC QP connects to exactly one remote QP, requiring O(n) QPs per node for full-mesh communication.

**RDMA (Remote Direct Memory Access)**
A technology that enables direct memory-to-memory data transfer between computers without involving the operating system or CPU of either machine in the data path. RDMA provides kernel bypass, zero-copy transfers, and hardware-managed transport, achieving low latency (sub-microsecond), high throughput (100+ Gbps), and minimal CPU overhead.

**RDMAP (RDMA Protocol)**
The top layer of the iWARP protocol stack (RFC 5040) that maps RDMA semantics (Send, RDMA Read, RDMA Write) onto the DDP layer. RDMAP provides the same programming abstraction as InfiniBand verbs but over a TCP/IP transport.

**RETH (RDMA Extended Transport Header)**
An InfiniBand packet header used in RDMA Read and Write operations that carries the remote virtual address, R_Key, and DMA length. The RETH enables the responder's HCA to perform the memory access without CPU involvement.

**RNIC (RDMA-capable Network Interface Card)**
A network adapter that implements RDMA protocols in hardware, specifically for Ethernet-based RDMA (RoCE or iWARP). The term RNIC is used to distinguish from InfiniBand HCAs, though both serve the same fundamental purpose.

**RNR NAK (Receiver Not Ready Negative Acknowledgment)**
A NAK sent when a message arrives at a QP that has no receive buffer posted. The sender retries after a delay specified by the `min_rnr_timer` value configured on the receiver's QP. Excessive RNR NAKs indicate the application is not posting receive buffers fast enough.

**RoCE (RDMA over Converged Ethernet)**
An RDMA transport protocol that runs over Ethernet networks. RoCEv1 uses a raw Ethertype and operates within a single L2 broadcast domain. RoCEv2 encapsulates the InfiniBand transport in UDP/IP packets, enabling L3 routing. RoCE requires a lossless Ethernet configuration (PFC) or tolerant transport (DCQCN + ECN) for good performance.

**RoCEv2 (RDMA over Converged Ethernet version 2)**
The routable version of RoCE that encapsulates InfiniBand BTH inside UDP/IP packets (destination port 4791). RoCEv2 uses GIDs mapped to IP addresses for addressing and enables RDMA over L3 routed networks. It is the dominant RDMA transport in modern data centers.

**RQ (Receive Queue)**
The receive side of a Queue Pair, holding posted receive work requests. Each receive WR describes buffers where incoming message data will be placed. For Send/Receive operations, the receiver must have a receive buffer posted before the sender's message arrives.

**SG (Scatter/Gather)**
A mechanism for specifying non-contiguous memory buffers in a single work request via a scatter/gather list (SGL). Each SGL entry specifies a memory address, length, and L_Key. On send, data is "gathered" from multiple buffers; on receive, data is "scattered" into multiple buffers.

**SL (Service Level)**
A 4-bit field in InfiniBand packets (carried in the LRH) that specifies the quality-of-service class. SLs are mapped to Virtual Lanes (VLs) by the Subnet Manager via SL-to-VL mapping tables. There are 16 possible SLs (0-15).

**SM (Subnet Manager)**
The entity responsible for configuring and managing an InfiniBand subnet. The SM assigns LIDs to ports, computes routing tables, configures switches, and manages partition membership. Every InfiniBand fabric requires at least one SM (typically running on a management node). OpenSM is the open-source SM implementation.

**SQ (Send Queue)**
The send side of a Queue Pair, holding posted send work requests. Send WRs describe the data to transmit and the RDMA operation type (Send, RDMA Read, RDMA Write, Atomic). The HCA processes WRs from the SQ in order and generates CQEs upon completion.

**SR-IOV (Single Root I/O Virtualization)**
A PCIe specification that allows a single physical NIC to present multiple virtual function (VF) interfaces to the host, each appearing as an independent device. SR-IOV enables hardware-level RDMA virtualization: each VM or container can receive a VF with direct hardware access, achieving near-native RDMA performance without software emulation.

**SRQ (Shared Receive Queue)**
A verbs object that provides a common pool of receive buffers shared among multiple QPs. SRQs reduce memory consumption in server applications with many connections by eliminating the need to provision per-QP receive buffers. When a message arrives on any QP associated with the SRQ, one of the SRQ's buffers is consumed.

**TLP (Transaction Layer Packet)**
A PCIe packet that carries data between a device (such as an RDMA NIC) and the host system. Understanding TLP sizes and PCIe link width/generation is important for RDMA performance tuning, as PCIe bandwidth can be a bottleneck, especially for small-message workloads.

**UC (Unreliable Connected)**
An RDMA transport type that provides connected (point-to-point) communication without reliability guarantees. UC supports Send and RDMA Write (but not RDMA Read or Atomics). Lost packets are silently dropped, and the application is responsible for detecting and handling message loss. UC is rarely used in practice.

**UCX (Unified Communication X)**
A communication framework providing a high-level API over multiple transports, including RDMA verbs, shared memory, and TCP. UCX automatically selects the best available transport and provides optimized implementations of point-to-point and collective communication patterns. It is widely used by MPI implementations (Open MPI, MPICH) and NCCL.

**UD (Unreliable Datagram)**
An RDMA transport type that provides connectionless, unreliable communication. A single UD QP can send to and receive from any other UD QP, making it highly scalable. However, UD is limited to send/receive operations (no RDMA Read/Write), has a per-message size limit of the path MTU, and provides no delivery guarantees. UD is used for management traffic, service discovery, and multicast.

**UAR (User Access Region)**
A hardware doorbell region mapped into user-space process memory via mmap. Writing to the UAR triggers the HCA to process new work queue entries. Each QP (or CQ) has an associated UAR page. BlueFlame doorbells are a special type of UAR write that embeds the WQE data.

**VCRC (Variant CRC)**
A 16-bit CRC at the end of every InfiniBand link-level packet that covers fields modified by switches (LRH fields). The VCRC provides hop-by-hop integrity checking, complementing the ICRC which provides end-to-end integrity.

**VDPA (Virtio Data Path Acceleration)**
A framework for offloading virtio device emulation to hardware, enabling near-native RDMA performance in virtualized environments. VDPA allows a physical NIC to present a virtio interface directly to a guest VM, combining the portability of virtio with hardware acceleration.

**VF (Virtual Function)**
A lightweight PCIe function created by SR-IOV that represents a virtual instance of the physical NIC. Each VF can be assigned to a VM or container, providing direct hardware access for RDMA operations. VFs share the physical NIC's resources (ports, bandwidth) but have independent QP spaces and protection domains.

**VL (Virtual Lane)**
A mechanism for creating independent logical communication channels over a single physical InfiniBand link. Each VL has its own flow control credits, enabling QoS differentiation and deadlock avoidance. InfiniBand supports up to 15 data VLs (VL0-VL14) plus one management VL (VL15). Service Levels (SLs) are mapped to VLs by the Subnet Manager.

**WQE (Work Queue Entry)**
A single entry in a Send Queue or Receive Queue describing a unit of work for the HCA to perform. For the SQ, a WQE specifies the operation type (Send, RDMA Write, etc.), the scatter/gather list, and flags. For the RQ, a WQE specifies the receive buffers. WQEs are written to host memory by the application and read by the HCA via DMA.

**WR (Work Request)**
The user-space representation of a work queue entry, submitted to a QP via `ibv_post_send()` or `ibv_post_recv()`. The verbs library translates the `ibv_send_wr` or `ibv_recv_wr` structure into the hardware-specific WQE format and writes it to the WQ buffer.

**XRC (Extended Reliable Connected)**
An RDMA transport type that combines the reliability of RC with improved scalability. XRC allows multiple QPs on the same node to share a single connection to a remote node, reducing the number of QPs needed for full-mesh communication from O(n * processes) to O(n). XRC uses an XRC Domain (XRCD) and SRQ for receive-side sharing.

**Zero-Copy**
A data transfer technique where data moves directly between application memory buffers and the network without being copied through intermediate kernel buffers. RDMA achieves zero-copy through memory registration and DMA: the HCA reads send data directly from and writes receive data directly to user-space memory, bypassing the kernel data path entirely.
