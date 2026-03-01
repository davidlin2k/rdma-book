# 15.1 SR-IOV for RDMA

Single Root I/O Virtualization (SR-IOV) is a PCI Express specification that allows a single physical device to present itself as multiple independent virtual devices. For RDMA NICs, SR-IOV enables a single physical NIC to be partitioned into dozens of virtual instances, each assignable to a different virtual machine with near-native performance. SR-IOV is the most widely deployed technology for providing RDMA access in virtualized environments, offering the best balance between performance and multi-tenancy among current approaches.

## PF and VF Architecture

An SR-IOV-capable NIC exposes two types of PCIe functions:

**Physical Function (PF)**: The full-featured PCIe function that represents the physical NIC. The PF has complete access to all NIC resources and configuration registers. It is managed by the hypervisor host and is responsible for creating, configuring, and managing Virtual Functions. The PF driver (e.g., `mlx5_core` for NVIDIA ConnectX NICs) runs on the host operating system.

**Virtual Function (VF)**: A lightweight PCIe function that provides a subset of the PF's capabilities. Each VF has its own set of PCIe configuration space, memory-mapped I/O (MMIO) registers, and MSI-X interrupt vectors. VFs can be passed through to VMs via VFIO (Virtual Function I/O), giving the guest VM direct hardware access without hypervisor involvement in the data path.

```
┌─────────────────────────────────────────────┐
│              Physical RDMA NIC               │
│                                             │
│  ┌──────┐  ┌──────┐  ┌──────┐  ┌──────┐   │
│  │  PF  │  │ VF 0 │  │ VF 1 │  │ VF 2 │   │
│  │      │  │      │  │      │  │      │   │
│  │ Host │  │ VM 1 │  │ VM 2 │  │ VM 3 │   │
│  └──┬───┘  └──┬───┘  └──┬───┘  └──┬───┘   │
│     │         │         │         │        │
│  ┌──┴─────────┴─────────┴─────────┴──┐     │
│  │       NIC Internal Switch          │     │
│  │    (embedded switching / eSwitch)  │     │
│  └────────────────┬──────────────────┘     │
│                   │                         │
│            ┌──────┴──────┐                  │
│            │ Network Port │                  │
│            └─────────────┘                  │
└─────────────────────────────────────────────┘
```

## Configuring SR-IOV for RDMA

### Enabling SR-IOV

SR-IOV must be enabled at multiple levels: BIOS, kernel, and NIC driver.

```bash
# 1. Verify IOMMU is enabled (required for VFIO passthrough)
dmesg | grep -i iommu
# Should show: "DMAR: IOMMU enabled" or "AMD-Vi: IOMMU enabled"

# If not enabled, add to kernel command line:
# Intel: intel_iommu=on iommu=pt
# AMD: amd_iommu=on iommu=pt

# 2. Check SR-IOV capability of the NIC
lspci -s 01:00.0 -vvv | grep -i "SR-IOV"
# Look for: "Single Root I/O Virtualization (SR-IOV)"

# 3. Enable VFs (example: create 8 VFs on mlx5 device)
echo 8 > /sys/class/infiniband/mlx5_0/device/sriov_numvfs

# 4. Verify VFs are created
lspci | grep "Virtual Function"
# Should show 8 new VF entries

# 5. Check RDMA devices
ibstat
# Should show the PF device and each VF as a separate port
```

### Binding VFs to VFIO

To pass a VF to a VM, it must be bound to the VFIO driver:

```bash
# Load VFIO modules
modprobe vfio
modprobe vfio-pci

# Unbind VF from the host driver
echo 0000:01:00.1 > /sys/bus/pci/devices/0000:01:00.1/driver/unbind

# Get VF vendor and device IDs
lspci -ns 0000:01:00.1
# Example output: 01:00.1 0207: 15b3:101e

# Bind to VFIO
echo 15b3 101e > /sys/bus/pci/drivers/vfio-pci/new_id
```

### Attaching VFs to VMs

With libvirt/QEMU, a VF is attached to a VM by adding a hostdev entry to the VM's XML configuration:

```xml
<hostdev mode='subsystem' type='pci' managed='yes'>
  <source>
    <address domain='0x0000' bus='0x01' slot='0x00' function='0x1'/>
  </source>
  <address type='pci' domain='0x0000' bus='0x06' slot='0x00' function='0x0'/>
</hostdev>
```

Inside the VM, the VF appears as a standard RDMA device, and the guest uses the same verbs API as a bare-metal deployment:

```bash
# Inside the VM: verify RDMA device
ibstat
ibv_devinfo
ibv_rc_pingpong -d mlx5_0  # Test RDMA connectivity
```

<div class="note">

When using SR-IOV with RDMA, the guest VM runs the same `mlx5_ib` (or equivalent) driver as a bare-metal host. The verbs API works identically, and existing RDMA applications run without modification. The only differences are in the available resources (number of QPs, CQs, memory registration limits) and certain administrative capabilities that are restricted to the PF.

</div>

## Performance Characteristics

SR-IOV provides near-native RDMA performance because the VF data path bypasses the hypervisor entirely. Performance measurements on modern hardware (ConnectX-6/7, KVM hypervisor):

| Metric                | Bare Metal  | SR-IOV VF   | Overhead    |
|-----------------------|-------------|-------------|-------------|
| Latency (0 byte)      | 1.1 μs      | 1.2 μs      | ~9%         |
| Latency (4 KB)        | 2.3 μs      | 2.4 μs      | ~4%         |
| Bandwidth (large msg) | 24.5 GB/s   | 24.2 GB/s   | ~1%         |
| IOPS (8 byte writes)  | 150 Mops/s  | 142 Mops/s  | ~5%         |
| MR registration       | 12 μs       | 14 μs       | ~17%        |

The overhead is minimal for data path operations (latency, bandwidth, IOPS) but slightly higher for control path operations (memory registration, QP creation) due to VF-to-PF communication required for resource allocation.

## GID Table Management in Virtualized Environments

The GID (Global Identifier) table is a critical component for RDMA addressing. In a virtualized environment, GID management requires special attention:

- Each VF has its own GID table, populated based on the VF's network configuration.
- For **RoCEv2**, GIDs are derived from the VF's IP addresses. When a VM's IP address changes (e.g., due to DHCP lease renewal), the GID table must be updated.
- The PF driver manages the GID table for all VFs, relaying changes from the hypervisor's network configuration to the NIC.
- **RoCEv2 GID index selection**: In virtualized environments, the correct GID index for RoCEv2 must be determined dynamically, as it depends on the VF's VLAN and IP configuration.

```bash
# Inside the VM: list GID table entries
for i in $(seq 0 15); do
    echo -n "GID $i: "
    cat /sys/class/infiniband/mlx5_0/ports/1/gids/$i
done

# Identify the RoCEv2 GID (type = RoCE v2)
cat /sys/class/infiniband/mlx5_0/ports/1/gid_attrs/types/0
```

<div class="warning">

GID table misconfiguration is the most common cause of RDMA connectivity failures in virtualized environments. When debugging connectivity issues between VMs, always verify that both sides are using the correct GID index. For RoCEv2, ensure that the GID corresponds to a valid, routable IP address. Use `ibv_devinfo -v` to inspect the GID table and `rdma resource show cm_id` to check connection manager state.

</div>

## Security Isolation Between VFs

SR-IOV provides hardware-enforced isolation between VFs:

- **Memory isolation**: Each VF has its own set of memory translation tables. A VF cannot access memory regions registered by another VF or the PF, even though they share the same physical NIC.
- **QP isolation**: Queue Pairs created by one VF are invisible to other VFs. A VF cannot modify or destroy QPs belonging to another VF.
- **Network isolation**: The NIC's embedded switch (eSwitch) enforces L2/L3 isolation between VFs. VFs on different VLANs cannot communicate directly through the NIC's internal switch.
- **Resource limits**: The PF can impose per-VF resource limits on QPs, CQs, MRs, and other RDMA resources, preventing a single VF from exhausting shared NIC resources.

### eSwitch Configuration

Modern NVIDIA ConnectX NICs support two eSwitch modes:

- **Legacy mode**: Basic L2 switching between VFs. Limited configurability.
- **Switchdev mode**: Full-featured software-defined networking. The eSwitch is represented as a Linux network device, and VF traffic can be processed by OVS (Open vSwitch) or TC (Traffic Control) rules before reaching the network. This enables rich network policies while maintaining data path acceleration via hardware offload.

```bash
# Set eSwitch mode to switchdev
devlink dev eswitch set pci/0000:01:00.0 mode switchdev

# Add OVS bridge with VF representors
ovs-vsctl add-br br0
ovs-vsctl add-port br0 enp1s0f0_0   # VF 0 representor
ovs-vsctl add-port br0 enp1s0f0_1   # VF 1 representor

# Apply flow rules (hardware offloaded)
ovs-ofctl add-flow br0 "in_port=enp1s0f0_0,actions=output:enp1s0f0_1"
```

## Limitations of SR-IOV

Despite its performance advantages, SR-IOV has significant limitations in cloud environments:

**Limited VF count**: A single NIC typically supports 64-128 VFs. On dense hosts running hundreds of containers or lightweight VMs, VFs become a scarce resource.

**No live migration**: Because the VF is a PCIe device passed through to the guest, live migration requires detaching the VF, migrating the VM, and reattaching a VF on the destination host. This process interrupts RDMA connectivity, and any active RDMA connections (QPs, MRs) are lost. Applications must handle reconnection, which many RDMA applications do not support gracefully.

<div class="tip">

To mitigate the live migration limitation, some deployments use a **bond** configuration inside the VM, combining an SR-IOV VF (for performance) with a virtio-net interface (for migration). During normal operation, traffic flows through the VF. Before migration, traffic is switched to the virtio interface, the VF is detached, the VM is migrated, a new VF is attached on the destination, and traffic is switched back to the VF. This approach provides near-zero downtime migration but requires application-level tolerance for brief connectivity interruptions.

</div>

**Inflexible resource partitioning**: VF resources (bandwidth, QPs, memory) are configured at VF creation time and cannot be dynamically adjusted without destroying and recreating the VF.

**Host driver dependency**: The guest must run a specific driver for the VF hardware. This ties the guest to the NIC vendor's driver, reducing portability.

**No software-defined networking by default**: In legacy eSwitch mode, VF traffic bypasses the host's software networking stack, making it invisible to host-based firewalls, monitoring tools, and SDN controllers. Switchdev mode addresses this but adds complexity.

Despite these limitations, SR-IOV remains the dominant technology for RDMA in virtualized environments, particularly for workloads that require near-native performance and can tolerate the migration constraints. For workloads that require live migration, VDPA (discussed in the next section) offers a promising alternative.
