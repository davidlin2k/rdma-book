# 12.6 Benchmarking

Rigorous benchmarking is the foundation of RDMA performance engineering. Without accurate measurements, optimization is guesswork. This section covers the standard RDMA benchmarking tools, their proper usage, common measurement pitfalls, and the methodology for building custom benchmarks. A complete benchmark implementation is provided in `src/code/ch12-benchmark/`.

## The perftest Suite

The **perftest** package is the standard benchmarking tool for RDMA. Developed and maintained by Mellanox/NVIDIA, it provides purpose-built benchmarks for every RDMA operation type. Install it from your distribution's package manager or build from source.

### Available Benchmarks

| Tool | Operation | Metric |
|------|-----------|--------|
| `ib_send_bw` | Send/Receive | Bandwidth |
| `ib_send_lat` | Send/Receive | Latency |
| `ib_write_bw` | RDMA Write | Bandwidth |
| `ib_write_lat` | RDMA Write | Latency |
| `ib_read_bw` | RDMA Read | Bandwidth |
| `ib_read_lat` | RDMA Read | Latency |
| `ib_atomic_bw` | Atomic (CAS/FAA) | Bandwidth |
| `ib_atomic_lat` | Atomic (CAS/FAA) | Latency |

### Basic Usage

All perftest tools use a client-server model. Start the server first, then the client:

```bash
# Server side (listens for connection)
ib_write_lat -d mlx5_0

# Client side (connects to server)
ib_write_lat -d mlx5_0 192.168.1.100
```

### Key Parameters

| Flag | Description | Default | Recommended |
|------|-------------|---------|-------------|
| `-s <size>` | Message size in bytes | 2 | Vary to understand scaling |
| `-n <iters>` | Number of iterations | 1000 | 10000+ for stable results |
| `-d <dev>` | IB device name | First device | Always specify explicitly |
| `-i <port>` | IB port number | 1 | Check with `ibstat` |
| `--all` | Test all sizes (2^0 to 2^23) | Off | Use for size sweeps |
| `-q <qps>` | Number of QP pairs | 1 | Increase for throughput |
| `-t <depth>` | Send queue depth | 128 | 256--512 for bandwidth |
| `--report_gbits` | Report in Gbps | Off | For network comparison |
| `-F` | Do not fail on conn. failure | Off | For CI/automation |
| `--cpu_util` | Report CPU utilization | Off | For efficiency analysis |

### Bandwidth Measurement

```bash
# Sweep all message sizes, report in Gbps
# Server:
ib_write_bw -d mlx5_0 --all --report_gbits

# Client:
ib_write_bw -d mlx5_0 --all --report_gbits 192.168.1.100

# Sample output:
#  #bytes  #iterations  BW peak[Gb/sec]  BW average[Gb/sec]  MsgRate[Mpps]
#  2       5000         0.31             0.30                18.75
#  4       5000         0.62             0.61                19.14
#  ...
#  1024    5000         79.50            78.90               9.63
#  4096    5000         97.20            96.80               2.95
#  65536   5000         98.50            98.40               0.19
#  1048576 5000         98.10            98.00               0.01
```

### Latency Measurement

```bash
# Measure RDMA Write latency for 64-byte messages, 100K iterations
# Server:
ib_write_lat -d mlx5_0 -s 64 -n 100000

# Client:
ib_write_lat -d mlx5_0 -s 64 -n 100000 192.168.1.100

# Sample output:
#  #bytes  #iterations  t_min[usec]  t_max[usec]  t_typical[usec]  t_avg[usec]
#  64      100000       1.05         8.32         1.12             1.15
```

### Multi-QP Bandwidth

```bash
# Test with 4 QPs for higher aggregate throughput
# Server:
ib_write_bw -d mlx5_0 -q 4 -s 64 -n 100000

# Client:
ib_write_bw -d mlx5_0 -q 4 -s 64 -n 100000 192.168.1.100
```

## Interpreting Results

### Bandwidth Results

- **BW peak**: Maximum bandwidth observed in any single measurement window
- **BW average**: Average bandwidth across all iterations
- **MsgRate**: Messages per second (total across all QPs)

The gap between peak and average indicates variability. A large gap suggests interference (other traffic, NUMA issues, or CPU scheduling).

### Latency Results

- **t_min**: Best-case latency (useful for understanding hardware floor)
- **t_max**: Worst-case latency (indicates tail latency issues)
- **t_typical**: Median or mode latency
- **t_avg**: Arithmetic mean

For production systems, focus on **t_typical** and **t_max**. The average can be skewed by outliers.

### Latency Percentiles

For detailed latency distribution, use the `--latency_gap` and `--output` options:

```bash
# Record per-iteration latency data
ib_write_lat -d mlx5_0 -s 64 -n 100000 --output=latency.csv 192.168.1.100

# Post-process for percentiles
sort -n latency.csv | awk '
    BEGIN { n=0 }
    { data[n++] = $1 }
    END {
        printf "p50:   %.2f us\n", data[int(n*0.50)];
        printf "p99:   %.2f us\n", data[int(n*0.99)];
        printf "p99.9: %.2f us\n", data[int(n*0.999)];
    }
'
```

## Common Benchmarking Pitfalls

### 1. Cold Start Effects

The first several hundred iterations often show higher latency due to:
- NIC cache population (QP context, MTT entries)
- CPU cache warming
- Memory page faults (first access triggers page allocation)

**Fix**: Always include warm-up iterations. perftest does this by default with `-n` iterations preceded by an implicit warm-up phase. For custom benchmarks, explicitly discard the first 1000--5000 iterations.

### 2. Single QP Saturation

A single QP may not saturate the NIC, especially for small messages. Reporting "100 Mpps" when the NIC can do 200 Mpps is a benchmarking error, not a hardware limitation.

**Fix**: Test with multiple QPs (`-q 4`, `-q 8`) and report the maximum achieved across configurations.

### 3. Wrong NUMA Node

Running on the wrong NUMA node adds 50--100% latency and reduces bandwidth by 20--30%. This is the single most common source of misleading benchmark results.

**Fix**: Always bind to the NIC's NUMA node:

```bash
NIC_NUMA=$(cat /sys/class/infiniband/mlx5_0/device/numa_node)
numactl --cpunodebind=$NIC_NUMA --membind=$NIC_NUMA ib_write_lat -d mlx5_0 ...
```

### 4. CPU Frequency Scaling

Modern CPUs use dynamic frequency scaling (Intel SpeedStep, AMD Cool'n'Quiet). A CPU core at idle may be running at 1.2 GHz; under load it ramps to 3.5+ GHz. If the benchmark starts measuring before the CPU reaches full frequency, early iterations show higher latency.

**Fix**: Set the CPU governor to `performance`:

```bash
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance > $cpu
done
```

### 5. Measuring the Wrong Thing

A common mistake is measuring round-trip time and reporting it as one-way latency, or measuring bandwidth with too few iterations to reach steady state.

**Fix**: Understand what each tool measures. `ib_write_lat` measures half-round-trip (ping-pong / 2). `ib_write_bw` measures unidirectional sustained bandwidth.

### 6. Background Traffic

Other processes generating network or PCIe traffic can interfere with measurements. Even PCIe traffic from other devices (NVMe SSDs, GPUs) can compete for PCIe bandwidth.

**Fix**: Isolate the test system. Quiesce other workloads. Use dedicated benchmark windows.

## qperf: Quick Performance Testing

`qperf` is a simpler alternative to perftest for quick measurements. It measures both RDMA and TCP performance, making it useful for comparing RDMA vs. socket-based performance:

```bash
# Server:
qperf

# Client - measure RDMA Write bandwidth and latency:
qperf 192.168.1.100 rc_rdma_write_bw rc_rdma_write_lat

# Client - compare RDMA vs TCP:
qperf 192.168.1.100 tcp_bw tcp_lat rc_rdma_write_bw rc_rdma_write_lat
```

## Custom Benchmarking Methodology

When perftest is insufficient -- for example, measuring application-specific access patterns, mixed operations, or multi-node performance -- you need custom benchmarks.

### What to Measure

1. **Throughput**: Operations per second or bytes per second under sustained load
2. **Latency**: Per-operation time, including percentile distribution
3. **CPU efficiency**: Operations per CPU cycle (or per watt)
4. **Scalability**: How performance changes with QP count, thread count, or node count

### Statistical Methodology

- **Warm-up**: Discard the first N iterations (typically 1000--5000)
- **Steady-state**: Measure for a fixed duration (e.g., 10 seconds) or fixed iteration count (e.g., 100,000)
- **Multiple runs**: Repeat the experiment 5--10 times and report the median
- **Report percentiles**: Always report p50, p99, and p99.9 for latency
- **Report confidence intervals**: For mean values, report the 95% confidence interval

```c
// Timing methodology for custom benchmarks
struct timespec start, end;

// Warm-up phase
for (int i = 0; i < WARMUP_ITERS; i++) {
    perform_rdma_operation();
    poll_completion();
}

// Measurement phase
clock_gettime(CLOCK_MONOTONIC, &start);
for (int i = 0; i < MEASURE_ITERS; i++) {
    perform_rdma_operation();
    poll_completion();
}
clock_gettime(CLOCK_MONOTONIC, &end);

double elapsed_sec = (end.tv_sec - start.tv_sec)
                   + (end.tv_nsec - start.tv_nsec) * 1e-9;
double ops_per_sec = MEASURE_ITERS / elapsed_sec;
double bandwidth = ops_per_sec * msg_size;
```

### Reference Benchmark

A complete RDMA Write bandwidth benchmark is provided in `src/code/ch12-benchmark/rdma_bench.c`. This benchmark demonstrates:

- Configurable message size, iteration count, and QP count
- Warm-up phase with discarded results
- Steady-state bandwidth and message rate measurement
- Statistical reporting (min, max, average, standard deviation)
- Proper NUMA-aware resource allocation
- Signaled completion interval for throughput optimization

Build and run:

```bash
cd src/code/ch12-benchmark
make
# Server:
./rdma_bench -d mlx5_0 -s 4096 -n 100000
# Client:
./rdma_bench -d mlx5_0 -s 4096 -n 100000 192.168.1.100
```

## Benchmark Environment Checklist

Before running any RDMA benchmark, verify the following:

| Check | Command | Expected |
|-------|---------|----------|
| NIC link speed | `ibstat mlx5_0` | Active, correct speed |
| PCIe link | `lspci -s XX:00.0 -vvv \| grep LnkSta` | Full speed, full width |
| NUMA binding | `numactl --show` | Correct node |
| CPU governor | `cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor` | performance |
| IRQ affinity | `cat /proc/irq/*/smp_affinity` | NIC's NUMA node |
| irqbalance | `systemctl status irqbalance` | Stopped |
| Huge pages | `cat /proc/meminfo \| grep HugePages` | Sufficient pages |
| MTU | `ibstat mlx5_0 \| grep mtu` | 4096 (IB) or 9000 (RoCE) |

<div class="tip">

**Reproducibility**: Document every environment parameter when publishing benchmark results. Results without configuration context are not meaningful. Include: NIC model, firmware version, driver version, PCIe config, NUMA topology, CPU model, kernel version, and compiler flags.

</div>
