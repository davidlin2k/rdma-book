# 17.4 Debugging Methodology

Having the right tools and knowing the common failure modes is necessary but not sufficient. Effective RDMA debugging requires a systematic methodology that eliminates possible causes in a logical order. This section presents that methodology, along with specialized techniques for RDMA-specific debugging challenges.

## Systematic Approach: The Six-Step Checklist

When an RDMA problem is reported, work through these steps in order. Each step builds on the previous one, and skipping steps leads to wasted time chasing unlikely causes.

### Step 1: Verify the Link Layer

Before investigating any RDMA-specific issue, confirm that the physical and link layer are healthy.

```bash
# InfiniBand: port must be ACTIVE
ibstat mlx5_0 | grep -E "State|Physical"
# Expected: State: Active, Physical state: LinkUp

# RoCE: interface must be UP with correct MTU
ip link show enp1s0f0
# Expected: state UP, mtu 9000

# Check link speed (is it running at expected rate?)
ethtool enp1s0f0 | grep Speed
# Expected: Speed: 100000Mb/s (for 100GbE)

# Check for recent link flaps
dmesg | grep -i "link\|enp1s0f0" | tail -20
```

If the link is down, the problem is physical: cable, transceiver, switch port, or NIC hardware. Do not proceed to higher-layer debugging until the link is stable.

### Step 2: Verify RDMA Connectivity

With a healthy link, test whether RDMA operations work at all between the two endpoints.

```bash
# On the server side:
ib_send_bw -d mlx5_0

# On the client side:
ib_send_bw -d mlx5_0 <server_ip>
```

If perftest works, the RDMA infrastructure is functional, and the problem is in the application or its configuration. If perftest fails, the problem is at the infrastructure level.

For RoCE, also verify that the correct GID index is being used:

```bash
# Test with explicit GID index
ib_send_bw -d mlx5_0 -x 3 <server_ip>
# Try different GID indices if the default fails
```

<div class="tip">

**Tip:** If perftest works but your application does not, the problem is almost certainly in how your application sets up QPs, exchanges connection information, or manages memory regions. Focus debugging on the application's control path, not the RDMA infrastructure.

</div>

### Step 3: Check Error Counters

Even if operations appear to work, error counters reveal underlying issues that may cause intermittent failures or performance degradation.

```bash
# Check all error-related counters
echo "=== Port Counters ==="
cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_errors
cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_xmit_discards
cat /sys/class/infiniband/mlx5_0/ports/1/counters/symbol_error_counter
cat /sys/class/infiniband/mlx5_0/ports/1/counters/link_downed
cat /sys/class/infiniband/mlx5_0/ports/1/counters/link_error_recovery

echo "=== HW Counters ==="
cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rx_icrc_encapsulated
cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/packet_seq_err
cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rnr_nak_retry_err
cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rx_out_of_buffer
```

Compare against baseline values. Any non-zero and increasing error counter warrants investigation even if the application appears to work.

### Step 4: Check Application Errors

Examine the completion status codes reported by the application.

```c
// Application should always check CQE status
struct ibv_wc wc;
int ne = ibv_poll_cq(cq, 1, &wc);
if (ne > 0) {
    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "CQE error: %s (%d), wr_id: %lu, opcode: %d\n",
                ibv_wc_status_str(wc.status), wc.status,
                wc.wr_id, wc.opcode);
    }
}
```

The CQE status code directly indicates the failure category. Map the status to the failure modes described in Section 17.3 and follow the corresponding diagnostic path.

### Step 5: Check System Logs

The kernel RDMA subsystem and NIC drivers log important events to the kernel message buffer.

```bash
# Check for RDMA-related kernel messages
dmesg | grep -iE "rdma|infiniband|mlx5|ib_|roce" | tail -50

# Check for memory-related issues (OOM, memlock)
dmesg | grep -iE "oom|memlock|mlock|memory" | tail -20

# Check system journal for RDMA service issues
journalctl -u rdma-ndd -u opensm -u ibacm --since "1 hour ago"
```

Common kernel messages to look for:
- `mlx5_core: ... error ...`: NIC hardware or firmware errors.
- `infiniband: ... failed ...`: Verbs layer errors.
- `ib_core: ... denied ...`: Permission or resource limit issues.

### Step 6: Check Configuration

If all previous steps pass but the problem persists, verify the complete configuration stack.

```bash
# MTU configuration (must be consistent end-to-end)
ip link show enp1s0f0 | grep mtu          # Ethernet MTU
ibv_devinfo | grep mtu                     # RDMA MTU

# PFC configuration (RoCE)
mlnx_qos -i enp1s0f0 2>/dev/null || lldptool -ti enp1s0f0

# ECN configuration (RoCE)
sysctl net.ipv4.tcp_ecn
cat /sys/class/net/enp1s0f0/queues/tc*/ecn 2>/dev/null

# DSCP/priority mapping
cat /sys/class/infiniband/mlx5_0/tc/1/traffic_class 2>/dev/null
```

## Common Debugging Patterns

Experienced RDMA engineers recognize patterns in failure symptoms that point toward specific causes. Here are the most common patterns.

### "Works with Small Messages, Fails with Large"

**Root cause: MTU mismatch.** Small messages fit within the minimum MTU and are delivered successfully. Large messages are segmented into MTU-sized packets, and if any switch in the path has a lower MTU than the RDMA path MTU, the larger packets are silently dropped.

```bash
# Test with different message sizes to find the threshold
ib_send_bw -s 256 <server>    # Works
ib_send_bw -s 512 <server>    # Works
ib_send_bw -s 1024 <server>   # Works
ib_send_bw -s 2048 <server>   # Fails!
# The failing threshold indicates the actual path MTU
```

**Resolution:** Set the RDMA path MTU to match the minimum MTU in the network path. For RoCE, ensure all switches and interfaces in the path have jumbo frames (MTU 9000) enabled.

### "Works for a While, Then Fails"

**Root cause: resource leak or buffer exhaustion.** The application creates resources (QPs, MRs, CQEs) without properly releasing them, or consumes receive buffers without replenishing them.

```bash
# Monitor resource usage over time
watch -n 5 'rdma resource show'
# Look for increasing QP, MR, or CQ counts

# Monitor memory usage
watch -n 5 'cat /proc/$(pgrep my_app)/status | grep -i vm'
# Look for increasing VmLck (locked memory)
```

**Resolution:** Audit the application for resource lifecycle management. Ensure every `ibv_create_*` has a corresponding `ibv_destroy_*`, every `ibv_reg_mr` has a corresponding `ibv_dereg_mr`, and receive buffers are re-posted after consumption.

### "Intermittent Failures"

**Root cause: congestion, PFC storms, or flaky physical link.** The failures correlate with network load or occur in bursts.

```bash
# Correlate failure times with counter snapshots
# Take snapshots every second during the failure window
while true; do
    echo "$(date): rcv_err=$(cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_errors) \
    sym_err=$(cat /sys/class/infiniband/mlx5_0/ports/1/counters/symbol_error_counter) \
    cnp=$(cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/np_cnp_sent)"
    sleep 1
done
```

If `symbol_error_counter` increments during failures: physical link problem (replace cable).
If `np_cnp_sent` spikes during failures: congestion-related (check network load, DCQCN tuning).
If PFC counters spike: possible PFC storm (check PFC configuration across the fabric).

### "Works on One Machine, Not Another"

**Root cause: configuration difference.** Driver version, firmware version, kernel version, or system configuration differs between the machines.

```bash
# Compare critical configuration between machines
# Run on both machines and diff the output:
echo "=== Firmware ==="
ibv_devinfo | grep fw_ver
echo "=== Driver ==="
modinfo mlx5_core | grep version
echo "=== Kernel ==="
uname -r
echo "=== MTU ==="
ip link show enp1s0f0 | grep mtu
echo "=== NUMA ==="
cat /sys/class/infiniband/mlx5_0/device/numa_node
echo "=== Memlock ==="
ulimit -l
echo "=== Hugepages ==="
cat /proc/meminfo | grep Huge
```

## GDB with RDMA Programs

Debugging RDMA programs with GDB requires awareness of several RDMA-specific concerns.

### Thread Awareness

RDMA applications typically use multiple threads: one or more for posting work requests and one or more for polling completions. When a failure occurs, the relevant context may be in a different thread than where the error is reported.

```bash
# Attach GDB and list all threads
gdb -p $(pgrep my_rdma_app)
(gdb) info threads
(gdb) thread apply all bt  # Backtrace all threads
```

### wr_id Tracking

The `wr_id` field in work requests is the primary tool for correlating completions with their originating operations. Use it systematically:

```c
// Encode debugging information in wr_id
#define MAKE_WR_ID(type, seq) (((uint64_t)(type) << 48) | (seq))
#define WR_TYPE(wr_id) ((wr_id) >> 48)
#define WR_SEQ(wr_id) ((wr_id) & 0xFFFFFFFFFFFF)

wr.wr_id = MAKE_WR_ID(OP_RDMA_WRITE, sequence_number++);
```

When a CQE reports an error, the `wr_id` tells you exactly which operation failed, making it possible to trace back to the specific buffer, remote address, and R_Key used.

## Enabling rdma-core Debug Output

The rdma-core library supports environment variables for debug output:

```bash
# Enable verbose debug output
export RDMAV_DEBUG=1

# For even more detail (very verbose)
export RDMAV_DEBUG=2

# Run the application with debug output
RDMAV_DEBUG=1 ./my_rdma_app 2>&1 | tee rdma_debug.log
```

## RDMAV_FORK_SAFE and IBV_FORK_SAFE

One of the most confusing RDMA issues is fork() interaction. When a process that has registered memory regions calls `fork()`, the child process inherits the parent's address space. However, the NIC's memory translation table still points to the parent's physical pages. If the OS uses copy-on-write (COW), the child writing to a registered buffer triggers a page copy, and the NIC's mapping becomes stale.

<div class="warning">

**Warning:** Calling `fork()` after `ibv_reg_mr()` without proper precautions can cause silent data corruption. The NIC may read from or write to physical pages that no longer correspond to the correct virtual address in either the parent or child process.

</div>

The libibverbs library provides two mechanisms to handle this:

```bash
# Option 1: Set environment variable before any RDMA calls
export RDMAV_FORK_SAFE=1
# or
export IBV_FORK_SAFE=1

# Option 2: Call ibv_fork_init() early in the program
```

```c
// In code: call before any other ibv_ functions
int ret = ibv_fork_init();
if (ret) {
    fprintf(stderr, "ibv_fork_init failed: %s\n", strerror(ret));
    return -1;
}
```

`ibv_fork_init()` calls `madvise(MADV_DONTFORK)` on registered memory regions, preventing them from being inherited by child processes. This solves the COW problem but means the child cannot access RDMA buffers.

<div class="note">

**Note:** If your application uses `fork()` for any reason -- including indirectly through `system()`, `popen()`, or libraries that spawn subprocesses -- you must either call `ibv_fork_init()` or set the `RDMAV_FORK_SAFE` environment variable. Symptoms of fork-related RDMA corruption include intermittent data corruption, segfaults in the NIC driver, and mysterious CQE errors that do not reproduce under a debugger.

</div>

## strace for Verifying uverbs Calls

While the RDMA data path bypasses the kernel, the control path uses ioctl or write calls to `/dev/infiniband/uverbs*`. `strace` can capture these calls to verify that resource creation and configuration succeed:

```bash
# Trace uverbs calls
strace -e trace=ioctl,write,openat -f -p $(pgrep my_rdma_app) 2>&1 | \
    grep -i "uverbs\|infiniband"

# Trace only during startup to see resource creation
strace -e trace=ioctl,write,openat -f ./my_rdma_app 2>&1 | \
    grep -v EAGAIN | head -100
```

Look for:
- `openat("/dev/infiniband/uverbs0", ...)` -- device open.
- Failed ioctl calls with error codes like `EINVAL` (invalid argument), `ENOMEM` (out of memory), or `EPERM` (permission denied).

## Debugging Decision Tree

When faced with an RDMA problem, use this decision tree to guide your investigation:

```text
Problem Reported
│
├── Can you see RDMA devices? (ibv_devices)
│   ├── No → Check driver, PCI, dmesg
│   └── Yes ↓
│
├── Is the port ACTIVE? (ibv_devinfo)
│   ├── No → Check cable, switch, SM (IB) / interface config (RoCE)
│   └── Yes ↓
│
├── Does perftest work? (ib_send_bw)
│   ├── No → Check MTU, PFC, GID index, firewall
│   └── Yes ↓
│
├── Does your application fail with a CQE error?
│   ├── RETRY_EXC → MTU, PFC, network path
│   ├── RNR_RETRY_EXC → Post more receive buffers
│   ├── REM_ACCESS_ERR → R_Key, address, access flags
│   ├── LOC_PROT_ERR → L_Key, PD mismatch, MR bounds
│   └── Other → Check ibv_wc_status_str(), dmesg
│
├── Is it a performance problem?
│   ├── Check NUMA affinity (numactl)
│   ├── Check PCIe bandwidth (lspci)
│   ├── Check congestion counters (CNPs)
│   └── Check for PFC pauses (ethtool -S)
│
└── Is it intermittent?
    ├── Check error counters over time
    ├── Check for resource leaks (rdma resource show)
    └── Check for fork() issues (RDMAV_FORK_SAFE)
```

This systematic approach ensures that no common cause is overlooked and that debugging effort is directed toward the most likely root cause given the observed symptoms.
