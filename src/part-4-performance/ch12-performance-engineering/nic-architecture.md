# 12.5 NIC Architecture Internals

To truly understand RDMA performance, engineers must look inside the NIC itself. The network interface card is not a simple packet transceiver -- it is a sophisticated special-purpose processor with its own memory hierarchy, caches, and execution pipelines. This section examines the internal architecture of modern RDMA NICs, using the NVIDIA ConnectX series as the reference platform, and explains how NIC-internal design decisions affect application performance.

## User Access Region (UAR)

The User Access Region is the foundation of kernel-bypass RDMA. A UAR is a page of NIC memory mapped into the application's virtual address space through the PCIe BAR (Base Address Register). Each UAR page contains doorbell registers that the application writes to notify the NIC of new work.

### UAR Structure

A typical UAR page (4 KB) contains several registers:

| Offset | Register | Purpose |
|--------|----------|---------|
| 0x000 | Send doorbell | Notify NIC of new Send WQEs |
| 0x008 | Receive doorbell | Notify NIC of new Receive WQEs |
| 0x010 | CQ doorbell | Update CQ consumer index |
| 0x800 | BlueFlame register 0 | WC-mapped WQE posting area |
| 0xC00 | BlueFlame register 1 | Alternate BF register (for pipelining) |

The NIC provides multiple UAR pages. In the ConnectX series, there are typically 512--4096 UAR pages available. Each process that opens an RDMA device context (`ibv_open_device`) is allocated one or more UAR pages.

```c
// When you call ibv_open_device, the library:
// 1. Opens /dev/infiniband/uverbs0
// 2. Kernel allocates UAR pages and maps them into process
// 3. Library stores UAR pointers for later doorbell writes
struct ibv_context *ctx = ibv_open_device(dev);
// ctx now has UAR mappings for doorbell + BlueFlame
```

### UAR Contention

When multiple QPs share the same UAR page (same doorbell register), concurrent posts from multiple threads require serialization (locks). To avoid contention:

- **mlx5 provider**: Allocates dedicated UARs per QP or per thread
- **Thread-domain** (`ibv_alloc_td`): Explicitly partitions UAR resources among threads

```c
// Create a thread domain for lock-free operation
struct ibv_td_init_attr td_attr = {};
struct ibv_td *td = ibv_alloc_td(ctx, &td_attr);

// Create parent domain with thread domain
struct ibv_parent_domain_init_attr pd_attr = {
    .td = td,
    .pd = pd,
};
struct ibv_pd *parent_pd = ibv_alloc_parent_domain(ctx, &pd_attr);

// QPs created with parent_pd get dedicated UARs
```

## BlueFlame Deep Dive

BlueFlame is the most important single optimization in the ConnectX architecture for latency-sensitive workloads. Understanding its internal mechanics is essential.

### Write-Combining Mechanics

The BlueFlame register is mapped with write-combining (WC) memory type. When the application writes a 64-byte WQE to the BF register:

1. **CPU writes** 8 x 8-byte stores to the BF address range
2. **Write-combining buffer**: The CPU's WC buffer (typically 64 bytes per entry, 4--8 entries on modern CPUs) accumulates the writes
3. **Buffer flush**: When the WC buffer is full or explicitly flushed (`sfence` or `mfence`), the CPU issues a single 64-byte PCIe write TLP
4. **NIC receives**: The NIC detects the write to the BF register, extracts the embedded WQE, and processes it without any DMA read

```
Timeline comparison (approximate):

Standard doorbell:
  t=0      CPU writes doorbell (4B MMIO)
  t=100ns  NIC receives doorbell
  t=150ns  NIC issues DMA read for WQE
  t=350ns  NIC receives WQE (PCIe round-trip)
  t=400ns  NIC begins processing

BlueFlame:
  t=0      CPU writes 64B to BF register (WC MMIO)
  t=120ns  NIC receives doorbell + WQE together
  t=130ns  NIC begins processing
  Savings: ~270 ns
```

### BlueFlame Pipeline

ConnectX NICs provide two alternating BlueFlame registers (BF0 and BF1). The library alternates between them to allow the CPU's write-combining buffer to flush one while writing to the other:

```
Post 1: Write to BF0, flush
Post 2: Write to BF1, flush (overlaps with BF0 delivery)
Post 3: Write to BF0, flush (BF0 is now available)
...
```

This pipelining ensures back-to-back posts do not stall waiting for WC buffer flushes.

## CQE Compression

At high message rates, the NIC generates millions of CQEs per second, each requiring a DMA write to host memory. This CQE traffic can consume significant PCIe bandwidth and pollute CPU caches.

**CQE compression** reduces this overhead by encoding sequences of similar CQEs as a single compressed entry:

| Mode | CQE Size | Description |
|------|----------|-------------|
| Standard | 64 bytes | Full CQE per completion |
| Compressed | 8 bytes (mini CQE) | Compressed, expanded by driver |
| Stride index | 8 bytes | For striding RQ, encodes buffer index |

When compression is enabled, the NIC detects sequences of CQEs with identical fields (same status, same QP, sequential WR IDs) and replaces them with a single compressed block. The mlx5 driver transparently decompresses these when the application calls `ibv_poll_cq`.

Benefits:
- **PCIe bandwidth**: 8x reduction in CQE DMA traffic
- **Cache pollution**: Fewer cache lines consumed by CQE writes
- **Message rate**: Higher achievable message rate at the PCIe boundary

```bash
# Enable CQE compression (mlx5 module parameter)
modprobe mlx5_core cqe_comp_enabled=1

# Or via devlink (persistent)
devlink dev param set pci/0000:86:00.0 name cqe_comp_enabled \
    value true cmode runtime
```

## Multi-Packet Receive Queue (MPRQ)

Traditional receive queues allocate one receive buffer per incoming message. For small messages, this wastes memory and increases the rate of receive WQE posting. **Multi-Packet Receive Queue** (also called **Striding RQ**) packs multiple incoming messages into a single large receive buffer.

### How MPRQ Works

1. The application posts a single large receive buffer (e.g., 1 MB)
2. The buffer is logically divided into **strides** of fixed size (e.g., 256 bytes each)
3. The NIC places each incoming message in the next available stride
4. A single completion reports the stride index where the message was placed
5. When all strides are consumed, the entire buffer is released

```
Traditional RQ:                    MPRQ (Striding RQ):
+-------+ +-------+ +-------+     +---+---+---+---+---+---+---+---+
| Buf 0 | | Buf 1 | | Buf 2 |     |St0|St1|St2|St3|St4|St5|St6|St7|
| 4KB   | | 4KB   | | 4KB   |     |   256B strides in 1 buffer    |
+-------+ +-------+ +-------+     +---+---+---+---+---+---+---+---+
  3 WQEs needed                      1 WQE needed for 8 messages
```

Benefits of MPRQ:
- **Reduced WQE posting rate**: One WQE serves multiple messages
- **Better memory utilization**: No wasted space for small messages
- **Lower CPU overhead**: Fewer buffer allocations and WQE constructions

<div class="note">

**MPRQ availability**: MPRQ is primarily used internally by the mlx5 driver for high-performance networking (DPDK, raw Ethernet). For IB verbs RDMA, striding RQ is available through the mlx5dv (device-specific verbs) interface.

</div>

## Device-Managed Flow Steering

ConnectX NICs support hardware-accelerated flow steering, which directs incoming packets to specific QPs or receive queues based on packet header fields. This is critical for multi-tenant and multi-service deployments where different flows must be isolated.

Flow steering rules can match on:
- Source/destination MAC address
- VLAN ID
- Source/destination IP address
- UDP/TCP port numbers
- IB transport headers (DGID, QPN)

```c
// Create a flow steering rule (simplified)
struct ibv_flow_attr flow_attr = {
    .comp_mask = 0,
    .type = IBV_FLOW_ATTR_NORMAL,
    .size = sizeof(flow_attr) + sizeof(struct ibv_flow_spec_eth),
    .priority = 0,
    .num_of_specs = 1,
    .port = 1,
    .flags = 0,
};
struct ibv_flow *flow = ibv_create_flow(qp, &flow_attr);
```

## NIC Caches

Modern RDMA NICs maintain several internal caches. Cache behavior has a profound effect on performance, especially for workloads with many QPs or memory regions.

### QP Context Cache

The NIC stores the state of each QP (sequence numbers, buffer pointers, connection parameters) in **QP context** structures. These contexts reside in host memory but are cached in the NIC's internal SRAM.

| NIC Model | QP Cache Size | Cache Entries |
|-----------|--------------|---------------|
| ConnectX-5 | ~1 MB | ~4K QP contexts |
| ConnectX-6 | ~2 MB | ~8K QP contexts |
| ConnectX-7 | ~4 MB | ~16K QP contexts |

When an operation targets a QP whose context is **cached** (cache hit), the NIC processes it immediately. A **cache miss** requires fetching the context from host memory via DMA, adding 500--1000 ns to the operation.

### MR Translation Cache

The Memory Registration Table (MRT or MTT) maps virtual addresses to physical addresses for DMA. The NIC caches recent translations in an internal **MR translation cache**:

- **Cache hit**: Address translation is instant (~10 ns)
- **Cache miss**: NIC must fetch MTT entry from host memory (~300--500 ns)

Cache miss rates increase with:
- Large number of memory regions
- Large registered memory ranges (more MTT entries)
- Scattered access patterns

### Cache Thrashing

When the number of active QPs exceeds the cache size, **cache thrashing** occurs: QP contexts are constantly evicted and re-fetched. This manifests as:

- Increased latency (especially p99 and p99.9)
- Reduced message rate (NIC spends time on DMA fetches instead of packet processing)
- Higher PCIe utilization (cache miss DMA traffic)

```
Performance vs. Active QP Count:

Message Rate
    ^
    |  ___________
    | /           \___________
    |/                        \______________
    +-----------------------------------------> Active QPs
    0    4K     8K      16K      32K

    |--Cache--|--Thrashing--|--Severe thrashing--|
```

<div class="warning">

**Production guidance**: Keep the number of **simultaneously active** QPs (those with in-flight operations) within the NIC's cache capacity. It is fine to have thousands of **idle** QPs -- only active ones occupy cache entries. If your application needs many active QPs, consider connection sharing (multiple flows per QP) or Shared Receive Queues (SRQ) to reduce the active QP footprint.

</div>

## Clock Synchronization and Timestamps

ConnectX NICs include high-resolution hardware clocks that can timestamp events with sub-nanosecond precision. These timestamps are essential for:

- **Latency measurement**: Precise end-to-end latency without software timer overhead
- **Clock synchronization**: PTP (Precision Time Protocol) offloading
- **Debugging**: Correlating events across sender and receiver

```c
// Query NIC clock
struct ibv_values_ex values;
struct ibv_query_values_ex_input input = {
    .comp_mask = IBV_VALUES_MASK_RAW_CLOCK,
};
ibv_query_rt_values_ex(ctx, &input, &values);

uint64_t nic_timestamp = values.raw_clock.tv_nsec;
```

### Hardware Timestamping in Completions

CQEs can include hardware timestamps indicating when the NIC transmitted or received the packet:

```c
// Create CQ with timestamp support
struct ibv_cq_init_attr_ex cq_attr = {
    .cqe = 1024,
    .wc_flags = IBV_WC_EX_WITH_COMPLETION_TIMESTAMP,
};
struct ibv_cq_ex *cq_ex = ibv_create_cq_ex(ctx, &cq_attr);

// Poll and extract timestamp
int ret = ibv_start_poll(cq_ex, NULL);
if (ret == 0) {
    uint64_t ts = ibv_wc_read_completion_ts(cq_ex);
    // ts is in NIC clock ticks; convert using ibv_query_rt_values_ex
    ibv_end_poll(cq_ex);
}
```

## Advanced NIC Features

### Adaptive Routing

ConnectX-7 and later support **adaptive routing**, where the NIC can dynamically select the output port for multi-path fabrics. This avoids congested paths without requiring switch-level ECMP.

### In-Network Computing

ConnectX-7 introduces **SHARP (Scalable Hierarchical Aggregation and Reduction Protocol)**, which offloads collective operations (AllReduce, Barrier) to the network. The NIC and switches cooperate to perform aggregation in the network fabric, reducing data movement for distributed ML training.

### NIC-Level Encryption

Hardware-accelerated encryption (AES-GCM) for RDMA traffic, providing line-rate encrypted communication without CPU overhead.

### Device Emulation

ConnectX-7 supports **virtio emulation**, presenting a virtio-net device to VMs while internally using RDMA for backend communication. This enables RDMA-accelerated virtualized networking without guest modification.

## Summary

Understanding NIC internals allows engineers to make informed decisions about resource allocation, QP management, and performance tuning. The key takeaways are:

1. **UARs** are the kernel-bypass mechanism; contention on shared UARs limits scaling
2. **BlueFlame** eliminates a PCIe round-trip for small WQEs; always enable it for latency-sensitive paths
3. **CQE compression** reduces PCIe bandwidth for completions by up to 8x
4. **NIC caches** (QP context, MR translation) are finite; exceeding cache capacity causes severe performance degradation
5. **Hardware timestamps** provide precise latency measurement without software overhead
