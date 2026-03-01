# Appendix E: Lab Setup Guide

This appendix provides step-by-step instructions for setting up an RDMA development and testing environment without physical RDMA hardware. We cover four options using software-emulated RDMA providers: Soft-RoCE (rxe) on a single machine with network namespaces, Soft-RoCE with two VMs, a Docker-based two-node testbed, and SoftiWARP (siw). Each option produces a functional environment in which all the code examples in this book can be compiled and run.

<div class="note">

Software-emulated RDMA (rxe and siw) faithfully implements the verbs API and RDMA-CM semantics, making it suitable for development, functional testing, and learning. However, it does **not** provide the performance characteristics (latency, throughput, CPU offload) of hardware RDMA. Do not use software RDMA for performance benchmarking.

</div>

---

## Prerequisites (All Options)

All options require a Linux system (or Linux VMs) with kernel 5.4 or later. The following packages are needed:

**Debian/Ubuntu:**
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libibverbs-dev \
    librdmacm-dev \
    rdma-core \
    ibverbs-utils \
    rdmacm-utils \
    perftest \
    iproute2 \
    net-tools \
    infiniband-diags
```

**Fedora/RHEL/CentOS:**
```bash
sudo dnf install -y \
    gcc gcc-c++ make cmake \
    libibverbs-devel \
    librdmacm-devel \
    rdma-core \
    libibverbs-utils \
    librdmacm-utils \
    perftest \
    iproute \
    net-tools \
    infiniband-diags
```

Verify that the kernel modules are available:

```bash
modprobe rdma_rxe   # Soft-RoCE
modprobe siw        # SoftiWARP (alternative)
```

If `modprobe` fails, your kernel may not have the modules built. Check `CONFIG_RDMA_RXE` and `CONFIG_RDMA_SIW` in your kernel configuration.

---

## Option 1: Soft-RoCE on a Single Machine with Network Namespaces

This is the simplest setup: two network namespaces connected by a virtual Ethernet (veth) pair, each with its own rxe device. Both "nodes" run on the same machine, sharing the same kernel. This is ideal for development and running the book's examples.

### Step 1: Create Network Namespaces and veth Pair

```bash
# Create two network namespaces
sudo ip netns add ns1
sudo ip netns add ns2

# Create a veth pair connecting them
sudo ip link add veth1 type veth peer name veth2

# Move each end into its namespace
sudo ip link set veth1 netns ns1
sudo ip link set veth2 netns ns2

# Assign IP addresses and bring interfaces up
sudo ip netns exec ns1 ip addr add 192.168.1.1/24 dev veth1
sudo ip netns exec ns1 ip link set veth1 up
sudo ip netns exec ns1 ip link set lo up

sudo ip netns exec ns2 ip addr add 192.168.1.2/24 dev veth2
sudo ip netns exec ns2 ip link set veth2 up
sudo ip netns exec ns2 ip link set lo up
```

### Step 2: Verify Network Connectivity

```bash
sudo ip netns exec ns1 ping -c 3 192.168.1.2
```

You should see successful ping replies.

### Step 3: Create Soft-RoCE (rxe) Devices

```bash
# Load the rxe kernel module
sudo modprobe rdma_rxe

# Create rxe devices on each namespace's interface
sudo ip netns exec ns1 rdma link add rxe1 type rxe netdev veth1
sudo ip netns exec ns2 rdma link add rxe2 type rxe netdev veth2
```

<div class="warning">

On older systems (kernel < 5.3), the `rdma link add` command may not be available. In that case, use the legacy `rxe_cfg` tool:
```bash
sudo ip netns exec ns1 rxe_cfg add veth1
sudo ip netns exec ns2 rxe_cfg add veth2
```

</div>

### Step 4: Verify RDMA Devices

```bash
# List RDMA devices in each namespace
sudo ip netns exec ns1 ibv_devices
sudo ip netns exec ns2 ibv_devices

# Query device details
sudo ip netns exec ns1 ibv_devinfo
sudo ip netns exec ns2 ibv_devinfo
```

You should see `rxe1` in namespace `ns1` and `rxe2` in namespace `ns2`, both with state `PORT_ACTIVE`.

### Step 5: Test with a Ping-Pong

```bash
# Terminal 1: Start server in ns2
sudo ip netns exec ns2 ibv_rc_pingpong -g 0 -d rxe2

# Terminal 2: Start client in ns1
sudo ip netns exec ns1 ibv_rc_pingpong -g 0 -d rxe1 192.168.1.2
```

You should see output showing bytes transferred and message rates.

### Step 6: Test with Bandwidth Benchmark

```bash
# Terminal 1: Server
sudo ip netns exec ns2 ib_send_bw -d rxe2 --report_gbits

# Terminal 2: Client
sudo ip netns exec ns1 ib_send_bw -d rxe1 192.168.1.2 --report_gbits
```

### Cleanup

```bash
sudo ip netns exec ns1 rdma link delete rxe1
sudo ip netns exec ns2 rdma link delete rxe2
sudo ip netns delete ns1
sudo ip netns delete ns2
```

### Convenience Script

Save the following as `setup-rdma-lab.sh` for quick setup:

```bash
#!/bin/bash
set -e

echo "Creating network namespaces..."
sudo ip netns add ns1
sudo ip netns add ns2

echo "Creating veth pair..."
sudo ip link add veth1 type veth peer name veth2
sudo ip link set veth1 netns ns1
sudo ip link set veth2 netns ns2

echo "Configuring IP addresses..."
sudo ip netns exec ns1 ip addr add 192.168.1.1/24 dev veth1
sudo ip netns exec ns1 ip link set veth1 up
sudo ip netns exec ns1 ip link set lo up
sudo ip netns exec ns2 ip addr add 192.168.1.2/24 dev veth2
sudo ip netns exec ns2 ip link set veth2 up
sudo ip netns exec ns2 ip link set lo up

echo "Loading rdma_rxe module..."
sudo modprobe rdma_rxe

echo "Creating rxe devices..."
sudo ip netns exec ns1 rdma link add rxe1 type rxe netdev veth1
sudo ip netns exec ns2 rdma link add rxe2 type rxe netdev veth2

echo "Verifying..."
sudo ip netns exec ns1 ibv_devinfo -d rxe1
sudo ip netns exec ns2 ibv_devinfo -d rxe2

echo ""
echo "RDMA lab ready!"
echo "  Node 1: sudo ip netns exec ns1 <command>"
echo "  Node 2: sudo ip netns exec ns2 <command>"
echo "  IP addresses: 192.168.1.1 (ns1), 192.168.1.2 (ns2)"
```

---

## Option 2: Soft-RoCE with Two VMs

This option creates two separate virtual machines, each with its own rxe device. This more closely simulates a real two-node RDMA cluster and allows testing scenarios that require separate OS instances (e.g., independent failure modes).

### VM Setup with QEMU/KVM

#### Step 1: Create Two VMs

Use your preferred method to create two Linux VMs (Ubuntu Server or Fedora are recommended). Each VM needs at least 2 GB RAM and 10 GB disk. You can use `virt-manager`, `virsh`, or raw QEMU commands.

Example with virt-install:

```bash
# Create VM1
virt-install \
    --name rdma-node1 \
    --ram 2048 \
    --vcpus 2 \
    --disk size=10 \
    --os-variant ubuntu22.04 \
    --cdrom ubuntu-22.04-server-amd64.iso \
    --network bridge=virbr0

# Create VM2 (same command with different name)
virt-install \
    --name rdma-node2 \
    --ram 2048 \
    --vcpus 2 \
    --disk size=10 \
    --os-variant ubuntu22.04 \
    --cdrom ubuntu-22.04-server-amd64.iso \
    --network bridge=virbr0
```

#### Step 2: Configure Networking

With the default NAT bridge (`virbr0`), both VMs will be on the same `192.168.122.0/24` subnet and can communicate directly. Alternatively, create a host-only network for isolation:

```bash
# Create a host-only bridge
sudo ip link add br-rdma type bridge
sudo ip addr add 10.0.0.1/24 dev br-rdma
sudo ip link set br-rdma up
```

Then attach both VMs to this bridge.

#### Step 3: Install RDMA Packages and Create rxe Devices

On each VM, install the prerequisite packages (see the Prerequisites section above), then:

```bash
# On VM1 (assuming interface ens3 with IP 192.168.122.101)
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev ens3

# On VM2 (assuming interface ens3 with IP 192.168.122.102)
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev ens3
```

#### Step 4: Verify and Test

```bash
# On each VM
ibv_devices
ibv_devinfo

# Test connectivity
# VM2 (server): ibv_rc_pingpong -g 0 -d rxe0
# VM1 (client): ibv_rc_pingpong -g 0 -d rxe0 192.168.122.102
```

### VM Setup with VirtualBox

For VirtualBox, follow the same general steps:

1. Create two VMs with Ubuntu or Fedora.
2. Configure networking as "Host-Only Adapter" or "Internal Network" for both VMs to ensure they share a L2 network segment.
3. Install RDMA packages and create rxe devices inside each VM.

<div class="tip">

For persistent rxe device creation across reboots, add the following to `/etc/rc.local` or create a systemd service:
```bash
modprobe rdma_rxe
rdma link add rxe0 type rxe netdev ens3
```

</div>

---

## Option 3: Docker Two-Node Testbed

Docker containers provide a lightweight alternative to full VMs. This option creates two containers with RDMA capability, connected via a Docker bridge network.

<div class="warning">

RDMA in Docker requires the containers to share the host kernel's RDMA modules. The host kernel must have `rdma_rxe` (or `siw`) compiled and loaded. Additionally, containers need elevated privileges to create rxe devices and access `/dev/infiniband/`.

</div>

### Step 1: Create the Dockerfile

Create a file named `Dockerfile.rdma`:

```dockerfile
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    libibverbs-dev \
    librdmacm-dev \
    rdma-core \
    ibverbs-utils \
    rdmacm-utils \
    perftest \
    iproute2 \
    net-tools \
    iputils-ping \
    infiniband-diags \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
CMD ["/bin/bash"]
```

### Step 2: Create docker-compose.yml

```yaml
version: "3.8"

services:
  node1:
    build:
      context: .
      dockerfile: Dockerfile.rdma
    container_name: rdma-node1
    hostname: node1
    privileged: true
    cap_add:
      - NET_ADMIN
      - IPC_LOCK
    networks:
      rdma-net:
        ipv4_address: 172.20.0.11
    volumes:
      - ./src:/app/src
      - /dev/infiniband:/dev/infiniband
    ulimits:
      memlock:
        soft: -1
        hard: -1
    stdin_open: true
    tty: true

  node2:
    build:
      context: .
      dockerfile: Dockerfile.rdma
    container_name: rdma-node2
    hostname: node2
    privileged: true
    cap_add:
      - NET_ADMIN
      - IPC_LOCK
    networks:
      rdma-net:
        ipv4_address: 172.20.0.12
    volumes:
      - ./src:/app/src
      - /dev/infiniband:/dev/infiniband
    ulimits:
      memlock:
        soft: -1
        hard: -1
    stdin_open: true
    tty: true

networks:
  rdma-net:
    driver: bridge
    ipam:
      config:
        - subnet: 172.20.0.0/24
```

### Step 3: Build and Start Containers

```bash
# Load rxe module on the host first
sudo modprobe rdma_rxe

# Build and start
docker compose up -d --build
```

### Step 4: Create rxe Devices Inside Containers

```bash
# In node1
docker exec -it rdma-node1 bash
rdma link add rxe0 type rxe netdev eth0
ibv_devinfo

# In node2
docker exec -it rdma-node2 bash
rdma link add rxe0 type rxe netdev eth0
ibv_devinfo
```

### Step 5: Test RDMA Between Containers

```bash
# Terminal 1 (node2 as server):
docker exec -it rdma-node2 ibv_rc_pingpong -g 0 -d rxe0

# Terminal 2 (node1 as client):
docker exec -it rdma-node1 ibv_rc_pingpong -g 0 -d rxe0 172.20.0.12
```

### Cleanup

```bash
docker compose down
```

<div class="note">

The `privileged: true` flag and `/dev/infiniband` mount are required for RDMA operations in containers. In production, more fine-grained capabilities and device isolation should be used. The `IPC_LOCK` capability and unlimited memlock ulimit are needed for memory registration (page pinning).

</div>

---

## Option 4: SoftiWARP (siw)

SoftiWARP is a software iWARP implementation in the Linux kernel. Like Soft-RoCE, it provides a fully functional RDMA stack without hardware, but implements the iWARP protocol over TCP/IP instead of RoCE over UDP/IP.

### When to Use siw vs. rxe

| Criteria | rxe (Soft-RoCE) | siw (SoftiWARP) |
|----------|-----------------|-----------------|
| Protocol | RoCEv2 (UDP/IP) | iWARP (TCP/IP) |
| Connection management | RDMA-CM or manual QP setup | RDMA-CM (required for iWARP) |
| Multicast | Supported (UD) | Not supported |
| Network requirements | Works over any IP network | Works over any IP network |
| Typical use case | Simulating RoCE environments | Simulating iWARP environments |
| Kernel support | Since Linux 4.8 | Since Linux 5.3 |

For most readers of this book, **rxe is recommended** because the majority of RDMA deployments use RoCE or InfiniBand (which rxe emulates more closely). Use siw when you specifically need to test iWARP behavior or TCP-based RDMA.

### Setup with Network Namespaces (Same as Option 1)

The namespace setup is identical to Option 1. Only the device creation step changes:

```bash
# Create namespaces and veth pair (same as Option 1, Steps 1-2)
sudo ip netns add ns1
sudo ip netns add ns2
sudo ip link add veth1 type veth peer name veth2
sudo ip link set veth1 netns ns1
sudo ip link set veth2 netns ns2
sudo ip netns exec ns1 ip addr add 192.168.1.1/24 dev veth1
sudo ip netns exec ns1 ip link set veth1 up
sudo ip netns exec ns1 ip link set lo up
sudo ip netns exec ns2 ip addr add 192.168.1.2/24 dev veth2
sudo ip netns exec ns2 ip link set veth2 up
sudo ip netns exec ns2 ip link set lo up

# Load siw module instead of rxe
sudo modprobe siw

# Create siw devices
sudo ip netns exec ns1 rdma link add siw1 type siw netdev veth1
sudo ip netns exec ns2 rdma link add siw2 type siw netdev veth2
```

### Verify and Test

```bash
sudo ip netns exec ns1 ibv_devinfo -d siw1
sudo ip netns exec ns2 ibv_devinfo -d siw2

# Ping-pong test
# Terminal 1: sudo ip netns exec ns2 ibv_rc_pingpong -g 0 -d siw2
# Terminal 2: sudo ip netns exec ns1 ibv_rc_pingpong -g 0 -d siw1 192.168.1.2
```

<div class="note">

The siw provider does not support all features that rxe supports. Notably, siw does not support UD QPs, multicast, or memory windows. If you encounter `ENOSYS` or `EOPNOTSUPP` errors with certain operations, check whether the feature is supported by the siw provider.

</div>

---

## Verification Steps

After setting up any of the above options, run through these verification steps to confirm everything is working.

### 1. List RDMA Devices

```bash
ibv_devices
```

Expected output (example):

```
    device          node GUID
    ------          ----------------
    rxe1            505400fffe000001
```

### 2. Query Device Details

```bash
ibv_devinfo -d rxe1
```

Expected output includes:

```
hca_id: rxe1
    transport:          InfiniBand (0)
    fw_ver:             0.0.0
    node_guid:          5054:00ff:fe00:0001
    sys_image_guid:     ...
    vendor_id:          ...
    vendor_part_id:     0
    hw_ver:             0x0
    phys_port_cnt:      1
        port:   1
            state:          PORT_ACTIVE (4)
            max_mtu:        4096 (5)
            active_mtu:     1024 (3)
            sm_lid:         0
            port_lid:       0
            port_lmc:       0x00
            link_layer:     Ethernet
```

Verify that:
- The device is listed.
- The port state is `PORT_ACTIVE`.
- The `link_layer` is `Ethernet` (for rxe) or `Ethernet` (for siw).

### 3. RC Ping-Pong

```bash
# Server side
ibv_rc_pingpong -g 0 -d rxe1

# Client side (different terminal/namespace/VM/container)
ibv_rc_pingpong -g 0 -d rxe1 <server-ip>
```

The `-g 0` flag specifies GID index 0, which is required for RoCE/Ethernet link layers.

### 4. Send Bandwidth Test

```bash
# Server
ib_send_bw -d rxe1 --report_gbits

# Client
ib_send_bw -d rxe1 <server-ip> --report_gbits
```

### 5. RDMA Write Latency Test

```bash
# Server
ib_write_lat -d rxe1

# Client
ib_write_lat -d rxe1 <server-ip>
```

<div class="tip">

For Soft-RoCE, always use the `-g 0` flag with `ibv_rc_pingpong` and other tools that require it. Without this flag, the tool may try to use LID-based addressing, which does not work on Ethernet link layers. The `perftest` tools (e.g., `ib_send_bw`) usually auto-detect the link layer and select the appropriate GID index.

</div>

---

## Common Issues and Solutions

### "No RDMA devices found" (ibv_devices shows nothing)

**Cause:** The rxe/siw kernel module is not loaded, or the rxe/siw device was not created.

**Solution:**
```bash
sudo modprobe rdma_rxe    # or: sudo modprobe siw
sudo rdma link add rxe0 type rxe netdev <interface>
```

### "Failed to modify QP to INIT" or "Failed to modify QP to RTR"

**Cause:** The RDMA device's underlying network interface is not UP, or the IP address is not configured.

**Solution:** Ensure the network interface is up and has an IP address:
```bash
ip link set <interface> up
ip addr show <interface>
```

### "Connection refused" or timeout during ping-pong

**Cause:** Firewall rules blocking the connection, or the server is not running.

**Solution:** Temporarily disable the firewall or add rules:
```bash
sudo iptables -I INPUT -p tcp --dport 18515 -j ACCEPT  # Default pingpong port
sudo iptables -I INPUT -p udp --dport 4791 -j ACCEPT   # RoCEv2 port
```

### "Cannot allocate memory" during memory registration

**Cause:** The memlock ulimit is too low, preventing page pinning.

**Solution:**
```bash
# Check current limit
ulimit -l

# Set unlimited (for current shell)
ulimit -l unlimited

# For persistent change, edit /etc/security/limits.conf:
# * soft memlock unlimited
# * hard memlock unlimited
```

### "Operation not supported" for specific verbs operations

**Cause:** The software provider (rxe or siw) does not support the requested operation.

**Solution:** Check the provider's feature support. Soft-RoCE (rxe) supports most operations including atomics, but some advanced features (e.g., ODP, device memory) are not available. siw has more limited operation support.

### "Resource temporarily unavailable" or RNR errors

**Cause:** Receive buffers are not posted before the sender transmits.

**Solution:** Ensure the receiver posts receive work requests before the sender starts transmitting. Increase the RNR retry count on the QP (`rnr_retry = 7` for infinite retries during development).

### rxe device disappears after network interface goes down

**Cause:** The rxe device is tied to the underlying network interface and is destroyed when the interface goes down.

**Solution:** Re-create the rxe device after the interface comes back up:
```bash
sudo rdma link add rxe0 type rxe netdev <interface>
```

---

## Building the Book's Code Examples

All code examples in this book are designed to compile and run on any of the lab environments described above. This section explains how to build and run them.

### Prerequisites

Ensure you have the development headers and tools installed:

```bash
# Verify headers are present
ls /usr/include/infiniband/verbs.h
ls /usr/include/rdma/rdma_cma.h

# Verify pkg-config can find the libraries
pkg-config --libs libibverbs
pkg-config --libs librdmacm
```

### Building Individual Examples

Each example can be compiled directly with `gcc`:

```bash
# Simple verbs example
gcc -o example example.c -libverbs -lrdmacm -lpthread

# With debug symbols and warnings
gcc -g -Wall -Wextra -o example example.c -libverbs -lrdmacm -lpthread
```

### Building with CMake

If the book's example repository includes a `CMakeLists.txt`, build all examples at once:

```bash
cd /path/to/rdma-book-examples
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Building with Make

If a `Makefile` is provided:

```bash
cd /path/to/rdma-book-examples
make all
```

A minimal `Makefile` for the examples:

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -g -O2
LDFLAGS = -libverbs -lrdmacm -lpthread

SOURCES = $(wildcard *.c)
TARGETS = $(SOURCES:.c=)

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)
```

### Running the Examples

Most examples follow a client-server pattern. Run the server first, then the client:

```bash
# Using network namespaces (Option 1):
# Terminal 1 - Server:
sudo ip netns exec ns2 ./server

# Terminal 2 - Client:
sudo ip netns exec ns1 ./client 192.168.1.2

# Using VMs or Docker (Options 2-3):
# On the server machine/container:
./server

# On the client machine/container:
./client <server-ip>
```

<div class="tip">

When developing and debugging RDMA applications, these environment variables are helpful:

```bash
# Enable verbose debug output from rdma-core
export RDMAV_HUGEPAGES_SAFE=1
export IBV_FORK_SAFE=1        # Enable fork() safety (slight performance cost)
export MLX5_DEBUG_MASK=0      # Suppress mlx5 debug (set higher for more output)

# For rxe-specific debugging, check kernel logs:
dmesg | grep -i rxe
```

</div>

### Example Walkthrough: RC Send/Receive

Here is a minimal walkthrough of running the RC Send/Receive example from Chapter 4 using the namespace-based lab (Option 1):

```bash
# Step 1: Set up the lab (if not already done)
sudo bash setup-rdma-lab.sh

# Step 2: Build the example
gcc -g -Wall -o rc_send_recv rc_send_recv.c -libverbs -lrdmacm -lpthread

# Step 3: Start the server in ns2
sudo ip netns exec ns2 ./rc_send_recv --server --port 5555

# Step 4: Start the client in ns1 (in another terminal)
sudo ip netns exec ns1 ./rc_send_recv --client --addr 192.168.1.2 --port 5555

# Step 5: Observe the output
# Both sides should print the exchanged messages.

# Step 6: Clean up the lab when done
sudo ip netns exec ns1 rdma link delete rxe1
sudo ip netns exec ns2 rdma link delete rxe2
sudo ip netns delete ns1
sudo ip netns delete ns2
```

### Debugging Tips

When an example fails, use these diagnostic approaches:

1. **Check device status**: `ibv_devinfo` should show `PORT_ACTIVE`.
2. **Check network connectivity**: `ping` between the two endpoints.
3. **Check kernel logs**: `dmesg | tail -50` for RDMA-related errors.
4. **Run with strace**: `strace -e trace=ioctl,write,mmap ./example` to see verbs system calls.
5. **Use rdma-core debug output**: Set `RDMAV_FORK_SAFE=1` and check for warnings about fork safety.
6. **Verify resource limits**: `ulimit -l` must be `unlimited` or large enough for your memory registrations.
7. **Check for port conflicts**: If an example fails to bind, ensure no other process is using the same port.

<div class="warning">

Software RDMA providers (rxe and siw) run RDMA protocol processing in the kernel. Heavy RDMA workloads on software providers consume significant CPU. If you observe high CPU usage during testing, this is expected and does not indicate a bug. Hardware RDMA offloads this processing to the NIC.

</div>
