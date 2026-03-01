# Chapter 8: Getting Started

Up to this point, we have explored RDMA from a conceptual and architectural perspective. You understand why kernel bypass and zero-copy matter. You know how Queue Pairs, Completion Queues, and Memory Regions work together to form the verbs abstraction. You have studied the QP state machine, connection management, and the semantics of RDMA operations. Now it is time to write code.

This chapter marks the transition from theory to practice. By the end of it, you will have a working RDMA development environment, a mental model of the libibverbs API surface, and a complete program that opens a device, allocates resources, and queries hardware capabilities. More importantly, you will have the foundation to tackle the progressively more complex programs in Chapters 9 through 11.

## What You Will Build

The central artifact of this chapter is `hello_verbs.c`, a self-contained C program that exercises the fundamental resource allocation path of every RDMA application:

1. **Discover** available RDMA devices and open one.
2. **Allocate** a Protection Domain to isolate resources.
3. **Create** a Completion Queue for operation completions.
4. **Register** a Memory Region so the NIC can perform DMA.
5. **Create** a Queue Pair and bring it to a usable state.
6. **Query** device and port capabilities to understand hardware limits.
7. **Clean up** every resource in the correct order.

This program does not transfer any data---that comes in Chapter 9. Its purpose is to ensure your environment is correctly configured and to build familiarity with the API patterns you will use in every RDMA application you ever write.

## Chapter Structure

**Section 8.1: Environment Setup** walks you through installing rdma-core, configuring software RDMA transports (Soft-RoCE and SoftiWARP), and verifying that everything works with the standard diagnostic tools. You do not need physical InfiniBand or RoCE hardware to follow along; a single Linux machine with a loopback configuration is sufficient, though a two-node setup will let you run the ping-pong tests.

**Section 8.2: libibverbs API Overview** provides a map of the API before you start using it. You will learn the naming conventions, the object lifecycle pattern, the key data structures, and the error handling conventions. This section is reference material you will return to throughout the rest of the book.

**Section 8.3: Your First RDMA Program** presents the complete, annotated `hello_verbs.c` listing. Every API call is explained, every error path is handled, and the resource teardown follows the correct reverse-order pattern.

**Section 8.4: Device and Port Discovery** dives deeper into the device enumeration and capability query APIs. You will learn how to interpret port states, select the right GID index for RoCEv2, and write utility code that generalizes across different hardware.

**Section 8.5: Building and Running** covers the practical mechanics of compiling RDMA programs with both Make and CMake, the libraries you need to link against, useful environment variables, and common pitfalls.

## Prerequisites

You should have:

- A Linux system (bare metal, VM, or container) running a kernel version 4.9 or later. Kernel 5.x or 6.x is recommended.
- Root or sudo access for loading kernel modules and configuring RDMA devices.
- A C compiler (GCC or Clang) and basic familiarity with C programming, including pointers, structs, and standard library functions.
- Familiarity with the concepts from Chapters 4 through 7, particularly Queue Pairs, Completion Queues, Memory Regions, and the QP state machine.

Let us begin by setting up the development environment.
