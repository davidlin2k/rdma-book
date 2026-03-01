# Preface

In the summer of 2015, I watched a colleague demonstrate a key-value store that could serve ten million lookups per second from a single machine. The latency was under two microseconds. My first instinct was disbelief -- I had spent years building networked systems where a single remote read took hundreds of microseconds at best. When I looked at the code, I found no calls to `send()` or `recv()`. There were no sockets, no serialization libraries, no event loops. The application was writing directly into a remote machine's memory, and the remote CPU had no idea it was happening.

That was my introduction to RDMA.

In the decade since, Remote Direct Memory Access has moved from a niche technology used in high-performance computing clusters to a foundational building block of modern datacenter infrastructure. It underpins the storage fabrics at Microsoft, Meta, Google, and countless other hyperscalers. It is the transport layer behind NVMe over Fabrics. It makes distributed training of large language models practical at scale. And yet, for most systems programmers, RDMA remains unfamiliar territory -- a technology they have heard of but never used, surrounded by jargon and tribal knowledge that makes the learning curve unnecessarily steep.

This book aims to flatten that curve.

## Who This Book Is For

This book is written for four overlapping audiences:

**Systems programmers** who build storage engines, databases, distributed file systems, or any infrastructure software where network performance matters. If you have ever profiled your application and found the kernel's networking stack to be the bottleneck, this book will show you a way past that wall.

**Network engineers** who design and operate datacenter fabrics. RDMA places fundamentally different demands on the network compared to traditional TCP traffic. Understanding those demands -- lossless semantics, congestion control, traffic isolation -- is essential for anyone deploying RoCEv2 or InfiniBand at scale.

**Cloud and infrastructure architects** who make technology selection decisions. RDMA is increasingly available as a cloud primitive (Azure's Accelerated Networking, AWS EFA, GCP's gVNIC with RDMA support), and evaluating when and where to adopt it requires an understanding of both its capabilities and its operational costs.

**Researchers and graduate students** working in systems, networking, or high-performance computing. This book provides the practical grounding that academic papers often assume, bridging the gap between reading about one-sided RDMA operations and actually implementing them correctly.

If you have written networked applications and wished they were faster, or if you have heard that RDMA can deliver microsecond latencies and want to understand how, you are in the right place.

## Prerequisites

This book assumes you are comfortable with the following:

- **The C programming language.** RDMA's primary interface, the libibverbs API, is a C library. All code examples in this book are written in C (with occasional C++ where it improves clarity). You should be comfortable reading and writing C code, including pointer manipulation, memory allocation, and working with structures.

- **Linux systems programming fundamentals.** You should know what a file descriptor is, understand the basics of virtual memory, and be familiar with common system calls. Experience with multithreaded programming (pthreads) is helpful but not strictly required.

- **Basic networking concepts.** You should understand Ethernet, IP addresses, TCP vs. UDP, and the general concept of a protocol stack. Familiarity with terms like MTU, congestion window, and RTT will help, though we define them where they matter.

You do not need prior experience with RDMA, InfiniBand, or any RDMA-specific API. We start from first principles.

## How This Book Is Organized

The book is divided into six parts, each building on the last:

**Part I: Foundations** introduces the core problem that RDMA solves -- the overhead of traditional kernel-mediated networking -- and presents the key ideas that make zero-copy, kernel-bypass data transfer possible. We cover the history of RDMA, the ecosystem of transport protocols (InfiniBand, RoCEv2, iWARP), and the mental model you need before writing your first line of RDMA code.

**Part II: Architecture** dives deep into RDMA's internal machinery. You will learn how Queue Pairs, Completion Queues, Memory Regions, and Protection Domains fit together. We trace a packet's journey from a work request posted by the application, through the RDMA-capable network adapter (RNIC), across the fabric, and into the remote machine's memory -- all without involving either CPU's operating system.

**Part III: Programming** is the hands-on core of the book. We walk through the libibverbs and rdma-cm APIs in detail, building progressively more sophisticated applications: a simple ping-pong benchmark, a high-throughput streaming pipeline, and a key-value store that uses one-sided RDMA reads. Each chapter includes complete, runnable code.

**Part IV: Performance** addresses the gap between "it works" and "it works well." We cover performance analysis methodology, common pitfalls (PCIe bottlenecks, doorbell overhead, completion batching), NUMA-aware design, and the art of saturating a 200 Gbps or 400 Gbps link from a single application thread.

**Part V: Deployment** tackles the operational reality of running RDMA in production. Topics include network design for RoCEv2 (PFC, ECN, DCQCN), monitoring and diagnostics, multi-tenancy and isolation, security considerations, and integration with container orchestration and cloud platforms.

**Part VI: The Future** looks at where RDMA is heading -- programmable network adapters (SmartNICs and DPUs), in-network computing, CXL's relationship to RDMA, and how the technology is evolving to meet the demands of AI/ML workloads and disaggregated architectures.

Each part is designed to be read sequentially within itself, but you can skip between parts once you have the foundations from Parts I and II.

## Conventions Used in This Book

Throughout the book, we use several formatting conventions to help you navigate the material:

**Code listings** appear in monospaced font and are numbered for reference. Inline code such as function names (`ibv_post_send`), structure names (`struct ibv_qp_init_attr`), or command-line tools (`ibv_devinfo`) is also set in monospace.

```c
// Example: Posting a send work request
struct ibv_send_wr wr = {};
wr.opcode = IBV_WR_SEND;
wr.send_flags = IBV_SEND_SIGNALED;
// ... additional setup omitted for brevity
int ret = ibv_post_send(qp, &wr, &bad_wr);
```

**Admonition blocks** highlight important information:

> **Note:** General observations, clarifications, or supplementary information that enriches your understanding of the topic at hand.

> **Warning:** Common mistakes, subtle pitfalls, or non-obvious behavior that can lead to bugs, crashes, or silent data corruption. Pay close attention to these.

> **Performance Tip:** Practical advice for optimizing throughput, reducing latency, or avoiding common performance anti-patterns.

**Diagrams** are used extensively to illustrate data flows, memory layouts, and architectural relationships. RDMA is a visual topic -- the interaction between applications, adapters, and the network is much easier to grasp with a picture than with words alone.

**Terminal output** is shown in monospaced blocks with a `$` prefix for commands:

```
$ ibv_devinfo
hca_id: mlx5_0
  transport: InfiniBand (0)
  fw_ver: 20.31.1014
  ...
```

## Companion Code

All code examples in this book are available in the companion repository:

```
https://github.com/rdma-book/mastering-rdma
```

The repository is organized by chapter, with each example designed to compile and run on any Linux system equipped with an RDMA-capable network adapter and the `rdma-core` user-space libraries. A `README` in the repository root provides detailed setup instructions, including how to use SoftRoCE (the software-based RoCE implementation in the Linux kernel) for development and testing on machines without RDMA hardware.

We recommend cloning the repository and building the examples as you read. RDMA is a topic best learned by doing -- reading about Queue Pairs is useful, but posting your first work request and watching it complete is when the pieces truly click.

Build requirements are minimal:

- Linux kernel 5.4 or later (earlier kernels work but may lack some features)
- `rdma-core` libraries (libibverbs, librdmacm)
- GCC or Clang with C11 support
- CMake 3.16 or later

Each chapter's code directory contains a `CMakeLists.txt` and can be built independently.

## Acknowledgments

*[To be completed in the final manuscript.]*

A book of this scope is never the work of a single person. I owe debts of gratitude to the engineers, researchers, and practitioners who shaped my understanding of RDMA over the years, and to the reviewers whose feedback made this book far better than it would have been otherwise.

---

*I hope this book gives you the same excitement I felt watching that first RDMA-powered key-value store in action. The technology is remarkable, the performance is real, and the journey to understanding it is deeply rewarding. Let's begin.*
