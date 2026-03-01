# 18.3 GPUDirect RDMA

The convergence of GPU computing and RDMA networking has produced one of the most impactful technologies in modern data center architecture. GPUDirect RDMA enables the NIC to read from and write to GPU memory directly, eliminating the CPU and system memory from the data path entirely. This capability is fundamental to distributed machine learning training, GPU-accelerated databases, and any workload that needs to move data between GPUs on different machines at high speed.

## Evolution of GPUDirect

GPUDirect has evolved through several generations, each removing a data copy from the GPU-to-network path.

### GPUDirect 1.0: Shared Pinned Host Memory (2010)

The first generation eliminated one copy by allowing the GPU driver and the NIC driver to share the same pinned host memory buffer. Previously, transferring data from GPU to network required:

```text
Before GPUDirect 1.0:
  GPU Memory → GPU DMA → Host Buffer A → memcpy → Host Buffer B → NIC DMA → Network
  (3 data movements, 2 host buffers)

GPUDirect 1.0:
  GPU Memory → GPU DMA → Shared Host Buffer → NIC DMA → Network
  (2 data movements, 1 host buffer)
```

This was a useful optimization but still required data to pass through host system memory.

### GPUDirect 2.0: Peer-to-Peer (2011)

GPUDirect Peer-to-Peer (P2P) enabled direct memory access between GPUs on the same PCIe bus, without going through system memory. While not directly related to networking, P2P established the PCIe BAR-mapping techniques that would later enable GPUDirect RDMA.

### GPUDirect RDMA (2013)

GPUDirect RDMA is the breakthrough that removed the host CPU and system memory from the data path entirely. The NIC accesses GPU memory directly through PCIe peer-to-peer transactions.

```text
GPUDirect RDMA:
  GPU Memory ←→ PCIe Peer-to-Peer ←→ NIC ←→ Network
  (1 data movement, no host memory involved)
```

## Architecture: How GPUDirect RDMA Works

GPUDirect RDMA relies on PCIe Base Address Register (BAR) mapping to enable direct NIC-to-GPU communication.

### PCIe BAR Mapping

Every PCIe device exposes its memory through BARs -- address ranges that other devices can access via PCIe transactions. GPUs expose their device memory (VRAM) through a BAR. When GPUDirect RDMA is enabled, the NIC can issue PCIe read and write transactions targeting the GPU's BAR address range, directly accessing GPU memory without involving the CPU or system memory controller.

```text
PCIe Topology for GPUDirect RDMA:

  CPU
   │
  Root Complex
   │
  PCIe Switch (or Root Complex internal switching)
   ├── GPU (BAR exposes GPU memory)
   │    ↑
   │    │ PCIe P2P Read/Write
   │    ↓
   └── NIC (initiates PCIe transactions to GPU BAR)
```

### Memory Registration with GPU Memory

From the application's perspective, GPUDirect RDMA works through the standard RDMA verbs interface. The key difference is that the memory buffer passed to `ibv_reg_mr()` is a GPU memory pointer (obtained via CUDA) rather than a host memory pointer.

```c
#include <cuda_runtime.h>
#include <infiniband/verbs.h>

// Allocate GPU memory
void *gpu_buf;
cudaMalloc(&gpu_buf, buffer_size);

// Register GPU memory as an RDMA memory region
// The nvidia_peermem module enables this to work
struct ibv_mr *mr = ibv_reg_mr(pd, gpu_buf, buffer_size,
    IBV_ACCESS_LOCAL_WRITE |
    IBV_ACCESS_REMOTE_WRITE |
    IBV_ACCESS_REMOTE_READ);

if (!mr) {
    fprintf(stderr, "Failed to register GPU memory: %s\n",
            strerror(errno));
    // Check that nvidia_peermem module is loaded
}

// Use the MR exactly as you would with host memory
struct ibv_sge sge = {
    .addr   = (uint64_t)gpu_buf,
    .length = buffer_size,
    .lkey   = mr->lkey,
};

// Post RDMA operations targeting GPU memory
struct ibv_send_wr wr = {
    .sg_list = &sge,
    .num_sge = 1,
    .opcode  = IBV_WR_RDMA_WRITE,
    .wr.rdma = {
        .remote_addr = remote_gpu_addr,
        .rkey        = remote_rkey,
    },
};
ibv_post_send(qp, &wr, &bad_wr);
```

### The nvidia_peermem Kernel Module

The `nvidia_peermem` kernel module (previously called `nv_peer_memory`) is the glue that makes GPUDirect RDMA work. It registers with the RDMA subsystem as a "peer memory client," enabling `ibv_reg_mr()` to accept GPU memory pointers. When the NIC needs to access GPU memory, nvidia_peermem translates the virtual address to the GPU's PCIe BAR address.

```bash
# Load the nvidia_peermem module
modprobe nvidia_peermem

# Verify it is loaded
lsmod | grep nvidia_peermem

# Check that it registered with the RDMA subsystem
cat /sys/kernel/mm/memory_peers/nv_mem/version
```

<div class="warning">

**Warning:** GPUDirect RDMA requires the `nvidia_peermem` module to be loaded before any GPU memory is registered for RDMA. Without it, `ibv_reg_mr()` with a GPU pointer will fail with EFAULT or EINVAL. This is a common setup issue when deploying GPUDirect RDMA for the first time.

</div>

## Performance: Why GPUDirect RDMA Matters

The performance impact of GPUDirect RDMA is substantial and directly measurable:

### Latency Reduction

Without GPUDirect RDMA, sending GPU data over the network requires:
1. GPU-to-host DMA (cudaMemcpy): ~5-10 microseconds
2. Host-to-NIC DMA (RDMA post): ~1-2 microseconds
3. Network transit: ~1-2 microseconds
4. Total: ~7-14 microseconds

With GPUDirect RDMA:
1. GPU-to-NIC PCIe P2P (RDMA post): ~1-3 microseconds
2. Network transit: ~1-2 microseconds
3. Total: ~2-5 microseconds

The latency improvement is roughly 2-3x, primarily because the cudaMemcpy step is eliminated.

### Throughput Improvement

System memory bandwidth is a shared resource. Without GPUDirect RDMA, every byte transferred between GPU and network consumes system memory bandwidth twice (GPU-to-host DMA in, host-to-NIC DMA out). With GPUDirect RDMA, system memory bandwidth is not consumed at all.

For a system with multiple GPUs and multiple NICs, this can be the difference between saturating the network and being bottlenecked on system memory bandwidth.

### CPU Savings

Without GPUDirect RDMA, the CPU must orchestrate the data movement: initiate the cudaMemcpy, wait for completion, then post the RDMA operation. With GPUDirect RDMA, the CPU posts a single RDMA work request, and the NIC handles everything. The CPU savings are significant in GPU-heavy workloads where CPU cycles are at a premium.

## GPUDirect Storage

GPUDirect Storage extends the GPUDirect concept to NVMe storage devices:

```text
Traditional GPU Data Loading:
  NVMe SSD → DMA → System Memory → cudaMemcpy → GPU Memory
  (CPU orchestrates, system memory is bottleneck)

GPUDirect Storage:
  NVMe SSD → NIC or DMA → GPU Memory
  (CPU not involved in data movement)
```

GPUDirect Storage uses the NIC as a DMA engine between NVMe devices and GPU memory, bypassing both the CPU and system memory. This is particularly valuable for AI training workloads that need to load large datasets from storage directly into GPU memory.

```bash
# GPUDirect Storage requires:
# 1. A compatible NIC (ConnectX-5 or later)
# 2. A compatible NVMe device
# 3. The nvidia_fs kernel module
modprobe nvidia_fs
```

<div class="note">

**Note:** GPUDirect Storage works best when the NVMe device and GPU are on the same PCIe switch or root complex, enabling direct P2P transfers. If they are on different PCIe hierarchies, transfers may fall back to system memory bounce buffers, negating the benefit.

</div>

## PCIe Topology: Critical for Performance

GPUDirect RDMA performance depends heavily on the PCIe topology. The best performance occurs when the GPU and NIC are on the same PCIe switch, enabling direct P2P transactions without traversing the CPU's root complex.

```text
Optimal Topology (same PCIe switch):
  CPU Root Complex
   │
  PCIe Switch
   ├── GPU 0  ←──P2P──→  NIC 0
   │   (low latency, full bandwidth)

Suboptimal Topology (different root complexes):
  CPU 0 Root Complex        CPU 1 Root Complex
   │                         │
  GPU 0                     NIC 0
   │←── P2P through QPI/UPI ──→│
   (higher latency, reduced bandwidth)
```

Use `nvidia-smi topo --matrix` to visualize the GPU-NIC topology:

```bash
$ nvidia-smi topo --matrix
        GPU0  GPU1  NIC0  NIC1  CPU
GPU0     X    NV12  PIX   SYS   0
GPU1    NV12   X    SYS   PIX   1
NIC0    PIX   SYS    X    SYS   0
NIC1    SYS   PIX   SYS    X    1
```

The topology legend:
- **PIX:** Same PCIe switch (optimal for GPUDirect RDMA)
- **PHB:** Same PCIe root complex (good)
- **SYS:** Cross-socket, through QPI/UPI (suboptimal)
- **NV:** Connected via NVLink (GPU-to-GPU only)

<div class="tip">

**Tip:** In multi-GPU, multi-NIC systems, pair each GPU with its topologically closest NIC. For distributed training frameworks like NCCL, this pairing is configured via the `NCCL_NET_GDR_LEVEL` environment variable and the `CUDA_VISIBLE_DEVICES` / `NCCL_SOCKET_IFNAME` mappings.

</div>

## NVLink and NVSwitch: Beyond PCIe

For GPU-to-GPU communication within a node, PCIe bandwidth is often insufficient. NVIDIA's NVLink provides a much higher-bandwidth interconnect between GPUs:

| Interconnect | Bandwidth (bidirectional) | Latency |
|---|---|---|
| PCIe Gen4 x16 | 32 GB/s | ~1 us |
| PCIe Gen5 x16 | 64 GB/s | ~1 us |
| NVLink 3.0 (A100) | 600 GB/s (total) | ~0.7 us |
| NVLink 4.0 (H100) | 900 GB/s (total) | ~0.5 us |

**NVSwitch** extends NVLink from point-to-point connections to a full crossbar, connecting all GPUs in a node with NVLink bandwidth. In an NVIDIA DGX H100 system, all 8 GPUs are connected through NVSwitch with 900 GB/s aggregate bandwidth.

For inter-node communication, the data path is GPU → NVLink → NVSwitch → GPU → PCIe → NIC → Network → NIC → PCIe → GPU → NVSwitch → NVLink → GPU. The NVLink portion is extremely fast, but the network hop (through NICs and switches) remains the bottleneck. This is why GPUDirect RDMA matters: it minimizes the NIC-to-GPU latency on each side.

## Applications

### Distributed ML Training

The dominant application for GPUDirect RDMA is distributed training of large machine learning models. Frameworks like PyTorch, TensorFlow, and JAX use NCCL (NVIDIA Collective Communication Library) for multi-GPU, multi-node communication. NCCL uses GPUDirect RDMA transparently:

```bash
# Enable GPUDirect RDMA in NCCL
export NCCL_NET_GDR_LEVEL=5    # Enable GDR for all transfers
export NCCL_IB_HCA=mlx5_0      # Specify which NIC to use
export NCCL_DEBUG=INFO          # Enable debug logging

# Run distributed training
torchrun --nproc_per_node=8 --nnodes=4 \
    --rdzv_endpoint=master:29500 train.py
```

During training, GPUs exchange gradient tensors using AllReduce operations. With GPUDirect RDMA, gradient data moves directly from GPU memory on one node to GPU memory on another node, without any CPU involvement or system memory copies.

### GPU-Accelerated Databases

GPU databases like RAPIDS cuDF, BlazingSQL, and Kinetica use GPUDirect RDMA to transfer query results and intermediate data between nodes without CPU staging. This enables distributed SQL queries where the data remains in GPU memory throughout the query pipeline.

### Real-Time Inference Pipelines

Inference serving systems use GPUDirect RDMA to stream input data directly to GPU memory and stream results directly from GPU memory, minimizing the latency overhead that would otherwise make real-time inference impractical for latency-sensitive applications.

The combination of GPUDirect RDMA and GPUDirect Storage creates a zero-copy pipeline from storage to GPU to network to remote GPU, handling the entire data path without CPU data movement involvement. This pipeline is the foundation of modern AI infrastructure.
