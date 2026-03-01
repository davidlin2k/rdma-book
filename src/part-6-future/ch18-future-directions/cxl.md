# 18.1 CXL and Memory-Centric Computing

Compute Express Link (CXL) is a cache-coherent interconnect built on top of the PCIe physical layer. It represents a fundamentally different approach to remote memory access than RDMA, operating at lower latency, with hardware-managed coherence, and over shorter distances. Understanding CXL is essential for anyone working with RDMA because the two technologies address overlapping use cases and will increasingly coexist in data center architectures.

## What Is CXL?

CXL is an open industry standard, first published in 2019 by a consortium including Intel, AMD, ARM, Samsung, and others. It leverages the PCIe electrical specification (the same cables, connectors, and signal integrity) but defines new protocols on top of it that enable capabilities PCIe alone cannot provide.

CXL defines three protocols, each addressing a different interaction pattern:

### CXL.io: Standard I/O

CXL.io is functionally equivalent to PCIe. It provides the same register access, DMA, and interrupt capabilities that PCIe devices use today. This ensures backward compatibility -- a CXL device can operate as a standard PCIe device on a system that does not support CXL.

### CXL.cache: Device-to-Host Coherence

CXL.cache allows a device (such as an accelerator or SmartNIC) to cache host memory with full hardware coherence. The device can read and write host memory, and the CXL protocol ensures that the device's cache stays consistent with the host CPU's caches -- without any software intervention.

This is transformative for accelerator design. Without CXL.cache, a GPU or FPGA that needs to access host memory must either go through DMA (high latency, software-managed) or use complex software coherence protocols. With CXL.cache, the accelerator can read host memory as if it were a local CPU, with hardware handling all the cache invalidation and synchronization.

### CXL.mem: Memory Expansion

CXL.mem is the protocol most relevant to RDMA practitioners. It allows a host CPU to access memory attached to a remote device as if it were local DRAM. The CPU's memory controller speaks CXL.mem to a CXL memory expander or memory pooling device, and the remote memory appears in the system's physical address space.

```text
Traditional Memory Architecture:
  CPU ──── DDR5 ──── Local DRAM

CXL.mem Memory Expansion:
  CPU ──── DDR5 ──── Local DRAM
   │
   └──── CXL ──── CXL Memory Expander ──── Additional DRAM
                   (appears as local memory to the CPU)
```

The key distinction from RDMA: CXL.mem access is transparent to software. The CPU issues normal load and store instructions to CXL-attached memory. There is no special API, no memory registration, no queue pairs, and no completion queues. The memory controller handles everything in hardware.

## CXL for Memory Tiering

CXL.mem enables a tiered memory architecture where different types of memory have different performance characteristics:

| Tier | Technology | Latency | Bandwidth | Cost |
|---|---|---|---|---|
| Tier 0 | HBM (on-package) | ~10 ns | Very high | Very high |
| Tier 1 | Local DDR5 | ~80 ns | High | High |
| Tier 2 | CXL-attached DRAM | ~150-250 ns | Moderate | Lower |
| Tier 3 | CXL-attached persistent memory | ~300-500 ns | Lower | Lowest |

Operating systems can use CXL-attached memory as a lower tier, automatically placing cold pages on CXL memory and hot pages on local DRAM. The Linux kernel (5.18+) includes CXL memory tiering support through the memory tier framework.

<div class="note">

**Note:** CXL.mem latency (150-250 ns for DRAM behind a CXL controller) is significantly lower than RDMA latency (1-2 microseconds for a network round trip). However, CXL.mem bandwidth per device is limited by the PCIe link bandwidth (32 GB/s for PCIe 5.0 x16), while RDMA can aggregate bandwidth across many connections. The two technologies occupy different points in the latency-bandwidth-distance trade-off space.

</div>

## Relationship to RDMA: Complementary, Not Competing

RDMA and CXL are not competitors -- they address different scales and latency regimes:

**CXL** operates within a rack or chassis, over PCIe-distance connections (typically up to 2 meters with copper, extendable with retimers). It provides sub-microsecond latency with hardware coherence. It is ideal for memory expansion, disaggregated memory pools within a rack, and accelerator-to-host communication.

**RDMA** operates across a data center, over network-distance connections (tens to hundreds of meters). It provides single-digit microsecond latency with software-managed access. It is ideal for inter-server communication, distributed storage, and cross-rack data movement.

```text
Distance and Latency Spectrum:

  ◄─── CXL ───►◄──────────── RDMA ──────────────►
  Within chassis     Within data center

  10ns    100ns    1μs    10μs    100μs    1ms
   │       │       │       │       │       │
   L1    Local   CXL    RDMA   TCP    Wide
  Cache   DRAM   Memory  RPC   Socket  Area
```

A future data center architecture might use both:
- CXL for memory pooling within a rack: servers share a pool of CXL-attached memory for overflow capacity.
- RDMA for cross-rack communication: distributed storage and computing services use RDMA for inter-server data transfer.
- Both work together when an application needs to access a remote CXL memory pool: RDMA transfers data between racks, and CXL distributes it within a rack.

## RDMA + CXL: Remote Memory with Coherency

The combination of RDMA and CXL enables architectures that were previously impractical:

**CXL-attached NIC memory:** A SmartNIC with CXL can expose its local memory (or attached memory) to the host CPU as coherent, load/store-accessible memory. The NIC can simultaneously expose the same memory for RDMA access by remote hosts. This enables zero-copy networking where the CPU can directly read NIC buffers without DMA.

**CXL memory pools with RDMA access:** A CXL memory pooling device can be accessed locally via CXL.mem (by CPUs in the same rack) and remotely via RDMA (by CPUs in other racks). The pooling device mediates between the two access methods, handling coherence locally and RDMA semantics remotely.

**Disaggregated memory architectures:** In the long term, a data center could have separate pools of compute (CPUs), memory (CXL-attached DRAM), and networking (RDMA NICs). Applications would be composed from these resources dynamically, with CXL providing local memory access and RDMA providing remote access.

## CXL 3.0: Fabric-Attached Memory and Switching

CXL 3.0, released in 2023, extends CXL from point-to-point connections to a switched fabric:

**CXL switches:** Purpose-built switches that route CXL.mem traffic between multiple hosts and multiple memory devices. A CXL switch allows any host to access any memory device in the fabric, enabling true memory pooling.

**Multi-headed devices:** CXL 3.0 allows a single memory device to be accessed by multiple hosts simultaneously, with hardware-enforced isolation between hosts. This is conceptually similar to SR-IOV for NICs but applied to memory devices.

**Fabric-attached memory:** Memory devices can be attached to the CXL fabric and accessed by any host, creating a shared memory pool that can be dynamically allocated to workloads as needed.

```text
CXL 3.0 Fabric Architecture:

  Host A ──┐                    ┌── Memory Pool 1
  Host B ──┼── CXL Switch ─────┼── Memory Pool 2
  Host C ──┤                    ├── Accelerator
  Host D ──┘                    └── Memory Pool 3
```

<div class="tip">

**Tip:** CXL 3.0 fabric-attached memory is conceptually similar to RDMA-based disaggregated memory (as used in systems like Infiniswap or AIFM), but operates at a fundamentally lower latency. Where RDMA-based remote memory has 1-5 microsecond access latency, CXL 3.0 fabric memory targets 300-500 nanoseconds even through a switch -- close enough to local DRAM that many applications can use it transparently with modest performance impact.

</div>

## Hardware Coherence vs. Software-Managed Access

The fundamental difference between CXL and RDMA is coherence management:

**CXL (hardware coherence):**
```c
// CXL memory access - just normal loads and stores
volatile int *remote_counter = (int *)cxl_mmap(device, offset, size);
int value = *remote_counter;        // Load from CXL memory
*remote_counter = value + 1;        // Store to CXL memory
// Hardware ensures coherence automatically
```

**RDMA (software-managed):**
```c
// RDMA memory access - explicit operations with keys and completions
struct ibv_send_wr wr = {
    .opcode = IBV_WR_RDMA_READ,
    .wr.rdma = { .remote_addr = addr, .rkey = rkey },
    // ...
};
ibv_post_send(qp, &wr, &bad_wr);
// Must poll CQ for completion
// Must manage consistency in software
```

Hardware coherence is simpler to program but adds hardware complexity and limits scaling distance. Software-managed access requires more application effort but scales across the data center. Both approaches have their place.

## Industry Adoption Timeline and Outlook

CXL adoption is proceeding in waves:

**CXL 1.1 (2020-2023):** First-generation CXL memory expanders from Samsung, SK Hynix, and Micron. Single-host memory expansion over PCIe 5.0. Limited production deployment, primarily for memory capacity expansion.

**CXL 2.0 (2023-2025):** Memory pooling with basic switching. Multiple hosts can share memory devices through a CXL switch. Early adopters in hyperscale data centers.

**CXL 3.0 (2025+):** Full fabric with multi-level switching, peer-to-peer access, and enhanced coherence. This is where CXL becomes a fabric technology comparable in scope (if not distance) to RDMA.

For RDMA practitioners, the key implication is that the "remote memory access" problem space is expanding. RDMA addresses inter-server memory access over the network. CXL addresses intra-rack memory access over PCIe. Together, they form a continuum of remote memory technologies, each optimized for its target distance and latency regime. Applications that currently use RDMA for all remote memory access may partition their access patterns in the future: CXL for near memory, RDMA for far memory.

The challenge -- and the opportunity -- lies in building software abstractions that span both technologies, presenting applications with a unified memory access interface while automatically selecting the optimal transport based on the target's location and latency requirements.
