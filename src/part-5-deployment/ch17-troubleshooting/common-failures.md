# 17.3 Common Failure Modes

RDMA failures manifest as Work Completion (WC) error status codes. Each status code indicates a specific class of failure, but the root cause can vary widely. This section catalogs the most common failures, their possible causes, and systematic approaches to diagnosis and resolution.

## Transport Retry Counter Exceeded (IBV_WC_RETRY_EXC_ERR)

This is the most common and most frustrating RDMA error. It means the NIC attempted to send a packet, did not receive an acknowledgment, retried the configured number of times (`retry_cnt`), and gave up.

### What Happens Internally

When an RC QP sends a packet, it starts a timer. If no ACK arrives before the timer expires, the NIC retransmits the packet. This repeats up to `retry_cnt` times (configured during QP transition to RTS). If all retries are exhausted, the QP transitions to the Error state and reports `IBV_WC_RETRY_EXC_ERR`.

```text
Sender                         Receiver
  │── Packet ────────────────X     (lost or receiver unreachable)
  │   (timeout, retry 1)
  │── Packet ────────────────X
  │   (timeout, retry 2)
  │── Packet ────────────────X
  │   ...
  │── Packet ────────────────X
  │   (retry_cnt exhausted)
  │
  │   QP → Error state
  │   CQE: IBV_WC_RETRY_EXC_ERR
```

### Common Causes

**1. Network unreachable:** The most basic cause. The remote host is down, the cable is disconnected, or a switch in the path has failed.

```bash
# Diagnosis: check link status
ibstat mlx5_0 | grep State
# Check: is remote host reachable via management network?
ping <remote_host_mgmt_ip>
```

**2. MTU mismatch:** If the RDMA path MTU is configured larger than the network supports, packets are silently dropped. This is extremely common in RoCE deployments where one switch in the path has a 1500-byte MTU while others have 9000.

```bash
# Diagnosis: check MTU at every hop
# Local interface:
ip link show enp1s0f0 | grep mtu
# Every switch in the path must support the MTU
# RDMA MTU setting:
ibv_devinfo | grep active_mtu
```

<div class="warning">

**Warning:** An MTU mismatch is the single most common cause of "transport retry exceeded" errors in RoCE deployments. A single switch port with an incorrect MTU will silently drop oversized packets. Always verify MTU end-to-end, including every switch in the path, before investigating other causes.

</div>

**3. PFC misconfiguration:** For RoCE, Priority Flow Control (PFC) must be configured consistently across the entire path. If PFC is not enabled, or if the priority mapping is inconsistent, packets can be dropped during congestion, leading to retries.

```bash
# Check PFC configuration
ethtool -S enp1s0f0 | grep pfc
mlnx_qos -i enp1s0f0  # Mellanox-specific PFC tool
```

**4. Incorrect GID index:** In RoCE, the GID index determines the source address and VLAN used for RDMA traffic. An incorrect GID index can cause packets to be routed to the wrong VLAN or interface.

```bash
# List available GIDs
rdma link show mlx5_0/1
for i in $(seq 0 15); do
    cat /sys/class/infiniband/mlx5_0/ports/1/gids/$i 2>/dev/null
done
```

**5. Firewall or ACL blocking:** On RoCE networks, firewalls or switch ACLs may block UDP port 4791 (the RoCE v2 port), preventing RDMA packets from reaching the destination.

### Resolution Checklist

1. Verify physical link is up on both ends.
2. Verify MTU is consistent end-to-end (interface, switches, RDMA).
3. Verify PFC is enabled and consistent (RoCE).
4. Test with perftest tools to isolate the problem from the application.
5. Check error counters for drops or CRC errors.
6. Increase `retry_cnt` to 7 (maximum) as a temporary workaround while investigating.

## RNR Retry Counter Exceeded (IBV_WC_RNR_RETRY_EXC_ERR)

This error indicates that the receiver did not have posted receive buffers when a Send operation arrived. The sender received an RNR NAK (Receiver Not Ready), retried after the specified backoff, and exhausted its `rnr_retry` limit.

### Root Cause

The application is not posting receive buffers fast enough to keep up with incoming messages. This is an application-level flow control issue, not a network issue.

```text
Sender                         Receiver
  │── Send ────────────────────►│  (no receive buffer posted)
  │◄── RNR NAK ────────────────│
  │   (wait rnr_timer)
  │── Send (retry) ───────────►│  (still no buffer)
  │◄── RNR NAK ────────────────│
  │   ...
  │   (rnr_retry exhausted)
  │   CQE: IBV_WC_RNR_RETRY_EXC_ERR
```

### Diagnosis

```bash
# Check hw_counters for RNR events
cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rnr_nak_retry_err

# Check rx_out_of_buffer
cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rx_out_of_buffer
```

### Resolution

1. **Post more receive buffers:** The receiver must maintain a sufficient pool of posted receive buffers. A common pattern is to re-post a receive buffer immediately after consuming a completion.

2. **Use Shared Receive Queue (SRQ):** An SRQ pools receive buffers across multiple QPs, reducing the total number needed and smoothing out demand variations.

3. **Increase `rnr_retry`:** Set `rnr_retry` to 7 (infinite retries) during QP setup to prevent the QP from entering the error state due to transient buffer shortages:

```c
struct ibv_qp_attr attr = {
    .rnr_retry = 7,  // 7 = infinite retries
    // ...
};
```

4. **Set `min_rnr_timer`:** The receiver's `min_rnr_timer` controls how long the sender waits before retrying after an RNR NAK. A longer timer gives the receiver more time to post buffers.

<div class="tip">

**Tip:** Setting `rnr_retry = 7` (infinite) is common practice and prevents QP destruction due to temporary buffer shortages. However, if the receiver is genuinely unable to keep up, this converts a hard failure into a performance degradation -- the sender stalls waiting for the receiver. Monitor `rnr_nak_retry_err` counters to detect this condition.

</div>

## Remote Access Error (IBV_WC_REM_ACCESS_ERR)

This error occurs when an RDMA Read, Write, or Atomic operation fails due to an access control violation on the remote side.

### Common Causes

**1. Wrong R_Key:** The most common cause. The R_Key provided in the RDMA operation does not match any valid memory region on the remote host.

```c
// Typical bug: using stale R_Key after MR was deregistered and re-registered
struct ibv_send_wr wr = {
    .opcode = IBV_WR_RDMA_WRITE,
    .wr.rdma = {
        .remote_addr = remote_addr,
        .rkey = stale_rkey,  // This MR was deregistered!
    },
};
```

**2. Address out of range:** The remote address plus length exceeds the bounds of the registered memory region.

**3. Access flags mismatch:** The operation requires permissions that the MR was not registered with. For example, attempting an RDMA Write to a region registered with only `IBV_ACCESS_REMOTE_READ`.

**4. Byte ordering error:** R_Keys and remote addresses must be exchanged in a consistent byte order. If one side uses big-endian and the other little-endian, the values will be corrupted.

### Diagnosis

```bash
# On the remote side, check for access violation logs
dmesg | grep -i "access\|violation\|rdma"
```

Verify at the application level:
- Print and compare the R_Key values on both sides.
- Print and compare the remote address and length.
- Verify the MR access flags include the required permissions.
- Verify byte ordering is consistent.

## Local Protection Error (IBV_WC_LOC_PROT_ERR)

This error occurs on the local side when the NIC cannot validate the L_Key for a scatter/gather entry.

### Common Causes

1. **Invalid L_Key:** The L_Key does not correspond to any registered memory region.
2. **Buffer outside MR:** The address range specified in the SGE is not within the registered MR.
3. **PD mismatch:** The MR is in a different PD than the QP.
4. **MR deregistered:** The MR was deregistered while the work request was pending.

```c
// Common bug: MR and QP in different PDs
struct ibv_pd *pd1 = ibv_alloc_pd(ctx);
struct ibv_pd *pd2 = ibv_alloc_pd(ctx);

struct ibv_mr *mr = ibv_reg_mr(pd1, buf, size, access);
struct ibv_qp *qp = ibv_create_qp(pd2, &init_attr);  // Different PD!

// Using mr->lkey with this qp will cause LOC_PROT_ERR
```

## Memory Registration Failures

### ENOMEM: Insufficient Locked Memory

The most common memory registration failure. `ibv_reg_mr()` returns NULL with `errno = ENOMEM`.

```bash
# Check current memlock limit
ulimit -l
# Output: 65536  (64 KB - far too low for RDMA)

# Fix: increase memlock limit
# In /etc/security/limits.conf:
# * soft memlock unlimited
# * hard memlock unlimited

# Or for systemd services:
# LimitMEMLOCK=infinity
```

<div class="warning">

**Warning:** The default `memlock` ulimit on most Linux distributions is 64 KB, which is insufficient for any RDMA application. This is the most common setup issue for new RDMA deployments. Always set `memlock` to `unlimited` for RDMA applications.

</div>

### ENOMEM: NIC Memory Table Full

Even with sufficient memlock, the NIC has a finite number of memory translation table entries. Registering too many small regions can exhaust this table.

```bash
# Check maximum MR count
ibv_devinfo -v | grep max_mr
```

Resolution: use fewer, larger memory regions. Consider a memory pool pattern where you register one large region and sub-allocate from it.

## QP State Transition Failures

QPs must follow a specific state machine: RESET -> INIT -> RTR -> RTS. Each transition requires specific attributes. Providing wrong attributes or attempting an invalid transition results in errors.

```c
// Common mistake: wrong attributes for INIT -> RTR
struct ibv_qp_attr attr = {
    .qp_state = IBV_QPS_RTR,
    .path_mtu = IBV_MTU_4096,
    .dest_qp_num = remote_qpn,
    .rq_psn = remote_psn,
    .max_dest_rd_atomic = 16,
    .min_rnr_timer = 12,
    .ah_attr = { /* ... */ },
};

int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
            IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
            IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;

if (ibv_modify_qp(qp, &attr, flags)) {
    // Check errno: EINVAL usually means wrong attributes or wrong state
    perror("Failed to modify QP to RTR");
}
```

Common mistakes:
- Forgetting required attributes for a transition (check the spec for mandatory attributes per state).
- Attempting RTR->RTS without setting `sq_psn`, `timeout`, `retry_cnt`, and `rnr_retry`.
- Setting `path_mtu` to a value the device does not support.

## Connection Timeout

Connection timeouts during RDMA CM (`rdma_connect()` or `rdma_resolve_addr()`) have different causes than transport retries:

- **Address resolution failure:** `rdma_resolve_addr()` fails if the destination IP cannot be resolved to an RDMA device and port. Check that the IP is assigned to an RDMA-capable interface.
- **Route resolution failure:** `rdma_resolve_route()` fails if no path exists to the destination. For InfiniBand, this requires the Subnet Manager to have computed a route. For RoCE, it requires standard IP routing.
- **Connection refused:** The remote side is not listening for connections on the specified port.

## PFC Storm Symptoms

A PFC storm occurs when a feedback loop causes PFC pause frames to propagate across the fabric, effectively halting traffic on affected links. Symptoms include:

- Sudden, severe throughput drop across many connections simultaneously.
- PFC counters (`rx_pfc_pause`, `tx_pfc_pause`) increasing rapidly.
- The problem clears suddenly when the storm breaks, then may recur.

```bash
# Monitor PFC frames in real-time
watch -n 1 'ethtool -S enp1s0f0 | grep pfc'
```

Diagnosis requires checking PFC configuration across all switches and hosts. A common cause is a misconfigured host that responds to PFC pauses by sending more PFC pauses upstream.

## Performance Degradation

Not all failures are hard errors. Performance degradation can be equally problematic:

**Congestion:** CNP counters increasing, throughput below expected. Check DCQCN configuration and network load distribution.

**NUMA misalignment:** The application is running on a different NUMA node than the NIC. Latency increases by 200-500ns and throughput may drop by 20-40%.

```bash
# Check NIC's NUMA node
cat /sys/class/infiniband/mlx5_0/device/numa_node
# Run application on the correct NUMA node
numactl --cpunodebind=0 --membind=0 ./my_rdma_app
```

**PCIe throttling:** The NIC is connected to a PCIe slot with insufficient bandwidth. A 100 Gbps NIC requires PCIe Gen3 x16 or Gen4 x8 for full throughput.

```bash
# Check PCIe link status
lspci -s $(lspci | grep Mellanox | awk '{print $1}') -vvv | grep -E "LnkSta:|LnkCap:"
# Expected: Speed 16GT/s (Gen4), Width x16
```
