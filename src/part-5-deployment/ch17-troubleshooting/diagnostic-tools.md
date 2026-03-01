# 17.1 Diagnostic Tools

A comprehensive toolkit is essential for RDMA troubleshooting. Unlike traditional networking where a handful of tools (ping, traceroute, tcpdump, ss) cover most scenarios, RDMA diagnosis requires different tools for different layers of the stack. This section covers every tool you need, organized from basic device discovery to advanced packet capture.

## Device Information: ibv_devinfo

The `ibv_devinfo` command is the first tool to reach for when diagnosing any RDMA issue. It queries the NIC through the verbs interface and reports device capabilities, port status, and configuration.

```bash
# Basic device info
$ ibv_devinfo
hca_id: mlx5_0
    transport:          InfiniBand (0)
    fw_ver:             22.36.1010
    node_guid:          b8:ce:f6:03:00:a7:d4:92
    sys_image_guid:     b8:ce:f6:03:00:a7:d4:92
    vendor_id:          0x02c9
    vendor_part_id:     4123
    hw_ver:             0x0
    phys_port_cnt:      1
        port:   1
            state:          PORT_ACTIVE (4)
            max_mtu:        4096 (5)
            active_mtu:     4096 (5)
            sm_lid:         1
            port_lid:       5
            port_lmc:       0
            link_layer:     InfiniBand
```

Key fields to check:

- **state: PORT_ACTIVE (4)** -- The port must be in ACTIVE state. Other states indicate a problem:
  - `PORT_DOWN (1)`: No physical link. Check cable, switch port, or module.
  - `PORT_INIT (2)`: Link is up but not configured. The Subnet Manager has not yet activated the port (InfiniBand), or the interface has not been brought up (RoCE).
  - `PORT_ARMED (3)`: InfiniBand-specific; the SM has configured the port but has not activated it yet.

- **active_mtu:** Verify this matches your expected configuration. A 4096 (4K) MTU is standard for InfiniBand. For RoCE, this should match the Ethernet MTU configuration.

- **fw_ver:** Note the firmware version for cross-referencing with known issues.

```bash
# Verbose output shows capabilities and limits
$ ibv_devinfo -v -d mlx5_0
```

The verbose output includes maximum resource limits (max QPs, max MRs, max CQEs), which is useful when diagnosing resource exhaustion.

## Device Listing: ibv_devices

The simplest RDMA command, `ibv_devices` lists all RDMA devices visible to userspace:

```bash
$ ibv_devices
    device          node GUID
    ------          ----------------
    mlx5_0          b8cef60300a7d492
    mlx5_1          b8cef60300a7d493
```

If this command returns no devices, the problem is at the driver or hardware level:
- Verify the NIC is physically present: `lspci | grep -i mellanox` (or your vendor)
- Verify the driver is loaded: `lsmod | grep mlx5_core`
- Check dmesg for driver errors: `dmesg | grep mlx5`

## Modern Interface: rdma link show

The `rdma` command (from iproute2) provides a modern interface for RDMA device management:

```bash
$ rdma link show
link mlx5_0/1 state ACTIVE physical_state LINK_UP netdev enp1s0f0
link mlx5_1/1 state ACTIVE physical_state LINK_UP netdev enp1s0f1

# Show detailed resource usage
$ rdma resource show
1: mlx5_0: pd 4 cq 8 qp 12 mr 24 ctx 3

# Show specific resource types with details
$ rdma resource show qp
```

The `rdma` command also maps RDMA devices to their network interfaces (`netdev` field), which is essential for RoCE troubleshooting.

## InfiniBand Status: ibstat and ibstatus

For InfiniBand-specific information, `ibstat` provides detailed port status:

```bash
$ ibstat
CA 'mlx5_0'
    CA type: MT4123
    Number of ports: 1
    Firmware version: 22.36.1010
    Hardware version: 0
    Node GUID: 0xb8cef60300a7d492
    System image GUID: 0xb8cef60300a7d492
    Port 1:
        State: Active
        Physical state: LinkUp
        Rate: 100
        Base lid: 5
        LMC: 0
        SM lid: 1
        Capability mask: 0x2651e848
        Port GUID: 0xb8cef60300a7d492
        Link layer: InfiniBand
```

The **Rate** field shows the active link speed (e.g., 100 for EDR 100 Gbps, 200 for HDR 200 Gbps). If this is lower than expected, check cable quality and switch port configuration.

## Perftest: Verifying Basic Connectivity

The `perftest` package provides essential diagnostic tools that go beyond simple ping to verify that RDMA operations actually work. These tools test the full RDMA data path, including memory registration, QP setup, and data transfer.

```bash
# On the server:
$ ib_send_bw -d mlx5_0

# On the client:
$ ib_send_bw -d mlx5_0 192.168.1.1
```

<div class="tip">

**Tip:** Use perftest tools as diagnostic instruments, not just benchmarks. If `ib_send_bw` fails between two hosts, the problem is at the RDMA infrastructure level, not in your application. Perftest failures narrow the problem to link, driver, firmware, or configuration issues.

</div>

Common perftest commands for diagnosis:

| Tool | Tests | Use When |
|---|---|---|
| `ib_send_bw` | Send bandwidth | Verifying basic connectivity and throughput |
| `ib_write_bw` | RDMA Write bandwidth | Testing one-sided operations |
| `ib_read_bw` | RDMA Read bandwidth | Testing one-sided operations |
| `ib_send_lat` | Send latency | Checking for latency anomalies |
| `ib_write_lat` | RDMA Write latency | Checking for latency anomalies |
| `ib_atomic_lat` | Atomic operation latency | Verifying atomic support |

Useful perftest flags for diagnostics:

```bash
# Test with specific MTU
$ ib_send_bw -d mlx5_0 -m 4096 192.168.1.1

# Test with specific message size
$ ib_send_bw -d mlx5_0 -s 65536 192.168.1.1

# Test with GID index (for RoCE, specifies which GID to use)
$ ib_send_bw -d mlx5_0 -x 3 192.168.1.1

# Use specific port
$ ib_send_bw -d mlx5_0 -p 18515 192.168.1.1
```

## InfiniBand Connectivity: ibping

`ibping` provides a basic connectivity test for InfiniBand:

```bash
# On the server:
$ ibping -S

# On the client (ping by LID):
$ ibping -L 5

# On the client (ping by GUID):
$ ibping -G 0xb8cef60300a7d492
```

Note that `ibping` tests InfiniBand management-level connectivity, not the full verbs data path. A successful `ibping` means the IB fabric can route to the target, but does not verify that QP-level operations work.

## InfiniBand Route Tracing: ibtracert

`ibtracert` traces the path through InfiniBand switches between two endpoints:

```bash
# Trace route from local port to remote LID
$ ibtracert 5 12
From ca {0xb8cef60300a7d492} portnum 1 lid 5-5
[1] -> switch port {0x248a070300f5d6a0}[1] lid 1-1 "MF0;switch01:SX6036/U1"
[17] -> ca port {0xb8cef60300a7d494}[1] lid 12-12
To ca {0xb8cef60300a7d494} portnum 1 lid 12-12
```

This is invaluable for diagnosing routing issues, identifying which switches are in the data path, and finding the specific link where failures occur.

## Subnet Manager Information

```bash
# Query the Subnet Manager
$ sminfo
sminfo: sm lid 1 sm guid 0x248a070300f5d6a1, activity count 145232
        priority 14, state 3 SMINFO_MASTER

# Query the Subnet Administrator for various information
$ saquery -N  # Node records
$ saquery -P  # Path records
$ saquery -S  # Service records
```

## RoCE Diagnostics with Standard Tools

For RoCE deployments, traditional Ethernet tools provide valuable diagnostic information.

### ethtool

```bash
# Check link status and speed
$ ethtool enp1s0f0
Settings for enp1s0f0:
    Speed: 100000Mb/s
    Duplex: Full
    Link detected: yes

# Check PFC configuration (critical for RoCE)
$ ethtool -S enp1s0f0 | grep -i pfc
     rx_pfc_pause_0: 0
     rx_pfc_pause_3: 1523
     tx_pfc_pause_3: 892

# Check ring buffer sizes
$ ethtool -g enp1s0f0

# Check driver and firmware info
$ ethtool -i enp1s0f0
driver: mlx5_core
version: 5.15.0
firmware-version: 22.36.1010
```

### ip link

```bash
# Check interface status and MTU
$ ip link show enp1s0f0
2: enp1s0f0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 9000 qdisc mq state UP
    link/ether b8:ce:f6:a7:d4:92 brd ff:ff:ff:ff:ff:ff

# Verify the MTU is 9000 for jumbo frames (required for 4K RDMA MTU)
```

<div class="warning">

**Warning:** RoCE requires jumbo frames (MTU 9000) on the Ethernet interface to support the default RDMA MTU of 4096 bytes. The Ethernet MTU must be at least RDMA_MTU + 50 bytes for headers. An Ethernet MTU of 1500 limits RDMA MTU to 1024 bytes, which severely impacts performance and can cause "transport retry" errors if the RDMA MTU is set higher than the Ethernet path supports.

</div>

## Packet Capture for RoCE

One advantage of RoCE over InfiniBand for troubleshooting is the ability to capture packets with standard tools.

### tcpdump

```bash
# Capture RoCE v2 traffic (UDP port 4791)
$ tcpdump -i enp1s0f0 udp port 4791 -w roce_capture.pcap

# Capture with more detail
$ tcpdump -i enp1s0f0 -nn -v udp port 4791

# Capture between specific hosts
$ tcpdump -i enp1s0f0 host 10.0.100.1 and udp port 4791 -w capture.pcap
```

### Wireshark RoCE Dissector

Wireshark (version 2.6+) includes a built-in dissector for RoCE v2 traffic. It can decode:

- BTH (Base Transport Header): opcode, QPN, PSN, partition key
- RETH (RDMA Extended Transport Header): virtual address, R_Key, DMA length
- AETH (ACK Extended Transport Header): syndrome, MSN
- ImmDt (Immediate Data Header)

To analyze RoCE traffic in Wireshark:

1. Open the pcap file captured with tcpdump.
2. Apply the display filter: `roce` or `udp.port == 4791`.
3. Wireshark will automatically decode the InfiniBand transport headers within the UDP payload.

```text
Wireshark Display Filters for RoCE:
  roce                          - All RoCE packets
  infiniband.bth.opcode == 0x04 - RDMA WRITE First
  infiniband.bth.opcode == 0x06 - RDMA WRITE Only
  infiniband.bth.opcode == 0x0c - RDMA READ Request
  infiniband.bth.opcode == 0x10 - RDMA READ Response First
  infiniband.aeth.syndrome      - ACK/NAK responses
```

<div class="note">

**Note:** RoCE packet capture is a powerful diagnostic tool, but be aware that capturing at 100 Gbps generates enormous volumes of data. Use targeted filters and limited capture durations. On production systems, consider using tcpdump's `-c` flag to limit the number of captured packets.

</div>

### rdma-ndd: Node Description Daemon

The `rdma-ndd` daemon sets the InfiniBand node description to match the system hostname. This makes it easier to identify nodes in fabric management tools:

```bash
# Start the daemon
$ systemctl start rdma-ndd

# Verify node description
$ cat /sys/class/infiniband/mlx5_0/node_desc
myhost.example.com HCA-1
```

Without `rdma-ndd`, nodes may show generic descriptions in subnet management tools, making it harder to correlate fabric issues with specific hosts.

## Quick Reference: Which Tool for Which Problem

| Symptom | First Tool | Second Tool |
|---|---|---|
| No RDMA device visible | `ibv_devices`, `lspci` | `dmesg`, `lsmod` |
| Port not active | `ibv_devinfo`, `ibstat` | `ethtool` (RoCE) |
| Cannot connect to peer | `ibping`, perftest | `ibtracert`, `sminfo` |
| Low bandwidth | `ib_send_bw` | HW counters, `ethtool -S` |
| High latency | `ib_send_lat` | `numactl`, PCIe config |
| Intermittent errors | HW counters | `tcpdump` (RoCE), `dmesg` |
| Resource exhaustion | `rdma resource show` | `ibv_devinfo -v` |
