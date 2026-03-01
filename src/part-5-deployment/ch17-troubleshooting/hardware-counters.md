# 17.2 Hardware Counters and Monitoring

Hardware counters are the eyes into RDMA operations. Because the data path bypasses the kernel, there is no software log of individual operations. Instead, the NIC maintains hardware counters that aggregate statistics about traffic, errors, and protocol events. Reading and interpreting these counters is the primary method for understanding what is happening at the RDMA level.

## Port Counters

Port counters are exposed through sysfs at `/sys/class/infiniband/<device>/ports/<port>/counters/`. These counters are defined by the InfiniBand specification and are available on both InfiniBand and RoCE devices.

### Traffic Counters

```bash
# Bytes received and transmitted (divided by 4 for IB; direct bytes for some implementations)
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_data
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_xmit_data

# Packets received and transmitted
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_packets
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_xmit_packets
```

<div class="note">

**Note:** For InfiniBand, `port_rcv_data` and `port_xmit_data` count in units of 4 bytes (32-bit words), not bytes. Multiply by 4 to get the actual byte count. Some RoCE implementations report raw bytes. Check your vendor's documentation for the exact interpretation.

</div>

These counters are cumulative since the last reset. To measure throughput over an interval, sample the counter at two points and compute the difference:

```bash
# Simple throughput measurement
DATA1=$(cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_data)
sleep 1
DATA2=$(cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_data)
echo "Receive throughput: $(( (DATA2 - DATA1) * 4 * 8 )) bits/sec"
```

### Error Counters

Error counters are the most important counters for troubleshooting:

```bash
# Receive errors: packets received with errors (CRC, length, etc.)
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_rcv_errors

# Transmit discards: packets that could not be transmitted
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/port_xmit_discards

# Symbol errors: encoding errors on the link (physical layer problems)
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/symbol_error_counter

# Link downed: number of times the link went down
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/link_downed

# Link error recovery: number of times link error recovery was initiated
$ cat /sys/class/infiniband/mlx5_0/ports/1/counters/link_error_recovery
```

Interpreting error counters:

| Counter | Meaning | Likely Cause |
|---|---|---|
| `port_rcv_errors` > 0 | Packets received with errors | Bad cable, dirty connector, switch issue |
| `symbol_error_counter` increasing | Physical encoding errors | Marginal cable, bend radius, connector |
| `link_downed` > 0 | Link has dropped | Cable pull, switch reboot, signal integrity |
| `link_error_recovery` > 0 | Link recovered from errors | Intermittent physical issue |
| `port_xmit_discards` > 0 | Transmit drops | Congestion, buffer overflow |

<div class="warning">

**Warning:** Any non-zero and increasing error counter demands investigation. Even a small number of `symbol_error_counter` increments indicates a degrading physical link that will eventually cause failures. Replace the cable or transceiver module before it becomes a production issue.

</div>

### Reading All Counters at Once

```bash
# Dump all port counters
for f in /sys/class/infiniband/mlx5_0/ports/1/counters/*; do
    echo "$(basename $f): $(cat $f)"
done
```

The `perfquery` command provides an alternative way to read port counters, especially useful for reading counters on remote ports via the Subnet Manager:

```bash
# Read local port counters
$ perfquery

# Read counters for a specific LID and port
$ perfquery -x 5 1

# Read extended counters (64-bit values)
$ perfquery -x -X 5 1

# Reset counters (use with caution)
$ perfquery -R
```

## Hardware-Specific Counters (hw_counters)

Beyond the standard port counters, NIC vendors provide additional hardware-specific counters at `/sys/class/infiniband/<device>/ports/<port>/hw_counters/`. These counters provide deeper visibility into NIC-specific behavior.

### Error and Diagnostic Counters

```bash
# ICRC errors on encapsulated (RoCE) packets
$ cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rx_icrc_encapsulated

# Out of receive buffer events
$ cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rx_out_of_buffer

# Packet sequence errors (packets received out of order or with wrong PSN)
$ cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/packet_seq_err

# RNR NAK retry errors (receiver not ready, retries exhausted)
$ cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rnr_nak_retry_err
```

### Congestion-Related Counters

These counters are critical for diagnosing performance issues related to ECN/DCQCN congestion control:

```bash
# CNP (Congestion Notification Packets) sent by this port as notification point
$ cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/np_cnp_sent

# CNP handled by this port as reaction point (this port's traffic caused congestion)
$ cat /sys/class/infiniband/mlx5_0/ports/1/hw_counters/rp_cnp_handled
```

Interpreting congestion counters:

- **`np_cnp_sent` increasing:** This port is detecting ECN-marked packets from the network and sending CNP notifications back to the source. This means traffic *arriving* at this port is experiencing congestion somewhere in the network.

- **`rp_cnp_handled` increasing:** This port is receiving CNP notifications, indicating that traffic *sent* by this port is causing congestion. The port should be reducing its sending rate in response.

```text
Congestion notification flow:

Sender (RP)                    Switch              Receiver (NP)
    │                            │                      │
    │──── Data packet ──────────►│──── ECN-marked ─────►│
    │                            │                      │
    │◄──── CNP ─────────────────────────────────────────│
    │                            │                      │
    │ rp_cnp_handled++           │                      │ np_cnp_sent++
    │ (reduces send rate)        │                      │
```

<div class="tip">

**Tip:** A healthy RoCE network under load will show some `np_cnp_sent` and `rp_cnp_handled` activity. This indicates that DCQCN congestion control is working as designed. What you want to avoid is *sustained high* CNP rates, which indicate persistent congestion that the control algorithm cannot resolve. This typically points to a traffic pattern problem or insufficient network bandwidth.

</div>

### Complete hw_counters Dump

```bash
# List all available hw_counters
for f in /sys/class/infiniband/mlx5_0/ports/1/hw_counters/*; do
    echo "$(basename $f): $(cat $f)"
done
```

The available hw_counters vary by NIC vendor, model, and firmware version. Consult your vendor's documentation for the complete list and interpretation.

## Monitoring with Prometheus and Grafana

For production deployments, manual counter reading is insufficient. You need continuous monitoring with alerting. The standard approach is to export RDMA counters to Prometheus and visualize them in Grafana.

### RDMA Exporters

Several Prometheus exporters can collect RDMA counters:

**node_exporter with InfiniBand collector:** The standard Prometheus `node_exporter` includes an InfiniBand collector that exports port counters:

```bash
# Enable the InfiniBand collector in node_exporter
$ node_exporter --collector.infiniband
```

This exports metrics like:
```
node_infiniband_port_data_received_bytes_total{device="mlx5_0",port="1"}
node_infiniband_port_data_transmitted_bytes_total{device="mlx5_0",port="1"}
node_infiniband_port_receive_errors_total{device="mlx5_0",port="1"}
node_infiniband_port_transmit_discards_total{device="mlx5_0",port="1"}
```

**Vendor-specific exporters:** NIC vendors may provide exporters with access to hw_counters and additional vendor-specific metrics.

**Custom exporter:** For hw_counters not covered by standard exporters, a simple script can read sysfs and expose metrics:

```python
#!/usr/bin/env python3
"""Simple RDMA counter exporter for Prometheus."""
from prometheus_client import start_http_server, Gauge
import time
import os

COUNTER_DIR = "/sys/class/infiniband/mlx5_0/ports/1"

gauges = {}

def read_counters():
    for subdir in ["counters", "hw_counters"]:
        path = os.path.join(COUNTER_DIR, subdir)
        if not os.path.isdir(path):
            continue
        for name in os.listdir(path):
            filepath = os.path.join(path, name)
            try:
                with open(filepath) as f:
                    value = int(f.read().strip())
                metric_name = f"rdma_{subdir}_{name}"
                if metric_name not in gauges:
                    gauges[metric_name] = Gauge(metric_name, f"RDMA {name}")
                gauges[metric_name].set(value)
            except (ValueError, PermissionError):
                pass

if __name__ == "__main__":
    start_http_server(9190)
    while True:
        read_counters()
        time.sleep(10)
```

### Grafana Dashboard Design

A useful RDMA monitoring dashboard includes:

1. **Throughput panel:** `rate(rdma_counters_port_rcv_data[5m]) * 4 * 8` for receive bits/sec.
2. **Error rate panel:** `rate(rdma_counters_port_rcv_errors[5m])` with alert threshold.
3. **Congestion panel:** `rate(rdma_hw_counters_np_cnp_sent[5m])` and `rate(rdma_hw_counters_rp_cnp_handled[5m])`.
4. **Link stability panel:** `rdma_counters_link_downed` and `rdma_counters_link_error_recovery` as counters (any increase is noteworthy).
5. **Resource usage panel:** QP count, MR count from `rdma resource show` output.

## Setting Up Alerting

Configure alerts for conditions that require immediate attention:

```yaml
# Prometheus alerting rules for RDMA
groups:
  - name: rdma_alerts
    rules:
      - alert: RDMAReceiveErrors
        expr: rate(node_infiniband_port_receive_errors_total[5m]) > 0
        for: 2m
        labels:
          severity: warning
        annotations:
          summary: "RDMA receive errors on {{ $labels.device }}"

      - alert: RDMALinkDown
        expr: increase(node_infiniband_link_downed_total[5m]) > 0
        labels:
          severity: critical
        annotations:
          summary: "RDMA link down event on {{ $labels.device }}"

      - alert: RDMAHighCongestion
        expr: rate(rdma_hw_counters_rp_cnp_handled[5m]) > 10000
        for: 5m
        labels:
          severity: warning
        annotations:
          summary: "High RDMA congestion rate on {{ $labels.device }}"

      - alert: RDMASymbolErrors
        expr: rate(node_infiniband_symbol_error_counter_total[5m]) > 0
        for: 1m
        labels:
          severity: critical
        annotations:
          summary: "Physical link degradation on {{ $labels.device }}"
```

## Baseline Measurement and Anomaly Detection

Effective monitoring requires establishing baselines for your specific deployment:

1. **Measure baseline throughput:** Run perftest between representative host pairs and record expected throughput. Alert when production throughput drops below a percentage of the baseline.

2. **Establish normal congestion levels:** Under typical workload, record the CNP rates. Alert when they deviate significantly (e.g., 10x normal).

3. **Characterize normal counter behavior:** Some counters (like `rx_out_of_buffer`) may show small numbers during normal operation. Understand what is "normal" for your workload before setting alert thresholds.

4. **Track firmware and driver versions:** Correlate counter anomalies with firmware or driver changes. Some firmware versions have known issues that manifest as specific counter patterns.

```bash
# Baseline measurement script
#!/bin/bash
DEVICE="mlx5_0"
PORT="1"
COUNTER_PATH="/sys/class/infiniband/$DEVICE/ports/$PORT"

echo "=== Baseline Snapshot ==="
echo "Timestamp: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "Device: $DEVICE Port: $PORT"
echo ""
echo "--- Port Counters ---"
for f in $COUNTER_PATH/counters/*; do
    printf "%-40s %s\n" "$(basename $f)" "$(cat $f)"
done
echo ""
echo "--- HW Counters ---"
for f in $COUNTER_PATH/hw_counters/*; do
    printf "%-40s %s\n" "$(basename $f)" "$(cat $f)"
done
```

Save baseline snapshots before and after configuration changes, firmware updates, and workload changes. When problems arise, comparing current counters to the baseline quickly identifies what has changed.
