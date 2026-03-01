# 15.4 Cloud Provider Implementations

Each major cloud provider has developed a distinct approach to delivering RDMA capabilities in their infrastructure. These approaches reflect different engineering philosophies, existing infrastructure investments, and target workload priorities. Understanding the differences is essential for architects designing applications that must run across multiple clouds or selecting the best cloud platform for RDMA-intensive workloads.

## Microsoft Azure

Azure has made the most aggressive investment in RDMA infrastructure among the major cloud providers, deploying both InfiniBand and RoCEv2 at massive scale.

### InfiniBand for HPC and AI VMs

Azure offers InfiniBand-connected VM instances for HPC and AI workloads:

- **HB-series** (HPC): AMD EPYC processors with HDR InfiniBand (200 Gbps). Designed for computational fluid dynamics, weather modeling, and other tightly-coupled HPC workloads.
- **HC-series** (HPC): Intel Xeon processors with HDR InfiniBand. Optimized for compute-intensive workloads.
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
- **Reliable**: SRD provides reliable delivery with selective retransmission, without requiring the lossless fabric that InfiniBand and RoCEv2 depend on.

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
- **No one-sided RDMA Read**: EFA supports RDMA Write and Send/Receive but does not support RDMA Read. Applications relying on RDMA Read must be redesigned.
- **Security group enforcement**: EFA traffic passes through AWS security groups, adding a small amount of latency compared to bare-metal RDMA but providing cloud-standard network security.

## Google Cloud Platform (GCP)

Google Cloud has developed a custom networking stack that integrates RDMA capabilities into its proprietary infrastructure.

### gVNIC and RDMA

Google's **gVNIC (Google Virtual NIC)** is a custom virtual network interface that replaces the standard virtio-net in GCP VMs. For GPU-accelerated instances, gVNIC supports RDMA capabilities through Google's custom networking fabric.

### GPU Instance Types

- **A3 Mega**: 8x H100 GPUs with 8x ConnectX-7 NICs (400 Gbps each). Uses InfiniBand via Google's **Jupiter** network fabric. Provides 3.2 Tbps aggregate RDMA bandwidth.
- **A3 High**: 8x H100 GPUs with 4x ConnectX-7 NICs. Provides 1.6 Tbps aggregate bandwidth.
- **A2 Ultra**: 8x A100 80GB GPUs with multi-NIC RDMA support.

### Jupiter and Custom Networking

Google's networking infrastructure is built on custom-designed switches and a proprietary protocol stack:

- **Jupiter fabric**: Google's data center network fabric providing petabit-scale bisection bandwidth.
- **Snap**: A user-space networking stack that handles packet processing, virtualization, and network function offload.
- **Pony Express**: Google's custom transport protocol that provides RDMA-like semantics over the Jupiter fabric.

For GPU instances, Google provisions dedicated InfiniBand or RoCE network segments with GPUDirect RDMA support, enabling NCCL-based distributed training with near-bare-metal performance.

```bash
# GCP: create A3 Mega GPU instance
gcloud compute instances create gpu-node-01 \
    --machine-type=a3-megagpu-8g \
    --zone=us-central1-a \
    --maintenance-policy=TERMINATE \
    --accelerator=count=8,type=nvidia-h100-mega-80gb \
    --network-interface=nic-type=GVNIC

# Verify RDMA inside the instance
ibstat
ibv_devinfo
```

<div class="note">

Google Cloud's RDMA implementation is tightly integrated with its custom infrastructure and is less "standard" than Azure's InfiniBand or even AWS's EFA. While this provides excellent performance within GCP, it means that networking code and configurations are less portable to other environments. Applications using standard frameworks (MPI, NCCL, Libfabric) abstract these differences, but custom RDMA applications may require GCP-specific adaptations.

</div>

### Multi-Slice Training

GCP offers a unique feature called **multi-slice** training, which allows distributed ML training across multiple groups of GPUs (slices) connected via RDMA. Each slice is a set of 8 GPUs with full-bandwidth internal connectivity, and slices are connected through the data center network with RDMA support. This enables training jobs that span hundreds or thousands of GPUs.

## Oracle Cloud Infrastructure (OCI)

Oracle Cloud offers a distinctive approach: **bare-metal RDMA clusters** without virtualization overhead.

### Bare-Metal GPU Clusters

OCI provides bare-metal instances with direct InfiniBand access:

- **BM.GPU.H100.8**: 8x H100 GPUs with 8x ConnectX-7 (NDR, 400 Gbps each). Full bare-metal access -- no hypervisor.
- **BM.GPU.A100-v2.8**: 8x A100 GPUs with 8x HDR InfiniBand (200 Gbps each).
- **BM.HPC2.36**: CPU-only HPC instances with HDR InfiniBand.

**RDMA cluster networking**: OCI provisions dedicated InfiniBand fabrics for customer clusters, providing a non-oversubscribed, low-latency interconnect. Customers get bare-metal access to the InfiniBand HCA and can run standard RDMA applications without any cloud abstraction layer.

```bash
# OCI: launch bare-metal GPU cluster
oci compute instance launch \
    --shape BM.GPU.H100.8 \
    --cluster-network-id ocid1.clusternetwork... \
    --availability-domain AD-1

# Inside the instance: full bare-metal InfiniBand access
ibstat       # Shows physical ConnectX-7 HCA
ibping       # Standard IB diagnostics work
opensm       # Can even run Subnet Manager (if needed)
```

### OCI Supercluster

OCI offers **Superclusters** -- pre-built clusters of up to thousands of bare-metal GPU nodes with a dedicated InfiniBand fabric. These are designed for large-scale ML training and provide the closest cloud analog to an on-premises supercomputer.

## Comparison Table

| Feature              | Azure                  | AWS                    | GCP                    | OCI                    |
|----------------------|------------------------|------------------------|------------------------|------------------------|
| **RDMA type**        | InfiniBand (IB)        | EFA/SRD (custom)       | IB + custom            | InfiniBand (IB)        |
| **Max GPU/node**     | 8x H100                | 8x H100                | 8x H100                | 8x H100                |
| **Max BW/node**      | 3.2 Tbps (NDR)         | 3.2 Tbps (EFA)         | 3.2 Tbps               | 3.2 Tbps (NDR)         |
| **API**              | libibverbs             | Libfabric              | libibverbs / custom    | libibverbs             |
| **Virtualization**   | SR-IOV VF              | Nitro (custom)         | gVNIC + passthrough    | Bare metal             |
| **Managed fabric**   | Yes (Subnet Manager)   | Yes (SRD)              | Yes (Jupiter)          | Yes (customer fabric)  |
| **RDMA Read support**| Yes                    | No                     | Yes                    | Yes                    |
| **Live migration**   | No (IB VMs)            | No (EFA instances)     | No (GPU instances)     | N/A (bare metal)       |
| **Max cluster size** | 4,500+ GPUs            | 20,000+ GPUs           | 26,000+ GPUs           | 16,000+ GPUs           |
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
- You want full control over the InfiniBand fabric configuration
- Your workload requires standard InfiniBand tools and diagnostics

The cloud RDMA landscape is converging toward a common goal: providing bare-metal-equivalent RDMA performance with cloud-native management, security, and scalability. The approaches differ in how they balance standardization versus customization, but the trajectory is clear -- RDMA is becoming a standard cloud infrastructure capability, not a specialized exception.
