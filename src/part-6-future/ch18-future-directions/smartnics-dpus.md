# 18.2 SmartNICs and DPUs

The network interface card is no longer just a network interface card. Modern SmartNICs and DPUs (Data Processing Units) embed general-purpose processors, dedicated accelerators, and switching capabilities directly into the NIC, transforming it from a passive data pipe into a programmable computing platform. This evolution has profound implications for RDMA deployment, performance, and architecture.

## From NIC to SmartNIC to DPU

The terminology in this space has evolved rapidly. Understanding the distinctions helps navigate vendor marketing and architectural decisions.

### Traditional NIC

A traditional NIC handles basic network I/O: transmit and receive packets, compute checksums, and interrupt the host CPU. It has no general-purpose processing capability. Mellanox ConnectX-series adapters are high-performance NICs with extensive RDMA offload, but they are not SmartNICs -- their processing is fixed-function, optimized for specific protocols.

### SmartNIC

A SmartNIC adds programmable processing to the NIC. This processing can take two forms:

**FPGA-based SmartNICs** contain a Field-Programmable Gate Array that can be loaded with custom logic. Microsoft's Azure SmartNIC (used in the Azure cloud) is FPGA-based, implementing virtual networking, encryption, and storage acceleration in reconfigurable hardware. FPGA SmartNICs offer extremely high performance for specific workloads but require specialized development skills.

**SoC-based SmartNICs** contain a System-on-Chip with general-purpose ARM cores alongside the NIC hardware. These are programmable with standard software tools and operating systems, making them more accessible to develop for.

### DPU (Data Processing Unit)

A DPU is a SoC-based SmartNIC specifically designed to offload infrastructure functions from the host CPU. The term was popularized by NVIDIA (after acquiring Mellanox) to describe BlueField, but AMD/Pensando and Intel have adopted similar terminology. A DPU typically includes:

- A high-performance RDMA NIC (e.g., ConnectX-7 equivalent)
- Multiple ARM cores running a full Linux OS
- Hardware accelerators for specific functions (crypto, compression, regex)
- An embedded switch for traffic steering
- Separate memory (DRAM) and storage (eMMC/NVMe) for the ARM subsystem

```text
DPU Architecture (e.g., BlueField-3):

┌─────────────────────────────────────────────────┐
│                     DPU                          │
│  ┌────────────┐  ┌────────────┐  ┌───────────┐ │
│  │ ConnectX-7 │  │  16x ARM   │  │ Hardware  │ │
│  │  NIC Core  │  │  A78 Cores │  │ Accel.    │ │
│  │ (RDMA,RoCE │  │  (Linux OS)│  │ (crypto,  │ │
│  │  offload)  │  │            │  │  regex,   │ │
│  └─────┬──────┘  └──────┬─────┘  │  decomp.) │ │
│        │                │         └───────────┘ │
│  ┌─────┴────────────────┴───────────────────┐   │
│  │          Embedded Switch / DMA           │   │
│  └──────────────┬───────────────────────────┘   │
│                 │                                │
│        ┌────────┴────────┐                       │
│        │  PCIe to Host   │  ◄── Network Ports   │
│        └─────────────────┘      (2x 100/200G)   │
└─────────────────────────────────────────────────┘
```

## NVIDIA BlueField

NVIDIA BlueField is the most widely deployed DPU and the most relevant to RDMA practitioners, as it combines a full ConnectX NIC with ARM processing.

### Architecture

BlueField presents two separate computing environments:

1. **The host side:** The server's x86 (or ARM) CPU sees a standard ConnectX NIC via PCIe. The host can use all standard RDMA verbs, just as it would with a ConnectX NIC.

2. **The DPU side (ARM):** The BlueField's ARM cores run a full Linux distribution (Ubuntu-based). They can independently access the NIC hardware, run RDMA applications, and process network traffic -- all without involving the host CPU.

The embedded switch steers traffic between the host and the ARM cores, and between the network ports and either processing domain.

### Running Infrastructure on the DPU

The transformative idea behind the DPU is running infrastructure services on the ARM cores rather than the host CPU:

**Networking:** The DPU implements virtual switching (OVS), firewall rules, tunneling (VXLAN/Geneve), and traffic shaping on the ARM cores. The host CPU does not run any networking software beyond basic drivers.

**Storage:** NVMe-oF (NVMe over Fabrics) target and initiator software runs on the DPU. The host sees NVMe devices, but the actual storage protocol processing happens on the ARM cores.

**Security:** IPsec, TLS termination, and access control run on the DPU. The host's traffic is encrypted and authenticated without any host CPU involvement.

```text
Traditional Architecture:        DPU Architecture:
┌──────────────┐                 ┌──────────────┐
│   Host CPU   │                 │   Host CPU   │
│  ┌─────────┐ │                 │  ┌─────────┐ │
│  │ App     │ │                 │  │ App     │ │
│  ├─────────┤ │                 │  │ (only)  │ │
│  │ Storage │ │                 │  └────┬────┘ │
│  │ Network │ │                 │       │ PCIe │
│  │ Security│ │                 └───────┼──────┘
│  └────┬────┘ │                         │
│       │      │                 ┌───────┼──────┐
└───────┼──────┘                 │  DPU  │      │
        │                        │  ┌────┴────┐ │
   ┌────┴────┐                   │  │ Storage │ │
   │   NIC   │                   │  │ Network │ │
   └─────────┘                   │  │ Security│ │
                                 │  └────┬────┘ │
                                 │  ┌────┴────┐ │
                                 │  │   NIC   │ │
                                 │  └─────────┘ │
                                 └──────────────┘
```

### DOCA Framework

NVIDIA's DOCA (Data Center Infrastructure On a Chip Architecture) is the programming framework for BlueField DPUs. It provides libraries and APIs for:

- **Flow processing:** Hardware-accelerated packet classification and steering.
- **RDMA:** Standard verbs plus DPU-specific optimizations.
- **GPUDirect:** Coordinating data flow between GPUs, NICs, and DPU ARM cores.
- **Security:** Crypto offload, firewall, and deep packet inspection.
- **Storage:** NVMe emulation, compression, and RAID.

<div class="note">

**Note:** DOCA is NVIDIA-specific. Applications written with DOCA run only on BlueField DPUs. For portable RDMA code, continue using standard libibverbs/rdma-core. DOCA adds value for infrastructure-level code that leverages DPU-specific accelerators, not for standard RDMA applications.

</div>

## AMD/Pensando and Intel IPU

NVIDIA is not the only DPU player. Each vendor has a different architectural approach:

### AMD Pensando DSC (Distributed Services Card)

AMD's Pensando (acquired in 2022) takes a different approach: instead of ARM cores running Linux, the DSC uses a custom P4-programmable ASIC for data path processing, with ARM cores for control plane only. This provides deterministic, line-rate processing for networking functions but is less flexible for general-purpose computation.

The Pensando DSC is optimized for cloud infrastructure, providing hardware-accelerated:
- Virtual switching and routing
- Firewall and microsegmentation
- Encryption (IPsec, TLS)
- Telemetry and flow monitoring

### Intel Infrastructure Processing Unit (IPU)

Intel's IPU (formerly code-named Mt. Evans, later Oak Springs Canyon) combines Intel's FPGA technology with embedded ARM or Xeon cores. The FPGA provides a reconfigurable data plane, while the CPU cores handle control plane and complex processing.

Intel positions the IPU as part of a broader "Infrastructure Processing Unit" concept where the IPU manages all infrastructure-level I/O, presenting simplified, virtualized devices to the host CPU.

## RDMA Offload to DPU

With a DPU, RDMA processing can be offloaded from the host CPU entirely. The DPU's ARM cores can run RDMA applications, respond to RDMA requests, and manage RDMA resources without involving the host CPU.

### Use Cases

**Storage disaggregation:** A DPU can run an NVMe-oF target that accepts RDMA connections from remote initiators, accesses local NVMe storage, and serves data -- all without using any host CPU cycles. The host can even be powered off, and the DPU continues serving storage.

**Key-value stores:** A DPU can run a key-value store (like a subset of Redis or Memcached) that responds to RDMA Read/Write operations. The host CPU handles application logic, while the DPU handles data serving.

**Network function virtualization:** Virtual routers, firewalls, and load balancers run on the DPU, processing RDMA and non-RDMA traffic at line rate without host CPU involvement.

### Performance Implications

Moving RDMA processing to the DPU has both advantages and disadvantages:

**Advantages:**
- Host CPU is free for application computation.
- Infrastructure functions are isolated from application bugs and security issues.
- Multiple tenants' networking is isolated in hardware (each tenant sees a VF, but the DPU manages the physical NIC).

**Disadvantages:**
- DPU ARM cores are slower than host x86 cores. Processing-intensive RDMA operations (e.g., data transformation) may be slower on the DPU.
- Data that must reach the host CPU still traverses PCIe. The DPU does not eliminate the PCIe bottleneck; it just moves processing before it.
- Programming the DPU adds complexity. Two software environments (host and DPU) must be managed, debugged, and updated.

## Programmable RDMA: Custom Operations in the Data Path

The most forward-looking aspect of DPU technology is the ability to implement custom operations in the RDMA data path. Instead of the fixed set of RDMA operations (Read, Write, Send, Atomic), a DPU can implement application-specific operations that execute on the NIC itself.

Examples of programmable RDMA operations:

- **Scatter-gather with transformation:** An RDMA Read that gathers data from multiple memory locations and applies a transformation (encryption, compression, format conversion) before sending.
- **Conditional writes:** An RDMA Write that only modifies memory if a condition is met (more complex than standard CAS atomics).
- **In-NIC aggregation:** Receiving data from multiple sources, aggregating (summing, averaging) on the NIC, and making the result available -- useful for distributed machine learning gradient aggregation.

<div class="tip">

**Tip:** Programmable RDMA is still in its early stages. Standard RDMA verbs remain the most portable and well-understood approach. Consider programmable RDMA only when you have a specific, performance-critical operation that benefits from NIC-level execution, and when you can accept the vendor lock-in that custom NIC programming implies.

</div>

## Infrastructure Isolation: The DPU Security Model

From a security perspective, the DPU represents a fundamental improvement over traditional RDMA deployments. The DPU creates a hardware-enforced separation between infrastructure and tenant workloads:

- **The host (tenant) cannot access the DPU's management plane.** The DPU controls which network functions, VLANs, and QoS policies apply to the host's traffic. A compromised host cannot modify its own network isolation.

- **The DPU can inspect and filter traffic.** Unlike traditional RDMA where kernel bypass prevents inspection, the DPU sits between the host and the network and can apply security policies to all traffic, including RDMA.

- **Firmware updates on the DPU do not require host downtime.** Infrastructure security patches can be applied to the DPU independently of the host operating system.

This "infrastructure on the DPU" model is increasingly important for cloud providers, where the host is untrusted tenant hardware and the DPU is the provider's trusted infrastructure layer. RDMA, which traditionally required trusting the host because of kernel bypass, can now be secured by the DPU without sacrificing data path performance.

The trajectory is clear: the future of RDMA in multi-tenant environments will involve DPUs that manage security, isolation, and policy enforcement at the hardware level, transparent to both the host application and the RDMA protocol.
