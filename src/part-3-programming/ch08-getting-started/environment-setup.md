# 8.1 Environment Setup

Before you can write your first line of RDMA code, you need three things: the user-space libraries and headers (rdma-core), a kernel driver that exposes an RDMA device, and a way to verify that the pieces fit together. This section walks through each of these steps on all major Linux distributions, with particular attention to software-emulated transports that let you develop and test without specialized hardware.

## Installing rdma-core from Packages

The rdma-core project provides the user-space components of the Linux RDMA stack: libibverbs, librdmacm, provider libraries for specific hardware, and diagnostic utilities. Every major distribution packages it.

### Ubuntu / Debian

```bash
# Install the development libraries, headers, and diagnostic tools
sudo apt update
sudo apt install -y rdma-core libibverbs-dev librdmacm-dev \
                    ibverbs-utils rdmacm-utils ibverbs-providers \
                    perftest infiniband-diags
```

The `libibverbs-dev` and `librdmacm-dev` packages provide the header files and shared library symlinks you need to compile RDMA programs. The `ibverbs-providers` package contains the user-space drivers for various hardware and software transports, including Soft-RoCE (rxe) and SoftiWARP (siw). The `ibverbs-utils` package gives you command-line tools like `ibv_devices` and `ibv_devinfo`. The `perftest` package provides bandwidth and latency benchmarks.

### RHEL / CentOS / Fedora

```bash
# RHEL 8+ / CentOS Stream / Fedora
sudo dnf install -y rdma-core rdma-core-devel libibverbs-utils \
                    librdmacm-utils perftest infiniband-diags
```

On older RHEL 7 systems that use yum, replace `dnf` with `yum`. The package names are the same. RHEL 8 and later include rdma-core in the base repositories; on RHEL 7 you may need to enable the `optional` or `InfiniBand` channels.

### SUSE / openSUSE

```bash
sudo zypper install rdma-core rdma-core-devel libibverbs-utils \
                     librdmacm-utils perftest infiniband-diags
```

SUSE Linux Enterprise Server (SLES) includes rdma-core in the HPC module. On openSUSE Tumbleweed and Leap, the packages are in the standard repositories.

## Building rdma-core from Source

If you need a newer version than your distribution provides, or if you want to modify the libraries themselves, you can build rdma-core from source.

```bash
# Install build dependencies (Ubuntu/Debian)
sudo apt install -y build-essential cmake gcc libudev-dev \
                    libnl-3-dev libnl-route-3-dev ninja-build \
                    pkg-config python3-docutils pandoc

# Clone and build
git clone https://github.com/linux-rdma/rdma-core.git
cd rdma-core
mkdir build && cd build
cmake -GNinja -DCMAKE_INSTALL_PREFIX=/usr ..
ninja
sudo ninja install
```

The CMake build system is the canonical way to build rdma-core. The `-GNinja` flag selects the Ninja build tool, which is faster than Make for this project. If you prefer Make, omit that flag and use `make -j$(nproc)` instead.

<div class="warning">

Building from source will overwrite files installed by your distribution's packages. If you want both, use a non-default `CMAKE_INSTALL_PREFIX` such as `/usr/local` or `/opt/rdma-core`, and adjust your `LD_LIBRARY_PATH` and `PKG_CONFIG_PATH` accordingly.

</div>

## Soft-RoCE (rxe) Setup

Soft-RoCE implements the RoCEv2 protocol entirely in software, in the Linux kernel. It requires no special hardware---any Ethernet interface will do, including the loopback interface. This makes it ideal for development and testing.

### Loading the Kernel Module

```bash
# Load the Soft-RoCE kernel module
sudo modprobe rdma_rxe
```

On most modern kernels (4.9+), the `rdma_rxe` module is available as part of the standard kernel build. If the `modprobe` command fails with "Module not found," your kernel was built without `CONFIG_RDMA_RXE`. You will need to rebuild the kernel or switch to a distribution kernel that includes it.

### Creating an rxe Device

Once the module is loaded, you create an rxe device bound to an existing network interface:

```bash
# Create an rxe device on top of eth0
# Replace eth0 with your actual interface name (e.g., ens33, enp0s3)
sudo rdma link add rxe0 type rxe netdev eth0
```

The `rdma` command is part of iproute2 and provides a unified interface for managing RDMA links. The name `rxe0` is arbitrary---you can call it anything you like.

### Verification

Three commands confirm that the device is up and working:

```bash
# List RDMA links
rdma link show
# Expected output:
# link rxe0/1 state ACTIVE physical_state LINK_UP netdev eth0

# List RDMA devices (from ibverbs-utils)
ibv_devices
# Expected output:
#     device          node GUID
#     ------          ----------------
#     rxe0            505400fffe000001

# Show detailed device information
ibv_devinfo -d rxe0
# Shows port state, GID table, capabilities, etc.
```

If `ibv_devices` shows no output, the most common cause is that the `ibverbs-providers` package is not installed, or the provider library for rxe (`librxe-rdmav2.so`) cannot be found. Verify that `/etc/libibverbs.d/` contains a configuration file for rxe, or that the provider `.so` files are in a directory on the library search path.

### Making It Persistent Across Reboots

The module and device configuration do not survive a reboot by default. To make them persistent:

```bash
# Load the module at boot
echo "rdma_rxe" | sudo tee /etc/modules-load.d/rdma_rxe.conf

# Create the rxe device at boot via a systemd service or udev rule
# Simple approach with a systemd oneshot service:
sudo tee /etc/systemd/system/rxe-setup.service > /dev/null <<EOF
[Unit]
Description=Create Soft-RoCE device
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
ExecStart=/usr/sbin/rdma link add rxe0 type rxe netdev eth0
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl daemon-reload
sudo systemctl enable rxe-setup.service
```

## SoftiWARP (siw) Setup

SoftiWARP is the software implementation of iWARP, available in the kernel since version 5.3. Its setup is nearly identical to Soft-RoCE:

```bash
# Load the SoftiWARP kernel module
sudo modprobe siw

# Create a SoftiWARP device on eth0
sudo rdma link add siw0 type siw netdev eth0

# Verify
rdma link show
ibv_devices
ibv_devinfo -d siw0
```

<div class="note">

You can have both an rxe and a siw device active on the same network interface simultaneously. They will appear as separate devices in `ibv_devices`. This can be useful for testing transport-layer differences, but in practice you should use one or the other to avoid confusion.

</div>

SoftiWARP tends to show slightly different behavior from Soft-RoCE because it runs on top of TCP (as iWARP specifies). For the examples in this book, either transport will work. We default to Soft-RoCE because it is available on a wider range of kernel versions.

## Two-Node Setup for Testing

While a single machine is sufficient for resource allocation and basic testing, data transfer tests require two endpoints. You have several options:

### Two VMs on the Same Host

Create two VMs connected via a host-only or bridged network. Install rdma-core and configure Soft-RoCE on each. This is the simplest approach for local development.

```
Host machine
├── VM1 (192.168.56.10) ─── rxe0 on eth1
└── VM2 (192.168.56.11) ─── rxe0 on eth1
```

### Two Network Namespaces

For lightweight testing on a single machine, you can use network namespaces with a veth pair:

```bash
# Create a veth pair connecting two namespaces
sudo ip netns add ns1
sudo ip netns add ns2
sudo ip link add veth1 type veth peer name veth2
sudo ip link set veth1 netns ns1
sudo ip link set veth2 netns ns2
sudo ip netns exec ns1 ip addr add 10.0.0.1/24 dev veth1
sudo ip netns exec ns1 ip link set veth1 up
sudo ip netns exec ns1 ip link set lo up
sudo ip netns exec ns2 ip addr add 10.0.0.2/24 dev veth2
sudo ip netns exec ns2 ip link set veth2 up
sudo ip netns exec ns2 ip link set lo up

# Create rxe devices in each namespace
sudo ip netns exec ns1 rdma link add rxe0 type rxe netdev veth1
sudo ip netns exec ns2 rdma link add rxe0 type rxe netdev veth2
```

### Docker Containers

Docker containers with `--privileged` mode and the `/dev/infiniband/` devices mounted can run RDMA workloads. Appendix E provides a complete Docker Compose configuration for a two-container RDMA test environment.

```bash
# Quick test with a privileged container
docker run --rm -it --privileged --network host \
    ubuntu:22.04 bash -c "apt update && apt install -y rdma-core ibverbs-utils && ibv_devices"
```

## Verifying with Ping-Pong Tests

The definitive verification that your RDMA stack is working end-to-end is the `ibv_rc_pingpong` test. It establishes an RC connection between two processes and exchanges messages:

```bash
# On the server side (Node 1 / 192.168.56.10):
ibv_rc_pingpong -d rxe0 -g 0

# On the client side (Node 2 / 192.168.56.11):
ibv_rc_pingpong -d rxe0 -g 0 192.168.56.10
```

The `-d rxe0` flag selects the RDMA device. The `-g 0` flag specifies GID index 0, which is required for RoCE (unlike InfiniBand, which uses LID-based addressing). The client specifies the server's IP address.

A successful run prints statistics like:

```
8192000 bytes in 0.02 seconds = 3571.43 Mbit/sec
1000 iters in 0.02 seconds = 18.36 usec/iter
```

## Performance Testing with perftest

The `perftest` suite provides standardized bandwidth and latency benchmarks. A few commonly used tests:

```bash
# Bandwidth test with RDMA Send (server, then client)
ib_send_bw -d rxe0 --gid-index 0
ib_send_bw -d rxe0 --gid-index 0 192.168.56.10

# Latency test with RDMA Write
ib_write_lat -d rxe0 --gid-index 0
ib_write_lat -d rxe0 --gid-index 0 192.168.56.10

# RDMA Read bandwidth
ib_read_bw -d rxe0 --gid-index 0
ib_read_bw -d rxe0 --gid-index 0 192.168.56.10
```

<div class="note">

Performance numbers with Soft-RoCE will be far lower than hardware RDMA. This is expected---the entire data path runs in the kernel, with no hardware offload. Soft-RoCE latencies are typically in the tens of microseconds, compared to single-digit microseconds for hardware RoCE and sub-microsecond for InfiniBand. The point of Soft-RoCE is functional correctness, not performance.

</div>

## Common Setup Issues and Troubleshooting

### `ibv_devices` shows no devices

- The kernel module is not loaded. Run `lsmod | grep rdma_rxe` (or `siw`) to check.
- The rxe device was not created. Run `rdma link show` to verify.
- The provider library is missing. Ensure `ibverbs-providers` is installed.
- On older kernels, the legacy `rxe_cfg` tool was used instead of `rdma link`. If `rdma link add` fails, try: `rxe_cfg add eth0`.

### `ibv_rc_pingpong` hangs or times out

- Firewall rules are blocking UDP traffic. Soft-RoCE uses raw Ethernet / UDP on port 4791. Ensure this port is open:
  ```bash
  sudo iptables -A INPUT -p udp --dport 4791 -j ACCEPT
  ```
- The GID index is wrong. Use `ibv_devinfo -d rxe0 -v` to list available GIDs, and select the index that corresponds to a valid IPv4 or IPv6 address on the interface.
- IP routing is broken. Verify that the two nodes can ping each other at the IP layer before testing RDMA.

### Permission errors on `/dev/infiniband/`

- RDMA device nodes require appropriate permissions. Check that `/dev/infiniband/uverbs0` exists and is readable by your user. On systemd-based systems, the `rdma` package typically installs udev rules to set permissions. You can also add your user to the `rdma` group if one exists, or run your program with `sudo` during development.

### Memory registration fails with ENOMEM

- The process has hit its locked memory limit. RDMA requires pinned (locked) memory, which is subject to `ulimit -l`. Increase it:
  ```bash
  # Temporary (current shell only)
  ulimit -l unlimited

  # Persistent (add to /etc/security/limits.conf)
  *    soft    memlock    unlimited
  *    hard    memlock    unlimited
  ```

### `modprobe rdma_rxe` fails

- The module is not built for your kernel. Check `uname -r` and verify that your kernel configuration includes `CONFIG_RDMA_RXE=m` or `CONFIG_RDMA_RXE=y`. Distribution kernels from Ubuntu 18.04+, RHEL 8+, and Fedora 28+ include it by default. If running a custom kernel, enable it under Networking support -> InfiniBand support -> Software RDMA over Ethernet.

With the environment set up and verified, you are ready to explore the API that makes it all work.
