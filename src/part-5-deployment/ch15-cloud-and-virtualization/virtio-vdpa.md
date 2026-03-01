# 15.2 virtio and VDPA

SR-IOV delivers near-native RDMA performance, but it welds the guest VM to a specific physical device. That tight coupling breaks live migration -- you cannot move a running VM to a different host when the guest holds a direct PCIe passthrough to hardware on the source host. For cloud operators, live migration is not optional: it is how they perform maintenance, rebalance load, and recover from hardware failures without customer-visible downtime. The virtio and VDPA (virtio Data Path Acceleration) technologies address this by interposing a standardized abstraction between the guest and the physical hardware, trading some performance for the operational flexibility that cloud environments require.

## virtio: The Standard Virtualized Device Interface

virtio is a standardized interface for paravirtualized I/O devices in virtual machines.[^1] Rather than emulating a specific physical device (which requires the guest to run the real hardware driver), virtio defines a generic device model with a well-known register layout and shared memory communication protocol. The guest runs a virtio driver (included in all major operating systems), which communicates with the hypervisor through shared memory ring buffers called **virtqueues**.

### virtio-net Architecture

The standard virtio-net device provides network connectivity to VMs:

```
┌──────────────────────────┐
│          Guest VM         │
│  ┌────────────────────┐  │
│  │  virtio-net driver  │  │
│  └────────┬───────────┘  │
│           │ virtqueues    │
├───────────┼──────────────┤
│           │ Hypervisor    │
│  ┌────────┴───────────┐  │
│  │  virtio-net backend │  │
│  │  (QEMU / vhost)    │  │
│  └────────┬───────────┘  │
│           │               │
│  ┌────────┴───────────┐  │
│  │  Physical NIC       │  │
│  └────────────────────┘  │
└──────────────────────────┘
```

In a pure software virtio implementation, every packet passes through the hypervisor's virtual switch (typically OVS or the Linux bridge), which processes it in software before forwarding it to the physical NIC. This provides maximum flexibility -- the hypervisor has full visibility into and control over all VM traffic -- but introduces significant overhead:

- **Additional memory copies**: Data is copied between the guest's virtqueue buffers and the hypervisor's networking stack.
- **Context switches**: Each packet triggers transitions between guest and host contexts.
- **CPU overhead**: The host CPU must process every packet through the software networking stack.

For standard networking, these overheads are manageable. However, for RDMA workloads that require microsecond latencies and millions of operations per second, pure software virtio is entirely inadequate. The overhead of a single context switch (several microseconds) exceeds the entire latency budget of an RDMA operation.

### vhost: Kernel-Accelerated virtio

The **vhost** framework moves the virtio backend processing from the QEMU user-space process into a kernel thread (vhost-net) or a separate user-space process (vhost-user). This eliminates one layer of context switching and reduces per-packet overhead. However, the data path still flows through the host's software networking stack, and the fundamental limitations for RDMA remain.

## VDPA: virtio Data Path Acceleration

VDPA (virtio Data Path Acceleration) bridges the gap between SR-IOV's performance and virtio's flexibility. The core idea is simple but powerful: a hardware NIC presents a **virtio-compatible data path** directly to the guest VM, while the control path remains mediated by the hypervisor. The guest sees a standard virtio device and runs a standard virtio driver, but the data path bypasses the hypervisor and flows directly through hardware.

### Architecture

```
┌──────────────────────────┐
│          Guest VM         │
│  ┌────────────────────┐  │
│  │  virtio-net driver  │  │  ← Standard virtio driver (unchanged)
│  └────────┬───────────┘  │
│           │ virtqueues    │
├───────────┼──────────────┤
│     ┌─────┴──────┐       │
│     │ VDPA       │       │  ← Control path through hypervisor
│     │ Framework   │       │     Data path direct to hardware
│     └─────┬──────┘       │
│           │               │
│  ┌────────┴───────────┐  │
│  │  Physical NIC       │  │  ← NIC implements virtio data path
│  │  (VDPA-capable)    │  │     in hardware
│  └────────────────────┘  │
└──────────────────────────┘
```

A VDPA device consists of:

1. **VDPA hardware**: The NIC implements the virtio data path (virtqueues, descriptor rings, notification mechanisms) in hardware. The NIC's DMA engine reads and writes directly to the guest's virtqueue memory, using the same descriptor ring format defined by the virtio specification. This means the NIC must implement the specific virtio descriptor layout (available ring, used ring, descriptor table) in its hardware DMA engine -- a non-trivial engineering effort that explains why VDPA support is limited to newer NIC generations.
2. **VDPA kernel framework**: The Linux kernel's VDPA framework (`drivers/vdpa/`) manages VDPA devices and presents them to the hypervisor. The framework provides a bus abstraction (`vdpa_bus`) with standard operations for device configuration, virtqueue setup, and DMA mapping.
3. **vhost-vdpa**: A vhost backend that connects the guest's virtio driver to the VDPA hardware device, allowing the guest to drive the hardware virtqueues directly. The vhost-vdpa module maps the guest's virtqueue memory into the NIC's IOMMU domain, enabling zero-copy DMA between the NIC and guest memory.

### How VDPA Enables Live Migration

VDPA's key advantage over SR-IOV is live migration. Because the guest uses a standard virtio driver and the device state is bounded and well-defined by the virtio specification, the hypervisor can:

1. **Quiesce** the VDPA device, stopping data path operations.
2. **Save** the virtio device state (virtqueue addresses, indices, feature bits) from the hardware.
3. **Migrate** the VM to a destination host using standard live migration protocols.
4. **Restore** the virtio device state on a VDPA device at the destination host.
5. **Resume** data path operations.

This process is transparent to the guest -- it sees only a brief pause in device activity, not a device removal and re-addition as with SR-IOV. The amount of device state that must be migrated is bounded and well-defined by the virtio specification: the virtqueue indices, feature negotiation bits, and device-specific configuration. This is far less state than migrating an arbitrary NIC's internal state (QP tables, MR mappings, completion queue positions), which is what makes VDPA migration tractable where SR-IOV migration is not.

```bash
# VDPA device management commands

# List available VDPA devices
vdpa dev show

# Create a VDPA device from an SR-IOV VF
vdpa dev add name vdpa0 mgmtdev pci/0000:01:00.2

# Show device details
vdpa dev show vdpa0 -jp

# Bind to vhost-vdpa for VM use
ls /dev/vhost-vdpa-*

# Attach to QEMU VM
qemu-system-x86_64 \
    -netdev type=vhost-vdpa,vhostdev=/dev/vhost-vdpa-0,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    ...
```

<div class="note">

VDPA live migration requires support at multiple levels: the NIC hardware must support state save/restore, the VDPA kernel framework must implement the migration protocol, and the hypervisor (QEMU) must coordinate the process. As of recent kernel versions, NVIDIA ConnectX-6 Dx and ConnectX-7 NICs support VDPA live migration, and QEMU has integrated the necessary migration handlers.

</div>

## RDMA over VDPA

Can VDPA provide RDMA capabilities, not just Ethernet networking? The short answer is "not yet, and not easily":

**Current state**: VDPA primarily targets the virtio-net device type, providing accelerated Ethernet networking. For RDMA workloads, the guest must implement RDMA in software on top of the virtio-net device -- for example, using RXE (Soft-RoCE) over the VDPA-accelerated virtio-net interface. This provides functional RDMA with better performance than pure software virtio but does not match SR-IOV's hardware-accelerated RDMA performance. The main bottleneck is that RXE performs all RDMA protocol processing (packet segmentation, retransmission, completion generation) on the guest CPU, consuming cores that would otherwise be available to the application. For latency-sensitive workloads, expect 5-10x higher latency compared to hardware RDMA.

**Future direction**: The virtio specification has reserved a device ID (42) for an RDMA device type, but no ratified virtio-rdma specification exists as of this writing. The OASIS virtio Technical Committee has discussed proposals, but hardware-accelerated virtio-rdma remains aspirational rather than imminent.[^3]

**Hybrid approach**: Some deployments combine VDPA (for migratable network connectivity) with SR-IOV (for RDMA performance), using bonding or failover mechanisms to switch between them during migration events.

## Performance Comparison

The following table compares the three main approaches to providing network I/O to VMs:

| Metric               | Software virtio | VDPA           | SR-IOV VF      | Bare Metal     |
|-----------------------|----------------|----------------|----------------|----------------|
| Latency (64B)         | ~25 μs         | ~3 μs          | ~1.3 μs        | ~1.1 μs        |
| Throughput (64B)      | ~2 Mpps        | ~15 Mpps       | ~60 Mpps       | ~65 Mpps       |
| Bandwidth (large)     | ~15 Gbps       | ~80 Gbps       | ~95 Gbps       | ~100 Gbps      |
| CPU overhead          | Very High      | Low            | Minimal        | Minimal        |
| Live migration        | Yes            | Yes            | No (graceful)  | N/A            |
| Guest driver          | virtio (std)   | virtio (std)   | Vendor-specific| Vendor-specific|
| Host visibility       | Full           | Configurable   | Limited*       | N/A            |
| RDMA support          | Software only  | Software only**| Hardware       | Hardware       |

\* Unless eSwitch is in switchdev mode.
\*\* Hardware RDMA via virtio-rdma specification is in development.

<div class="warning">

The performance numbers in this table represent typical values on modern hardware (ConnectX-7, 100 GbE) with a well-tuned configuration. Actual performance varies significantly based on the hypervisor, guest OS, NIC firmware version, and workload characteristics. Always benchmark with your specific configuration before making architectural decisions.

</div>

## Vendor Support and Current State

**NVIDIA/Mellanox**: The `mlx5_vdpa` kernel driver supports VDPA on ConnectX-6, ConnectX-6 Dx, ConnectX-6 Lx, ConnectX-7, and BlueField-2/3 DPUs. These devices implement the virtio data path in hardware and support live migration. The `mlx5_vdpa` driver has been in the mainline Linux kernel since version 5.9, with migration support added in later versions.

**Intel**: Intel's E810 network adapters (part of the Intel Ethernet 800 Series, not to be confused with Ice Lake CPUs) use the `ice` kernel driver. Intel's VDPA support in the upstream kernel is primarily through the `ifcvf` driver, which targets FPGA-based virtio-net devices rather than the E810 ASIC directly.[^2] Intel has contributed significantly to the VDPA kernel framework and the virtio specification.

**Red Hat**: As the primary developer of QEMU and a major contributor to the Linux kernel's virtualization stack, Red Hat has invested heavily in VDPA. The VDPA framework, vhost-vdpa backend, and QEMU integration are largely driven by Red Hat engineers.

```bash
# Check available VDPA management devices
vdpa mgmtdev show

# Example output for ConnectX-7:
# pci/0000:01:00.0:
#   supported_classes net

# Create multiple VDPA devices for multiple VMs
for i in $(seq 0 3); do
    vdpa dev add name vdpa$i mgmtdev pci/0000:01:00.0
done

# Monitor VDPA device statistics
vdpa dev show vdpa0 -s -jp
```

## When to Use Which Technology

The choice between SR-IOV, VDPA, and software virtio depends on the deployment's priorities:

**Use SR-IOV when:**
- RDMA performance is the primary requirement
- Live migration is not needed (or application-level reconnection is acceptable)
- The workload is long-running and pinned to specific hosts (e.g., HPC, ML training)

**Use VDPA when:**
- Near-native Ethernet performance is needed with live migration support
- The deployment uses standard virtio drivers for portability
- RDMA is not required, or software RDMA (RXE) over VDPA provides sufficient performance

**Use software virtio when:**
- Performance is not critical
- Maximum flexibility and compatibility are required
- The deployment must support the widest range of guest operating systems

<div class="tip">

For cloud providers building RDMA-capable infrastructure, the emerging best practice is to offer a tiered approach: SR-IOV VF passthrough for dedicated RDMA workloads (HPC, ML training) that can tolerate migration constraints, and VDPA for general-purpose workloads that need good network performance with full cloud manageability. This allows the provider to optimize the tradeoff between performance and operational flexibility on a per-workload basis.

</div>

SR-IOV, VDPA, and software networking are converging. Future NIC generations will blur the boundaries further -- hardware-accelerated virtio with full RDMA capabilities and seamless live migration is the obvious end state. Whether it arrives via virtio-rdma standardization, vendor-specific extensions, or an entirely different abstraction remains an open question.

[^1]: The virtio specification is maintained by the OASIS Virtual I/O Device (VIRTIO) Technical Committee. The current ratified version is [virtio v1.2](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html). Rusty Russell's original virtio paper, "virtio: Towards a De-Facto Standard For Virtual I/O Devices" (Ottawa Linux Symposium, 2008), describes the design rationale.

[^2]: The `ifcvf` (Intel FPGA Configurable Virtual Function) driver targets Intel FPGA SmartNIC platforms. The E810 ASIC's `ice` driver includes some virtio-related offload capabilities but is architecturally distinct from the VDPA framework's `ifcvf` path.

[^3]: The virtio device ID reservation (42 for RDMA) is listed in the [OASIS virtio specification](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html), but the corresponding device-specific configuration and behavior are not defined in any ratified version of the spec.
