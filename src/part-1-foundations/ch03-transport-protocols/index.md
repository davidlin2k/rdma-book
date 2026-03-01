# Chapter 3: Transport Protocols

RDMA is not a single protocol but a programming model that can ride on top of three distinct transport families, each born from different engineering traditions and optimized for different deployment realities. Understanding the transport layer is not optional knowledge for the RDMA practitioner --- it determines how you configure your network, what failure modes you must handle, what performance characteristics you can expect, and what hardware you need to procure. This chapter dissects all three.

## The Three Transport Families

**InfiniBand (IB)** is the original and remains the gold standard for raw RDMA performance. It defines a complete network architecture from the physical layer through the transport layer, with its own switching infrastructure, addressing scheme, and management model. InfiniBand networks are purpose-built for RDMA: every component in the data path --- from the Host Channel Adapter (HCA) to each switch ASIC --- understands the protocol natively. This vertical integration yields the lowest latencies (sub-microsecond) and the highest bandwidths (up to 400 Gbps per port with NDR, and 800 Gbps with XDR on the horizon). The cost is a dedicated fabric that is separate from the Ethernet network most organizations already operate.

**RDMA over Converged Ethernet (RoCE)** carries InfiniBand transport packets over standard Ethernet infrastructure. RoCE v1, specified in the InfiniBand Architecture Annex, places IB packets directly inside Ethernet frames, limiting communication to a single Layer 2 broadcast domain. RoCE v2 wraps the same transport headers inside UDP/IP, enabling Layer 3 routing and compatibility with standard Ethernet switches and routers. RoCE has become the dominant RDMA transport in hyperscale data centers --- AWS, Azure, Google Cloud, and Oracle Cloud all deploy RoCE v2 at massive scale. The challenge is that RoCE inherits InfiniBand's assumption of a lossless fabric, which Ethernet does not natively provide. Making Ethernet behave losslessly requires Priority Flow Control (PFC) and, in practice, explicit congestion notification (ECN) paired with algorithms like DCQCN.

**Internet Wide Area RDMA Protocol (iWARP)** takes the opposite approach: rather than demanding a lossless network, it layers RDMA semantics on top of TCP/IP, which already handles loss recovery, reordering, and congestion control. The iWARP stack comprises three sub-protocols --- MPA for framing, DDP for data placement, and RDMAP for RDMA operations --- all riding on a standard TCP connection. iWARP works over any IP network without special switch configuration, making it the easiest RDMA transport to deploy. The tradeoff is higher latency and lower throughput compared to InfiniBand and RoCE, primarily because TCP's in-order delivery requirement limits the hardware's ability to process packets independently.

## Why Protocol Details Matter

A common misconception is that the `libibverbs` API abstracts away the transport, and application developers need not care which protocol runs underneath. This is true at the API level --- the same `ibv_post_send()` call works on IB, RoCE, and iWARP --- but false at every other level. The transport protocol dictates:

- **Address resolution**: IB uses path records from the Subnet Manager; RoCE v2 uses ARP/ND and GID tables; iWARP uses standard TCP connection establishment.
- **Connection management**: IB and RoCE use the Communication Manager (CM) protocol; iWARP uses TCP's three-way handshake augmented by MPA negotiation.
- **Flow control and congestion**: IB has credit-based flow control built into the link layer; RoCE depends on PFC and ECN; iWARP inherits TCP's congestion window.
- **Failure semantics**: A single PFC pause storm on a RoCE network can deadlock an entire VLAN. An IB credit stall is contained to a single virtual lane. A TCP retransmission timeout on iWARP stalls one connection.
- **MTU considerations**: IB supports 256, 512, 1024, 2048, and 4096-byte MTUs (negotiated per path). RoCE uses Ethernet MTU (typically 1500 or 9000 bytes). iWARP uses TCP MSS.

The sections that follow examine each transport family in depth, then compare packet formats side by side, and conclude with a decision framework for choosing among them.
