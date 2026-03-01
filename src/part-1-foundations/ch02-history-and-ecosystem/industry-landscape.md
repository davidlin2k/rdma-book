# 2.5 Industry Landscape

## A Consolidated Market

The RDMA industry in the mid-2020s looks radically different from the crowded, multi-vendor landscape of the early 2000s. Acquisitions, exits, and the gravitational pull of the AI/HPC boom have consolidated the market around a small number of players, with one company---NVIDIA---dominating to a degree that shapes the technology's evolution, pricing, and competitive dynamics.

Understanding who builds what, which products matter, and how cloud providers make their networking choices is practical knowledge for any RDMA practitioner. The hardware you deploy determines your driver, your firmware update cadence, your performance envelope, and your support options. This section maps the current landscape.

## NVIDIA (formerly Mellanox): The Dominant Force

### The Acquisition

On April 27, 2020, NVIDIA completed its acquisition of Mellanox Technologies for approximately $6.9 billion. This was, at the time, the largest acquisition in NVIDIA's history, and it signaled a strategic thesis that has since been validated by the AI boom: that high-performance networking and GPU computing would converge into a unified system problem. Training a large language model across thousands of GPUs is as much a networking challenge as a compute challenge---the GPUs spend a significant fraction of their time waiting for gradient synchronization across the network, and reducing that synchronization latency translates directly into faster training and lower cost.

The acquisition gave NVIDIA control of the dominant RDMA hardware platform, the dominant InfiniBand switching platform, and the software ecosystem (MLNX_OFED, UCX, NCCL's transport layer) that ties GPUs to the network. The result is a vertically integrated stack from GPU to NIC to switch fabric, optimized as a system.

### The ConnectX Series

The ConnectX family of network adapters is the backbone of NVIDIA's RDMA strategy. Each generation has brought significant capabilities:

**ConnectX-3 / ConnectX-3 Pro (2012--2013).** The first widely deployed dual-mode adapters supporting both InfiniBand (FDR, 56 Gbps) and Ethernet (40/56 GbE) with RoCE. ConnectX-3 Pro added hardware-based RoCE and improved stateless offloads. These adapters established the pattern of a single adapter supporting multiple RDMA transports---a strategy that reduced adoption risk for customers uncertain about which transport to choose.

**ConnectX-4 / ConnectX-4 Lx (2015--2016).** The jump to 100 Gbps (EDR InfiniBand, 100 GbE) and a major architectural change: ConnectX-4 moved to the `mlx5` driver model that remains current today. ConnectX-4 Lx was an Ethernet-only variant at lower cost, targeting the broader datacenter market. These adapters introduced enhanced RoCE v2 support, improved ECMP hashing, and better congestion management.

**ConnectX-5 / ConnectX-5 Ex (2017).** Brought 200 Gbps HDR InfiniBand and 100/200 GbE. Key innovations included enhanced adaptive routing, Multi-Path capability for bonding multiple ports, and critically, **selective retransmission** for RoCE---replacing the go-back-N behavior that made earlier RoCE implementations fragile under packet loss. ConnectX-5 Ex added PCIe 4.0 support.

**ConnectX-6 / ConnectX-6 Dx / ConnectX-6 Lx (2019--2020).** HDR200 InfiniBand (200 Gbps) and up to 200 GbE. ConnectX-6 Dx added inline hardware acceleration for IPsec, TLS, and other security protocols. ConnectX-6 Lx was a cost-optimized 25/50 GbE variant for cloud deployments. This generation significantly improved RoCE congestion management and introduced programmable congestion control.

**ConnectX-7 (2022).** NDR InfiniBand (400 Gbps) and 400 GbE. Added in-network computing capabilities, enhanced GPUDirect RDMA performance, and PCIe Gen 5. ConnectX-7 represents the current high-end for standalone NIC deployments.

### BlueField DPUs

Alongside ConnectX NICs, NVIDIA has developed the **BlueField** line of Data Processing Units (DPUs)---SmartNICs that combine ConnectX networking silicon with embedded Arm cores, hardware accelerators, and programmable packet processing engines. BlueField is relevant to the RDMA story because it enables infrastructure-level RDMA offload: a BlueField DPU can terminate RDMA connections, perform storage virtualization (NVMe-oF over RDMA), and enforce network policies, all without consuming host CPU cycles.

**BlueField-2** (2020) paired ConnectX-6 networking with 8 Arm A72 cores. **BlueField-3** (2022) paired ConnectX-7 networking with 16 Arm A78 cores and added hardware-accelerated cryptography, regex, and decompression engines. **BlueField-4** is expected to further integrate networking and compute.

<div class="note">

For RDMA application developers, BlueField DPUs are largely transparent. The RDMA Verbs API works the same whether the underlying hardware is a ConnectX NIC or a BlueField DPU. The differences matter primarily to infrastructure teams deploying storage-over-RDMA, SR-IOV virtualization, or DPU-offloaded network functions.

</div>

## Intel: A Shifting Strategy

Intel's RDMA history is a story of multiple starts, pivots, and organizational changes.

**Early InfiniBand and iWARP.** Intel was a founding member of the IBTA and an early InfiniBand proponent, but exited the InfiniBand HCA market in the early 2000s to focus on PCI Express. Intel later championed iWARP on its Ethernet controllers, notably the X722, positioning iWARP as the RDMA solution for Intel's Ethernet ecosystem.

**Omni-Path Architecture (OPA).** In 2012, Intel acquired QLogic's InfiniBand business and used the technology as the foundation for Omni-Path, a proprietary high-performance fabric aimed at HPC. Omni-Path used a custom transport protocol distinct from InfiniBand, with 100 Gbps link speeds and architecture optimizations for small-message MPI workloads. Intel deployed Omni-Path in several major supercomputers, including the Aurora exascale system at Argonne National Laboratory. However, Intel discontinued internal Omni-Path development in 2019 and spun out the technology to **Cornelis Networks**, an independent company that continues to develop and sell Omni-Path products.

**E810 and the `irdma` driver.** Intel's current RDMA-capable Ethernet adapter is the **E810** series (marketed as Intel Ethernet 800 Series), supported by the `ice` network driver and the `irdma` RDMA driver. The E810 supports both iWARP and RoCE v2, making it one of the few adapters that offers both RDMA-over-TCP and RDMA-over-UDP from the same hardware. However, the E810's RDMA performance and feature set lag behind NVIDIA's ConnectX in most benchmarks, and Intel's RDMA market share is correspondingly small.

**Gaudi accelerators.** Intel's Gaudi AI accelerators (from the Habana Labs acquisition) use their own integrated networking based on Ethernet and RoCE, with a proprietary scale-out architecture. This represents another distinct networking path within Intel's product portfolio.

## Broadcom: Ethernet-First, RDMA-Second

Broadcom (which acquired Emulex in 2015 and Brocade in 2017) is the world's largest merchant Ethernet switching and NIC silicon provider. Its **NetXtreme-E** series of Ethernet adapters support RoCE v2 through the `bnxt_re` kernel driver. Broadcom's RDMA support is adequate for basic RoCE workloads but has historically received less engineering investment and community attention than NVIDIA's, resulting in a narrower feature set and later adoption of advanced capabilities.

Broadcom's strategic emphasis is on Ethernet switching silicon (Memory, Memory and Memory families) and mainstream NIC features (SR-IOV, hardware timestamping, flow offloads) rather than on RDMA-specific innovation. For organizations already standardized on Broadcom NICs, RoCE is available as an incremental capability; for organizations where RDMA performance is a primary requirement, NVIDIA ConnectX remains the more common choice.

## HCA vs. RNIC: Terminology and Architecture

The RDMA ecosystem uses two distinct terms for network adapters, and the distinction reflects real architectural differences:

**Host Channel Adapter (HCA)** is the InfiniBand term for an RDMA-capable network adapter. The term "channel adapter" comes from InfiniBand's channel-based I/O heritage (as opposed to a network-based heritage). An HCA is designed from the ground up for RDMA: it has dedicated hardware for queue pair management, reliable transport, memory translation tables, and completion processing. Every transistor on an HCA is optimized for RDMA workloads.

**RDMA-capable NIC (RNIC)** is the term used for Ethernet NICs that have been enhanced with RDMA support, typically for iWARP or RoCE. An RNIC starts as a conventional Ethernet NIC and adds RDMA offload engines. The RDMA capability may share silicon resources with other NIC functions (TCP offload, SR-IOV, flow steering), leading to potential contention under load.

In practice, the distinction between HCA and RNIC has blurred. NVIDIA's ConnectX adapters are both HCAs (when used with InfiniBand) and RNICs (when used with Ethernet/RoCE), and they deliver comparable RDMA performance in both modes. The terms persist mainly in specifications and vendor documentation.

## Comparison of Major RDMA Vendors and Products

| Vendor | Product Line | RDMA Transport(s) | Max Speed | Kernel Driver | Key Market |
|--------|-------------|-------------------|-----------|---------------|------------|
| NVIDIA | ConnectX-7 | InfiniBand, RoCE v2 | 400 Gbps | mlx5_ib | HPC, AI, Cloud |
| NVIDIA | BlueField-3 | InfiniBand, RoCE v2 | 400 Gbps | mlx5_ib | Cloud infrastructure |
| Intel | E810 | iWARP, RoCE v2 | 100 Gbps | irdma | Enterprise |
| Cornelis | Omni-Path Express | OPA (Verbs-compatible) | 400 Gbps | opa | HPC |
| Broadcom | NetXtreme-E | RoCE v2 | 200 Gbps | bnxt_re | Enterprise Ethernet |
| Chelsio | T6/T7 | iWARP | 100 Gbps | cxgb4, iw_cxgb4 | Storage, Enterprise |
| AWS | EFA | Custom (SRD) | 3200 Gbps | efa | AWS cloud only |
| Microsoft | MANA | RoCE v2 | 200 Gbps | mana_ib | Azure cloud only |
| Huawei | HiNIC3 | RoCE v2 | 200 Gbps | hns_roce | China market |
| AMD/Xilinx | Alveo | RoCE v2 | 100 Gbps | (varies) | Specialized |

## Cloud Provider Strategies

Each major cloud provider has taken a distinct approach to RDMA, reflecting their infrastructure philosophies and workload requirements.

### Microsoft Azure

Azure was an early and aggressive adopter of RDMA in the public cloud. Azure's RDMA story began with iWARP (using Chelsio and other NICs in early deployments) and transitioned to RoCE v2 running on NVIDIA ConnectX adapters in the current generation. Azure exposes RDMA to customers through its **HB-series** (HPC) and **ND-series** (AI/GPU) virtual machine families, where the RDMA NIC is passed through to the guest VM using SR-IOV.

Azure has also developed **MANA (Microsoft Azure Network Adapter)**, a custom SmartNIC with RDMA support for general-purpose VMs, with a corresponding `mana_ib` kernel driver contributed upstream. Azure's extensive investment in RDMA---and its publicly documented experiences with PFC deadlocks, congestion management, and large-scale RoCE deployment---has produced some of the most valuable operational literature in the RDMA field.

### Amazon Web Services (AWS)

AWS took a different path. Rather than deploying standard InfiniBand or RoCE, AWS developed **Elastic Fabric Adapter (EFA)**, a custom network interface with a proprietary RDMA-like transport called **Scalable Reliable Datagram (SRD)**. SRD is designed for AWS's network environment: it provides reliable, unordered delivery (unlike TCP's ordered delivery), multipath operation, and tolerance for packet reordering---characteristics that align with AWS's large-scale, multi-path network fabric.

EFA exposes a Verbs-like API through the `efa` provider in rdma-core, allowing existing RDMA applications (particularly those using libfabric, the MPI-oriented fabric abstraction) to run with minimal modification. However, EFA is not standard RoCE or iWARP---it is a proprietary transport available only on AWS. EFA is available on P4d/P5 (GPU), Hpc6a/Hpc7g (HPC), and other instance types.

<div class="note">

EFA's approach---a custom transport with a Verbs-compatible API---illustrates an important trend: the separation of the RDMA programming model (queue pairs, completion queues, memory registration) from the specific wire protocol. Applications written to the Verbs API can run on InfiniBand, RoCE, iWARP, or EFA with minimal changes, even though the underlying transports are radically different. This portability is a testament to the durability of the abstraction that originated with InfiniBand.

</div>

### Google Cloud Platform (GCP)

Google has been more measured in its public RDMA offerings. Internally, Google uses a custom network stack (including custom NICs and switches) for its datacenter infrastructure, and Google's published research on RDMA (including work on Swift, a custom congestion control protocol for RoCE) indicates significant internal use. For external customers, GCP offers **A3 instances** with NVIDIA H100 GPUs connected via ConnectX-7 NICs with RoCE v2 support for AI training workloads.

## Emerging Players and Technologies

Several developments are shaping the next phase of the RDMA landscape:

**Ultra Ethernet Consortium (UEC).** Founded in 2023, the UEC brings together AMD, Broadcom, Cisco, Intel, Meta, Microsoft, and others to develop a next-generation Ethernet-based transport protocol purpose-built for AI and HPC workloads. The UEC aims to address RoCE's limitations (lossless requirements, congestion sensitivity) with a new transport designed for lossy networks, multipath operation, and massive scale. If successful, UEC could displace RoCE v2 as the standard Ethernet-based RDMA transport, though production-ready implementations are likely several years away.

**CXL (Compute Express Link).** While not an RDMA technology per se, CXL is expanding the concept of remote memory access beyond the network and into the memory bus. CXL 3.0's memory sharing and fabric-attached memory capabilities overlap with some RDMA use cases, particularly for rack-scale disaggregated memory. The relationship between CXL and RDMA---complementary, competitive, or converging---is an open question in the industry.

**AMD/Xilinx.** AMD's acquisition of Xilinx brought FPGA-based SmartNIC technology (the Alveo series) with programmable RDMA offload capabilities. AMD's Pensando DPU (from the 2022 acquisition) also supports RoCE v2. While AMD has not yet challenged NVIDIA's RDMA dominance, its combined portfolio of GPUs, FPGAs, DPUs, and CPUs positions it as a potential competitor for integrated AI infrastructure.

**Fungible (acquired by Microsoft).** Fungible developed DPU technology with custom networking silicon designed for composable infrastructure. Microsoft's acquisition of Fungible in 2023 folded this technology into Azure's infrastructure roadmap, potentially influencing future Azure RDMA offerings.

## Market Share and Deployment Patterns

Precise RDMA market share data is difficult to obtain, as many deployments are internal to cloud providers and are not publicly reported. However, several patterns are clear:

**InfiniBand** dominates in dedicated HPC and AI supercomputing. The largest AI training clusters in the world---including those operated by Meta, Microsoft, Tesla, and xAI---use InfiniBand fabrics with NVIDIA ConnectX-7 adapters and Quantum-2 switches. InfiniBand's share of the Top500 supercomputer interconnects remains above 40%.

**RoCE v2** dominates in cloud and enterprise datacenter deployments where RDMA is used for storage (NVMe-oF, disaggregated storage), inter-service communication, and mid-scale AI training. Azure, GCP, and many enterprise organizations deploy RoCE v2 on NVIDIA ConnectX adapters over their existing Ethernet fabrics.

**iWARP** continues to serve niche deployments, particularly Windows Server environments (where iWARP's compatibility with SMB Direct over standard Ethernet is valued) and legacy installations. New iWARP deployments are uncommon.

**Custom transports** (AWS EFA/SRD, various internal hyperscaler designs) are growing in significance as cloud providers optimize their stacks for specific workloads, particularly AI training.

The overall trend is clear: RDMA has moved from a niche HPC technology to a mainstream datacenter capability. The question for most organizations is no longer *whether* to use RDMA, but *which transport* and *which vendor* to choose. For the majority of deployments in the mid-2020s, the answer is RoCE v2 on NVIDIA ConnectX hardware---but this default is being challenged by custom cloud-provider solutions from above and by emerging standards like UEC from below.
