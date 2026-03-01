# 2.4 OFED and rdma-core

## The Software Ecosystem Challenge

Hardware is only half the RDMA story. InfiniBand, iWARP, and RoCE each have distinct physical and link layers, but they share the same programming model: queue pairs, completion queues, memory regions, and the Verbs API. The software ecosystem that implements this shared model---spanning kernel drivers, user-space libraries, management daemons, and diagnostic tools---has its own complex history of evolution, reorganization, and consolidation.

Understanding this ecosystem is essential for anyone who will write, deploy, or debug RDMA applications. Knowing *which library provides which function*, *where the kernel boundary lies*, and *how the pieces fit together on a modern Linux distribution* will save hours of confusion when a build fails because of a missing package, when `ibv_devinfo` reports an unexpected error, or when you need to determine whether a problem is in user-space or kernel-space code.

## The OpenFabrics Alliance

The **OpenFabrics Alliance (OFA)** was founded in 2004 (originally as the OpenIB Alliance, later renamed) to develop and maintain an open-source software stack for RDMA. At the time, InfiniBand drivers and libraries were fragmented: different vendors shipped different user-space libraries with incompatible interfaces, and the kernel drivers were in various states of upstream acceptance.

The OFA set out to solve three problems:

1. **A common Verbs API** that application developers could code against regardless of the underlying RDMA transport (InfiniBand, iWARP, or later RoCE).
2. **Upstream kernel integration** of RDMA drivers and core infrastructure into the mainline Linux kernel, rather than maintaining out-of-tree patches.
3. **A distribution package** that users could install to get a complete, tested RDMA stack: kernel modules, user-space libraries, management daemons, and diagnostic tools.

The result was **OFED: the OpenFabrics Enterprise Distribution**.

## OFED: The Distribution

OFED has existed in two forms that are important to distinguish:

**Upstream OFED (the kernel RDMA subsystem)** refers to the RDMA drivers and core infrastructure that live in the mainline Linux kernel, under `drivers/infiniband/`. Despite the directory name (a historical artifact from when InfiniBand was the only RDMA transport), this subsystem handles all RDMA transports. It includes:

- **Core modules**: `ib_core` (the core RDMA abstraction layer), `ib_uverbs` (the user-space verbs interface that enables kernel bypass), `ib_cm` (the InfiniBand Communication Manager), `rdma_cm` (the RDMA Communication Manager, transport-agnostic), `iw_cm` (the iWARP Connection Manager).
- **Hardware drivers**: `mlx5_ib` (NVIDIA/Mellanox ConnectX-4 and later), `mlx4_ib` (ConnectX-3), `cxgb4` (Chelsio iWARP), `irdma` (Intel E810 iWARP/RoCE), `bnxt_re` (Broadcom), `rxe` (SoftRoCE, a software-based RoCE implementation for testing), `siw` (Soft-iWARP), and others.
- **Upper-layer protocols (ULPs)**: `ib_srp` (SCSI RDMA Protocol for storage), `ib_iser` (iSCSI Extensions for RDMA), `rpcrdma` (NFS over RDMA), `smc` (Shared Memory Communications for socket acceleration).

These components are maintained by the Linux kernel RDMA subsystem maintainers and follow the standard kernel development process. They ship with every Linux distribution as part of the kernel.

**Vendor OFED (e.g., MLNX_OFED)** refers to vendor-specific distributions that package the upstream kernel modules (sometimes with proprietary patches or backports) together with user-space libraries, firmware, and vendor-specific tools. NVIDIA's MLNX_OFED is the most prominent example. It provides newer driver versions, proprietary features (such as NVIDIA's GPUDirect RDMA kernel modules), and certified compatibility with specific firmware and adapter versions.

<div class="note">

For production deployments using NVIDIA ConnectX adapters, many organizations use MLNX_OFED rather than the distribution-provided kernel drivers. MLNX_OFED typically includes newer features, bug fixes, and firmware that have not yet reached the upstream kernel. However, MLNX_OFED installs its own kernel modules that replace the upstream ones, which can create dependency management challenges during kernel upgrades. Weigh the benefits of newer features against the operational complexity of maintaining an out-of-tree driver package.

</div>

## The Kernel RDMA Subsystem

The kernel side of the RDMA stack provides three critical functions:

### Resource Management and Protection

Even though RDMA's data path bypasses the kernel, the **control path**---creating queue pairs, registering memory regions, allocating protection domains---goes through the kernel. This is essential for security: the kernel must verify that an application has permission to register a given memory region, that queue pair parameters are within allowed limits, and that different applications cannot access each other's RDMA resources.

The `ib_uverbs` module exposes this control path to user space through a device file (`/dev/infiniband/uverbsN`). When an application calls `ibv_create_qp()`, the libibverbs library translates this into an ioctl or write command on the uverbs device file, which the kernel processes, allocates the resources, and returns a handle. Subsequent data-path operations (posting work requests, polling completions) bypass the kernel entirely, operating on memory-mapped hardware registers and shared memory queues.

### Hardware Abstraction

The kernel RDMA subsystem defines a set of callback functions (the `ib_device_ops` structure) that each hardware driver must implement. This abstraction layer allows the core RDMA code to operate on any RDMA device without knowing the hardware-specific details. When user space calls `ibv_post_send()`, the call flows through the user-space provider library (e.g., `libmlx5`) directly to hardware-mapped memory, but the initial setup that makes this possible flows through the kernel's abstraction layer.

### Memory Registration and Page Pinning

When an application registers a memory region for RDMA, the kernel must **pin** the physical pages backing that virtual address range---that is, prevent the operating system from swapping them out or moving them. The RDMA NIC accesses memory using physical addresses (translated from virtual addresses via the NIC's internal page tables), so if the OS were to swap a page to disk or relocate it in physical memory, the NIC would read or write the wrong data. The kernel's memory registration path pins pages, builds the translation table, and programs it into the NIC.

This pinning has significant implications: it reduces the amount of memory available for the OS page cache and other uses, and it interacts with cgroups memory limits, NUMA placement, and transparent huge pages in ways that can surprise the unwary. Later chapters address these interactions in detail.

## User-Space Libraries: From Fragmentation to rdma-core

The user-space side of the RDMA stack has undergone a more dramatic evolution than the kernel side.

### The Original Structure (Pre-2016)

Originally, the user-space RDMA stack consisted of several independent libraries, each maintained in its own repository with its own release cycle:

- **libibverbs**: The core Verbs API library. Provided the `ibv_*` functions that applications call: `ibv_open_device()`, `ibv_create_qp()`, `ibv_post_send()`, `ibv_poll_cq()`, and so on.
- **librdmacm**: The RDMA Connection Manager library. Provided `rdma_*` functions for connection establishment and address resolution, abstracting the differences between InfiniBand, iWARP, and RoCE connection setup.
- **Hardware provider libraries**: `libmlx4` (ConnectX-3), `libmlx5` (ConnectX-4+), `libcxgb4` (Chelsio), `libi40iw` (Intel), and others. Each of these implemented the hardware-specific fast path for a particular adapter: the exact format of work request entries, the doorbell mechanism, the CQ polling logic.
- **Diagnostic utilities**: `ibv_devinfo`, `ibv_rc_pingpong`, `ibv_devices`, and other test and diagnostic programs.
- **Management tools**: `ibstat`, `ibswitches`, `ibnetdiscover`, `opensm` (the Subnet Manager for InfiniBand), and others.

This fragmented structure was difficult to maintain: a change to libibverbs might require coordinated updates to every provider library, versioning was inconsistent, and building a complete RDMA user-space from source required cloning and configuring half a dozen repositories.

### rdma-core: The Unification (2016--Present)

In 2016, the community consolidated the user-space RDMA libraries into a single repository and build system: **rdma-core**. Hosted on GitHub at `github.com/linux-rdma/rdma-core`, this repository contains:

- **libibverbs** and its plugin/provider infrastructure
- **librdmacm**
- All hardware provider libraries (mlx4, mlx5, cxgb4, irdma, bnxt_re, rxe, siw, efa, hns, mana, etc.)
- Diagnostic and benchmarking utilities (`ibv_devinfo`, `ibv_rc_pingpong`, etc.)
- The `pyverbs` Python bindings for libibverbs
- The `rdma` utility for configuring RDMA devices via netlink
- Man pages and documentation

The consolidation into rdma-core brought several benefits:

**Unified versioning.** A single version number identifies a compatible set of core library, provider libraries, and utilities.

**Simplified packaging.** Distributions package rdma-core as a single source tree, producing a set of packages (`rdma-core`, `libibverbs`, `librdmacm`, `ibverbs-providers`, etc.) that are tested together.

**Provider model.** Hardware-specific provider libraries are loaded as plugins by libibverbs at runtime. When an application opens an RDMA device, libibverbs queries the kernel for the device type, then loads the appropriate provider shared library (e.g., `libmlx5.so`) from a standard directory (`/usr/lib/x86_64-linux-gnu/libibverbs/` on Debian/Ubuntu, `/usr/lib64/libibverbs/` on RHEL/Fedora). The provider library implements the fast-path functions (post_send, post_recv, poll_cq) that the application calls through function pointers in the `ibv_context` structure.

**Consistent build system.** rdma-core uses CMake, and a single `cmake && make && make install` builds everything.

## The Provider Model in Detail

The interaction between libibverbs and a hardware provider is the architectural key to the RDMA user-space stack:

1. The application calls `ibv_open_device()`, passing a device from the list returned by `ibv_get_device_list()`.
2. libibverbs reads `/sys/class/infiniband/<device>/` to determine the device type and driver.
3. libibverbs loads the matching provider shared library (e.g., `libmlx5.so` for mlx5 devices).
4. The provider's initialization function allocates a device context, memory-maps the hardware's doorbell registers and UAR (User Access Region) pages, and populates a table of function pointers for fast-path operations.
5. The application receives an `ibv_context *` that contains these function pointers. Subsequent calls like `ibv_post_send(qp, wr, bad_wr)` are dispatched through these function pointers directly to provider code that writes work request entries into hardware-mapped memory---no system call, no kernel involvement.

This architecture is what makes kernel bypass work: the provider library knows the exact hardware-specific layout of a work queue entry and the exact doorbell register address, so it can submit work to the NIC entirely from user space.

<div class="note">

The provider shared libraries are not merely convenience wrappers. They contain the performance-critical code path for every RDMA operation. The difference between a well-optimized provider (like `libmlx5`) and a generic implementation can be hundreds of nanoseconds per operation, which matters enormously at scale. This is why vendor-specific providers exist rather than a single generic implementation.

</div>

## Configuration and Diagnostic Tools

The rdma-core package and the broader OFED ecosystem provide several essential tools:

### Device Discovery and Status

```
ibv_devinfo        # List RDMA devices and their attributes (ports, speeds, state)
ibv_devices        # Simple list of RDMA device names
rdma link show     # Modern netlink-based device and port information
ibstat             # InfiniBand-specific device statistics
```

### Connectivity Testing

```
ibv_rc_pingpong    # Simple RC (Reliable Connection) ping-pong test
ibv_ud_pingpong    # UD (Unreliable Datagram) ping-pong test
rping              # RDMA ping using librdmacm
ib_send_bw         # Bandwidth benchmark (from perftest package)
ib_send_lat        # Latency benchmark (from perftest package)
```

### Network Discovery (InfiniBand)

```
ibnetdiscover      # Discover InfiniBand network topology
ibswitches         # List InfiniBand switches
iblinkinfo         # Display link information for all ports
sminfo             # Query the Subnet Manager
```

### Configuration

```
rdma link set <dev>/<port> ...    # Configure RDMA device parameters
rdma system show                  # Show system-level RDMA settings
cma_roce_mode                     # Set RoCE mode for rdma_cm connections
```

The `rdma` command (part of the `iproute2` package, with RDMA support built from rdma-core's netlink library) is the modern, recommended interface for RDMA device configuration. It follows the same command structure as `ip link`, `ip addr`, and other iproute2 tools, making it familiar to Linux network administrators.

## Package Management on Major Distributions

Installing RDMA support varies by distribution, and getting the right packages installed is often the first hurdle for new RDMA users.

### Debian/Ubuntu

```bash
# Core libraries and providers
sudo apt install rdma-core libibverbs-dev librdmacm-dev ibverbs-providers

# Diagnostic and benchmarking tools
sudo apt install ibverbs-utils rdmacm-utils infiniband-diags perftest

# InfiniBand subnet manager (if needed)
sudo apt install opensm
```

### RHEL/Fedora/Rocky Linux

```bash
# Core libraries and providers
sudo dnf install rdma-core libibverbs-devel librdmacm-devel

# Diagnostic tools
sudo dnf install libibverbs-utils infiniband-diags perftest

# Enable RDMA service
sudo systemctl enable --now rdma
```

### SUSE Linux Enterprise

```bash
# Core packages
sudo zypper install rdma-core libibverbs-devel librdmacm-devel

# Tools
sudo zypper install infiniband-diags perftest
```

<div class="warning">

On RHEL-based distributions, the `rdma` systemd service must be enabled for RDMA kernel modules to be loaded at boot. Without this service, the kernel modules for your RDMA hardware may not load automatically, and `/dev/infiniband/` device nodes may not be created. If `ibv_devinfo` reports no devices, check `systemctl status rdma` first.

</div>

### Verifying the Installation

After installing the packages, verify that the RDMA stack is functional:

```bash
# Check that RDMA devices are visible
ibv_devinfo

# Expected output (example for ConnectX-5):
# hca_id: mlx5_0
#     transport:          InfiniBand (0)
#     fw_ver:             16.35.1012
#     node_guid:          b8:59:9f:03:00:a1:2b:4e
#     sys_image_guid:     b8:59:9f:03:00:a1:2b:4e
#     vendor_id:          0x02c9
#     vendor_part_id:     4119
#     hw_ver:             0x0
#     phys_port_cnt:      1
#         port:   1
#             state:          PORT_ACTIVE (4)
#             max_mtu:        4096 (5)
#             active_mtu:     1024 (3)
#             ...

# Check that the provider library is loaded
ibv_devices
# Expected output:
#     device          node GUID
#     ------          ----------------
#     mlx5_0          b8599f0300a12b4e
```

If `ibv_devinfo` reports `No IB devices found`, the troubleshooting path is: check that the kernel driver is loaded (`lsmod | grep mlx5_ib` or equivalent for your hardware), check that the hardware is visible on the PCIe bus (`lspci | grep Mellanox` or equivalent), check that the firmware is up to date, and check that the `rdma` service is running on distributions that require it.

## The Relationship Between Kernel and User Space

A summary diagram of the RDMA software architecture helps cement the relationships:

| Component | Location | Purpose |
|-----------|----------|---------|
| Application code | User space | Business logic, RDMA operations |
| libibverbs | User space | Verbs API, provider loading |
| Provider (e.g., libmlx5) | User space | Hardware-specific fast path |
| librdmacm | User space | Connection management |
| ib_uverbs | Kernel | User-space access to control path |
| ib_core | Kernel | Transport-agnostic RDMA core |
| Hardware driver (e.g., mlx5_ib) | Kernel | Hardware initialization, resource management |
| NIC hardware | Hardware | Executes RDMA operations |

The critical insight is that the **control path** (resource creation and destruction) flows through the kernel for security and resource management, while the **data path** (posting work requests, receiving completions) stays entirely in user space for performance. This split---kernel for control, user space for data---is the architectural foundation that makes RDMA's microsecond-scale latency possible.
