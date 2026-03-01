# 14.3 Databases and Key-Value Stores

Databases and key-value stores represent one of the most intellectually rich application domains for RDMA. Unlike storage or HPC, where RDMA is primarily used as a faster transport for existing protocols, the database community has fundamentally rethought data structure design, concurrency control, and system architecture to exploit RDMA's unique capabilities. The result is a new generation of systems that achieve tens of millions of operations per second from a single server -- performance that would be unattainable with traditional TCP/IP networking.

The central design question in RDMA-based databases is the choice between **one-sided** and **two-sided** operations. One-sided RDMA Read and Write operations bypass the remote CPU entirely, allowing a client to access remote memory without interrupting the server. This enables extraordinary scalability since the server CPU is not involved in data transfer. However, one-sided operations require the client to understand the remote memory layout and cannot perform server-side computation. Two-sided Send/Receive operations involve the remote CPU but allow arbitrary processing. The most successful systems use a carefully chosen combination of both.

## HERD: RDMA-Based Key-Value Store

HERD, developed at Carnegie Mellon University, is a seminal RDMA-based key-value store that demonstrated how to achieve maximum throughput by combining one-sided and two-sided operations in a counterintuitive way.

### Architecture

HERD's key insight is that **RDMA Write is faster than RDMA Read** for small messages because RDMA Write is one-sided at the NIC level (the sender's NIC pushes data without waiting for a response), while RDMA Read requires a round trip (the initiator's NIC requests data, the target's NIC fetches it and sends a response). HERD exploits this asymmetry:

1. **Requests**: Clients send requests to the server using **RDMA Write** into a pre-allocated request region in the server's memory. Each client writes to a dedicated slot, avoiding contention.
2. **Server processing**: The server polls the request region, processes each request (lookup, insert, delete) using its local hash table, and prepares a response.
3. **Responses**: The server sends responses back using **RDMA Send** (two-sided), which the client receives with a pre-posted Receive.

```
Client                          Server
  │                               │
  │  RDMA Write (request)         │
  ├──────────────────────────────►│
  │                               │  Process request
  │                               │  (hash table lookup)
  │       RDMA Send (response)    │
  │◄──────────────────────────────┤
  │                               │
```

### Why Not Pure One-Sided?

A natural question is why HERD does not use purely one-sided operations, where clients read the hash table directly via RDMA Read. The answer reveals a fundamental tension:

- **One-sided reads require multiple round trips**: A hash table lookup may require reading the hash bucket, following a chain of pointers, and reading the final value. Each step requires a separate RDMA Read, multiplying latency.
- **Concurrency control is difficult**: Without server CPU involvement, clients cannot take locks or use compare-and-swap operations to handle concurrent updates safely.
- **Server-side processing is needed**: Real databases need to perform validation, access control, and logging that cannot be offloaded to the client.

HERD's hybrid approach achieves **26 million operations per second** on a single server using a single ConnectX-3 NIC -- roughly 10x the throughput of the best TCP-based key-value stores of its era.

<div class="note">

HERD's design principle -- use RDMA Write for the fast direction and RDMA Send for the direction that needs server processing -- has become a widely adopted pattern in RDMA system design. The key lesson is that the fastest RDMA operation should be used for the most frequent communication direction, even if it means using a two-sided operation for the other direction.

</div>

## FaRM: Fast Remote Memory

FaRM, developed at Microsoft Research, takes RDMA-based distributed systems to an entirely different level. It is a general-purpose distributed computing platform that uses RDMA for both data access and transaction coordination, achieving distributed transactions in under 60 microseconds.

### Architecture

FaRM stores all data in the main memory of a cluster of machines, using RDMA to access remote data. Its key innovations include:

**Optimistic concurrency control over RDMA**: FaRM implements a variant of optimistic concurrency control where:
1. **Read phase**: The transaction reads all needed objects using **one-sided RDMA Reads** directly from remote memory. Each object has a version number that is read atomically.
2. **Validation phase**: Before committing, the transaction re-reads the version numbers of all accessed objects (via RDMA Read) to verify they have not changed.
3. **Commit phase**: If validation succeeds, the transaction writes updates using RDMA Writes and logs them for durability.

**RDMA-based replication**: FaRM replicates data across multiple machines for fault tolerance. Log entries are written to backup replicas using RDMA Writes, with the backup's CPU only involved during recovery -- not on the critical path.

**Lock-free reads**: Since reads use one-sided RDMA, they do not block writers on the remote machine. Version numbers provide consistency checking without locks.

### Performance

FaRM achieves remarkable performance:
- **90 million operations per second** on a 90-machine cluster
- **Sub-60 microsecond** latency for distributed transactions involving reads and writes across multiple machines
- **Near-linear scalability** with the number of machines

```
FaRM Transaction Flow:

Client              Machine A           Machine B           Machine C
  │                    │                    │                    │
  │  RDMA Read (obj1)  │                    │                    │
  ├───────────────────►│                    │                    │
  │  RDMA Read (obj2)  │                    │                    │
  ├─────────────────────────────────────────►│                    │
  │                    │                    │                    │
  │  Lock (RDMA CAS)   │                    │                    │
  ├───────────────────►│                    │                    │
  │  Validate ver.     │                    │                    │
  ├─────────────────────────────────────────►│                    │
  │  Write (RDMA Write)│                    │                    │
  ├───────────────────►│                    │                    │
  │  Log (RDMA Write)  │                    │                    │
  ├──────────────────────────────────────────────────────────────►│
  │  Unlock            │                    │                    │
  ├───────────────────►│                    │                    │
```

## Pilaf: Self-Verifying RDMA Key-Value Store

Pilaf explores the opposite end of the design spectrum from HERD: it uses **purely one-sided RDMA Reads** for all GET operations, completely bypassing the server CPU for the read path.

### Cuckoo Hashing with RDMA Read

Pilaf uses **cuckoo hashing**, a hash table design where each key has exactly two possible locations. This is ideal for RDMA because:

1. A client can read both possible locations with a **single RDMA Read** (or at most two), compared to following a chain of pointers in a linked-list-based hash table.
2. The client verifies the data's integrity using checksums embedded in each entry, detecting concurrent modifications without server coordination.
3. PUT operations are handled by the server (two-sided), but GETs -- which are far more frequent in most workloads -- are entirely one-sided.

### Self-Verification

Since clients read memory without server involvement, they need a mechanism to detect torn reads (reading a partially updated entry). Pilaf includes a **version number and checksum** in each hash table entry. The client reads the entry, verifies the checksum, and retries if verification fails. This provides eventual consistency without server-side locking for reads.

<div class="warning">

Self-verification via checksums introduces a subtle race condition: a client may read a consistent but stale entry if the server updates the entry between the client's read and its verification. Pilaf handles this with version numbers, but applications that require strict linearizability cannot rely solely on client-side verification. Consider whether your consistency requirements allow this tradeoff.

</div>

## eRPC: Efficient RPC over RDMA

eRPC, developed at Carnegie Mellon University, takes a different approach: rather than designing custom data structures for RDMA, it provides a general-purpose **RPC framework** that achieves near-RDMA-verbs performance while offering the familiar RPC programming model.

### Design Principles

- **Zero-copy**: Request and response buffers are pre-registered with the NIC. Applications allocate buffers from eRPC's memory pool, and data flows directly between application buffers and the NIC without intermediate copies.
- **Congestion-aware**: Unlike raw RDMA verbs, eRPC implements end-to-end congestion control, making it safe to deploy in large-scale clusters where congestion can cause catastrophic performance degradation with uncongested RDMA.
- **Transport-agnostic**: eRPC works over both RDMA (InfiniBand, RoCE) and DPDK (raw Ethernet), using unreliable datagrams as the underlying transport in both cases.

### Performance

eRPC achieves median RPC latency of **2.3 microseconds** for small messages over RDMA, including dispatch overhead. For comparison, gRPC over TCP typically achieves latencies of 50-200 microseconds. eRPC handles **10 million small RPCs per second** per core.

```cpp
// eRPC server handler example
void read_handler(erpc::ReqHandle *req_handle, void *context) {
    auto *req = req_handle->get_req_msgbuf();
    auto &resp = req_handle->pre_resp_msgbuf_;

    // Process request, populate response
    Key *key = reinterpret_cast<Key *>(req->buf_);
    Value *val = lookup(key);
    memcpy(resp.buf_, val, sizeof(Value));

    rpc->resize_msg_buffer(&resp, sizeof(Value));
    rpc->enqueue_response(req_handle, &resp);
}
```

## Design Patterns for RDMA Databases

Several design patterns have emerged from the research on RDMA-based databases:

### One-Sided vs Two-Sided: Decision Framework

| Criterion                    | Favor One-Sided          | Favor Two-Sided          |
|------------------------------|--------------------------|--------------------------|
| Read-heavy workload          | Yes                      |                          |
| Simple data structure        | Yes                      |                          |
| Need server-side computation |                          | Yes                      |
| Strong consistency required  |                          | Yes                      |
| Server CPU is bottleneck     | Yes                      |                          |
| Network is bottleneck        |                          | Yes (fewer round trips)  |
| Complex multi-key operations |                          | Yes                      |

### RDMA-Optimized Data Structures

Traditional data structures are poorly suited for one-sided RDMA access because they involve pointer chasing, which translates to multiple sequential RDMA Reads. RDMA-optimized data structures minimize the number of remote memory accesses:

**Hash tables**: Cuckoo hashing (as in Pilaf) and hopscotch hashing provide O(1) lookups with a bounded number of RDMA Reads. Open addressing is generally preferred over chaining because it avoids pointer following.

**B-trees**: RDMA-friendly B-trees use large node sizes (matching or exceeding the RDMA Read granularity) and store leaf nodes contiguously. A lookup requires O(log_B(N)) RDMA Reads, where B is the branching factor. The **DrTM+B** system demonstrated that RDMA-based B-trees can achieve millions of range queries per second.

**Skip lists**: RDMA-friendly skip lists use a flattened representation where forward pointers at each level are stored contiguously, allowing a single RDMA Read to fetch all forward pointers for a node. This avoids the multiple pointer dereferences needed in a traditional skip list traversal.

**Sorted arrays with interpolation search**: For read-mostly workloads with numeric keys, a sorted array with interpolation search achieves O(1) expected RDMA Reads -- a single read fetches the expected location, and a second read fetches the actual value if the first guess was wrong.

<div class="tip">

When designing RDMA-optimized data structures, the key metric is the **number of sequential RDMA Read round trips**, not the total number of memory accesses. Techniques that increase local computation or memory consumption but reduce the number of RDMA round trips almost always improve performance. For example, a B-tree with a branching factor of 1000 (requiring large reads per node) will outperform a binary search tree (requiring small reads per node) because it requires far fewer round trips.

</div>

## The Broader Landscape

Beyond the systems described above, RDMA has been adopted across the database ecosystem:

- **DrTM**: Distributed transaction manager combining RDMA and hardware transactional memory (HTM) for single-node atomicity with RDMA for inter-node communication.
- **Xstore**: Persistent key-value store that extends RDMA data access to storage-class memory (SCM).
- **Sherman**: Distributed B-tree that uses one-sided RDMA operations with a hierarchical locking protocol.
- **RACE**: A hash table design that uses one-sided RDMA operations for both read and write paths, achieving lock-free concurrent access.

The rapid evolution of RDMA-based databases reflects a broader trend: as network performance approaches memory bus performance, the traditional assumption that remote access is orders of magnitude slower than local access no longer holds. This breaks fundamental assumptions in distributed systems design and opens the door to architectures that were previously impractical -- such as distributed shared memory, remote memory pooling, and globally consistent in-memory databases.
