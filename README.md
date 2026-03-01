# Mastering RDMA: From Fundamentals to Production Systems

A comprehensive, publishable-quality textbook covering Remote Direct Memory Access (RDMA) from absolute fundamentals to advanced production topics. Built with [mdBook](https://rust-lang.github.io/mdBook/).

## Overview

This book targets readers with basic networking knowledge and brings them to expert-level understanding of RDMA. It covers the full stack: hardware architecture, protocol internals, the libibverbs programming model, performance engineering, deployment patterns, and emerging technologies.

**18 chapters + 5 appendices | ~210,000 words | ~530 pages | 10 complete C programs | 45+ Mermaid diagrams**

## Structure

| Part | Chapters | Topic |
|------|----------|-------|
| **I: Foundations** | 1--3 | Why RDMA exists, history, transport protocols (InfiniBand, RoCE, iWARP) |
| **II: Architecture** | 4--7 | Core abstractions (QP, CQ, MR, PD), RDMA operations, memory management, connection management |
| **III: Programming** | 8--11 | Getting started, programming patterns (RC, UD, RDMA Read/Write), RDMA_CM, advanced topics (SRQ, XRC, DCT, multi-threading) |
| **IV: Performance** | 12--13 | Latency/throughput optimization, PCIe, NUMA, NIC internals, congestion control (PFC, DCQCN) |
| **V: Deployment** | 14--17 | Applications (NVMe-oF, MPI, databases, ML), cloud/virtualization, security, troubleshooting |
| **VI: Future** | 18 | CXL, SmartNICs/DPUs, GPUDirect RDMA, computational storage |
| **Appendices** | A--E | Verbs API reference, RDMA_CM API reference, glossary, further reading, lab setup guide |

## Code Examples

Complete, compilable C programs in `src/code/`:

| Directory | Program | Description |
|-----------|---------|-------------|
| `common/` | `rdma_common.{h,c}` | Shared helpers: TCP exchange, QP info, completion polling |
| `ch08-hello-verbs/` | `hello_verbs.c` | Device discovery, resource creation, capability queries |
| `ch09-rc-pingpong/` | `rc_pingpong.c` | RC Send/Receive ping-pong with latency measurement |
| `ch09-rdma-write/` | `rdma_write.c` | One-sided RDMA Write with completion notification |
| `ch09-rdma-read/` | `rdma_read.c` | One-sided RDMA Read with metadata-then-data pattern |
| `ch09-ud-example/` | `ud_send.c` | Unreliable Datagram messaging with Address Handles |
| `ch10-cm-client-server/` | `cm_server.c`, `cm_client.c` | RDMA_CM-based client-server |
| `ch11-srq-example/` | `srq_example.c` | Shared Receive Queue across multiple QPs |
| `ch12-benchmark/` | `rdma_bench.c` | RDMA Write bandwidth benchmark |

## Building the Book

### Prerequisites

```bash
cargo install mdbook
cargo install mdbook-mermaid
cargo install mdbook-katex
```

### Setup and Build

```bash
# Install mermaid JS assets (first time only)
mdbook-mermaid install .

# Build
mdbook build

# Serve locally with live reload
mdbook serve
# Open http://localhost:3000
```

## Building the Code Examples

### Prerequisites

- Linux with rdma-core installed (`libibverbs-dev`, `librdmacm-dev`)
- GCC and Make
- Soft-RoCE (rxe) or SoftiWARP (siw) for testing without hardware

### Quick Start with Soft-RoCE

```bash
# Load the kernel module and create a software RDMA device
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev eth0

# Verify
ibv_devices
ibv_devinfo

# Build an example
cd src/code/ch08-hello-verbs
make
./hello_verbs
```

### Build All Examples

```bash
cd src/code
for dir in ch08-* ch09-* ch10-* ch11-* ch12-*; do
    echo "=== Building $dir ==="
    make -C "$dir"
done
```

See [Appendix E: Lab Setup Guide](src/appendices/appendix-e-lab-setup.md) for detailed environment setup including Docker-based two-node testbeds.

## License

All rights reserved.
