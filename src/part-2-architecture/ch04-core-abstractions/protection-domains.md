# 4.5 Protection Domains (PD)

The Protection Domain is the simplest of the core RDMA abstractions to create -- a single function call with no configuration parameters -- yet it serves one of the most important roles: **isolation**. In a system where the NIC can directly access application memory without CPU intervention, the consequences of unauthorized access are severe. A Protection Domain groups RDMA objects together and ensures that objects in different domains cannot interact, providing hardware-enforced isolation that is fundamental to RDMA's security model.

## The Purpose of Protection Domains

Consider a multi-tenant environment: two applications, belonging to different users, are running on the same machine and using the same RDMA device. Application A has registered a memory buffer containing sensitive financial data. Application B has created a Queue Pair for its own communication. Without some isolation mechanism, what prevents Application B from constructing a work request that references Application A's memory key and reading its data?

The answer is the Protection Domain. Every MR, every QP, every Address Handle, and every Shared Receive Queue must be associated with a PD at creation time. The NIC hardware enforces the following rule:[^1]

> A work request posted to a QP in PD X can only reference memory keys (lkeys) from MRs in PD X.

If a WQE in QP (PD=1) references an lkey from an MR (PD=2), the NIC rejects the operation with a Local Protection Error -- even if the lkey is otherwise valid. This check is performed entirely in hardware, on every operation, with zero software overhead.

## Creating and Destroying Protection Domains

```c
/* Allocate a Protection Domain */
struct ibv_pd *pd = ibv_alloc_pd(ctx);
if (!pd) {
    perror("ibv_alloc_pd");
    exit(1);
}

/* Use the PD when creating other objects */
struct ibv_mr *mr = ibv_reg_mr(pd, buffer, size, access_flags);
struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
struct ibv_ah *ah = ibv_create_ah(pd, &ah_attr);
struct ibv_srq *srq = ibv_create_srq(pd, &srq_init_attr);

/* ... use objects ... */

/* Cleanup: all objects must be destroyed before the PD */
ibv_dereg_mr(mr);
ibv_destroy_qp(qp);
ibv_destroy_ah(ah);
ibv_destroy_srq(srq);

int ret = ibv_dealloc_pd(pd);
if (ret) {
    fprintf(stderr, "ibv_dealloc_pd failed: %s\n", strerror(ret));
    /* This typically means objects still reference this PD */
}
```

<div class="admonition warning">
<div class="admonition-title">Warning</div>
A Protection Domain cannot be deallocated while any objects (QPs, MRs, AHs, SRQs, MWs) still reference it. <code>ibv_dealloc_pd()</code> will fail with <code>EBUSY</code> if there are outstanding references. Always destroy all dependent objects before deallocating the PD. In complex applications, tracking these dependencies carefully is essential to avoid resource leaks.
</div>

## The PD as an RDMA Address Space

It is helpful to think of the Protection Domain as the RDMA analog of a process address space:

| Concept | Process Model | RDMA Model |
|---------|---------------|------------|
| Isolation boundary | Process address space | Protection Domain |
| Memory access control | Page table permissions | MR access flags + PD membership |
| Identifier | Process ID (PID) | PD number (internal to NIC) |
| Objects within boundary | Memory mappings, file descriptors | MRs, QPs, AHs, SRQs |
| Cross-boundary access | Not possible (separate page tables) | Not possible (PD check in hardware) |
| Creation cost | Fork (expensive) | ibv_alloc_pd (cheap) |

Just as two processes cannot access each other's memory (without explicit shared memory setup), two Protection Domains cannot access each other's memory regions. And just as all threads within a process share the same address space, all QPs and MRs within a PD can interact freely.

## Object Relationships

The following diagram illustrates how the Protection Domain relates to other RDMA objects:

```
                    ┌──────────────────────────────────┐
                    │        ibv_context (device)       │
                    └──────────────────┬───────────────┘
                                       │
                    ┌──────────────────┴───────────────┐
                    │                                   │
              ┌─────┴─────┐                      ┌─────┴─────┐
              │   PD #1   │                      │   PD #2   │
              │ (Tenant A)│                      │ (Tenant B)│
              └─────┬─────┘                      └─────┬─────┘
                    │                                   │
     ┌──────┬──────┼──────┬──────┐       ┌──────┬──────┼──────┐
     │      │      │      │      │       │      │      │      │
   QP_A1  QP_A2  MR_A1  MR_A2  AH_A1  QP_B1  MR_B1  MR_B2  AH_B1
```

In this example:
- QP_A1 can reference MR_A1 and MR_A2 (same PD).
- QP_A1 **cannot** reference MR_B1 or MR_B2 (different PD).
- QP_B1 can reference MR_B1 and MR_B2 (same PD).
- QP_B1 **cannot** reference MR_A1 or MR_A2 (different PD).

The PD check applies to both local and remote operations:

- **Local check**: When a QP processes a WQE, the NIC verifies that every lkey in the scatter-gather list belongs to an MR in the same PD as the QP.
- **Remote check**: When a remote RDMA Read or Write arrives, the NIC verifies that the rkey belongs to an MR in the same PD as the target QP. (Technically, for RC QPs, the rkey check uses the PD of the QP that is the target of the remote operation.)

## Multi-Tenant Implications

In cloud and multi-tenant environments, Protection Domains are a critical security boundary:

### Single Application, Single PD

The simplest and most common case. A single-process application creates one PD and places all its objects in it. This is appropriate for HPC applications, storage clients, and other single-purpose workloads.

```c
/* Typical single-PD application */
struct ibv_pd *pd = ibv_alloc_pd(ctx);
/* All QPs, MRs, AHs belong to this one PD */
```

### Multi-Tenant Service

A server application that serves multiple clients or tenants should use separate PDs for each tenant:

```c
/* Per-tenant isolation */
struct tenant {
    struct ibv_pd *pd;
    struct ibv_qp *qp;
    struct ibv_mr *mr;
    /* ... */
};

struct tenant *create_tenant(struct ibv_context *ctx) {
    struct tenant *t = calloc(1, sizeof(*t));

    /* Each tenant gets its own PD */
    t->pd = ibv_alloc_pd(ctx);

    /* QPs and MRs belong to the tenant's PD */
    t->mr = ibv_reg_mr(t->pd, buffer, size, flags);
    t->qp = ibv_create_qp(t->pd, &attr);

    return t;
}
```

This ensures that a buggy or malicious tenant cannot access another tenant's memory, even if it somehow obtains the other tenant's lkey -- the PD mismatch will cause a hardware-level rejection.

### RDMA-Based Storage Systems

Storage systems like NVMe-over-Fabrics (NVMeoF) often use separate PDs per namespace or per initiator connection.[^2] This prevents one storage client from accessing another client's data buffers, even though all clients share the same RDMA device and physical network.

<div class="admonition note">
<div class="admonition-title">Note</div>
While Protection Domains provide strong isolation within a single machine, they do not protect against attacks from remote machines on the RDMA fabric. A remote attacker who knows the rkey and virtual address of a registered MR can access it via RDMA Read/Write, regardless of the PD arrangement on the remote machine. Securing against remote attacks requires careful rkey management, and is covered in Chapter 16.
</div>

## PD and Memory Windows

For scenarios where an application needs finer-grained control over remote access authorization, **Memory Windows (MW)** provide a way to grant temporary, revocable access to a subset of a registered MR. Memory Windows are bound to both an MR and a QP, and they exist within a PD. When a Memory Window is invalidated (unbound), its rkey becomes immediately invalid, even though the underlying MR remains registered.

There are two types:

- **Type 1 MW**: Bound via a special verb call. Simpler but requires a control-path operation to bind/unbind.
- **Type 2 MW**: Bound via a work request on the QP (`IBV_WR_BIND_MW`). More flexible because binding can be pipelined with data operations on the data path.[^3]

Memory Windows add a layer of indirection between the rkey presented to remote peers and the underlying MR. By invalidating a Memory Window's binding, the application can instantly revoke remote access without the cost of deregistering and re-registering the underlying MR.

We discuss Memory Windows in detail in Chapter 6.

## PD Overhead and Limits

Protection Domains are lightweight objects. Creating a PD involves:

1. A system call to the kernel's ib_uverbs module.
2. Allocation of a small PD context structure in the NIC (typically a few bytes of NIC metadata).
3. Assignment of a PD number used internally for the hardware protection checks.

The creation cost is typically a few microseconds -- negligible compared to QP creation or MR registration. The device limit on PDs can be queried:

```c
struct ibv_device_attr attr;
ibv_query_device(ctx, &attr);
printf("Max PDs: %d\n", attr.max_pd);  /* Typical: unlimited or very large */
```

On most modern hardware, the PD limit is effectively unlimited (often reported as `INT_MAX`). PDs consume minimal resources, and there is no meaningful performance difference between using one PD and using thousands.

## PD in the Kernel

For kernel-space RDMA consumers (such as NVMe-over-Fabrics target implementations, SRP, and iSER), the same PD abstraction exists. The kernel's `ib_alloc_pd()` function serves the same role as the user-space `ibv_alloc_pd()`, and the same isolation guarantees apply. Kernel consumers and user-space consumers can coexist on the same device, each with their own PDs, and the hardware prevents any cross-PD access.

## Common Patterns and Best Practices

**One PD per logical security domain.** If your application has multiple logical tenants, clients, or trust boundaries, give each one its own PD. The overhead is negligible and the security benefit is significant.

**One PD for the common case.** For a single-purpose application that does not need internal isolation, using a single PD is simpler and sufficient. There is no performance benefit to splitting objects across multiple PDs in this case.

**PD as a resource management boundary.** When you destroy a PD, you must first destroy all objects within it. This provides a natural "cleanup" pattern: track objects by PD, and tear them down in reverse order.

**Do not share PD handles across processes.** While it is technically possible (via shared file descriptors and advanced rdma-core features) for multiple processes to share a PD, this is uncommon and requires careful synchronization. In the standard model, each process creates its own PDs.

## Summary

The Protection Domain is the isolation primitive of the RDMA programming model. It groups QPs, MRs, AHs, and SRQs into security domains and hardware-enforces that objects in different domains cannot interact. While simple to create and lightweight in resource consumption, the PD is foundational to RDMA security -- it prevents unauthorized memory access in multi-tenant environments and provides the basis for more advanced authorization mechanisms like Memory Windows. Every RDMA application creates at least one PD, and understanding its role is essential for building secure, correctly isolated RDMA systems.

[^1]: Protection Domain semantics and the PD-based access check are defined in the InfiniBand Architecture Specification, Volume 1, Section 10.2.3 (Protection Domain). The specification mandates that every QP, MR, MW, AH, and SRQ be associated with a PD, and that the HCA enforce PD matching on every operation.

[^2]: The Linux kernel's NVMe-over-Fabrics RDMA transport (`nvmet-rdma`) allocates a PD per controller instance. See the kernel source at `drivers/nvme/target/rdma.c`. The SPDK NVMe-oF target uses a similar per-controller PD model.

[^3]: Memory Window types are defined in the InfiniBand Architecture Specification, Volume 1, Section 10.6.7. Type 2 Memory Windows were introduced in later revisions of the specification to allow data-path binding via work requests, avoiding the control-path overhead of Type 1 bind operations. See `ibv_alloc_mw(3)` in the [rdma-core man pages](https://github.com/linux-rdma/rdma-core/blob/master/libibverbs/man/ibv_alloc_mw.3).
