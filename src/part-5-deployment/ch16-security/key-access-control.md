# 16.2 Key and Access Control

RDMA's access control mechanisms are built into the hardware, enforced at line rate on every operation. Understanding these mechanisms in detail is essential for building secure RDMA applications. This section covers the full hierarchy of access control, from memory keys to protection domains to partition keys.

## R_Key Security: Remote Memory Access Authorization

The Remote Key (R_Key) is the primary authorization token for RDMA one-sided operations. When a remote node wants to read or write memory on a local node, it must present the correct R_Key along with the target virtual address. The NIC validates the R_Key in hardware before allowing the operation to proceed.

An R_Key is a 32-bit value composed of two parts:

- **Index (24 bits):** Identifies the Memory Region in the NIC's internal table.
- **Key portion (8 bits):** A validation tag that must match the stored value.

This structure is important for security because it determines how guessable an R_Key is.

### Predictable R_Key Generation

Older NIC firmware and some libibverbs implementations generated R_Keys with a predictable pattern. The index portion would increment sequentially, and the key portion might start at zero or follow a simple pattern. In such implementations, if an attacker knows one valid R_Key, they can predict others with reasonable probability.

```c
// Example of what predictable R_Keys might look like:
// MR 1: R_Key = 0x00000100
// MR 2: R_Key = 0x00000200
// MR 3: R_Key = 0x00000300
// An attacker who sees 0x00000100 can guess 0x00000200
```

This predictability is a genuine security risk. An attacker with access to the RDMA fabric could systematically probe R_Key values to discover valid memory regions on remote hosts.

### Randomized R_Key Generation

Modern NIC firmware and rdma-core implementations randomize the key portion of the R_Key. When you call `ibv_reg_mr()`, the returned R_Key contains random bits in the key field, making it infeasible to guess valid keys by probing.

```c
struct ibv_mr *mr = ibv_reg_mr(pd, buffer, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);

// mr->rkey now contains a randomized key
// The 8-bit key portion is random, not sequential
printf("R_Key: 0x%08x\n", mr->rkey);
```

<div class="warning">

**Warning:** Even with randomized R_Keys, the key space is only 8 bits (256 possible values) for a given index. If the index can be guessed, an attacker needs at most 256 attempts to find a valid R_Key. Do not rely on R_Key secrecy as your sole security mechanism. Always combine R_Key protection with network isolation and application-level authentication.

</div>

To verify that your hardware uses randomized keys, register multiple memory regions and inspect the R_Key values. If the key portions are sequential or identical, consult your NIC vendor about firmware updates.

## L_Key Validation: Local Access Control

The Local Key (L_Key) serves a similar role for local operations. When the NIC processes a Send or Receive work request, it validates the L_Key of each scatter/gather entry against the memory region table. This ensures that the application can only use buffers it has properly registered.

L_Key validation prevents several classes of bugs and attacks:

- **Use-after-deregister:** If a buffer is deregistered while still referenced by a pending work request, the L_Key validation will fail, preventing access to potentially reallocated memory.
- **Cross-PD access:** An L_Key from one protection domain cannot be used with a QP in a different protection domain.
- **Privilege escalation:** An application cannot forge L_Keys to access memory regions it did not register.

```c
// L_Key is stored in the sge (scatter/gather entry)
struct ibv_sge sge = {
    .addr   = (uint64_t)buffer,
    .length = buffer_size,
    .lkey   = mr->lkey,  // Must match a valid MR in the same PD
};
```

## Protection Domain Isolation

Protection Domains (PDs) are the fundamental security boundary within a single RDMA device. A PD groups together related resources -- QPs, MRs, MWs, and SRQs -- and enforces that they can only interact within the same domain.

### PD as Security Boundary

When an application creates a PD with `ibv_alloc_pd()`, it establishes an isolated namespace. The NIC hardware enforces the following invariants:

1. **A QP can only post work requests referencing MRs in the same PD.** If a QP in PD-A attempts to use an L_Key belonging to PD-B, the NIC rejects the operation with a local protection error.

2. **An MR's R_Key is only valid within the context of its PD.** Even if an attacker obtains an R_Key, they cannot use it from a QP in a different PD.

3. **Memory Windows can only be bound to MRs in the same PD.** This prevents using MWs to bypass PD isolation.

```text
PD-A (Application 1)        PD-B (Application 2)
┌─────────────────────┐     ┌─────────────────────┐
│  QP-1    QP-2       │     │  QP-3    QP-4       │
│  MR-1    MR-2       │     │  MR-3    MR-4       │
│  MW-1               │     │  MW-2               │
└─────────────────────┘     └─────────────────────┘
    ✗ Cannot cross PD boundary ✗
```

### Cross-PD Access Prevention

The NIC's memory key validation includes a PD check. Each entry in the NIC's memory translation table records the PD that owns the MR. When validating an R_Key or L_Key, the NIC checks:

1. The key value matches the stored key.
2. The PD of the MR matches the PD of the QP performing the operation.
3. The access flags permit the requested operation (read, write, atomic).
4. The address and length fall within the MR's registered range.

All four checks must pass for the operation to succeed. This is implemented in hardware and adds no latency to the data path.

### PD-Per-Tenant Design Pattern

In multi-tenant systems, the best practice is to allocate a separate PD for each tenant or application:

```c
// Server handling multiple tenants
struct ibv_pd *tenant_pds[MAX_TENANTS];

for (int i = 0; i < num_tenants; i++) {
    tenant_pds[i] = ibv_alloc_pd(context);
    if (!tenant_pds[i]) {
        fprintf(stderr, "Failed to allocate PD for tenant %d\n", i);
        return -1;
    }
}

// Each tenant's QPs and MRs are created in their own PD
struct ibv_qp *create_tenant_qp(int tenant_id, struct ibv_cq *cq) {
    struct ibv_qp_init_attr attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .cap = { .max_send_wr = 128, .max_recv_wr = 128,
                 .max_send_sge = 1, .max_recv_sge = 1 },
        .qp_type = IBV_QPT_RC,
    };
    return ibv_create_qp(tenant_pds[tenant_id], &attr);
}
```

## Memory Access Flags

When registering a memory region, the access flags determine what operations are permitted. Principle of least privilege applies: grant only the access that is actually needed.

```c
// Read-only remote access (most restrictive for RDMA reads)
struct ibv_mr *mr_readonly = ibv_reg_mr(pd, buf, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);

// Remote write access (needed for RDMA writes)
struct ibv_mr *mr_writable = ibv_reg_mr(pd, buf, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

// Atomic access (needed for CAS and fetch-and-add)
struct ibv_mr *mr_atomic = ibv_reg_mr(pd, buf, size,
    IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_ATOMIC);
```

The available flags and their security implications:

| Flag | Permits | Security Risk |
|---|---|---|
| `IBV_ACCESS_LOCAL_WRITE` | Local write via NIC | Minimal -- required for receives |
| `IBV_ACCESS_REMOTE_READ` | Remote RDMA reads | Data exposure to remote peer |
| `IBV_ACCESS_REMOTE_WRITE` | Remote RDMA writes | Data corruption by remote peer |
| `IBV_ACCESS_REMOTE_ATOMIC` | Remote atomic ops | Data corruption, race conditions |
| `IBV_ACCESS_MW_BIND` | Memory Window binding | Enables dynamic sub-region access |

<div class="tip">

**Tip:** Never register memory with `IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_ATOMIC` unless the application genuinely requires all three. Each flag expands the attack surface. A common mistake is copying example code that enables all flags for convenience.

</div>

## Memory Windows for Temporary, Revocable Access

Memory Windows (MWs) address a fundamental limitation of Memory Regions: once you share an R_Key, you cannot revoke it without deregistering the entire MR. Memory Windows provide fine-grained, temporary, and revocable access to subsets of a registered MR.

### Type 1 Memory Windows

Type 1 MWs are bound using a special verb call (`ibv_bind_mw()`). They provide:
- Access to a sub-range of an existing MR
- Independent R_Key from the underlying MR
- Revocable by re-binding or deallocating

### Type 2 Memory Windows

Type 2 MWs are bound using a work request posted to a QP. They provide stronger security guarantees:
- Bound atomically with respect to other QP operations
- Can be invalidated remotely via `IBV_WR_LOCAL_INV` or `IBV_WR_SEND_WITH_INV`
- Each bind generates a new R_Key, invalidating the previous one

```c
// Create a Type 2 Memory Window
struct ibv_mw *mw = ibv_alloc_mw(pd, IBV_MW_TYPE_2);

// Bind it to a sub-range of an existing MR via a work request
struct ibv_send_wr wr = {
    .opcode     = IBV_WR_BIND_MW,
    .bind_mw = {
        .mw      = mw,
        .rkey    = ibv_inc_rkey(mw->rkey), // New R_Key for this binding
        .bind_info = {
            .mr        = mr,
            .addr      = (uint64_t)buf + offset,
            .length    = subset_size,
            .mw_access_flags = IBV_ACCESS_REMOTE_READ,
        },
    },
};
```

Memory Windows are particularly valuable for storage systems and key-value stores where access to specific data regions needs to be granted and revoked dynamically as operations complete.

## QP Access Control

Queue Pair access control determines which remote QPs can communicate with a local QP. The mechanisms differ between connection types:

**Reliable Connected (RC):** Each QP is connected to exactly one remote QP. The connection itself serves as access control -- only the connected peer can send operations. Connection setup happens out-of-band, giving the application an opportunity to authenticate the remote peer before exchanging QP information.

**Unreliable Datagram (UD):** Any QP can send to any other UD QP if it knows the QPN and the address handle (LID for IB, GID for RoCE). UD QPs have no inherent access control beyond Q_Key matching. The Q_Key is a 32-bit value that must match between sender and receiver.

```c
// UD QP: Q_Key provides basic access control
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_RTS,
    .qkey     = 0x11111111,  // Must match remote QP's Q_Key
};
```

<div class="note">

**Note:** For UD QPs, the Q_Key is the only access control mechanism. If an attacker can guess or obtain the Q_Key, they can send messages to the QP. Use sufficiently random Q_Key values and do not reuse them across security domains.

</div>

## Port-Level Access Control: P_Key Partitioning

InfiniBand provides fabric-level access control through Partition Keys (P_Keys). A P_Key is a 16-bit value (with the high bit indicating full or limited membership) that segments the fabric into isolated partitions. Only QPs with matching P_Keys can communicate.

P_Keys are managed by the Subnet Manager, which configures each port's P_Key table. An application can only use P_Keys that have been assigned to its port by the administrator.

```bash
# View P_Key table for a port
cat /sys/class/infiniband/mlx5_0/ports/1/pkeys/0
# Output: 0xffff (default full-membership partition)

cat /sys/class/infiniband/mlx5_0/ports/1/pkeys/1
# Output: 0x8001 (full-membership in partition 1)
```

P_Key partitioning is analogous to VLANs in Ethernet: it provides logical network segmentation without requiring separate physical infrastructure. In multi-tenant InfiniBand deployments, each tenant is assigned a unique P_Key partition, and the Subnet Manager enforces that only ports in the same partition can communicate.

For RoCE deployments, there is no P_Key equivalent in the Ethernet fabric. Instead, network isolation must be achieved through VLANs, ACLs on switches, and firewall rules -- traditional Ethernet segmentation mechanisms that operate independently of RDMA.

The complete access control hierarchy, from broadest to most specific, is:

```text
P_Key Partition (fabric-level, IB only)
  └── Protection Domain (device-level)
        └── Memory Region + Access Flags (memory-level)
              └── Memory Window (sub-region-level, revocable)
```

Each layer narrows the scope of permitted access. Effective RDMA security uses all applicable layers rather than relying on any single mechanism.
