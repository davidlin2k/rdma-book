# 12.2 Throughput Optimization

While latency measures how quickly a single operation completes, throughput measures how much work the system can sustain per unit time. RDMA throughput has two distinct dimensions: **bandwidth** (bytes per second) and **message rate** (messages per second). Optimizing for one does not necessarily optimize for the other, and understanding which metric is the bottleneck for a given workload is the first step in throughput engineering.

This section presents the techniques that allow applications to approach the theoretical limits of modern RDMA hardware.

## Bandwidth vs. Message Rate

Modern RDMA NICs advertise both bandwidth and message rate capabilities. A ConnectX-7 at 400 Gbps, for example, can deliver:

- **Bandwidth**: up to 400 Gbps (50 GB/s) of payload throughput
- **Message rate**: up to 200+ million messages per second (Mpps)

These two metrics represent different bottlenecks that dominate at different message sizes.

**Small messages** are limited by message rate. Each message, regardless of size, requires the NIC to process a WQE, construct a packet, and generate a completion. The per-message overhead is roughly constant, so the NIC can process a fixed number of messages per second. At 200 Mpps with 8-byte payloads, the achieved bandwidth is only:

$$BW_{small} = 200 \times 10^6 \times 8 = 1.6 \text{ GB/s}$$

This is a tiny fraction of the 50 GB/s link capacity.

**Large messages** are limited by link bandwidth. A 1 MB RDMA Write fully utilizes the link's serialization capacity, and the per-message overhead is amortized over the large payload. The message rate for large transfers is:

$$MR_{large} = \frac{BW_{link}}{msg\_size} = \frac{50 \text{ GB/s}}{1 \text{ MB}} = 50{,}000 \text{ msg/s}$$

The **crossover point** -- the message size where the bottleneck transitions from message rate to bandwidth -- can be calculated:

$$msg_{crossover} = \frac{BW_{link}}{MR_{max}} = \frac{50 \text{ GB/s}}{200 \text{ Mpps}} = 250 \text{ bytes}$$

Below 250 bytes, optimizing message rate is critical. Above 250 bytes, optimizing for bandwidth dominates.

## Effective Bandwidth

The effective bandwidth delivered to the application is always less than the raw link rate due to protocol and framing overhead:

$$BW_{eff} = \frac{payload}{payload + overhead} \times BW_{link}$$

The overhead includes:

| Component | Size (bytes) | Notes |
|-----------|-------------|-------|
| Ethernet preamble + SFD | 8 | Physical layer |
| Ethernet header | 14 | Destination + source MAC + EtherType |
| IP header | 20 | IPv4 (RoCEv2) |
| UDP header | 8 | RoCEv2 encapsulation |
| BTH (Base Transport Header) | 12 | InfiniBand transport |
| RETH (RDMA Extended Header) | 16 | For RDMA Write/Read |
| ICRC | 4 | Invariant CRC |
| FCS | 4 | Ethernet Frame Check Sequence |
| Inter-frame gap | 12 | Physical layer |
| **Total overhead** | **~98** | Per packet |

For a 4 KB payload (a common page-sized RDMA Write):

$$BW_{eff} = \frac{4096}{4096 + 98} \times 400 \text{ Gbps} = 390.6 \text{ Gbps} \approx 97.7\%$$

For a 64-byte payload:

$$BW_{eff} = \frac{64}{64 + 98} \times 400 \text{ Gbps} = 158 \text{ Gbps} \approx 39.5\%$$

This demonstrates why small-message bandwidth efficiency is inherently limited by protocol overhead.

## Message Rate Ceiling

The theoretical maximum message rate is bounded by the lesser of the NIC's processing capacity and the link bandwidth:

$$MR_{max} = \min\left(MR_{nic}, \frac{BW_{link}}{msg\_size + header\_overhead}\right)$$

Where $MR_{nic}$ is the NIC's maximum message processing rate (determined by the NIC's internal pipeline width and clock speed). For modern ConnectX NICs, $MR_{nic} \approx 150\text{--}200$ Mpps.

## Batching Work Requests

One of the most impactful throughput optimizations is **batching**: posting multiple WQEs before ringing the doorbell. Each doorbell write is an expensive MMIO operation (~100--200 ns). By amortizing one doorbell across N work requests, the per-message doorbell cost drops to $t_{doorbell} / N$.

The libibverbs API supports batching through the linked-list interface of `ibv_post_send`:

```c
#define BATCH_SIZE 32

struct ibv_send_wr wr[BATCH_SIZE];
struct ibv_sge sge[BATCH_SIZE];
struct ibv_send_wr *bad_wr;

// Build a chain of work requests
for (int i = 0; i < BATCH_SIZE; i++) {
    sge[i].addr   = (uintptr_t)buffers[i];
    sge[i].length = msg_size;
    sge[i].lkey   = mr->lkey;

    wr[i].wr_id      = i;
    wr[i].sg_list    = &sge[i];
    wr[i].num_sge    = 1;
    wr[i].opcode     = IBV_WR_RDMA_WRITE;
    wr[i].send_flags = 0;  // Unsignaled
    wr[i].next       = (i < BATCH_SIZE - 1) ? &wr[i + 1] : NULL;

    wr[i].wr.rdma.remote_addr = remote_addrs[i];
    wr[i].wr.rdma.rkey        = rkey;
}
// Signal only the last WR in the batch
wr[BATCH_SIZE - 1].send_flags = IBV_SEND_SIGNALED;

// Single doorbell for entire batch
ibv_post_send(qp, &wr[0], &bad_wr);
```

<div class="tip">

**Batch size trade-off**: Larger batches amortize doorbell cost but increase latency for the first messages in the batch (they wait for the batch to fill). A typical sweet spot is 16--32 WQEs per batch. For latency-sensitive workloads, use smaller batches or single-post with BlueFlame.

</div>

## Signaled Completion Interval

Every signaled work request generates a CQE, which the NIC must DMA-write to host memory and which the application must poll and process. By reducing the frequency of signaled completions, both NIC and CPU overhead decrease.

The technique is straightforward: set `IBV_SEND_SIGNALED` only on every Nth work request:

```c
#define SIGNAL_INTERVAL 32

for (int i = 0; i < total_ops; i++) {
    wr.send_flags = ((i % SIGNAL_INTERVAL) == (SIGNAL_INTERVAL - 1))
                    ? IBV_SEND_SIGNALED : 0;
    ibv_post_send(qp, &wr, &bad_wr);
}
```

When the signaled completion arrives, the application knows that all preceding unsignaled operations have also completed (completions are ordered within a QP).

<div class="warning">

**Send queue overflow**: The send queue has a finite depth. If you never signal completions, completed WQEs are never retired, and the send queue will eventually fill. You **must** signal at least once before the send queue fills. The safe rule is: signal at least once per send queue depth. A common pattern is to signal every `sq_depth / 2` operations.

</div>

Impact of signaling interval on message rate:

| Signal Interval | Relative Message Rate | CQE Overhead |
|----------------|----------------------|--------------|
| 1 (every WR) | 1.0x (baseline) | 100% |
| 8 | ~1.3x | 12.5% |
| 32 | ~1.5x | 3.1% |
| 64 | ~1.6x | 1.6% |

## Multi-QP Throughput Scaling

A single Queue Pair may not saturate the NIC's full message rate or bandwidth capacity, particularly for small messages. The NIC's internal processing pipeline has limited parallelism per QP, and a single QP serializes all operations through one WQE pipeline.

Spreading work across multiple QPs enables the NIC to process messages in parallel:

```c
#define NUM_QPS 4

struct ibv_qp *qps[NUM_QPS];
// Create and connect NUM_QPS queue pairs...

// Round-robin across QPs
for (int i = 0; i < total_ops; i++) {
    int qp_idx = i % NUM_QPS;
    ibv_post_send(qps[qp_idx], &wr[i], &bad_wr);
}
```

Typical scaling behavior:

| QP Count | Relative Throughput (small msgs) | Notes |
|----------|----------------------------------|-------|
| 1 | 1.0x | Single pipeline |
| 2 | ~1.8x | Near-linear scaling |
| 4 | ~3.2x | Good scaling |
| 8 | ~5x | Diminishing returns begin |
| 16+ | ~6--7x | NIC saturation |

The optimal QP count depends on the NIC model and message size. For large messages (> 4 KB), a single QP can often saturate the link. For small messages (< 256 bytes), 4--8 QPs are typically needed.

<div class="note">

**Multi-QP ordering**: Operations across different QPs have no ordering guarantees. If your application requires ordering, you must implement it in software (e.g., using sequence numbers or barriers between QPs).

</div>

## Send Queue Depth: Keeping the Pipeline Full

The send queue acts as a pipeline between the CPU and the NIC. If the send queue runs empty, the NIC stalls waiting for new work. The send queue depth must be large enough to keep the NIC busy while the CPU prepares the next batch of WQEs.

The minimum send queue depth to sustain maximum throughput can be estimated:

$$SQ_{depth} \geq MR_{target} \times t_{repost}$$

Where $t_{repost}$ is the time between polling a completion and posting a new WQE. If the application achieves 100 Mpps and takes 100 ns to repost, the send queue needs at least:

$$SQ_{depth} \geq 100 \times 10^6 \times 100 \times 10^{-9} = 10 \text{ entries}$$

In practice, use at least 128--512 entries to accommodate bursts and jitter. Most applications use 256 or 512.

```c
struct ibv_qp_init_attr qp_attr = {
    .cap = {
        .max_send_wr  = 512,   // Deep send queue
        .max_recv_wr  = 512,
        .max_send_sge = 1,
        .max_recv_sge = 1,
        .max_inline_data = 64,
    },
    // ...
};
```

## Unsignaled Sends

As discussed above, marking work requests as unsignaled eliminates CQE generation overhead. But unsignaled sends also have a subtler benefit: the NIC can **coalesce** multiple unsignaled operations into more efficient internal processing batches.

For maximum throughput with unsignaled sends:

1. Post a batch of unsignaled WREs
2. Signal only the last one
3. When the signaled completion arrives, repost the entire batch
4. Repeat

This creates a natural double-buffering pattern where one batch is in-flight while the next is being prepared.

## Scatter-Gather Optimization

Each scatter-gather entry (SGE) in a WQE adds processing cost. The NIC must fetch each SGE's address, perform memory translation, and issue a separate DMA read. Minimizing the SGE count per WQE improves throughput:

```c
// Suboptimal: 4 SGEs for a 4 KB transfer
sge[0] = { .addr = buf + 0,    .length = 1024, .lkey = mr->lkey };
sge[1] = { .addr = buf + 1024, .length = 1024, .lkey = mr->lkey };
sge[2] = { .addr = buf + 2048, .length = 1024, .lkey = mr->lkey };
sge[3] = { .addr = buf + 3072, .length = 1024, .lkey = mr->lkey };

// Optimal: 1 SGE for contiguous 4 KB transfer
sge[0] = { .addr = buf, .length = 4096, .lkey = mr->lkey };
```

When multiple SGEs are unavoidable (e.g., gathering data from non-contiguous buffers), consider copying data into a contiguous buffer first if the copy cost is less than the multi-SGE overhead.

## Throughput Optimization Checklist

| Technique | Impact | Best For |
|-----------|--------|----------|
| WR batching (N per doorbell) | 30--50% improvement | All workloads |
| Signaled interval (every 32) | 20--50% improvement | High message rate |
| Multi-QP (4--8 QPs) | 3--5x improvement | Small messages |
| Deep send queue (256--512) | Prevents stalls | Sustained throughput |
| Unsignaled sends | 20--30% improvement | Bulk transfers |
| Minimize SGE count | 10--20% improvement | Multi-buffer operations |
| Inline data (small msgs) | 15--25% improvement | Messages < 256 bytes |

## Putting It Together: Maximum Throughput Recipe

For maximum small-message throughput:

1. Create 4--8 QPs to the same destination
2. Use send queue depth of 256--512 per QP
3. Batch 16--32 WRs per `ibv_post_send` call
4. Signal every 32 completions
5. Use inline data for messages under 64 bytes
6. Bind to the NIC's NUMA node
7. Dedicate a CPU core to polling

For maximum bandwidth with large messages:

1. A single QP usually suffices for 100 Gbps; use 2--4 QPs for 200--400 Gbps
2. Use large messages (64 KB -- 1 MB) to amortize per-message overhead
3. Keep the send queue depth at 128+ to maintain pipeline utilization
4. Signal every 16--32 completions
5. Use a single SGE with contiguous buffers
6. Ensure PCIe bandwidth is not the bottleneck (see Section 12.3)
