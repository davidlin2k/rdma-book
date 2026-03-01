# 2.2 iWARP: RDMA Over TCP/IP

## The Motivation: RDMA Without Special Networks

By the early 2000s, InfiniBand had demonstrated that RDMA could deliver transformative performance for clustered applications. But InfiniBand required dedicated hardware, dedicated cabling, and dedicated network infrastructure separate from the Ethernet networks that already connected every server in every datacenter. For organizations that could not justify the cost and complexity of a parallel network---or for applications that needed RDMA semantics over wide-area links where InfiniBand's subnet model did not reach---a natural question arose: could RDMA be made to work over standard TCP/IP?

The answer was iWARP: the Internet Wide Area RDMA Protocol. Developed through the IETF (Internet Engineering Task Force) rather than a vendor consortium, iWARP was designed to layer RDMA semantics on top of the existing TCP/IP stack. The premise was appealing: every datacenter already had Ethernet switches, every server already had Ethernet NICs, and TCP was a well-understood, universally deployed reliable transport. If RDMA could be delivered over this infrastructure, the barrier to entry for RDMA adoption would collapse.

The reality proved more complex. iWARP achieved its goal of running RDMA over TCP/IP, but the protocol complexity required to reconcile RDMA's memory-semantic operations with TCP's byte-stream model produced a stack with significant overhead and implementation challenges. Understanding why requires examining each layer of the iWARP protocol suite.

## The Protocol Stack: Four Layers of Specification

iWARP is not a single protocol but a suite of four protocols, each specified in its own RFC, stacked on top of TCP. The IETF's Remote Direct Data Placement (RDDP) working group published the core specifications between 2007 and 2008.

### MPA: Marker PDU Aligned Framing (RFC 5044)

The lowest layer of iWARP solves a fundamental impedance mismatch between TCP and RDMA. TCP is a byte-stream protocol: it guarantees that bytes arrive in order, but it provides no concept of message boundaries. Data sent in one `write()` call may be delivered across multiple TCP segments, or data from multiple `write()` calls may be coalesced into a single segment. RDMA, by contrast, operates on discrete messages---each RDMA operation has a well-defined beginning and end, and the receiving hardware needs to know where each message starts so it can place the data directly into the correct destination buffer.

MPA bridges this gap by adding framing on top of TCP's byte stream. Each MPA frame (called a Framing Protocol Data Unit, or FPDU) is preceded by a header that contains the frame length, allowing the receiver to delineate message boundaries even when TCP delivers data in arbitrary chunks.

MPA also optionally inserts **markers** at fixed intervals within the TCP byte stream. Markers are small pointers (4 bytes each) placed every 512 bytes that point back to the start of the current FPDU. Their purpose is to help the receiver find FPDU boundaries quickly, even when starting to process data in the middle of a TCP segment. This is particularly important for hardware implementations that must begin processing data as it arrives from the wire, without buffering entire TCP segments.

Additionally, MPA includes CRC-32c checksums for each FPDU, providing end-to-end data integrity verification independent of any lower-layer checksums. This was a deliberate design choice: RDMA places data directly into application memory, bypassing the usual kernel networking stack where corrupted data would typically be caught and discarded. A silent data corruption that reaches application memory could have catastrophic consequences, so iWARP adds its own integrity check.

<div class="note">

The marker mechanism in MPA is one of the protocol's most criticized features. It adds complexity to both sender and receiver implementations, consumes bandwidth (roughly 0.8% overhead at the default 512-byte interval), and interacts poorly with TCP optimizations like segmentation offload. MPA-Rev1 made markers optional for the receiver to simplify implementations, and most modern iWARP NICs negotiate to disable markers when both sides support it.

</div>

### DDP: Direct Data Placement Protocol (RFC 5041)

DDP sits above MPA and provides the mechanism for placing incoming data directly into a specified location in the receiver's memory, without intermediate buffering. This is the layer that actually delivers the "zero-copy" promise of RDMA.

DDP defines two models for identifying destination buffers:

**Tagged buffers** are identified by a Steering Tag (STag), an offset, and a length. The STag is a handle that the receiver has previously registered with its hardware, mapping to a specific region of memory. When a DDP segment arrives with a tagged buffer designation, the receiving NIC uses the STag to look up the physical memory pages and the offset to determine exactly where within that region to place the data. This model is used for RDMA Write and RDMA Read operations, where the sender specifies where the data should land in the remote machine's memory.

**Untagged buffers** use a queue-based model where the receiver has pre-posted a set of receive buffers, and incoming data is placed into the next available buffer in queue order. This model is used for Send/Receive operations, where the receiver controls which buffer receives the data by the order in which buffers were posted.

DDP also handles segmentation: a single RDMA operation may be larger than a single MPA frame (and thus a single TCP segment), so DDP breaks large transfers into multiple DDP segments, each carrying a portion of the data along with placement information (the STag and offset for tagged buffers, or the queue number and buffer index for untagged buffers).

### RDMAP: RDMA Protocol (RFC 5040)

RDMAP is the top layer of the iWARP stack and maps RDMA semantics---Send, Receive, RDMA Write, RDMA Read---onto DDP's data placement primitives. RDMAP defines the message types, the request/response protocols for RDMA Read (which requires a request from the initiator and a response from the target), and the error handling and connection termination procedures.

RDMAP also defines the completion model: when an RDMA operation completes, the application receives a completion event through the same Completion Queue mechanism used by InfiniBand. From the application's perspective, the Verbs API looks the same whether the underlying transport is InfiniBand, iWARP, or RoCE---RDMAP provides the semantic mapping that makes this possible.

### RDMA Security (RFC 5042) and MPA Negotiation (RFC 5043)

Two additional RFCs round out the iWARP suite. RFC 5042 specifies the security model for iWARP, particularly the rules for STag management. Since STags allow a remote machine to read or write the local machine's memory, their creation, distribution, and invalidation must be carefully controlled to prevent unauthorized memory access. RFC 5043 specifies the MPA negotiation mechanism used during connection establishment to agree on parameters like marker usage, CRC usage, and RDMA mode.

## The Full Picture: iWARP in Context

The complete iWARP protocol stack, from application to wire, looks like this:

| Layer            | Protocol | Function                                        |
|------------------|----------|-------------------------------------------------|
| Application      | Verbs API| RDMA operations: Send, Write, Read              |
| RDMA Protocol    | RDMAP    | Maps RDMA semantics to DDP                      |
| Data Placement   | DDP      | Places data into destination buffers             |
| Framing          | MPA      | Message boundaries over TCP byte stream          |
| Reliable Transport| TCP     | Ordered, reliable byte delivery                  |
| Network          | IP       | Routing                                          |
| Link             | Ethernet | Frame delivery                                   |

Each layer adds headers and potentially trailers, and each layer introduces processing that must be performed either in hardware (for a hardware iWARP implementation) or in software (for a software fallback). The cumulative header overhead and protocol complexity is significantly greater than InfiniBand or RoCE, where the RDMA transport layer sits directly above the link layer without the intervening TCP and MPA layers.

## Advantages of iWARP

Despite its complexity, iWARP offers several genuine advantages:

**Works over existing Ethernet infrastructure.** No special switches, no special cabling, no lossless Ethernet configuration. Standard Ethernet switches forward iWARP traffic exactly as they forward any other TCP traffic. This is perhaps iWARP's single most important advantage: it eliminates the network infrastructure barrier to RDMA adoption.

**Routable across IP networks.** Because iWARP runs over standard TCP/IP, it can traverse routers, cross subnet boundaries, and even (in principle) operate over wide-area networks. InfiniBand is confined to a single subnet, and RoCE v1 is confined to a single Layer 2 domain.

**TCP flow control and congestion management.** TCP's well-understood congestion control algorithms (Reno, CUBIC, BBR, and others) manage bandwidth sharing and congestion avoidance automatically. This is a significant operational advantage over RoCE, which requires the network to be configured for lossless operation (PFC/ECN) to avoid packet drops that would trigger expensive retransmissions.

**Graceful degradation under loss.** Because TCP handles retransmission transparently, iWARP connections survive packet loss without application-visible errors. The performance degrades (as it does for any TCP connection experiencing loss), but the connection does not break. RoCE connections, by contrast, can enter pathological states when packets are lost.

## Disadvantages and Challenges

**TCP processing overhead.** TCP is a complex protocol: it maintains per-connection state, computes checksums, manages sliding windows, handles retransmissions, and processes acknowledgments. Even when offloaded to hardware, the TCP state machine consumes silicon area and introduces latency. The end-to-end latency of iWARP is typically 2--5 microseconds higher than InfiniBand or RoCE at comparable link speeds, and the connection setup time is significantly longer due to TCP's three-way handshake.

**Connection scalability.** Each iWARP connection requires a full TCP connection, including dedicated state in the NIC's hardware. TCP Offload Engines (TOEs) that manage this state in hardware have limited connection tables---typically in the tens of thousands to low hundreds of thousands of connections. For applications that require millions of connections (such as large storage clusters), this is a serious constraint.

**MPA complexity.** The marker and framing layer adds protocol complexity that has historically led to interoperability issues between different vendors' implementations. The interaction between MPA framing and TCP segmentation offload (TSO) and large receive offload (LRO) in the NIC hardware is particularly tricky to implement correctly.

**Connection setup latency.** Establishing an iWARP connection requires a TCP three-way handshake followed by MPA negotiation, followed by RDMA connection setup. This multi-step process takes milliseconds, compared to the microsecond-scale connection setup of InfiniBand. For workloads that create and tear down connections frequently, this overhead is significant.

<div class="warning">

A subtle but important limitation of iWARP: because TCP guarantees in-order delivery, the NIC's hardware must handle out-of-order TCP segment arrival. This requires either large reorder buffers in the NIC (consuming expensive on-chip memory) or falling back to software processing for out-of-order segments. This "out-of-order" problem has been a persistent engineering challenge for iWARP NIC designers.

</div>

## Key Vendors

**Chelsio Communications** has been the most persistent and successful iWARP vendor. Chelsio's Terminator series of Ethernet controllers (T4, T5, T6, T7) include full hardware iWARP offload along with TCP offload, and are the most widely deployed iWARP adapters. Chelsio has consistently argued that iWARP's ability to work over standard Ethernet without lossless configuration gives it an operational advantage over RoCE, particularly in environments where network teams are unwilling or unable to deploy PFC and ECN.

**Intel** supported iWARP in several generations of its Ethernet controllers, notably the X722 and early E810 series. However, Intel's RDMA strategy has shifted over time, and more recent Intel Ethernet products have added RoCE support alongside or instead of iWARP. Intel's acquisition of QLogic's InfiniBand business and subsequent development of Omni-Path further complicated its RDMA messaging.

## Current State and Market Position

iWARP occupies a small but defensible niche in the RDMA market. It is the right choice when RDMA is needed over an existing Ethernet network that cannot be configured for lossless operation---for example, in environments where the network team manages the switches as shared infrastructure and will not enable PFC for a single application. It also remains relevant for RDMA over routed or wide-area networks, though this use case is uncommon in practice.

However, the market has moved decisively toward RoCE v2 for new datacenter RDMA deployments. The combination of RoCE's lower latency, simpler protocol stack, and the willingness of hyperscale operators to invest in lossless Ethernet configuration has relegated iWARP to a secondary role. Microsoft's Azure initially used iWARP for its RDMA-based storage infrastructure but later transitioned to RoCE v2, a move that was widely seen as a bellwether for the broader market.

iWARP's IETF-based standardization---once seen as an advantage over InfiniBand's vendor-consortium model---also proved to be a double-edged sword. The multi-year IETF process produced a technically rigorous specification, but by the time the RFCs were published in 2007, RoCE was already under development and would arrive in 2010 with a far simpler protocol stack and backing from the dominant RDMA hardware vendor.

Despite its reduced market share, iWARP remains a valuable option in the RDMA toolkit. Understanding its architecture is important both for practitioners who encounter it in existing deployments and for appreciating the design trade-offs that led the industry to develop RoCE as an alternative approach to the same problem.
