# 16.4 Secure Deployment Practices

Understanding RDMA's security model and attack surfaces is necessary but not sufficient. This section provides concrete, actionable guidance for deploying RDMA securely in production environments. The recommendations are organized from network-level isolation down to application-level practices, reflecting the defense-in-depth approach that RDMA deployments require.

## Network Isolation: Dedicated RDMA VLANs and Subnets

The single most effective security measure for RDMA deployments is network isolation. RDMA traffic should traverse a dedicated network segment, physically or logically separated from general-purpose network traffic.

### Physical Isolation

For InfiniBand deployments, physical isolation is inherent: the InfiniBand fabric uses separate cables, switches, and HCAs from the Ethernet network. This provides strong isolation with no additional configuration required.

For RoCE deployments, the ideal approach is a dedicated physical network:

```text
Recommended RoCE Network Architecture:
┌─────────┐     ┌──────────────────┐     ┌─────────┐
│  Host A  │     │  Dedicated RoCE  │     │  Host B  │
│  NIC p1 ─┼─────┤  Switch Fabric   ├─────┼─ NIC p1 │
│  NIC p2 ─┼─────┤  (no uplinks to  ├─────┼─ NIC p2 │
│          │     │  general network)│     │          │
└─────────┘     └──────────────────┘     └─────────┘
```

When a dedicated physical network is not feasible, use VLANs to create logical isolation:

```bash
# Create a dedicated VLAN for RoCE traffic
ip link add link enp1s0f0 name enp1s0f0.100 type vlan id 100

# Assign an IP address on the RDMA subnet
ip addr add 10.0.100.1/24 dev enp1s0f0.100
ip link set enp1s0f0.100 up

# Configure RoCE to use the VLAN interface
# The RDMA device will automatically use the VLAN GID
ibv_devinfo -d mlx5_0 -v | grep GID
```

<div class="warning">

**Warning:** VLAN isolation is only as strong as the switch configuration. Ensure that the RDMA VLAN is not trunked to ports that do not need access. Verify that VLAN hopping protections are enabled on the switch. VLAN isolation is a logical boundary, not a physical one -- a compromised switch can bridge VLANs.

</div>

## Access Control Lists on Switches

For RoCE deployments, configure switch ACLs to restrict which hosts can send and receive RoCE traffic:

```text
# Example switch ACL (syntax varies by vendor)
# Allow RoCE (UDP port 4791) only between known RDMA hosts
permit udp 10.0.100.0/24 10.0.100.0/24 eq 4791
deny   udp any any eq 4791
```

This prevents unauthorized hosts on the same network from injecting RoCE packets. While not foolproof (an attacker who compromises a permitted host bypasses this), it narrows the attack surface significantly.

For InfiniBand, the equivalent is port-level access control on the Subnet Manager, which determines which ports can join which partitions.

## P_Key Partitioning for InfiniBand

InfiniBand's P_Key (Partition Key) mechanism provides fabric-level isolation between groups of hosts. Proper P_Key configuration is essential for multi-tenant InfiniBand deployments.

### Configuring P_Key Partitions with OpenSM

The Subnet Manager (OpenSM) controls P_Key assignments through its partition configuration file:

```ini
# /etc/opensm/partitions.conf

# Default partition - all ports (limited membership for most)
Default=0x7fff,ipoib:ALL=full

# Tenant A partition - only tenant A's ports
TenantA=0x0001:
    HostA1_HCA1_P1=full,
    HostA2_HCA1_P1=full,
    HostA3_HCA1_P1=full;

# Tenant B partition - only tenant B's ports
TenantB=0x0002:
    HostB1_HCA1_P1=full,
    HostB2_HCA1_P1=full;

# Storage partition - accessible by all tenants (read-only storage)
Storage=0x0003:
    StorageNode1_HCA1_P1=full,
    HostA1_HCA1_P1=limited,
    HostB1_HCA1_P1=limited;
```

Full membership allows a port to communicate with all other members of the partition. Limited membership restricts communication to full members only, preventing limited-limited communication within the partition.

### Verifying P_Key Configuration

```bash
# Check which P_Keys are assigned to a port
cat /sys/class/infiniband/mlx5_0/ports/1/pkeys/*
# Expected output: one entry per assigned P_Key

# Query the SA for partition information
saquery -P
```

<div class="tip">

**Tip:** Always assign tenants to non-default partitions. The default partition (P_Key 0x7FFF) typically includes all ports and should only be used for infrastructure services like IPoIB management traffic.

</div>

## Application-Level Authentication Before Sharing MR Info

RDMA does not provide built-in authentication, so applications must implement their own. The critical window is during connection setup, before QP numbers, R_Keys, and memory addresses are exchanged.

### Authentication Protocol Pattern

```text
Secure RDMA Connection Setup:

1. Establish TCP/TLS connection for control plane
2. Authenticate using TLS certificates, tokens, or passwords
3. Exchange authorization context (tenant ID, access level)
4. Only after authentication succeeds:
   a. Exchange QPN, GID/LID for RDMA connection
   b. Transition QPs to RTR/RTS
   c. Exchange R_Keys and memory addresses for RDMA ops
5. Use the authenticated control channel for ongoing key management
```

```c
// Pseudocode for authenticated RDMA setup
int setup_rdma_connection(int peer_fd) {
    // Step 1-2: Authenticate over TCP/TLS
    if (!authenticate_peer(peer_fd)) {
        fprintf(stderr, "Authentication failed, refusing RDMA setup\n");
        close(peer_fd);
        return -1;
    }

    // Step 3: Check authorization
    if (!authorize_rdma_access(peer_fd)) {
        fprintf(stderr, "Peer not authorized for RDMA access\n");
        close(peer_fd);
        return -1;
    }

    // Step 4: Only now exchange RDMA connection info
    exchange_qp_info(peer_fd, &local_qp_info, &remote_qp_info);
    connect_qps(&local_qp_info, &remote_qp_info);

    // Step 5: Exchange MR info over the authenticated channel
    exchange_mr_info(peer_fd, &local_mr_info, &remote_mr_info);

    return 0;
}
```

The key principle is that R_Keys and memory addresses should never be shared with unauthenticated peers. Treat R_Key exchange as you would sharing a private key -- it grants direct access to your memory.

## R_Key Lifetime Management

R_Keys should follow the principle of minimal lifetime: create access only when needed, and revoke it as soon as possible.

### Best Practices

1. **Register late:** Do not register memory regions at application startup and keep them registered forever. Register when access is needed.

2. **Deregister promptly:** When a data transfer is complete and the remote peer no longer needs access, deregister the MR or invalidate the Memory Window.

3. **Use Memory Windows for short-lived access:** For operations where a peer needs temporary access to a buffer (e.g., a single RDMA Write target), bind a Memory Window for the duration of the operation and invalidate it upon completion.

4. **Rotate keys periodically:** For long-lived MRs, consider periodically deregistering and re-registering to get a new R_Key. Notify the peer through the control channel.

```c
// Pattern: short-lived R_Key for a single operation
struct ibv_mr *mr = ibv_reg_mr(pd, buf, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

// Share R_Key with peer for one write operation
send_rkey_to_peer(peer_fd, mr->rkey, (uint64_t)buf, size);

// Wait for peer to complete the write
wait_for_completion_notification(peer_fd);

// Immediately revoke access
ibv_dereg_mr(mr);
```

## Memory Zeroing Before Registration

Registering memory for remote access exposes its contents to remote peers. If the memory contains residual data from previous use, this data becomes accessible.

```c
// Secure buffer registration pattern
void *buf = mmap(NULL, size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
if (buf == MAP_FAILED) {
    perror("mmap");
    return NULL;
}

// mmap with MAP_ANONYMOUS returns zero-filled pages
// But if reusing buffers, explicitly zero before re-registering
memset(buf, 0, size);

struct ibv_mr *mr = ibv_reg_mr(pd, buf, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
```

For sensitive applications, consider using `explicit_bzero()` instead of `memset()` to prevent compiler optimization from eliding the zeroing:

```c
#include <string.h>
explicit_bzero(buf, size);  // Cannot be optimized away
```

## Monitoring and Auditing RDMA Connections

RDMA's kernel bypass makes traditional system monitoring tools ineffective for the data path. However, several monitoring approaches provide visibility:

### Hardware Counter Monitoring

NIC hardware counters track operations, errors, and traffic volumes. Export these to your monitoring system:

```bash
# Key security-relevant counters
cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_errors
cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_xmit_discards
cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/packet_seq_err
```

Alert on sudden increases in error counters, which may indicate scanning attempts or configuration issues.

### Connection Tracking

The `rdma` tool and `/sys/class/infiniband/` filesystem provide visibility into active RDMA resources:

```bash
# List active RDMA resources
rdma resource show

# Show QPs with details
rdma resource show qp

# Show MRs (limited info available for security reasons)
rdma resource show mr
```

### Kernel Log Monitoring

The kernel RDMA subsystem logs resource creation and errors to dmesg:

```bash
# Monitor RDMA-related kernel messages
dmesg | grep -i "rdma\|infiniband\|mlx5\|ib_"
```

## Container and VM Isolation with RDMA

Running RDMA workloads in containers or VMs introduces additional isolation requirements.

### SR-IOV for VM Isolation

SR-IOV Virtual Functions (VFs) provide hardware-isolated RDMA devices for VMs. Each VF has its own set of QPs, MRs, and PDs, isolated by the NIC hardware:

```bash
# Enable SR-IOV on the NIC
echo 8 > /sys/class/infiniband/mlx5_0/device/sriov_numvfs

# Assign a VF to a VM via VFIO passthrough
# The VM gets a fully isolated RDMA device
```

### Container RDMA Access

For containers, RDMA device access can be controlled through device cgroups and the RDMA cgroup controller:

```bash
# Limit RDMA resources for a container cgroup
echo "mlx5_0 hca_handle=256 hca_object=4096" > \
    /sys/fs/cgroup/rdma/container_1/rdma.max
```

<div class="note">

**Note:** When using RDMA in containers, be aware that the `/dev/infiniband/` device files and the `sysfs` entries must be accessible within the container. Most container runtimes require explicit device passthrough configuration. Kubernetes provides the `rdma/hca` device plugin for managing RDMA device allocation to pods.

</div>

### Namespace Isolation

Linux RDMA namespace support (introduced in kernel 5.1+) allows isolating RDMA devices into network namespaces, providing stronger isolation for containers:

```bash
# Move an RDMA device to a network namespace
rdma system set netns exclusive
# Now RDMA devices follow their net_device into namespaces
```

## Future: Encryption Offload and Secure RDMA

Several technologies are emerging to address RDMA's security limitations:

**MACsec offload:** Modern NICs can perform MACsec (802.1AE) encryption in hardware, encrypting all Ethernet frames -- including RoCE -- at line rate. This provides link-layer encryption without software overhead.

**IPsec offload:** NIC-based IPsec encryption can protect RoCE traffic at the network layer, with hardware acceleration maintaining near-line-rate throughput.

**TLS/kTLS offload:** Some NICs support kernel TLS offload, which could potentially be extended to protect RDMA control channels.

**Confidential computing integration:** AMD SEV-SNP, Intel TDX, and ARM CCA create hardware-enforced trusted execution environments. Integrating RDMA with these technologies would allow direct NIC-to-TEE memory access without exposing data to the host OS. This is an active area of research and development.

**In-NIC encryption for RDMA:** Proprietary solutions from NIC vendors are beginning to offer per-QP or per-MR encryption, where the NIC encrypts data before transmission and decrypts it upon reception, transparent to the application. This directly addresses the plaintext-on-the-wire concern without application changes.

The trajectory is clear: RDMA security is evolving from the "trusted cluster" model toward a "zero-trust" model where endpoints authenticate each other, data is encrypted in transit, and hardware enforces isolation at every level. The challenge is achieving this without sacrificing the latency and throughput advantages that make RDMA valuable in the first place.
