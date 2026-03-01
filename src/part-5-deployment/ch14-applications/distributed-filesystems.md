# 14.4 Distributed File Systems

Distributed file systems form the storage backbone of supercomputing centers, research institutions, and large-scale data analytics platforms. These systems aggregate the storage capacity and bandwidth of many servers into a single, coherent namespace that thousands of compute nodes can access simultaneously. When a scientific simulation runs across 10,000 nodes, each producing gigabytes of output per second, the parallel file system must deliver aggregate bandwidth measured in terabytes per second. RDMA is the enabling technology that makes this possible, eliminating the CPU overhead and memory copies that would otherwise throttle the storage servers.

## Lustre: The HPC Parallel File System

Lustre is the most widely deployed parallel file system in high-performance computing. It powers many of the world's largest supercomputers, including systems at national laboratories, research universities, and government facilities. Lustre separates metadata operations (file creation, directory listing, permission checking) from data operations (read/write), allowing each to scale independently.

### Architecture

A Lustre file system consists of three main components:

- **MDS (Metadata Server)**: Manages the file system namespace, file attributes, and layout information. Uses an MDT (Metadata Target) for storage.
- **OSS (Object Storage Server)**: Stores file data. Each OSS manages one or more OSTs (Object Storage Targets). Files are striped across multiple OSTs to achieve parallel I/O bandwidth.
- **Client**: Mounts the Lustre file system and accesses files through POSIX I/O calls. The client communicates with the MDS for metadata and directly with OSS nodes for data.

```
┌─────────┐ ┌─────────┐ ┌─────────┐     Compute Nodes
│ Client  │ │ Client  │ │ Client  │     (thousands)
└────┬────┘ └────┬────┘ └────┬────┘
     │           │           │
     │      RDMA Network (LNet)
     │           │           │
┌────┴────┐ ┌───┴─────┐ ┌──┴──────┐
│  MDS    │ │  OSS    │ │  OSS    │    Storage Servers
│  (MDT)  │ │(OST×4) │ │(OST×4) │    (hundreds)
└─────────┘ └─────────┘ └─────────┘
```

### LNet: Lustre Networking

Lustre's networking layer, **LNet (Lustre Networking)**, provides an abstraction over multiple network transports. For RDMA networks, LNet uses the **o2ib** (OpenFabrics over InfiniBand) transport, which implements Lustre's messaging protocol over libibverbs.

LNet supports several critical features for large-scale deployments:

- **Multi-rail**: A single node can use multiple RDMA NICs simultaneously, multiplying available bandwidth.
- **Routing**: LNet routers bridge between different network segments, allowing Lustre traffic to traverse networks that are not directly interconnected.
- **Dynamic discovery**: Nodes discover each other's network endpoints dynamically, simplifying configuration in large clusters.

The LNet RDMA transport (`ko2iblnd` kernel module) operates as follows:

1. **Small messages** (metadata operations, RPCs < 4 KB): Sent inline via RDMA Send/Receive.
2. **Bulk data transfers** (file I/O): The sender registers the data buffer and sends the remote memory key to the receiver. The receiver issues an RDMA Read (for read operations) or RDMA Write (for write operations) to transfer data directly between the application buffer and the OST storage buffer.

```bash
# Configure LNet with InfiniBand
lnetctl lnet configure
lnetctl net add --net o2ib --if ib0

# Check LNet status
lnetctl net show
lnetctl peer show

# Configure multi-rail
lnetctl net add --net o2ib --if ib0
lnetctl net add --net o2ib --if ib1
```

### Performance with RDMA

On a modern Lustre deployment with HDR InfiniBand (200 Gbps per link):

- **Single OSS throughput**: 20-25 GB/s with RDMA, compared to 8-12 GB/s with TCP
- **Aggregate throughput**: Scales linearly with the number of OSS nodes; large deployments achieve 1+ TB/s
- **Metadata operations**: 100K+ operations per second per MDS with RDMA-accelerated RPCs

<div class="tip">

For optimal Lustre performance over RDMA, tune the following parameters:
- `peer_credits`: Controls the number of concurrent RDMA operations to a single peer. Increase for high-throughput workloads (default: 8, recommended: 32-128).
- `concurrent_sends`: Maximum concurrent Send operations. Match to `peer_credits`.
- `fmr_pool_size`: Size of the Fast Memory Registration pool. Increase for workloads with many concurrent I/O operations.
Monitor LNet statistics via `/proc/sys/lnet/` for bottleneck identification.

</div>

## GPFS / IBM Spectrum Scale

IBM's General Parallel File System (GPFS), now marketed as IBM Spectrum Scale, is another widely deployed parallel file system used in both HPC and enterprise environments. Unlike Lustre's separate metadata and data servers, GPFS uses a symmetric architecture where any node can serve both metadata and data.

### RDMA-Enhanced Data Shipping

GPFS supports two data access modes:

- **Direct I/O**: Clients access storage directly through locally attached disks (in a shared-disk SAN architecture).
- **Data shipping**: When a client needs to access data stored on a remote node's disks, the remote node "ships" the data over the network. RDMA significantly accelerates this data shipping path.

GPFS implements its RDMA transport using the **verbs** API directly. Key optimizations include:

- **RDMA-based remote buffer cache access**: Remote file system cache contents are transferred via RDMA Read/Write, avoiding TCP/IP overhead.
- **Multipath RDMA**: GPFS automatically uses multiple RDMA NICs for bandwidth aggregation and fault tolerance.
- **Adaptive message protocol**: Small messages use Send/Receive, while large transfers use RDMA Read/Write with memory registration caching.

```bash
# Configure GPFS with RDMA (verbsRdma)
mmchconfig verbsRdma=enable
mmchconfig verbsRdmaSend=yes
mmchconfig verbsPorts="mlx5_0/1 mlx5_1/1"

# Verify RDMA status
mmdiag --network | grep -i rdma
```

## Ceph: Distributed Storage System

Ceph is an open-source distributed storage system that provides object, block, and file storage in a unified platform. Originally designed around the TCP/IP networking model, Ceph has progressively added RDMA support to address performance requirements in HPC and high-end enterprise deployments.

### Messenger Architecture

Ceph's networking layer is built around the **messenger** abstraction. There have been three generations:

1. **Simple Messenger**: Original implementation, TCP-only, one thread per connection.
2. **Async Messenger**: Event-driven design supporting multiple connections per thread. Added RDMA as an optional transport backend.
3. **RDMA Messenger**: The async messenger's RDMA backend uses libibverbs for data transfer between Ceph daemons (OSDs, MONs, MDS).

The RDMA messenger operates as follows:

- **Connection setup**: Uses RDMA CM (Connection Manager) to establish RC (Reliable Connected) QPs between Ceph daemons.
- **Small messages**: Ceph protocol messages (heartbeats, OSD map updates) are sent via RDMA Send/Receive.
- **Large data transfers**: RADOS object data is transferred using RDMA Write, with the sender registering data buffers dynamically.

```ini
# ceph.conf RDMA configuration
[global]
ms_type = async+rdma
ms_async_rdma_device_name = mlx5_0
ms_async_rdma_port_num = 1
ms_async_rdma_buffer_size = 131072
ms_async_rdma_send_buffers = 1024
ms_async_rdma_receive_buffers = 1024
ms_async_rdma_polling_us = 1000
```

<div class="warning">

Ceph's RDMA support has historically been less mature than its TCP implementation. Before deploying RDMA in production Ceph clusters, test thoroughly with your specific workload patterns and Ceph version. Memory registration overhead can be significant for small I/O sizes, and some Ceph features (such as messenger-level compression) may not work with the RDMA transport. Check the release notes for your Ceph version for known RDMA limitations.

</div>

### Performance Impact

With RDMA enabled, Ceph OSD-to-OSD replication latency drops significantly:

| Operation              | TCP Latency | RDMA Latency | Improvement |
|------------------------|-------------|--------------|-------------|
| 4 KB write (3x repl)  | ~400 μs     | ~180 μs      | 2.2x        |
| 4 KB read              | ~150 μs     | ~60 μs       | 2.5x        |
| 128 KB write (3x repl)| ~800 μs     | ~350 μs      | 2.3x        |
| 128 KB read            | ~250 μs     | ~90 μs       | 2.8x        |

## GlusterFS: RDMA Transport for Volume Access

GlusterFS is a scalable network file system that aggregates storage from multiple servers. It supports RDMA as an alternative transport to TCP for both client-to-server and server-to-server communication.

GlusterFS RDMA transport:

- Uses RDMA CM for connection establishment
- Transfers file data via RDMA Read/Write operations
- Supports concurrent TCP and RDMA transports on the same volume

```bash
# Create a volume with RDMA transport
gluster volume create myvolume transport rdma \
    server1:/data/brick1 server2:/data/brick2

# Or enable both TCP and RDMA
gluster volume create myvolume transport tcp,rdma \
    server1:/data/brick1 server2:/data/brick2

# Mount with RDMA transport
mount -t glusterfs -o transport=rdma server1:/myvolume /mnt/gluster
```

<div class="note">

GlusterFS RDMA support has received less development attention in recent years as the project has focused on its TCP transport. For new GlusterFS deployments requiring high performance, evaluate whether the RDMA transport is actively maintained in your target version. In some cases, TCP with modern kernel optimizations may deliver comparable performance with simpler operations.

</div>

## BeeGFS: Built for RDMA

BeeGFS (originally FhGFS, developed at the Fraunhofer Center for High Performance Computing) distinguishes itself from other parallel file systems by being designed with RDMA as a first-class transport from the beginning, rather than retrofitting RDMA onto a TCP-based architecture.

### Architecture

BeeGFS uses a similar separation of metadata and storage services as Lustre, but with a lighter-weight design:

- **Management service**: Cluster coordination and monitoring.
- **Metadata service**: File system namespace management. Can run on multiple servers for scalability.
- **Storage service**: Stores file data on local disks. Files are striped across multiple storage targets.
- **Client**: Kernel module that mounts BeeGFS and translates POSIX operations to BeeGFS protocol messages.

### Native RDMA Support

BeeGFS's RDMA implementation differs from Lustre and GPFS in its approach to memory registration:

- **Pre-registered buffer pools**: BeeGFS pre-allocates and pre-registers large buffer pools with the RDMA NIC, avoiding the latency and overhead of dynamic memory registration during I/O operations.
- **Direct data placement**: Storage servers use RDMA Write to place data directly into the client's application buffer, achieving true zero-copy for the read path.
- **Efficient metadata**: Metadata operations use RDMA Send/Receive with inline data for low-latency RPCs.

```bash
# BeeGFS client configuration for RDMA
# /etc/beegfs/beegfs-client.conf
connRDMAEnabled = true
connRDMABufSize = 65536
connRDMABufNum = 128
connRDMATypeOfService = 0
```

BeeGFS achieves strong scaling with RDMA: a 10-server BeeGFS cluster with HDR InfiniBand can deliver over 100 GB/s aggregate sequential read throughput, compared to approximately 50 GB/s with TCP.

## Performance Impact: RDMA vs TCP for Parallel I/O

Across all the file systems discussed, RDMA consistently delivers substantial performance improvements over TCP for parallel I/O workloads. The magnitude of improvement depends on the I/O pattern:

| Workload Pattern      | Typical RDMA Advantage | Reason                            |
|----------------------|------------------------|-----------------------------------|
| Large sequential I/O | 1.5-2.5x bandwidth    | Reduced CPU overhead, zero-copy   |
| Small random I/O     | 2-4x IOPS             | Lower per-operation latency       |
| Metadata-heavy       | 2-3x ops/sec          | Lower RPC latency                 |
| Many-to-one (N:1)    | 2-5x throughput       | CPU no longer the bottleneck      |
| Checkpoint/restart   | 2-3x throughput       | Burst write absorption            |

The advantage is most pronounced for workloads that stress the server CPU -- such as many-client-to-one-server access patterns where the server must handle thousands of connections simultaneously. With TCP, the server CPU spends a large fraction of its cycles in protocol processing. With RDMA, protocol processing is offloaded to the NIC, freeing the server CPU for file system operations.

<div class="tip">

When benchmarking parallel file system performance with RDMA, use tools that generate realistic parallel I/O patterns. The IOR benchmark (Interleaved-Or-Random) is the standard tool for measuring parallel file system performance:

```bash
# IOR large sequential write test
mpirun -np 256 ior -a POSIX -t 1m -b 16g -F -w -o /lustre/testfile

# IOR small random read test
mpirun -np 256 ior -a POSIX -t 4k -b 1g -z -r -o /lustre/testfile

# MDTest for metadata performance
mpirun -np 256 mdtest -n 10000 -d /lustre/mdtest
```

Always compare TCP and RDMA with the same benchmark parameters, number of clients, and server configuration to isolate the transport's impact.

</div>

The trend in distributed file systems is clear: RDMA is transitioning from an optional optimization to a required capability. As NVMe storage devices get faster and all-flash file system deployments become the norm, the network fabric -- and specifically, the choice of transport protocol -- becomes the primary determinant of achievable I/O performance. File systems that were designed with RDMA as an afterthought are being re-architected to make it central, while new file systems are being built RDMA-native from the ground up.
