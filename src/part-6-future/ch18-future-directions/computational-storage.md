# 18.4 Computational Storage and Beyond

The boundaries between computing, storage, and networking continue to blur. This section surveys the technologies and research directions that extend beyond traditional RDMA, pointing toward a future where data processing happens wherever it is most efficient -- in the network, at the storage device, or across a unified global memory fabric.

## Computational Storage

Computational storage moves processing to where the data resides, rather than moving data to where the processing occurs. The idea is straightforward: if you need to scan a 10 TB dataset to find records matching a predicate, it is far more efficient to run the predicate check at the storage device and return only matching records than to transfer all 10 TB across the network and storage bus to the CPU.

### Architecture

A computational storage device combines storage media (NAND flash, typically) with an embedded processor (FPGA or ARM cores) capable of executing simple computations:

```text
Traditional Query Execution:
  Storage → PCIe → System Memory → CPU (filter) → Result
  (10 TB transferred, 10 MB result)

Computational Storage:
  Storage → Embedded Processor (filter) → PCIe → Result
  (10 MB transferred)
```

### RDMA for Computational Storage Access

RDMA provides a natural interface for accessing computational storage devices across the network. A client can issue an RDMA operation that encodes a computation request, and the target (a computational storage device exposed via NVMe-oF or a custom RDMA service) performs the computation and returns the result.

This pattern already exists in rudimentary form in NVMe-oF: a remote NVMe Read command is essentially a computation (fetch data from flash, perform error correction, format into block) offloaded to the storage device and accessed via RDMA. Extending this to more complex computations (filtering, aggregation, compression) is a natural evolution.

<div class="note">

**Note:** Computational storage is standardized by SNIA (Storage Networking Industry Association) through the Computational Storage Architecture and Programming Model specification. However, the interface between RDMA and computational storage functions is not yet standardized, and current implementations are vendor-specific.

</div>

## P4-Programmable Switches: In-Network Computing

P4 is a domain-specific language for programming the data plane of network switches. A P4-programmable switch can execute custom packet processing logic at line rate -- billions of packets per second -- enabling computation to happen inside the network itself.

### How P4 Switches Work

A traditional fixed-function switch has a hardcoded pipeline: parse packet headers, look up the destination in a forwarding table, modify headers if needed, and forward. A P4-programmable switch (such as those based on the Intel Tofino ASIC) lets you define:

1. **Parser:** What headers to extract from packets (including custom headers).
2. **Match-action tables:** Rules that match on header fields and execute actions.
3. **Deparser:** How to reconstruct the packet for forwarding.

```p4
// Simplified P4 example: in-network counter
header rdma_custom_t {
    bit<32> key;
    bit<64> value;
}

control IngressPipeline(inout headers_t hdr, ...) {
    register<bit<64>>(65536) counters;

    action increment_counter() {
        bit<64> current;
        counters.read(current, hdr.custom.key);
        current = current + hdr.custom.value;
        counters.write(hdr.custom.key, current);
    }

    table counter_table {
        key = { hdr.custom.key: exact; }
        actions = { increment_counter; }
    }

    apply { counter_table.apply(); }
}
```

### Implications for RDMA

P4 switches can process RDMA traffic in transit, enabling operations that would otherwise require endpoint processing:

- **Traffic monitoring:** Deep inspection of RoCE headers (BTH, RETH) without terminating the RDMA connection.
- **Load balancing:** Intelligent distribution of RDMA flows across multiple paths based on QP state, not just hash-based ECMP.
- **Access control:** Per-flow or per-QP filtering of RDMA traffic in the network.

## In-Network Aggregation

In-network aggregation performs data reduction operations (sum, min, max, average) inside the network switches, reducing the amount of data that endpoints must process.

### NVIDIA SHArP (Scalable Hierarchical Aggregation and Reduction Protocol)

SHArP is the most mature in-network aggregation technology for RDMA networks. It is built into NVIDIA (Mellanox) InfiniBand switches and used extensively for distributed machine learning.

```text
Traditional AllReduce (Ring):
  GPU 0 ──gradient──► GPU 1 ──gradient──► GPU 2 ──gradient──► GPU 3
  (N-1 steps, each transferring full gradient)

SHArP In-Network AllReduce:
  GPU 0 ──gradient──┐
  GPU 1 ──gradient──┤
                    ├── Switch aggregates ──► All GPUs receive result
  GPU 2 ──gradient──┤
  GPU 3 ──gradient──┘
  (1 step up + 1 step down, reduced data movement)
```

SHArP reduces AllReduce latency from O(N) to O(log N) in the number of participants, and reduces total data movement by performing partial aggregations at each level of the switch hierarchy. For large-scale distributed training (hundreds or thousands of GPUs), this translates to significant training time reduction.

```bash
# Enable SHArP in NCCL
export NCCL_NET_PLUGIN=sharp
export SHARP_COLL_LOG_LEVEL=3
```

### NetReduce and Research Prototypes

Academic research has produced several in-network aggregation systems:

- **NetReduce:** Performs floating-point aggregation on P4 switches using integer arithmetic approximations.
- **SwitchML:** Implements ML-specific aggregation (gradient compression, quantization) in the switch data plane.
- **ATP (In-Network Aggregation for Shared Machine Learning Clusters):** Dynamically schedules aggregation across available switch resources.

These systems demonstrate that in-network computing can provide order-of-magnitude improvements for specific workloads, but they require careful integration with RDMA transport and are limited by switch memory and processing capabilities.

## Key-Value Stores in the Switch/NIC

Research has explored implementing key-value stores entirely within the network -- either in programmable switches or in SmartNIC processing units.

**NetCache** (2017) implemented a key-value cache in a P4 switch, handling hot-key queries at switch line rate (billions of lookups per second). The switch maintains a small cache of frequently accessed keys and serves queries directly, forwarding cache misses to backend servers.

**NIC-based KV stores** use the SmartNIC/DPU's ARM cores or FPGA logic to implement a key-value store that responds to RDMA requests without involving the host CPU. This provides:
- Sub-microsecond read latency (no host CPU in the path).
- Predictable tail latency (no OS scheduling jitter).
- Host CPU savings for compute-intensive workloads.

<div class="tip">

**Tip:** In-network key-value stores are most effective for read-heavy, hot-key workloads where a small number of keys account for a large fraction of accesses. For write-heavy or uniformly distributed workloads, the limited memory in switches and NICs makes them impractical as primary stores, though they remain useful as caches.

</div>

## Consensus in the Network

Distributed consensus protocols (Paxos, Raft) are fundamental to replicated systems but add latency to every write operation. Research has explored implementing consensus logic inside the network:

**NetPaxos** (2015) demonstrated that a P4 switch can act as a Paxos acceptor, processing consensus messages at line rate. By placing the consensus logic in the switch, the latency of the consensus round is reduced to the network round-trip time plus switch processing time, eliminating the server-side processing delay.

**NOPaxos** (Network-Ordered Paxos) uses the network to provide total ordering of messages, simplifying the consensus protocol. A sequencer in the network assigns sequence numbers to all messages, and replicas can agree on the order without running a full consensus round for each message.

These approaches use RDMA or RDMA-like transports for the data path while leveraging programmable network elements for the protocol logic.

## Post-RDMA Networking: What Comes After?

RDMA, for all its performance advantages, has well-known limitations:

- **Connection-oriented scaling:** RC QPs require per-connection state on the NIC, limiting the number of concurrent connections.
- **Limited operation set:** The fixed set of RDMA operations (Read, Write, Send, Atomic) cannot be extended without hardware changes.
- **Reliability overhead:** Go-back-N retransmission in hardware is inefficient for lossy networks.
- **Security model:** Trust-the-endpoint does not fit multi-tenant clouds.

Several efforts are working on "what comes after" RDMA.

## Ultra Ethernet Consortium

The Ultra Ethernet Consortium (UEC), founded in 2023 by AMD, Broadcom, Cisco, Intel, Meta, Microsoft, and others, is developing a next-generation Ethernet transport protocol designed for AI and HPC workloads. The UEC aims to provide:

- **Packet spraying with reordering:** Spread traffic across all available paths (not just ECMP hashing) and reassemble in order at the receiver. This dramatically improves bandwidth utilization.
- **Improved congestion control:** Hardware-based congestion control designed for the bursty, all-to-all traffic patterns of AI training.
- **Multipath reliability:** If one path fails, traffic is seamlessly rerouted to another without application-visible errors.
- **Scalable connection model:** Reduce per-connection state on the NIC to support millions of concurrent flows.
- **Built-in security:** Encryption and authentication as first-class protocol features.

```text
RDMA/RoCE (current):
  Packet → ECMP hash → Single path → Destination
  (head-of-line blocking if path is congested)

UEC (future):
  Packet → Spray across all paths → Reorder at destination
  (utilizes all available bandwidth)
```

<div class="note">

**Note:** The UEC transport protocol is complementary to RDMA verbs, not a replacement. The goal is to provide a better transport layer (replacing the RoCE/IB transport) while potentially maintaining compatibility with the verbs programming interface. Applications using libibverbs may be able to use UEC transport without code changes.

</div>

## Unified Memory Semantics

The ultimate vision of memory-centric computing is a single global address space where any processor can access any memory location, regardless of where it is physically located. The distinction between local memory, remote memory, GPU memory, storage, and NIC memory dissolves -- all memory is simply "memory," accessible through load/store instructions with hardware-managed coherence and transport.

This vision requires the convergence of several technologies:

1. **CXL** provides cache-coherent access to nearby (intra-rack) memory.
2. **RDMA** (or its successor) provides access to remote (inter-rack) memory.
3. **GPUDirect** provides access to accelerator memory.
4. **Computational storage** provides access to storage-attached memory.
5. **A unified software layer** abstracts all of these behind a single interface.

Linux's Heterogeneous Memory Management (HMM) framework is an early step in this direction, providing a unified interface for managing memory across CPUs, GPUs, and other devices. The ongoing development of CXL, RDMA, and accelerator interconnects is gradually building the hardware foundation.

```text
Today's Architecture:
  CPU Memory ←── CPU API (malloc)
  GPU Memory ←── GPU API (cudaMalloc)
  Remote Memory ←── RDMA API (ibv_reg_mr)
  Storage ←── File API (read/write)
  (4 different APIs, 4 different memory spaces)

Future Vision:
  Global Address Space ←── Unified API (load/store everywhere)
  (hardware manages location, coherence, transport)
```

## The Long-Term Vision

The trajectory of high-performance computing points toward a world where:

- **Memory is disaggregated:** Compute and memory are separate, composable resources allocated dynamically based on workload needs.
- **Processing is distributed:** Computation happens at the optimal location -- CPU, GPU, NIC, switch, or storage device -- determined by data locality and processing requirements.
- **Transport is transparent:** Applications express data access patterns, and the hardware/runtime selects the optimal transport (CXL, RDMA, or their successors) based on distance, latency, and bandwidth requirements.
- **Security is built in:** Encryption, authentication, and access control are integral to the transport protocol, not afterthoughts bolted on top.

RDMA, as we know it today, is a milestone on this path -- the first widely deployed technology that proved direct memory access across a network could be practical, performant, and useful. The concepts it pioneered -- kernel bypass, zero-copy, hardware-offloaded protocols, and one-sided memory access -- will persist even as the specific implementations evolve. The engineer who understands RDMA deeply is well-positioned to work with whatever comes next, because the fundamental trade-offs -- latency versus throughput, hardware versus software, generality versus performance -- remain the same.

What changes is the scale of ambition. RDMA made it practical to access memory on another server as if it were almost local. The technologies surveyed in this chapter are working toward making it practical to access any memory, anywhere, with the performance characteristics appropriate to the physical distance and the application's needs. That is a worthy goal, and it is closer today than it has ever been.
