# 16.3 Attack Surfaces and Mitigations

RDMA introduces attack surfaces that do not exist in traditional networking. The combination of kernel bypass, direct memory access, and hardware-enforced (rather than software-enforced) security creates opportunities for exploitation that require specific awareness and countermeasures. This section catalogs the known attack vectors and the mitigations available today.

## Remote Memory Scanning

The most distinctive RDMA attack vector is remote memory scanning: an attacker with access to the RDMA fabric attempts to guess valid R_Key and address combinations to read or write memory on a remote host.

### How the Attack Works

An attacker with a connected QP to a target host can issue RDMA Read or Write operations with speculative R_Key values and addresses. If the R_Key and address happen to be valid, the operation succeeds and the attacker obtains data (for reads) or corrupts data (for writes). If invalid, the NIC returns a Remote Access Error, and the QP transitions to an error state.

The QP error state is both a protection and a limitation. On Reliable Connected QPs, a single failed probe destroys the QP, requiring reconnection. This makes brute-force scanning slow and detectable. However, an attacker with the ability to create many QPs (perhaps via SR-IOV) could parallelize the probing.

```text
Attacker's Strategy:
1. Connect RC QP to target
2. Try RDMA Read with guessed R_Key and address
3. If success → data obtained
4. If failure → QP enters error state, create new QP, try next guess
```

### Mitigations

**Randomized R_Keys:** Modern NIC firmware randomizes the key portion of R_Keys. While the key portion is only 8 bits (256 values), the index portion adds 24 bits, yielding a 32-bit search space of roughly 4 billion possibilities. Combined with the QP-destruction penalty per failed attempt, brute-force scanning becomes impractical.

**Address Space Layout Randomization (ASLR):** Even with a valid R_Key, the attacker must guess the correct virtual address within the memory region. With 64-bit virtual addresses and ASLR enabled, the address search space is enormous.

**Connection authentication:** Requiring application-level authentication before establishing RDMA connections ensures that only authorized peers can even attempt memory operations.

**Monitoring:** Track QP error rates. A sudden spike in Remote Access Errors may indicate a scanning attempt.

<div class="note">

**Note:** On Unreliable Datagram (UD) QPs, the scanning problem is different. UD does not support RDMA Read/Write operations, so memory scanning via one-sided operations is not possible on UD. However, UD QPs can receive unsolicited messages from any sender that knows the QPN and Q_Key.

</div>

## Denial of Service

RDMA systems are vulnerable to several denial-of-service vectors that exploit the high-speed, low-latency nature of the protocol.

### QP Flooding

An attacker with a connected QP can flood the target with operations at line rate. Since RDMA operations bypass the kernel, there is no kernel-level rate limiting or connection throttling. A single 100 Gbps connection can generate millions of operations per second, overwhelming the target's completion queue processing.

**Mitigation:** Hardware rate limiting on the NIC, if available. Some NICs support per-QP or per-VF rate limiting. At the application level, set reasonable QP depth limits and monitor CQ processing rates.

### NIC Resource Exhaustion

NIC hardware has finite resources: memory translation table entries, QP contexts, completion queue entries, and internal buffer space. An attacker who can create many QPs or register many memory regions can exhaust these resources, denying service to legitimate applications.

```bash
# Check current NIC resource limits
ibv_devinfo -v | grep -E "(max_qp|max_mr|max_cq)"
#   max_qp:           262144
#   max_qp_wr:        32768
#   max_mr:           16777216
#   max_cq:           16777216
```

**Mitigation:** Use cgroups or RDMA controller to limit per-process or per-container resource allocation:

```bash
# Using rdma cgroup controller (Linux 4.11+)
echo "mlx5_0 hca_handle=100 hca_object=200" > \
    /sys/fs/cgroup/rdma/tenant_1/rdma.max
```

The RDMA cgroup controller can limit:
- `hca_handle`: Maximum number of handles (PDs, MRs, QPs, etc.)
- `hca_object`: Maximum number of objects of any type

### Receive Buffer Exhaustion

For Send/Receive operations, the receiver must have pre-posted receive buffers. An attacker can deplete the receiver's buffers by sending many small messages rapidly, causing subsequent legitimate messages to be dropped with RNR (Receiver Not Ready) NAKs.

**Mitigation:** Use Shared Receive Queues (SRQs) to pool receive buffers across connections. Set appropriate SRQ watermarks to trigger refilling before exhaustion. Configure `rnr_retry` to allow temporary buffer shortages to resolve without killing the connection.

## Unauthorized Memory Access

Beyond brute-force scanning, unauthorized memory access can result from application-level bugs in R_Key management:

### Use-After-Deregister

If an application shares an R_Key with a peer, then deregisters the memory region, the R_Key becomes invalid. However, if the same index is reused for a new memory region with a different key value, the old R_Key will fail validation. The risk arises if the NIC reassigns the same index *and* key value (unlikely with randomization, but possible).

```c
// Dangerous pattern:
struct ibv_mr *mr = ibv_reg_mr(pd, secret_data, size, access);
share_rkey_with_peer(mr->rkey, secret_data);
ibv_dereg_mr(mr);

// If MR index is reused, peer might access new data
struct ibv_mr *mr2 = ibv_reg_mr(pd, other_data, size, access);
// Risk: if mr2 gets same R_Key, peer can access other_data
```

**Mitigation:** Always notify remote peers that an R_Key has been invalidated before deregistering the MR. Use Memory Windows Type 2 with explicit invalidation for fine-grained lifetime control.

### Overprovisioned Access Flags

Registering memory with more access flags than needed is a common source of vulnerability. If a memory region only needs to be read remotely, granting `IBV_ACCESS_REMOTE_WRITE` unnecessarily allows a compromised or malicious peer to corrupt the data.

**Mitigation:** Apply the principle of least privilege. Audit all `ibv_reg_mr()` calls and remove unnecessary access flags.

## Side-Channel Attacks

RDMA's predictable, low-latency nature makes it susceptible to timing-based side-channel attacks.

### Timing Attacks on RDMA Operations

RDMA operations complete with highly consistent latency -- often within a few hundred nanoseconds with very low variance. This consistency can leak information:

- **Cache-line detection:** The latency of an RDMA Read may vary slightly depending on whether the target data is in the remote CPU's cache, in local DRAM, or involves a TLB miss. While the differences are small (tens of nanoseconds), they may be measurable with RDMA's precise completion timestamps.

- **Contention detection:** If RDMA operations to a particular address range show increased latency, it may indicate that another process is actively accessing that memory, revealing information about the other process's behavior.

**Mitigation:** These attacks are theoretical and have not been widely demonstrated in practice. Constant-time programming practices and avoiding shared memory regions between security domains reduce the risk.

### RDMA-Assisted Rowhammer

Research has demonstrated that RDMA Write operations can be used to perform Rowhammer attacks on remote machines. By repeatedly writing to carefully chosen addresses, an attacker can induce bit flips in adjacent DRAM rows, potentially corrupting security-critical data structures.

<div class="warning">

**Warning:** RDMA-assisted Rowhammer is a demonstrated attack (see Tatar et al., "Throwhammer: Rowhammer Attacks over the Network and Defenses," 2018). If you grant `IBV_ACCESS_REMOTE_WRITE` to untrusted peers, they can potentially induce bit flips in your system's DRAM. Use ECC memory and limit remote write access to trusted peers only.

</div>

## Network-Level Attacks

### InfiniBand Fabric Attacks

InfiniBand's security relies heavily on the Subnet Manager (SM), which controls all fabric configuration:

- **Rogue Subnet Manager:** An attacker who can run a Subnet Manager on the fabric can reconfigure routing, P_Key assignments, and port properties. **Mitigation:** Use SM authentication (available in recent OpenSM versions) and physical port security.

- **Packet injection:** InfiniBand does not authenticate packet sources at the hardware level. An attacker with physical access to the fabric could inject packets. **Mitigation:** Physical security of the fabric; monitoring for unexpected traffic patterns.

- **P_Key bypass:** If the SM misconfigures P_Key tables, tenants in different partitions could communicate. **Mitigation:** Validate SM configuration; use automated configuration management.

### RoCE Network Attacks

RoCE runs over standard Ethernet and inherits its network-level vulnerabilities:

- **ARP spoofing:** An attacker on the same L2 network can redirect RoCE traffic by spoofing ARP responses. **Mitigation:** Use static ARP entries or ARP protection features on managed switches.

- **VLAN hopping:** Misconfigured switches may allow traffic to cross VLAN boundaries. **Mitigation:** Proper switch configuration, VLAN access control lists.

- **Traffic sniffing:** RoCE traffic on shared Ethernet is visible to anyone with access to the network segment. **Mitigation:** Network isolation; consider MACsec encryption at the link layer.

```bash
# Verify RoCE traffic isolation: check that only expected VLANs are present
bridge vlan show dev enp1s0f0
# Should show only the RDMA VLAN, not trunk ports
```

## Memory Exposure Risks

### Uninitialized Buffers

When memory is registered for RDMA operations, it may contain residual data from previous use. If this memory is exposed via `IBV_ACCESS_REMOTE_READ`, an attacker could read sensitive data that the application has not explicitly placed there.

```c
// Dangerous: registering uninitialized heap memory
void *buf = malloc(4096);
// buf may contain data from previous allocations
struct ibv_mr *mr = ibv_reg_mr(pd, buf, 4096,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
// Remote peer can now read whatever was in that memory
```

**Mitigation:** Always zero memory before registering it for remote access:

```c
void *buf = calloc(1, 4096);  // Zero-initialized
// Or:
void *buf = malloc(4096);
memset(buf, 0, 4096);
struct ibv_mr *mr = ibv_reg_mr(pd, buf, 4096,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
```

### Use-After-Free via RDMA

If memory is freed but the MR is not deregistered, or if a race condition allows access between deregistration and freeing, RDMA operations could access freed memory. With modern allocators that reuse memory aggressively, this could expose data from unrelated allocations.

**Mitigation:** Always deregister the MR before freeing the underlying memory. Use a strict ordering protocol: deregister MR, ensure all pending operations complete, then free memory.

## RDMA-Specific CVEs and Lessons Learned

Several CVEs have highlighted RDMA security issues in production systems:

- **CVE-2019-19462 (Linux kernel):** A vulnerability in the rdma-core kernel module allowed local privilege escalation through malformed verbs commands. This underscores that while the data path bypasses the kernel, the control path (resource creation and management) remains a kernel attack surface.

- **CVE-2021-3444 (Linux kernel):** A bug in BPF verification could be exploited via RDMA code paths, allowing unauthorized memory access.

- **NIC firmware vulnerabilities:** Several NIC vendors have issued firmware updates to address issues where VF isolation could be weakened or R_Key validation could be bypassed under specific conditions.

The common lessons from these incidents:

1. **Keep firmware and drivers updated.** RDMA security improvements are frequently delivered through firmware updates.
2. **The control path is still a kernel attack surface.** Even though the data path bypasses the kernel, verbs calls go through kernel drivers that may have vulnerabilities.
3. **Defense in depth matters.** Do not rely solely on R_Key secrecy or PD isolation. Layer network isolation, application authentication, and resource limits.
4. **Monitor and audit.** RDMA's performance-oriented design makes monitoring harder, but not impossible. Hardware counters and connection tracking provide visibility into RDMA activity.
