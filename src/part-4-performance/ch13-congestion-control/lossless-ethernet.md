# 13.1 Lossless Ethernet Fundamentals

RDMA protocols were designed with lossless networks in mind. InfiniBand, the original RDMA transport, guarantees lossless delivery at the link level through credit-based flow control. When RDMA was extended to Ethernet via RoCEv2, a fundamental mismatch emerged: Ethernet is inherently a lossy network, and RDMA's retransmission behavior makes it catastrophically sensitive to packet loss. This section explains why lossless delivery matters so much for RDMA and how Ethernet can be adapted to provide it.

## Why RDMA Needs Lossless Delivery

### The Go-Back-N Problem

The Reliable Connection (RC) transport -- the most widely used RDMA transport -- uses a **go-back-N** retransmission strategy. When the sender detects a lost packet (through a timeout or a NAK), it must retransmit the lost packet **and every subsequent packet** that was already transmitted.

Consider a sender transmitting at 100 Gbps with a round-trip time (RTT) of 10 microseconds. In the time between sending the lost packet and detecting the loss:

$$data\_in\_flight = BW \times RTT = 100 \text{ Gbps} \times 10 \text{ us} = 1 \text{ Mbit} = 125 \text{ KB}$$

All 125 KB of in-flight data must be retransmitted, even if only a single 64-byte packet was lost. At 400 Gbps, this grows to 500 KB per loss event.

But the damage extends beyond the retransmission itself:

1. **Throughput collapse**: During retransmission, the QP's effective throughput drops to zero for the duration of the timeout (typically 8--65 ms for the initial retry)
2. **Cascade effect**: Retransmitted packets consume bandwidth, potentially causing congestion for other flows, leading to more losses
3. **Latency spikes**: A single loss event adds milliseconds of latency to a path that normally delivers microsecond latency
4. **Application impact**: For storage systems, a single retransmission timeout can trigger I/O deadline failures; for MPI jobs, one slow rank stalls all ranks

### Quantifying the Impact

A simple model illustrates the severity. Assume a flow operating at 100 Gbps with a loss rate of $p$:

$$BW_{effective} = BW_{link} \times (1 - p)^{N}$$

Where $N$ is the number of packets in flight. For $N = 1000$ packets and $p = 10^{-6}$ (one in a million):

$$BW_{effective} = 100 \times (1 - 10^{-6})^{1000} \approx 99.9 \text{ Gbps}$$

This seems acceptable. But with go-back-N, the cost of each loss event is not one packet but $N/2$ packets on average. The effective throughput including retransmission becomes:

$$BW_{effective} \approx BW_{link} \times \frac{1}{1 + p \times N / 2}$$

For $p = 10^{-4}$ (modest congestion):

$$BW_{effective} \approx 100 \times \frac{1}{1 + 10^{-4} \times 500} = 100 \times \frac{1}{1.05} \approx 95.2 \text{ Gbps}$$

And the tail latency impact is far worse than the throughput numbers suggest, because each loss event introduces a discrete delay of $t_{retry\_timeout}$.

## InfiniBand: Credit-Based Flow Control

InfiniBand solves the lossless problem at the link level with **credit-based flow control**. Before a sender can transmit, it must have credits from the receiver. Credits represent available buffer space at the receiver.

### How Credits Work

1. When a link is initialized, the receiver advertises its available buffer space as initial credits
2. The sender decrements its credit count for each packet sent
3. The receiver processes packets and returns credits as buffer space frees up
4. If the sender's credit count reaches zero, it stops transmitting -- no packets are ever dropped due to buffer overflow

```
Sender                        Receiver
  |                              |
  |  <--- Initial credits: 16   |
  |                              |
  |  Packet 1 (credits: 15) --> |
  |  Packet 2 (credits: 14) --> |
  |  ...                        |
  |  Packet 16 (credits: 0) --> |
  |  [BLOCKED - no credits]     |
  |                              |
  |  <--- Credit return: +8     |  (receiver processed 8 packets)
  |  Packet 17 (credits: 7) --> |
  |  ...
```

### Virtual Lanes and Credit Isolation

InfiniBand supports up to 16 Virtual Lanes (VLs) per port. Each VL has its own independent credit pool. This provides:

- **Traffic isolation**: Congestion on one VL does not affect others
- **Deadlock avoidance**: VL15 is reserved for management traffic and is never subject to flow control
- **QoS**: Different service levels map to different VLs with different credit allocations

Credit-based flow control is simple, effective, and deadlock-free (when VLs are properly assigned). It is the gold standard for lossless networking. The challenge with RoCEv2 is that Ethernet does not have this mechanism.

## Ethernet: A Lossy Heritage

Ethernet was designed in the 1970s as a shared medium with collision detection (CSMA/CD). Its fundamental assumption is that frames may be lost -- due to collisions, buffer overflows, or bit errors -- and that higher-layer protocols will handle retransmission.

### The TCP Model

TCP, the dominant transport protocol on Ethernet, handles loss gracefully:

- **Selective retransmission**: TCP retransmits only the lost segments (selective acknowledgment, SACK)
- **Congestion window**: TCP dynamically adjusts its sending rate based on loss signals
- **Software implementation**: Retransmission logic runs in the OS kernel, where it has full visibility into connection state

This model works well for TCP because:
1. Retransmission is selective (not go-back-N)
2. The congestion control algorithm prevents persistent congestion
3. Software retransmission, while adding latency, is functionally correct

### Why TCP's Model Does Not Work for RDMA

RDMA's kernel-bypass architecture means that retransmission logic runs in NIC hardware, where it must be simple and fast. Implementing selective retransmission in hardware is complex and requires significant buffer space on the NIC. Most RDMA NICs use go-back-N because it is simpler to implement in hardware.

Furthermore, RDMA's performance promise -- microsecond latency, zero CPU overhead -- is fundamentally incompatible with loss. A single retransmission event negates the latency advantage that RDMA provides over TCP. If an application tolerates loss-induced latency spikes, it might as well use TCP and avoid the complexity of RDMA deployment.

## Making Ethernet Lossless

The IEEE Data Center Bridging (DCB) standards provide the mechanisms to make Ethernet lossless:

| Standard | Name | Purpose |
|----------|------|---------|
| 802.1Qbb | Priority Flow Control (PFC) | Per-priority pause frames |
| 802.1Qaz | Enhanced Transmission Selection (ETS) | Bandwidth allocation per priority |
| 802.1AB | LLDP-DCBX | DCB capability exchange |

PFC is the cornerstone. It allows a receiver to send a PAUSE frame for a specific priority class, stopping the sender from transmitting frames of that priority. This prevents buffer overflow and hence packet loss.

However, PFC introduces its own problems:

- **Head-of-line blocking**: Pausing one flow blocks all flows on the same priority
- **PFC storms**: Cascading pauses can propagate across the entire fabric
- **PFC deadlocks**: Circular buffer dependencies can halt traffic permanently
- **Unfairness**: Some flows may be disproportionately paused ("victim flows")

These problems are so severe that PFC alone is insufficient. Modern RoCEv2 deployments use PFC as a **safety net** combined with ECN/DCQCN as the primary congestion control mechanism. The goal is to resolve congestion before PFC is ever triggered. Sections 13.2 and 13.3 cover these mechanisms in detail.

## The Fundamental Tension

There is an inherent tension in running lossless traffic over a shared Ethernet fabric:

- **Lossless** requires that no packets are ever dropped due to congestion
- **Shared** means multiple flows compete for the same resources
- **Congestion** is inevitable when multiple flows converge on the same output port

The resolution is a layered approach:

1. **ECN/DCQCN** detects congestion early and signals senders to reduce rate (prevents congestion from building)
2. **PFC** provides a hard backstop when ECN cannot react fast enough (prevents loss)
3. **Fabric design** provides sufficient bandwidth and path diversity to minimize congestion (reduces the need for either mechanism)

## Selective Retransmission: The Alternative

Some modern RDMA NICs (ConnectX-6 Dx and later) support **selective retransmission** for RC transport, where only the actually lost packet is retransmitted rather than all subsequent packets. This dramatically reduces the cost of individual loss events:

- Go-back-N: retransmit $N$ packets per loss ($N$ = number of in-flight packets)
- Selective repeat: retransmit 1 packet per loss

Selective retransmission reduces the penalty of loss but does not eliminate it. Each loss still triggers a retransmission timeout or NAK, adding latency. Lossless operation remains the goal for optimal performance.

## iWARP: A Different Approach

iWARP (Internet Wide Area RDMA Protocol) takes a fundamentally different approach by running RDMA over TCP. Because TCP handles loss through its own retransmission and congestion control mechanisms, iWARP does not require lossless Ethernet:

- **No PFC needed**: TCP's congestion control prevents persistent congestion
- **No special switch configuration**: Works on any Ethernet fabric
- **Trade-off**: Higher latency than RoCEv2 due to TCP processing overhead

iWARP is covered in detail in the transport protocols chapter. For high-performance data center deployments, RoCEv2 with proper congestion control is the dominant choice, but iWARP remains a viable option for environments where lossless Ethernet configuration is impractical.

<div class="note">

**Key takeaway**: RDMA's sensitivity to packet loss is not a bug -- it is a fundamental consequence of the kernel-bypass, hardware-offloaded architecture that enables microsecond latency. The engineering challenge is to provide a lossless environment through a combination of PFC, ECN/DCQCN, and proper fabric design.

</div>
