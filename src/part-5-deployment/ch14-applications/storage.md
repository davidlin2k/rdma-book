# 14.1 Storage: NVMe-oF and iSER

Remote storage access has always been one of the most compelling use cases for RDMA. When applications access storage over a network, every microsecond of protocol overhead translates directly into increased I/O latency and reduced IOPS. With the advent of NVMe solid-state drives capable of delivering sub-10 microsecond access times, the network fabric connecting hosts to storage has become the dominant bottleneck. RDMA eliminates this bottleneck by providing a zero-copy, kernel-bypass path between the storage initiator and target, enabling remote NVMe devices to be accessed with performance approaching that of locally attached drives.

## NVMe over Fabrics (NVMe-oF)

NVMe over Fabrics extends the NVMe storage protocol beyond the local PCIe bus, allowing NVMe commands to be transported over a network fabric. While NVMe-oF supports multiple transport types -- including TCP, Fibre Channel, and RDMA -- the RDMA transport delivers the lowest latency and highest IOPS, making it the preferred choice for performance-critical deployments.

### Architecture

The NVMe-oF architecture mirrors the local NVMe model. An **NVMe host** (initiator) submits NVMe commands to a remote **NVMe target** (controller) over the fabric. The target processes these commands against its local NVMe drives and returns completions to the host.

```
┌─────────────────┐          RDMA Fabric          ┌─────────────────┐
│    NVMe Host     │                                │   NVMe Target    │
│                  │                                │                  │
│  ┌────────────┐  │    NVMe-oF Capsules over      │  ┌────────────┐  │
│  │ NVMe Driver│──┼────── RDMA Send/Recv ─────────┼──│ NVMe Target│  │
│  │  (Host)    │  │                                │  │  Driver    │  │
│  └────────────┘  │    Data via RDMA Read/Write    │  └─────┬──────┘  │
│        │         │◄──────────────────────────────►│        │         │
│  ┌─────┴──────┐  │                                │  ┌─────┴──────┐  │
│  │ RDMA NIC   │  │                                │  │ RDMA NIC   │  │
│  └────────────┘  │                                │  └────────────┘  │
└─────────────────┘                                └─────────────────┘
```

The key insight of NVMe-oF/RDMA is the direct mapping between NVMe and RDMA concepts. NVMe **Submission Queues (SQs)** and **Completion Queues (CQs)** are mapped onto RDMA **Queue Pairs (QPs)**. Each NVMe I/O queue corresponds to an RDMA QP, with NVMe commands encapsulated in **capsules** that are transmitted via RDMA Send/Receive operations.

### Capsules and Data Transfer

An NVMe-oF capsule contains an NVMe command (64 bytes) plus optional inline data. For small I/O operations, the data can be included directly in the capsule (inline data transfer). For larger transfers, the capsule carries a **Scatter Gather List (SGL)** describing the remote memory regions, and the target uses RDMA Read or RDMA Write operations to transfer the data directly to or from the host's memory -- achieving true zero-copy I/O.

For a **read** operation:
1. The host posts an NVMe Read command capsule via RDMA Send.
2. The target receives the capsule, reads data from its local NVMe drive.
3. The target issues an RDMA Write to place the data directly into the host's buffer.
4. The target sends a completion capsule via RDMA Send.

For a **write** operation:
1. The host posts an NVMe Write command capsule via RDMA Send, including an SGL describing the data location.
2. The target issues an RDMA Read to fetch the data from the host's memory.
3. The target writes the data to its local NVMe drive.
4. The target sends a completion capsule via RDMA Send.

### Performance

NVMe-oF over RDMA routinely achieves:
- **Latency**: Sub-10 microsecond additional overhead beyond local NVMe latency, yielding total remote access latencies of 15-25 microseconds for 4 KB random reads.
- **IOPS**: Millions of IOPS per target, scaling linearly with the number of NVMe drives and RDMA connections.
- **Bandwidth**: Near-line-rate utilization of the RDMA fabric (e.g., 90+ Gbps on a 100 Gbps link for sequential workloads).

<div class="warning">

NVMe-oF/RDMA performance is highly sensitive to the number of I/O queues and their mapping to CPU cores. For optimal results, configure one NVMe-oF queue per CPU core on the host, and ensure that each queue's RDMA QP has its completion vector mapped to the corresponding core's interrupt. Misaligned queue-to-core mappings can introduce cross-core cache bouncing and significantly degrade IOPS.

</div>

### Linux Kernel Implementation

The Linux kernel provides both host (initiator) and target implementations:

- **nvme-rdma**: The host-side driver that connects to remote NVMe-oF targets over RDMA. It registers as an NVMe transport and creates RDMA QPs for each I/O queue.
- **nvmet-rdma**: The target-side driver that exposes local NVMe namespaces over RDMA. It accepts incoming RDMA connections and maps NVMe commands to local NVMe operations.

**Connecting to a target from the host:**

```bash
# Discover available subsystems on the target
nvme discover -t rdma -a 192.168.1.100 -s 4420

# Connect to a specific subsystem
nvme connect -t rdma -n nqn.2024-01.com.example:nvme-target \
    -a 192.168.1.100 -s 4420

# Verify the connection
nvme list
```

**Configuring a target using nvmetcli:**

```bash
# Create a subsystem
cd /sys/kernel/config/nvmet/subsystems
mkdir nqn.2024-01.com.example:nvme-target
echo 1 > nqn.2024-01.com.example:nvme-target/attr_allow_any_host

# Create a namespace backed by a local NVMe device
mkdir nqn.2024-01.com.example:nvme-target/namespaces/1
echo /dev/nvme0n1 > nqn.2024-01.com.example:nvme-target/namespaces/1/device_path
echo 1 > nqn.2024-01.com.example:nvme-target/namespaces/1/enable

# Create an RDMA port
mkdir /sys/kernel/config/nvmet/ports/1
echo 192.168.1.100 > /sys/kernel/config/nvmet/ports/1/addr_traddr
echo rdma > /sys/kernel/config/nvmet/ports/1/addr_trtype
echo 4420 > /sys/kernel/config/nvmet/ports/1/addr_trsvcid
echo ipv4 > /sys/kernel/config/nvmet/ports/1/addr_adrfam

# Link the subsystem to the port
ln -s /sys/kernel/config/nvmet/subsystems/nqn.2024-01.com.example:nvme-target \
    /sys/kernel/config/nvmet/ports/1/subsystems/
```

## iSER: iSCSI Extensions for RDMA

iSER adapts the widely deployed iSCSI protocol to use RDMA as its transport, replacing the TCP/IP stack with RDMA Send/Receive and RDMA Read/Write operations. This allows existing iSCSI infrastructure -- including management tools, authentication mechanisms, and discovery protocols -- to benefit from RDMA performance without requiring a complete architectural overhaul.

### How iSER Works

In standard iSCSI over TCP, every SCSI block must be copied through the kernel's TCP/IP stack, incurring multiple data copies and context switches. iSER eliminates these overheads:

1. **Control messages** (iSCSI PDUs for login, logout, task management) are sent via RDMA Send/Receive.
2. **Data transfers** (SCSI read/write data) use RDMA Read and RDMA Write for zero-copy placement directly into application buffers.
3. The iSCSI **Data-In** and **Data-Out** PDU types are replaced by RDMA data transfer operations, bypassing the TCP/IP stack entirely.

### Linux Implementation

The Linux kernel provides both initiator and target support:

- **iser** (initiator): The `ib_iser` kernel module implements the iSER initiator. It integrates with the standard `open-iscsi` initiator stack, replacing the TCP transport with RDMA.
- **isert** (target): The `ib_isert` module implements the iSER target, integrating with the LIO target framework.

```bash
# Initiator: discover targets (standard iscsiadm)
iscsiadm -m discovery -t sendtargets -p 192.168.1.100

# Connect using iSER transport
iscsiadm -m node -T iqn.2024-01.com.example:target \
    -p 192.168.1.100 --login -o update -n iface.transport_name -v iser
```

<div class="note">

iSER is most valuable in environments with existing iSCSI infrastructure that need a performance upgrade. For greenfield deployments, NVMe-oF/RDMA is generally preferred, as it avoids the overhead of the SCSI command layer and provides a more direct mapping to modern NVMe storage.

</div>

## SPDK: User-Space NVMe-oF Target

The Storage Performance Development Kit (SPDK) provides a user-space NVMe-oF target implementation that combines kernel bypass for both the storage and network paths. By using user-space NVMe drivers (polling-based, lockless) and user-space RDMA, SPDK achieves even lower latency and higher IOPS than the kernel-based nvmet-rdma target.

Key SPDK architectural decisions:

- **Polling-based I/O**: No interrupts on either the storage or network path. Dedicated CPU cores poll both NVMe completion queues and RDMA completion queues.
- **Lockless design**: Each reactor (polling thread) owns specific NVMe and RDMA resources, eliminating lock contention.
- **Zero-copy throughout**: Data flows from the NVMe drive through DMA to host memory, then via RDMA DMA to the network, without any CPU-initiated memory copies.

```bash
# Start SPDK NVMe-oF target with RDMA transport
./spdk/scripts/rpc.py nvmf_create_transport -t RDMA \
    -u 16384 -i 131072 -c 8192

# Create a subsystem
./spdk/scripts/rpc.py nvmf_create_subsystem \
    nqn.2024-01.com.example:spdk-target -a -s SPDK00001

# Add an NVMe namespace
./spdk/scripts/rpc.py nvmf_subsystem_add_ns \
    nqn.2024-01.com.example:spdk-target Nvme0n1

# Add an RDMA listener
./spdk/scripts/rpc.py nvmf_subsystem_add_listener \
    nqn.2024-01.com.example:spdk-target -t rdma \
    -a 192.168.1.100 -s 4420
```

## Performance Comparison

The following table summarizes typical performance characteristics for different remote storage protocols, measured with 4 KB random read workloads on modern hardware (100 Gbps network, Gen4 NVMe SSDs):

| Protocol            | Latency (avg) | IOPS (single target) | CPU Overhead |
|---------------------|---------------|----------------------|--------------|
| NVMe-oF/RDMA (SPDK)| ~12 μs        | ~8M                  | Very Low     |
| NVMe-oF/RDMA (kernel)| ~18 μs      | ~4M                  | Low          |
| NVMe-oF/TCP (kernel)| ~35 μs       | ~2M                  | Moderate     |
| iSER               | ~25 μs        | ~2.5M                | Low          |
| iSCSI/TCP          | ~60 μs        | ~800K                | High         |
| Local NVMe         | ~8 μs         | ~1.5M (per drive)    | Minimal      |

<div class="tip">

When evaluating remote storage protocols, consider the complete picture: NVMe-oF/RDMA delivers the best raw performance, but NVMe-oF/TCP requires no special network hardware and works over standard Ethernet. For many deployments, the operational simplicity of TCP may outweigh the performance advantage of RDMA. The right choice depends on your latency sensitivity, IOPS requirements, and existing infrastructure.

</div>

Several trends are shaping the future of RDMA-based storage:

- **Computational storage**: NVMe-oF targets that perform computation (compression, encryption, search) on the data path, leveraging RDMA for efficient result delivery.
- **Disaggregated memory**: Extending the NVMe-oF model to expose remote DRAM and persistent memory via RDMA, creating a unified fabric for both storage and memory pooling.
- **CXL integration**: Compute Express Link (CXL) provides cache-coherent memory access over PCIe, and emerging architectures combine CXL for local/rack-scale memory with RDMA for datacenter-scale fabric connectivity.

RDMA has transformed remote storage from a high-latency compromise into a viable alternative to locally attached devices. As NVMe drives continue to get faster, the importance of RDMA as the storage network fabric will only increase.
