# 15.4 Cloud Provider Implementations

Every major cloud provider now offers RDMA-capable instances, but their approaches differ fundamentally in transport protocol, API surface, and virtualization strategy. These differences matter: code that runs on Azure's InfiniBand will not run unmodified on AWS's EFA, and neither will work on GCP's custom GPUDirect stack. This section maps out what each provider actually deploys, so you can make informed decisions about portability and performance tradeoffs.

## Microsoft Azure

Azure has made the most aggressive investment in RDMA infrastructure among the major cloud providers, deploying both InfiniBand and RoCEv2 at massive scale.

### InfiniBand for HPC and AI VMs

Azure offers InfiniBand-connected VM instances for HPC and AI workloads:[^4]

- **HB-series** (HPC): AMD EPYC processors with InfiniBand. The HBv3 generation provides HDR InfiniBand (200 Gbps), while the HBv4 generation upgrades to NDR InfiniBand (400 Gbps). Designed for computational fluid dynamics, weather modeling, and other tightly-coupled HPC workloads.
- **HC-series** (HPC): Intel Xeon processors with EDR InfiniBand (100 Gbps). An older generation optimized for compute-intensive workloads. Note: HC-series uses EDR (100 Gbps), not HDR.
- **ND-series** (AI/ML): NVIDIA GPU instances with InfiniBand. The ND H100 v5 series provides 8x H100 GPUs with 8x NDR InfiniBand (400 Gbps each), delivering 3.2 Tbps of aggregate RDMA bandwidth per VM.
- **NDm A100 v4**: 8x A100 80GB GPUs with 8x HDR InfiniBand (200 Gbps each).

InfiniBand-connected VMs are grouped into **placement groups** that ensure all VMs in the group are on the same InfiniBand fabric partition, minimizing network hops and latency.

```bash
# Azure CLI: create an InfiniBand-connected HPC VM
az vm create \
    --resource-group myRG \
    --name hpc-vm-01 \
    --size Standard_HB120rs_v3 \
    --image microsoft-dsvm:ubuntu-hpc:2204:latest \
    --proximity-placement-group myPPG \
    --accelerated-networking true

# Inside the VM: verify InfiniBand
ibstat
# Should show mlx5 HCA with 200 Gbps (HDR) or 400 Gbps (NDR)

# Test RDMA connectivity between VMs
ib_write_bw --size=65536 --duration=10 <remote_ip>
```

### Accelerated Networking

Beyond InfiniBand, Azure deploys **Accelerated Networking** across its general-purpose VM fleet using SR-IOV. While this primarily provides TCP/IP acceleration rather than RDMA verbs access, it uses the same ConnectX NIC hardware and SR-IOV VF passthrough technology. Azure has deployed RoCEv2 at fleet scale for internal services, including its storage infrastructure.

<div class="note">

Azure's InfiniBand deployment is notable for its scale and integration. The ND H100 v5 VMs include a full InfiniBand Subnet Manager running in the infrastructure, transparent to the user. Users get InfiniBand VFs passed through to their VMs and can run standard RDMA applications (MPI, NCCL) without managing any fabric infrastructure. This level of managed InfiniBand is unique among cloud providers.

</div>

### Azure RDMA Architecture

Azure's RDMA infrastructure includes several distinctive features:

- **FPGA-based SmartNIC**: Azure uses custom FPGA-based SmartNICs (Azure Boost/previously FPGA) for general networking, offloading virtual networking, storage, and security functions. InfiniBand HCAs are provided as additional devices for RDMA-capable VM sizes.
- **Managed InfiniBand fabric**: Azure operates and manages the InfiniBand subnet manager, QoS policies, and fabric monitoring. Users interact only with the VF-level interface.
- **CycleCloud**: Azure CycleCloud provides orchestration for HPC clusters with automatic InfiniBand configuration, Lustre/BeeGFS parallel file system deployment, and job scheduler integration.

## Amazon Web Services (AWS)

AWS has taken a fundamentally different approach to high-performance networking. Rather than deploying standard InfiniBand or RoCE, AWS developed a custom networking stack optimized for cloud-scale deployment.

### EFA: Elastic Fabric Adapter

The **Elastic Fabric Adapter (EFA)** is AWS's custom network interface for HPC and ML workloads. EFA provides an RDMA-like programming model with a key distinction: it uses AWS's proprietary **SRD (Scalable Reliable Datagram)** transport protocol instead of standard InfiniBand or RoCE.

EFA architecture:

```
┌─────────────────────────────────────┐
│           Application               │
│   (MPI, NCCL, custom)              │
├─────────────────────────────────────┤
│          Libfabric                   │
│   (OFI - OpenFabrics Interface)     │
├─────────────────────────────────────┤
│        EFA Provider                  │
│   (libfabric EFA provider)          │
├─────────────────────────────────────┤
│      EFA Device Driver               │
│   (kernel: ena + efa)               │
├─────────────────────────────────────┤
│    AWS Nitro Card (EFA)              │
│   SRD Transport Protocol            │
└─────────────────────────────────────┘
```

### SRD: Scalable Reliable Datagram

SRD is AWS's transport protocol designed specifically for cloud-scale RDMA-like communication:

- **Multi-path**: SRD automatically sprays packets across multiple network paths, achieving better bandwidth utilization than single-path protocols like RC (Reliable Connected) verbs.
- **Congestion-aware**: SRD implements congestion control designed for the specific characteristics of AWS's network topology, avoiding the PFC (Priority Flow Control) dependency of RoCEv2.
- **Scalable**: SRD uses a connectionless (datagram) model that avoids the per-connection state scaling issues of RC QPs. A single SRD endpoint can communicate with thousands of peers without creating thousands of QPs.
- **Reliable**: SRD provides reliable delivery with selective retransmission, without requiring the lossless fabric that InfiniBand and RoCEv2 depend on. This is a significant architectural advantage: SRD can tolerate packet loss gracefully (via per-packet sequence numbers and NACKs), whereas RoCEv2 depends on PFC to prevent any packet loss, and PFC itself can cause head-of-line blocking, deadlocks, and congestion spreading (see Chapter 13). By designing SRD to work over a lossy network, AWS avoids the entire class of PFC-related reliability problems.

<div class="tip">

EFA does not expose standard RDMA verbs. Applications must use the **Libfabric** (OFI - OpenFabrics Interfaces) API, which is a higher-level abstraction over network fabrics. Most HPC applications (Open MPI, NCCL, Intel MPI) already support Libfabric, so the transition from verbs to EFA is transparent for applications using these frameworks. However, applications that use libibverbs directly will need to be ported to Libfabric.

</div>

### EFA Instance Types

| Instance Type | GPUs        | EFA Bandwidth | Use Case           |
|---------------|-------------|---------------|--------------------|
| P5.48xlarge   | 8x H100     | 3200 Gbps     | Large-scale ML     |
| P4d.24xlarge  | 8x A100     | 400 Gbps      | ML training        |
| Hpc7a.96xlarge| None (CPU)  | 300 Gbps      | HPC simulation     |
| Hpc6a.48xlarge| None (CPU)  | 100 Gbps      | HPC simulation     |
| Trn1.32xlarge | 16x Trainium| 800 Gbps      | ML (Trainium)      |

```bash
# AWS CLI: launch EFA-enabled instances
aws ec2 run-instances \
    --instance-type p5.48xlarge \
    --network-interfaces "DeviceIndex=0,\
        InterfaceType=efa,\
        Groups=[sg-12345]" \
    --placement "GroupName=my-cluster,Strategy=cluster" \
    --image-id ami-12345678

# Inside the instance: verify EFA
fi_info -p efa
# Should show EFA provider with SRD transport

# Run NCCL test over EFA
export FI_PROVIDER=efa
export NCCL_NET=efa
mpirun -np 16 --hostfile hosts \
    ./all_reduce_perf -b 8 -e 2G -f 2 -g 1
```

### EFA Limitations

- **No standard verbs API**: Applications must use Libfabric, not libibverbs.
- **Evolving RDMA verb support**: Earlier EFA generations had limited one-sided RDMA support. On current Nitro v4+ instances (e.g., P5, Hpc7a), EFA supports both RDMA Read and RDMA Write. Notably, the P4d (Nitro v3) supports RDMA Read but *not* RDMA Write -- the reverse of what many assume.[^1] Always check the [EFA documentation](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/efa.html) for the current support matrix by instance type.
- **Security group enforcement**: EFA traffic passes through AWS security groups, adding a small amount of latency compared to bare-metal RDMA but providing cloud-standard network security.

## Google Cloud Platform (GCP)

Google Cloud has developed a custom networking stack that integrates RDMA capabilities into its proprietary infrastructure.

### gVNIC and RDMA

Google's **gVNIC (Google Virtual NIC)** is a custom virtual network interface that replaces the standard virtio-net in GCP VMs. For GPU-accelerated instances, gVNIC supports RDMA capabilities through Google's custom networking fabric.

### GPU Instance Types

- **A3 Mega**: 8x H100 GPUs with 9 NICs in an 8+1 configuration (8 data NICs + 1 management NIC). Uses **GPUDirect-TCPXO**, a custom RDMA-like offloaded networking stack over gVNIC -- not InfiniBand. Provides up to 1,800 Gbps aggregate network bandwidth.[^2]
- **A3 High**: 8x H100 GPUs with 5 NICs in a 4+1 configuration (4 data NICs + 1 management NIC). Uses **GPUDirect-TCPX**. Provides up to ~1,000 Gbps aggregate network bandwidth.
- **A2 Ultra**: 8x A100 80GB GPUs with multi-NIC RDMA support.

### Jupiter and Custom Networking

Google's networking infrastructure is built on custom-designed switches and a proprietary protocol stack:

- **Jupiter fabric**: Google's data center network fabric providing petabit-scale bisection bandwidth.
- **Snap**: A user-space networking stack that handles packet processing, virtualization, and network function offload.
- **GPUDirect-TCPX/TCPXO**: Google's custom networking stacks for GPU-to-GPU communication. TCPXO (the offloaded variant, used on A3 Mega) achieves RDMA-like performance by offloading packet processing from the host CPU.

For GPU instances, Google uses its custom networking infrastructure with gVNIC and GPUDirect support, enabling NCCL-based distributed training. While the underlying transport is not standard InfiniBand or RoCEv2, the performance characteristics approach bare-metal RDMA for collective operations.

```bash
# GCP: A3 Mega instances are typically provisioned via
# managed instance groups or GKE with GPUDirect enabled.
# See https://cloud.google.com/compute/docs/gpus/gpudirect

# Inside the instance: verify GPU networking
nvidia-smi         # Check GPU status
ip link show       # Show gVNIC interfaces
# Note: standard ibstat/ibv_devinfo do not apply to GCP's
# custom GPUDirect-TCPXO stack. Use NCCL tests to verify
# GPU-to-GPU communication bandwidth.
```

<div class="note">

Google Cloud's GPU networking uses custom transport stacks (GPUDirect-TCPX/TCPXO over gVNIC) rather than standard InfiniBand or RoCEv2. While this provides excellent performance within GCP, it means that networking code and configurations are less portable to other environments. Applications using standard frameworks (MPI, NCCL) abstract these differences through GCP-provided NCCL plugins, but applications using libibverbs directly will not work without adaptation.

</div>

### Multi-Slice Training

GCP offers a unique feature called **multi-slice** training, which allows distributed ML training across multiple groups of GPUs (slices) connected via RDMA. Each slice is a set of 8 GPUs with full-bandwidth internal connectivity, and slices are connected through the data center network with RDMA support. This enables training jobs that span hundreds or thousands of GPUs.

## Oracle Cloud Infrastructure (OCI)

Oracle Cloud offers a distinctive approach: **bare-metal RDMA clusters** without virtualization overhead, using RoCE v2 rather than InfiniBand.[^3]

### Bare-Metal GPU Clusters

OCI provides bare-metal instances with direct RDMA access over RoCE v2:

- **BM.GPU.H100.8**: 8x H100 GPUs with ConnectX-7 NICs over RoCE v2. Full bare-metal access -- no hypervisor.
- **BM.GPU.A100-v2.8**: 8x A100 GPUs with 16x 100 Gbps ConnectX NICs over RoCE v2 (1,600 Gbps total RDMA bandwidth).
- **BM.HPC2.36**: CPU-only HPC instances with cluster networking.

**RDMA cluster networking**: OCI provisions dedicated RoCE v2 cluster networks for customer clusters, providing a non-oversubscribed, low-latency interconnect with single-digit microsecond latency. Customers get bare-metal access to the ConnectX NICs and can run standard RDMA applications using libibverbs (RoCE v2 supports the same verbs API as InfiniBand).

```bash
# OCI: launch bare-metal GPU cluster
oci compute instance launch \
    --shape BM.GPU.H100.8 \
    --cluster-network-id ocid1.clusternetwork... \
    --availability-domain AD-1

# Inside the instance: full bare-metal RoCE v2 access
ibstat       # Shows physical ConnectX-7 HCA
ibv_devinfo  # Standard verbs diagnostics
show_gids    # RoCEv2 GID table (IP-based addressing)
```

### OCI Supercluster

OCI offers **Superclusters** -- pre-built clusters of up to tens of thousands of bare-metal GPU nodes with a dedicated RoCE v2 cluster network. These are designed for large-scale ML training and provide the closest cloud analog to an on-premises supercomputer.

## Comparison Table

| Feature              | Azure                  | AWS                    | GCP                    | OCI                    |
|----------------------|------------------------|------------------------|------------------------|------------------------|
| **RDMA type**        | InfiniBand (IB)        | EFA/SRD (custom)       | Custom (GPUDirect-TCPX/O) | RoCE v2             |
| **Max GPU/node**     | 8x H100                | 8x H100                | 8x H100                | 8x H100                |
| **Max BW/node**      | 3.2 Tbps (NDR)         | 3.2 Tbps (EFA)         | 1.8 Tbps (A3 Mega)    | RoCE v2 cluster net    |
| **API**              | libibverbs             | Libfabric              | gVNIC / custom         | libibverbs             |
| **Virtualization**   | SR-IOV VF              | Nitro (custom)         | gVNIC + GPUDirect      | Bare metal             |
| **Managed fabric**   | Yes (Subnet Manager)   | Yes (SRD)              | Yes (Jupiter)          | Yes (customer fabric)  |
| **RDMA Read support**| Yes                    | Yes (Nitro v4+)[^1]    | N/A (custom transport) | Yes                    |
| **Live migration**   | No (IB VMs)            | No (EFA instances)     | No (GPU instances)     | N/A (bare metal)       |
| **Max cluster size** | Thousands of GPUs      | Tens of thousands      | Tens of thousands      | Tens of thousands      |
| **CPU HPC instances**| HB/HC series           | Hpc6a/Hpc7a            | C2D HPC                | BM.HPC2                |

<div class="warning">

Cloud RDMA performance can vary significantly based on placement. Always use **placement groups** (Azure: proximity placement groups, AWS: cluster placement groups, GCP: compact placement policy) to ensure your instances are on the same network fabric segment. Without placement groups, instances may be placed across different data center pods, introducing additional network hops and reducing RDMA bandwidth by 50% or more.

</div>

## Selecting a Cloud Provider for RDMA

The choice of cloud provider for RDMA workloads depends on several factors:

**Choose Azure when:**
- You need standard InfiniBand with libibverbs compatibility
- You want managed InfiniBand infrastructure at scale
- Your workload uses tools from the traditional HPC ecosystem (MPI, Lustre, Slurm)

**Choose AWS when:**
- You need the largest cluster scale (20,000+ GPUs)
- Your application uses Libfabric or frameworks that support EFA (MPI, NCCL)
- You want RDMA-like performance integrated with the broader AWS ecosystem

**Choose GCP when:**
- You want deep integration with Google's ML ecosystem (TPUs, Vertex AI)
- Your workload benefits from multi-slice training architectures
- You need large-scale GPU clusters with managed RDMA

**Choose OCI when:**
- You need bare-metal RDMA performance without virtualization overhead
- You want full control over the NIC configuration (bare-metal access to ConnectX NICs)
- Your workload uses standard libibverbs over RoCE v2

The cloud RDMA landscape is converging on a common goal -- bare-metal-equivalent performance with cloud-native management -- but through divergent paths. Azure bets on standard InfiniBand. AWS built a custom protocol stack from scratch. Google integrated RDMA-like semantics into its existing proprietary networking. OCI skips virtualization entirely and hands you bare metal. For applications built on portable frameworks (MPI, NCCL), these differences are largely abstracted away. For anything using libibverbs directly, portability across clouds requires careful attention to which verbs, transports, and features each provider actually supports.

<div class="warning">

Cloud provider GPU instance specifications change frequently -- new instance types launch, bandwidth numbers increase, and feature support evolves. The specifications in this section reflect the state at time of writing. Always verify current specs against the provider's official documentation before making architectural decisions.

</div>

[^1]: EFA RDMA verb support varies by Nitro generation. P4d.24xlarge (Nitro v3) supports RDMA Read but not Write. Nitro v4+ instances (P5, Hpc7a) support both Read and Write. See [AWS EFA documentation](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/efa.html).

[^2]: GCP A3 Mega network bandwidth per [Google Cloud GPU networking documentation](https://docs.cloud.google.com/compute/docs/gpus/gpu-network-bandwidth). GCP uses GPUDirect-TCPXO, a custom offloaded networking stack, not standard InfiniBand or RoCE.

[^3]: OCI uses RDMA over Converged Ethernet v2 (RoCE v2) with NVIDIA ConnectX-7 NICs for its cluster networks. See ["OCI Accelerates HPC, AI, and Database Using RoCE and NVIDIA ConnectX"](https://blogs.oracle.com/cloud-infrastructure/oci-accelerates-hpc-ai-db-roce-nvidia-connectx).

[^4]: Azure InfiniBand VM sizes and specifications are documented at [Microsoft Learn: HPC VM sizes](https://learn.microsoft.com/en-us/azure/virtual-machines/sizes/high-performance-compute). The HC-series uses EDR (100 Gbps) per [HC size series documentation](https://learn.microsoft.com/en-us/azure/virtual-machines/sizes/high-performance-compute/hc-series).
