# 14.5 Machine Learning and AI

Machine learning -- and large-scale deep learning in particular -- has become the most important growth driver for RDMA technology. Training a modern large language model requires synchronizing gradients across thousands of GPUs, generating aggregate network traffic measured in petabits per second. Inference serving at scale demands rapid distribution of model weights and low-latency inter-stage communication for pipeline-parallel serving architectures. In both cases, RDMA's microsecond-scale latency and near-line-rate bandwidth are not optional optimizations but fundamental requirements that determine whether a training job completes in days or weeks.

## Why RDMA Matters for Distributed Training

Modern deep learning models are too large to train on a single GPU. Distributed training partitions the workload across multiple GPUs using one or more parallelism strategies, each with distinct communication requirements.

### Data Parallelism

In data parallelism, each GPU holds a complete copy of the model and processes a different mini-batch of training data. After each forward and backward pass, the GPUs must synchronize their gradients so that every copy of the model stays identical. This synchronization typically uses an **AllReduce** collective operation.

The communication volume per iteration is proportional to the model size. For a model with P parameters in 16-bit floating point:
- **AllReduce data volume**: 2P bytes per GPU (each GPU sends and receives approximately P bytes in a ring AllReduce)
- **GPT-3 (175B parameters)**: 350 GB of gradient data per iteration, per GPU
- **Llama-3 70B**: 140 GB of gradient data per iteration, per GPU

At these volumes, the gradient synchronization time directly impacts training throughput. With 400 Gbps NDR InfiniBand (50 GB/s effective bandwidth), a 350 GB AllReduce takes approximately 7 seconds in the ideal case -- comparable to the computation time on a modern GPU. Any additional network overhead directly reduces GPU utilization.

### Model Parallelism

In model parallelism, the model is partitioned across GPUs, with each GPU holding a subset of the model's layers or parameters. During forward and backward passes, activations and gradients must be communicated between GPUs at every partition boundary. This communication is on the critical path -- GPUs stall while waiting for data from their peers.

Model parallelism is extremely latency-sensitive. Even microseconds of network overhead per communication step accumulate across the hundreds of partition boundaries in a deep network. RDMA's sub-microsecond verbs-level latency is essential for keeping GPU utilization high.

### Pipeline Parallelism

Pipeline parallelism partitions the model into stages, each assigned to a different set of GPUs. Multiple micro-batches are processed simultaneously, with each stage forwarding its output to the next. Communication occurs at stage boundaries and is latency-sensitive but involves moderate data volumes (proportional to the activation size at the boundary, not the full model).

```
Data Parallelism:          Model Parallelism:         Pipeline Parallelism:

GPU0: Full Model           GPU0: Layers 1-4          Stage 1    Stage 2    Stage 3
GPU1: Full Model           GPU1: Layers 5-8          ┌──────┐  ┌──────┐  ┌──────┐
GPU2: Full Model           GPU2: Layers 9-12         │GPU0,1│─►│GPU2,3│─►│GPU4,5│
GPU3: Full Model           GPU3: Layers 13-16        │      │  │      │  │      │
      │                          │                    └──────┘  └──────┘  └──────┘
   AllReduce               Point-to-point              Micro-batch pipeline
   (gradients)             (activations)              (activations + gradients)
```

## GPUDirect RDMA

GPUDirect RDMA is an NVIDIA technology that allows the RDMA NIC to read from and write to GPU memory directly, without staging data through host (CPU) system memory. This eliminates two memory copies (GPU-to-host and host-to-GPU) and reduces latency significantly.

### Data Path

Without GPUDirect RDMA:
```
GPU Memory → PCIe → CPU/System Memory → PCIe → RDMA NIC → Network
                    (copy 1)                    (copy 2)
```

With GPUDirect RDMA:
```
GPU Memory → PCIe → RDMA NIC → Network
             (direct DMA, zero-copy)
```

### Requirements and Configuration

GPUDirect RDMA requires:
- NVIDIA GPU (Kepler generation or newer, recommended: Ampere/Hopper)
- NVIDIA RDMA-capable NIC (ConnectX-4 or newer)
- `nvidia-peermem` kernel module (replaces the older `nv_peer_mem`)
- GPU and NIC on the same PCIe root complex or connected via NVSwitch for optimal performance

```bash
# Load GPUDirect RDMA kernel module
modprobe nvidia-peermem

# Verify GPUDirect RDMA is active
cat /sys/kernel/mm/memory_peers/nv_mem/version

# Check GPU-NIC topology
nvidia-smi topo -m
# Look for "PIX" (same PCIe switch) or "PXB" (same PCIe root complex)
# connections between GPU and NIC
```

<div class="warning">

GPU-NIC topology is critical for GPUDirect RDMA performance. If a GPU and its RDMA NIC are on different NUMA nodes, data must traverse the CPU's inter-socket interconnect (QPI/UPI), which halves effective bandwidth and doubles latency. Always verify topology with `nvidia-smi topo -m` and configure NCCL or your application to use the NIC closest to each GPU. NVIDIA DGX systems are specifically designed with optimal GPU-NIC topology.

</div>

## NCCL over RDMA

NCCL (NVIDIA Collective Communications Library) is the standard communication library for distributed deep learning on NVIDIA GPUs. It provides optimized collective operations (AllReduce, AllGather, ReduceScatter, Broadcast) that automatically leverage GPUDirect RDMA for inter-node communication.

### Ring and Tree AllReduce

NCCL implements multiple AllReduce algorithms:

**Ring AllReduce**: Data is partitioned into chunks, and each GPU sends one chunk to its neighbor in a ring topology. After N-1 steps (where N is the number of GPUs), all GPUs have the complete reduced result. Ring AllReduce achieves near-optimal bandwidth utilization: each GPU sends and receives approximately 2(N-1)/N times the data volume.

**Tree AllReduce**: Uses a binary tree topology. Data is reduced up the tree (Reduce phase) and then broadcast down (Broadcast phase). Tree AllReduce achieves lower latency for small messages (O(log N) steps vs O(N) for ring) but lower bandwidth utilization for large messages.

NCCL automatically selects the optimal algorithm based on message size, number of GPUs, and network topology. For large gradient buffers typical in deep learning, the ring algorithm is usually selected.

### Configuration for RDMA

```bash
# Essential NCCL environment variables for RDMA training
export NCCL_DEBUG=INFO                   # Enable debug logging
export NCCL_IB_DISABLE=0                # Enable InfiniBand (default)
export NCCL_IB_HCA=mlx5                 # RDMA device name prefix
export NCCL_IB_GID_INDEX=3              # GID index for RoCEv2
export NCCL_NET_GDR_LEVEL=5             # GPUDirect RDMA level
export NCCL_IB_QPS_PER_CONNECTION=4     # QPs per connection
export NCCL_IB_TC=128                   # Traffic class for QoS
export NCCL_CROSS_NIC=2                 # Enable cross-NIC communication
export NCCL_IB_TIMEOUT=23               # QP timeout (increase for large clusters)
```

## Parameter Server Architecture with RDMA

The parameter server architecture is an alternative to AllReduce for gradient synchronization. In this model, one or more dedicated parameter servers store the global model parameters. Workers compute gradients on their local data and push them to the parameter servers, which aggregate the gradients, update the model, and allow workers to pull the updated parameters.

RDMA benefits the parameter server architecture in two ways:

1. **Push/Pull latency**: RDMA reduces the round-trip time for gradient push and parameter pull operations, reducing worker idle time.
2. **Server-side aggregation**: Using RDMA Write for gradient push allows multiple workers to write gradients to the server concurrently without server CPU involvement. The server CPU only needs to perform the aggregation step.

Some parameter server implementations use one-sided RDMA Write for gradient push, achieving much higher throughput than Send/Receive because the server NIC handles the data placement without CPU involvement.

## Large Model Training: Communication Analysis

For the largest models being trained today, communication overhead is the dominant factor limiting scalability:

| Model Scale   | Parameters | Gradient Size (FP16) | Time per AllReduce (400G IB) |
|--------------|------------|----------------------|------------------------------|
| 7B           | 7B         | 14 GB                | ~0.3 s                       |
| 70B          | 70B        | 140 GB               | ~2.8 s                       |
| 175B (GPT-3) | 175B      | 350 GB               | ~7.0 s                       |
| 540B (PaLM)  | 540B       | 1.08 TB              | ~21.6 s                      |

These numbers assume a single NIC per GPU. Modern training clusters use multiple techniques to hide communication latency:

- **Gradient compression**: Reducing gradient precision (FP16, BF16, or even INT8) to reduce communication volume.
- **Gradient accumulation**: Accumulating gradients over multiple micro-batches before synchronizing, amortizing communication cost.
- **Computation-communication overlap**: Issuing RDMA operations for earlier layers' gradients while still computing later layers' gradients.
- **Hierarchical AllReduce**: Using NVLink/NVSwitch for intra-node reduction before RDMA for inter-node reduction.

<div class="note">

Computation-communication overlap is where RDMA truly shines compared to TCP. Because RDMA operations are asynchronous and require no CPU involvement, the GPU compute kernels and the RDMA data transfers can proceed in parallel without competing for CPU cycles. With TCP, protocol processing would consume CPU resources needed for gradient computation preprocessing, limiting the achievable overlap.

</div>

## RDMA in Inference

While training has received the most attention, RDMA is increasingly important for large-scale inference serving:

### KV Cache Transfer

Large language model inference uses a key-value (KV) cache to store intermediate attention states. In disaggregated serving architectures (where prefill and decode phases run on different GPU groups), the KV cache must be transferred between machines. For a 70B model with a long context window, the KV cache can be several gigabytes, and transfer latency directly impacts time-to-first-token.

### Expert Routing in Mixture-of-Experts

Mixture-of-Experts (MoE) models route different tokens to different expert sub-networks. When experts are distributed across machines, RDMA enables low-latency token routing with minimal overhead.

### Model Weight Distribution

When serving multiple models or updating model weights, RDMA enables rapid distribution of model parameters across the serving cluster.

## RoCE vs InfiniBand for ML Clusters

The choice between RoCE and InfiniBand for ML clusters involves several tradeoffs:

| Factor                | InfiniBand            | RoCEv2                    |
|-----------------------|-----------------------|---------------------------|
| Latency               | ~1 μs                 | ~2 μs                     |
| Bandwidth (max)       | 400 Gbps (NDR)        | 400 Gbps (400GbE)         |
| Congestion control    | Credit-based (lossless)| ECN/PFC (requires tuning) |
| Multi-tenancy         | Partitioning (pkeys)  | VLANs                     |
| Cost                  | Higher (dedicated HCA)| Lower (Ethernet NICs)     |
| Fabric management     | Subnet Manager        | Standard Ethernet         |
| Lossless behavior     | Built-in              | Requires PFC configuration|
| Scale                 | Proven at 10K+ nodes  | Proven at 10K+ nodes      |

InfiniBand remains the dominant choice for the largest ML training clusters due to its built-in lossless fabric semantics, lower latency, and proven reliability at scale. However, RoCEv2 is gaining ground in cloud environments where Ethernet infrastructure is already deployed and operators have invested in PFC/ECN tuning.

## Real-World Deployments

### NVIDIA DGX / SuperPOD

NVIDIA's DGX systems are purpose-built for large-scale ML training with optimal RDMA configuration:

- **DGX H100**: 8x H100 GPUs connected via NVSwitch (intra-node), 8x ConnectX-7 400 Gbps NICs for RDMA (inter-node)
- **SuperPOD**: 32 DGX H100 nodes (256 GPUs) connected via a non-blocking NDR InfiniBand fabric
- **Total bisection bandwidth**: 51.2 Tbps per SuperPOD

### Cloud ML Instances

Cloud providers offer RDMA-capable instances for ML training (discussed further in Chapter 15):

- **Azure ND H100 v5**: 8x H100 + 8x ConnectX-7 (400G InfiniBand)
- **AWS P5**: 8x H100 + EFA (Elastic Fabric Adapter) with SRD transport
- **GCP A3 Mega**: 8x H100 + 8x ConnectX-7 (400G InfiniBand via Jupiter fabric)

<div class="tip">

When configuring RDMA for ML training, the single most impactful optimization is ensuring correct GPU-NIC affinity. Each GPU should be paired with the RDMA NIC on the same PCIe switch. On DGX systems, this mapping is pre-configured. On custom-built clusters, use `nvidia-smi topo -m` to verify the mapping, and configure NCCL with `NCCL_IB_HCA` to enforce the correct pairing. Incorrect affinity can reduce effective inter-node bandwidth by 50% or more.

</div>

The symbiotic relationship between RDMA and machine learning continues to deepen. As models grow larger and training clusters scale to tens of thousands of GPUs, the demands on the interconnect fabric intensify. RDMA is not merely a performance optimization for machine learning -- it is a fundamental enabler without which modern large-scale training would be impractical.
