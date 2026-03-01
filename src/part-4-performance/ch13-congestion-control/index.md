# Chapter 13: Congestion Control

Congestion control is arguably the most critical and most challenging aspect of deploying RDMA over Ethernet (RoCEv2) at scale. While InfiniBand has built-in credit-based flow control that inherently prevents packet loss at the link level, Ethernet was designed as a lossy network where higher-layer protocols like TCP handle retransmission. Running RDMA -- which assumes lossless delivery -- over a fundamentally lossy infrastructure creates a tension that must be carefully managed.

The consequences of getting congestion control wrong are severe. When an RDMA NIC loses a packet on a standard Reliable Connection (RC) queue pair, it triggers a go-back-N retransmission: the sender must retransmit not just the lost packet, but every packet sent after it. At 100 Gbps, even a brief congestion event can trigger retransmission of megabytes of data, causing latency spikes measured in milliseconds and throughput collapse that can cascade across the entire fabric.

To prevent this, modern RoCEv2 deployments employ a layered congestion control architecture. **Priority Flow Control (PFC)** provides a link-level lossless guarantee by allowing switches to pause upstream senders when buffers fill. However, PFC is a blunt instrument: it creates head-of-line blocking, can trigger cascading pauses (PFC storms), and in pathological topologies can cause deadlocks. PFC should be viewed as a safety net, not a primary congestion management mechanism.

**Explicit Congestion Notification (ECN)** with the **DCQCN** algorithm provides the intelligence layer. Switches detect early signs of congestion and mark packets rather than dropping them. The receiver sends Congestion Notification Packets (CNPs) back to the sender, which then reduces its transmission rate. This end-to-end feedback loop resolves congestion before PFC activation is necessary, avoiding the cascading problems that PFC alone would cause.

The fabric design itself plays a critical role. Clos and leaf-spine topologies provide the non-blocking, multi-path architecture that RDMA traffic demands. Switch buffer sizing, ECMP load balancing, QoS configuration, and PFC watchdog settings all interact to determine whether the network can sustain RDMA traffic under load.

This chapter provides a comprehensive treatment of congestion control for RDMA networks. We begin with the fundamentals of lossless Ethernet and why it matters for RDMA. We then examine PFC in detail -- its mechanisms, configuration, and failure modes. Next, we cover ECN and the DCQCN algorithm, including tuning guidance. Finally, we discuss fabric design principles that create a network foundation capable of supporting RDMA at scale.

The material in this chapter is essential for network engineers deploying RoCEv2, but also valuable for application developers who need to understand the network behaviors that affect their RDMA applications' performance characteristics.
