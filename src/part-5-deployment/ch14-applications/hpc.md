# 14.2 High-Performance Computing: MPI and UCX

High-performance computing was the original proving ground for RDMA technology. Long before RDMA found its way into cloud data centers and machine learning clusters, HPC applications running on supercomputers relied on InfiniBand and RDMA to achieve the inter-node communication performance demanded by large-scale scientific simulations. Today, the HPC communication stack has matured into a sophisticated layered architecture, with the Message Passing Interface (MPI) at the top, hardware-abstraction libraries like UCX in the middle, and RDMA verbs at the bottom.

## MPI: The Standard HPC Communication API

The Message Passing Interface (MPI) is the dominant programming model for distributed-memory parallel computing. An MPI application consists of multiple processes, each with its own address space, that communicate by explicitly sending and receiving messages. The MPI standard defines a rich set of communication primitives -- point-to-point operations like `MPI_Send` and `MPI_Recv`, collective operations like `MPI_Allreduce` and `MPI_Bcast`, and one-sided operations like `MPI_Put` and `MPI_Get` -- that map naturally onto RDMA capabilities.

### MPI over RDMA

Modern MPI implementations use RDMA verbs to implement message passing with minimal overhead. The mapping from MPI semantics to RDMA operations depends on the message size and communication pattern:

**Eager protocol** (small messages, typically < 8-16 KB):
1. The sender copies the message data into a pre-registered eager buffer.
2. The sender issues an RDMA Send (or RDMA Write with Immediate) to transfer the data to a pre-posted receive buffer on the receiver.
3. The receiver processes the completion and copies data to the application buffer.

This protocol is optimized for latency: the sender does not wait for the receiver to be ready.

**Rendezvous protocol** (large messages, typically > 16 KB):
1. The sender sends a small control message (via RDMA Send) containing the message metadata and the remote memory key (rkey) of the send buffer.
2. The receiver registers its receive buffer (if not already registered) and sends back an acknowledgment with its own rkey.
3. The sender (or receiver, depending on the implementation) performs an RDMA Read or RDMA Write to transfer the data directly between application buffers -- zero-copy.
4. A final acknowledgment signals completion.

This protocol avoids the copy into eager buffers, which is critical for large messages where bandwidth, not latency, is the primary concern.

```
Eager Protocol (small messages):          Rendezvous Protocol (large messages):

Sender          Receiver                  Sender          Receiver
  │                │                        │                │
  │  RDMA Send     │                        │  RTS (Send)    │
  │  (data inline) │                        │  (rkey, addr)  │
  ├───────────────►│                        ├───────────────►│
  │                │                        │                │
  │                │ copy to                │  CTS (Send)    │
  │                │ app buffer             │  (rkey, addr)  │
  │                │                        │◄───────────────┤
  │                │                        │                │
                                            │  RDMA Write    │
                                            │  (zero-copy)   │
                                            ├───────────────►│
                                            │                │
                                            │  FIN (Send)    │
                                            ├───────────────►│
```

### Major MPI Implementations

**Open MPI** uses a modular architecture with pluggable communication frameworks. The **BTL (Byte Transfer Layer)** and **PML (Point-to-point Messaging Layer)** components handle RDMA transport. Open MPI can use UCX (described below) as its primary communication backend, which provides optimized RDMA support.

```bash
# Build Open MPI with UCX support
./configure --with-ucx=/usr/local/ucx
make -j$(nproc) && make install

# Run an MPI application over RDMA
mpirun -np 64 --hostfile hosts.txt \
    --mca pml ucx --mca btl ^vader,tcp,openib \
    ./my_application
```

**MVAPICH2** is specifically designed for InfiniBand and RDMA networks. Developed at Ohio State University, it includes extensive optimizations for RDMA communication, including adaptive rendezvous thresholds, RDMA-based collectives, and GPUDirect support.

```bash
# Run with MVAPICH2 (RDMA is the default transport)
mpirun -np 64 -hostfile hosts.txt \
    MV2_USE_RDMA_CM=1 \
    MV2_IBA_HCA=mlx5_0 \
    ./my_application
```

**Intel MPI** provides optimized RDMA support for both InfiniBand and RoCE networks, with automatic transport selection and tuning based on the detected network hardware.

<div class="note">

The choice of MPI implementation matters significantly for RDMA performance. MVAPICH2 typically achieves the lowest latency on InfiniBand networks due to its deep integration with RDMA verbs. Open MPI with UCX provides excellent performance with broader hardware support. Intel MPI offers the best integration with Intel architectures and tools. Benchmark your specific workload with each implementation before committing to one.

</div>

### MPI Latency Benchmarks

With modern hardware (ConnectX-7 HCA, HDR InfiniBand), MPI point-to-point latencies are remarkably low:

| Message Size | MPI Latency (RDMA) | MPI Latency (TCP) |
|-------------|--------------------|--------------------|
| 0 bytes     | ~0.6 μs            | ~3.5 μs            |
| 4 bytes     | ~0.7 μs            | ~3.6 μs            |
| 256 bytes   | ~0.9 μs            | ~4.0 μs            |
| 4 KB        | ~1.5 μs            | ~6.5 μs            |
| 64 KB       | ~6.0 μs            | ~35 μs             |
| 1 MB        | ~55 μs             | ~350 μs            |

These benchmarks are typically measured using the OSU Micro-Benchmarks suite:

```bash
# Run latency benchmark
mpirun -np 2 -hostfile hosts.txt ./osu_latency
# Run bandwidth benchmark
mpirun -np 2 -hostfile hosts.txt ./osu_bw
# Run allreduce benchmark
mpirun -np 64 -hostfile hosts.txt ./osu_allreduce
```

## UCX: Unified Communication X

UCX is an open-source communication framework that provides a high-performance, portable abstraction over diverse network transports, including RDMA verbs, shared memory, TCP, and GPU interconnects. It has become the de facto communication backend for Open MPI, OpenSHMEM, and numerous other parallel programming frameworks.

### Architecture

UCX is organized into three layers:

```
┌─────────────────────────────────────────────┐
│              UCP (Protocols)                 │
│  Tag matching, RMA, Atomic, Streams, AM     │
├─────────────────────────────────────────────┤
│              UCT (Transports)               │
│  RC verbs, DC, UD, shared memory, TCP, ...  │
├─────────────────────────────────────────────┤
│              UCS (Services)                 │
│  Memory management, async, data structures  │
└─────────────────────────────────────────────┘
```

- **UCS (Unified Communication Services)**: Utility layer providing memory management, data structures, debugging facilities, and platform abstraction.
- **UCT (Unified Communication Transport)**: Transport layer that provides a low-level API for each network transport. Each transport (e.g., `rc_verbs`, `rc_mlx5`, `dc_mlx5`, `ud_verbs`, `sm/shm`) implements the UCT interface. The `rc_mlx5` transport bypasses the kernel verbs layer and programs the Mellanox/NVIDIA NIC directly through memory-mapped I/O, achieving even lower latency than standard verbs.
- **UCP (Unified Communication Protocols)**: Protocol layer that implements high-level communication semantics (tag matching, RMA, atomics, active messages, stream) on top of UCT transports.

### Transport Selection

One of UCX's most powerful features is its ability to **automatically select the optimal transport and protocol** based on the available hardware, message size, and communication pattern. When a UCX endpoint is created, the library queries the available network devices, evaluates their capabilities and performance characteristics, and constructs a communication plan that may use different transports for different message sizes.

For example, UCX might select:
- **Shared memory** for intra-node communication
- **RC (Reliable Connected)** transport for small inter-node messages
- **DC (Dynamically Connected)** transport for large-scale jobs where the number of QPs would be prohibitive
- **Rendezvous with RDMA Read** for large messages

```bash
# Query available UCX transports
ucx_info -d

# Run UCX performance benchmark
ucx_perftest -t tag_lat   # Tag matching latency
ucx_perftest -t tag_bw    # Tag matching bandwidth
ucx_perftest -t put_lat   # RMA put latency
```

### Active Messages

UCX Active Messages allow the sender to specify a handler function that the receiver will execute upon message arrival. This is more flexible than simple tag matching and enables complex communication patterns such as remote procedure calls and dynamic load balancing.

### Tag Matching Offload

Modern RDMA NICs (ConnectX-5 and later) support **hardware tag matching**, where the NIC itself matches incoming messages to pre-posted receive buffers based on MPI-style tags. UCX leverages this offload to further reduce CPU overhead for MPI tag-matching operations, allowing message reception to proceed without any CPU involvement until the completion is signaled.

<div class="tip">

To verify that tag matching offload is active, check the UCX log output for the `tag_offload` transport. Set `UCX_LOG_LEVEL=info` and look for messages indicating that the tag matching offload has been enabled. This can reduce MPI receive overhead by 30-50% for latency-sensitive workloads.

</div>

## NCCL: NVIDIA Collective Communications Library

NCCL (pronounced "Nickel") is NVIDIA's library for multi-GPU and multi-node collective communication operations. While NCCL is most commonly associated with machine learning (discussed further in Section 14.5), it is a critical component of the HPC communication stack for GPU-accelerated workloads.

### GPU-to-GPU Communication over RDMA

NCCL leverages **GPUDirect RDMA** to transfer data directly between GPU memory on different nodes without staging through host memory. The communication path is:

```
GPU Memory (Node A) → PCIe → RDMA NIC → Network → RDMA NIC → PCIe → GPU Memory (Node B)
```

This eliminates the two extra copies (GPU→Host and Host→GPU) that would otherwise be required, reducing latency and freeing host memory bandwidth for other operations.

### Collective Algorithms

NCCL implements highly optimized collective operations:

- **Ring AllReduce**: Each GPU sends and receives data from its neighbors in a ring topology. Requires 2(N-1)/N data transfers (where N is the number of GPUs), achieving near-optimal bandwidth utilization.
- **Tree AllReduce**: Uses a binary tree topology to reduce latency for small messages at the cost of lower bandwidth utilization. NCCL dynamically selects between ring and tree based on message size.
- **Recursive Halving-Doubling**: Used for power-of-two process counts, providing a good balance between latency and bandwidth.

```bash
# NCCL environment variables for RDMA
export NCCL_IB_HCA=mlx5_0:1        # Select RDMA device and port
export NCCL_IB_GID_INDEX=3          # RoCEv2 GID index
export NCCL_NET_GDR_LEVEL=5         # Enable GPUDirect RDMA
export NCCL_IB_QPS_PER_CONNECTION=4 # QPs per connection

# Run NCCL test
mpirun -np 8 --hostfile hosts.txt ./all_reduce_perf -b 8 -e 256M -f 2 -g 1
```

### Performance Characteristics

On a modern GPU cluster (8x A100/H100 per node, HDR/NDR InfiniBand), NCCL achieves:

- **Intra-node AllReduce**: 300-600 GB/s aggregate bandwidth using NVLink/NVSwitch
- **Inter-node AllReduce**: 20-24 GB/s per GPU using HDR InfiniBand (200 Gbps), 40-48 GB/s with NDR (400 Gbps)
- **Latency overhead**: ~5-10 μs per step in the ring algorithm

<div class="warning">

NCCL performance is extremely sensitive to network topology. Ensure that GPU-to-NIC affinity is correct: each GPU should communicate primarily through the RDMA NIC connected to the same PCIe switch or NUMA node. Use `nvidia-smi topo -m` to inspect the topology and `NCCL_TOPO_DUMP_FILE` to export the detected topology for debugging. Incorrect affinity can reduce bandwidth by 50% or more due to cross-NUMA PCIe traffic.

</div>

## Putting It All Together

The HPC communication stack forms a layered hierarchy where each layer adds value:

1. **Application** uses MPI (or NCCL for GPU workloads) for portable, high-level communication.
2. **MPI** delegates to **UCX** for transport-agnostic communication with automatic protocol selection.
3. **UCX** uses **RDMA verbs** (or direct hardware access via `mlx5` transport) for the actual data transfer.
4. The **RDMA NIC** performs DMA between host/GPU memory and the network fabric.

This layered design allows application developers to write portable code while still achieving near-hardware-level performance. The key to optimal performance in HPC is understanding the interactions between these layers -- selecting the right eager/rendezvous threshold, ensuring correct NUMA affinity, and matching the number of communication channels to the available network bandwidth.
