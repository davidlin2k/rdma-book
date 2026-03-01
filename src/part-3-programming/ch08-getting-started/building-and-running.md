# 8.5 Building and Running

With the API understood and the first program written, the remaining practical concern is how to build, link, and run RDMA programs reliably. This section covers build systems (Make and CMake), library dependencies, useful compiler flags, environment variables that affect runtime behavior, and common pitfalls.

## A Simple Makefile

For small programs and quick experiments, a Makefile is the most straightforward build system:

```makefile
# Makefile for RDMA programs
CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -O2 -g -std=c11
LDFLAGS = -libverbs

# Add librdmacm if using the Connection Manager
# LDFLAGS += -lrdmacm

TARGETS = hello_verbs

all: $(TARGETS)

hello_verbs: hello_verbs.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
```

The critical linking flag is `-libverbs`. Despite the `ib` prefix, this library works with all RDMA transports, including RoCE and iWARP. The name is a historical artifact from when InfiniBand was the only RDMA transport on Linux.

If you are writing programs that use the RDMA Connection Manager (covered in Chapter 10), add `-lrdmacm`. If you use threading, add `-pthread`.

### Using pkg-config

For more portable builds, use `pkg-config` to discover the correct flags:

```makefile
CC      = gcc
CFLAGS  = -Wall -Wextra -Werror -O2 -g -std=c11
CFLAGS += $(shell pkg-config --cflags libibverbs)
LDFLAGS = $(shell pkg-config --libs libibverbs)

hello_verbs: hello_verbs.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
```

`pkg-config` reads `.pc` files installed by rdma-core and emits the correct `-I`, `-L`, and `-l` flags. This is particularly useful when rdma-core is installed in a non-standard location (e.g., `/opt/rdma-core`).

To verify that pkg-config can find libibverbs:

```bash
$ pkg-config --modversion libibverbs
43.0

$ pkg-config --cflags --libs libibverbs
-I/usr/include  -libverbs

$ pkg-config --cflags --libs librdmacm
-I/usr/include  -lrdmacm
```

If pkg-config cannot find the `.pc` files, set `PKG_CONFIG_PATH`:

```bash
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
```

## CMake Build System

For larger projects, CMake provides better dependency management, out-of-source builds, and IDE integration:

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(rdma_examples C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
add_compile_options(-Wall -Wextra -Werror)

# Find libibverbs using pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(IBVERBS REQUIRED libibverbs)
pkg_check_modules(RDMACM librdmacm)

# Include directories
include_directories(${IBVERBS_INCLUDE_DIRS})
if(RDMACM_FOUND)
    include_directories(${RDMACM_INCLUDE_DIRS})
endif()

# hello_verbs executable
add_executable(hello_verbs hello_verbs.c)
target_link_libraries(hello_verbs ${IBVERBS_LIBRARIES})

# Example with RDMA CM (for Chapter 10+)
# add_executable(rdma_client rdma_client.c)
# target_link_libraries(rdma_client ${IBVERBS_LIBRARIES} ${RDMACM_LIBRARIES})
```

Build with:

```bash
mkdir build && cd build
cmake ..
make
```

An alternative to `pkg_check_modules` is to use CMake's `find_library` directly, which avoids the pkg-config dependency:

```cmake
find_library(IBVERBS_LIB ibverbs REQUIRED)
find_path(IBVERBS_INCLUDE infiniband/verbs.h REQUIRED)

add_executable(hello_verbs hello_verbs.c)
target_include_directories(hello_verbs PRIVATE ${IBVERBS_INCLUDE})
target_link_libraries(hello_verbs ${IBVERBS_LIB})
```

## Required Libraries

A summary of the libraries you may need to link against:

| Library | Package (Debian) | Flag | Purpose |
|---------|-----------------|------|---------|
| `libibverbs` | `libibverbs-dev` | `-libverbs` | Core verbs API |
| `librdmacm` | `librdmacm-dev` | `-lrdmacm` | Connection Manager |
| `libibumad` | `libibumad-dev` | `-libumad` | User-space MAD access (diagnostics) |
| `libmlx5` | `libmlx5-dev` | `-lmlx5` | Mellanox/NVIDIA direct verbs |
| `libpthread` | (system) | `-lpthread` | Thread support |

For the programs in this chapter, only `libibverbs` is needed. Programs from Chapter 10 onward will also require `librdmacm`.

## Compiler Flags and Warnings

The following flags are recommended for RDMA development:

```bash
# Standard warnings
-Wall -Wextra -Werror

# C standard (C11 provides stdatomic.h, useful for concurrent RDMA code)
-std=c11

# Optimization with debug info (for profiling and debugging)
-O2 -g

# Additional useful warnings
-Wshadow            # Warn about variable shadowing
-Wconversion        # Warn about implicit type conversions
-Wno-sign-conversion  # ibv_ APIs use mixed signed/unsigned frequently
-Wformat=2          # Extra format string checks
```

<div class="tip">

During development, compile with `-O0 -g` for easier debugging. The `-O2` or `-O3` flags are important for performance measurements, but they can make stepping through code in GDB harder because the compiler reorders and inlines aggressively.

</div>

## Running with Soft-RoCE

Once built, running the program against Soft-RoCE is straightforward:

```bash
# Ensure the rxe device is created
sudo modprobe rdma_rxe
sudo rdma link add rxe0 type rxe netdev eth0

# Run the program
./hello_verbs rxe0
```

If you omit the device name argument, the program selects the first available device. On a system with only Soft-RoCE configured, that will be `rxe0`.

For programs that require two endpoints (starting in Chapter 9), open two terminal windows or SSH sessions:

```bash
# Terminal 1 (server)
./rc_pingpong -d rxe0 -g 0

# Terminal 2 (client)
./rc_pingpong -d rxe0 -g 0 192.168.1.10
```

If testing on a single machine with network namespaces (as described in Section 8.1), use `ip netns exec` to run each side in its namespace:

```bash
sudo ip netns exec ns1 ./rc_pingpong -d rxe0 -g 0
sudo ip netns exec ns2 ./rc_pingpong -d rxe0 -g 0 10.0.0.1
```

## Common Build Errors and Fixes

### `fatal error: infiniband/verbs.h: No such file or directory`

The development headers are not installed. Install `libibverbs-dev` (Debian/Ubuntu) or `rdma-core-devel` (RHEL/Fedora).

### `undefined reference to 'ibv_get_device_list'`

The linker cannot find `libibverbs`. Ensure `-libverbs` appears after the source files on the command line (GCC resolves symbols left-to-right):

```bash
# Wrong (linker flag before source)
gcc -libverbs -o hello_verbs hello_verbs.c

# Correct (linker flag after source)
gcc -o hello_verbs hello_verbs.c -libverbs
```

### `error: 'IBV_ACCESS_RELAXED_ORDERING' undeclared`

Your rdma-core headers are too old for the feature you are trying to use. Either upgrade rdma-core or guard the code with a version check:

```c
#ifdef IBV_ACCESS_RELAXED_ORDERING
    access_flags |= IBV_ACCESS_RELAXED_ORDERING;
#endif
```

### `cannot find -libverbs`

The shared library is installed but the linker symlink (`libibverbs.so` without a version suffix) is missing. This symlink is provided by the `-dev` package. Install it, or create the symlink manually:

```bash
sudo ln -s /usr/lib/x86_64-linux-gnu/libibverbs.so.1 \
           /usr/lib/x86_64-linux-gnu/libibverbs.so
```

### Program compiles but `ibv_get_device_list` returns NULL

This is a runtime issue, not a build issue. The kernel module is not loaded, or no RDMA devices have been created. Follow the verification steps in Section 8.1.

## Environment Variables

Several environment variables influence the behavior of libibverbs and the RDMA stack at runtime:

### RDMAV_FORK_SAFE / IBV_FORK_SAFE

```bash
export RDMAV_FORK_SAFE=1
# or equivalently:
export IBV_FORK_SAFE=1
```

When set, libibverbs calls `ibv_fork_init()` automatically during library initialization. This configures the memory registration system to be safe across `fork()` calls by marking registered memory pages with `MADV_DONTFORK`, preventing the child process from inheriting (and potentially corrupting) DMA-mapped memory.

<div class="warning">

If your application (or any library it uses) calls `fork()`, you **must** either set this environment variable or call `ibv_fork_init()` before any memory registration. Without it, the child process may inherit page table entries for DMA-mapped memory, leading to data corruption or crashes when the NIC writes to a page that the child also maps.

However, enabling fork safety has a performance cost: it changes the memory registration path to use `madvise()` on every page. For applications that never fork, leave it disabled.

</div>

### RDMAV_DEBUG

```bash
export RDMAV_DEBUG=1
```

Enables verbose debug output from libibverbs. This prints information about provider library loading, device discovery, and internal operations. Useful when `ibv_get_device_list()` returns no devices despite the kernel module being loaded.

### MLX5_DEBUG_MASK (Mellanox/NVIDIA specific)

```bash
export MLX5_DEBUG_MASK=0xFFFF
```

Enables debug output from the mlx5 provider library. Useful for diagnosing hardware-specific issues on ConnectX NICs.

### IBV_PROVLIB_PATH

```bash
export IBV_PROVLIB_PATH=/opt/rdma-core/lib
```

Overrides the default search path for provider libraries. Useful when testing a custom build of rdma-core without installing it system-wide.

## Debugging RDMA Programs

### Using GDB

RDMA programs can be debugged with GDB like any other C program. A few tips:

```bash
# Compile with debug info and no optimization
gcc -O0 -g -o hello_verbs hello_verbs.c -libverbs

# Run under GDB
gdb ./hello_verbs
(gdb) break main
(gdb) run rxe0
```

Useful breakpoints:
- `ibv_create_qp` --- to inspect QP creation parameters.
- `ibv_post_send` --- to inspect work requests before posting.
- `ibv_poll_cq` --- to inspect completions.

### Using strace

`strace` can reveal what system calls libibverbs is making, which is useful for diagnosing permission errors and device access issues:

```bash
strace -e trace=open,openat,ioctl,mmap ./hello_verbs rxe0 2>&1 | head -50
```

You will see it opening `/dev/infiniband/uverbs0`, performing `mmap` calls for doorbell pages, and issuing `ioctl` calls for resource allocation.

### Using ibv_devinfo for Sanity Checks

Before debugging your program, always verify the environment with `ibv_devinfo`:

```bash
ibv_devinfo -d rxe0 -v
```

This exercises the same API calls your program uses. If `ibv_devinfo` fails, the problem is in the environment, not in your code.

### Kernel Logs

The kernel RDMA subsystem logs useful information to `dmesg`:

```bash
# Show recent RDMA-related kernel messages
dmesg | grep -i -E "rdma|rxe|ib_|mlx"
```

Module load errors, device creation failures, and QP error events all appear here.

## Summary Checklist

Before moving to Chapter 9, verify the following:

- [ ] rdma-core is installed, including development headers.
- [ ] A Soft-RoCE (or SoftiWARP) device is created and in ACTIVE state.
- [ ] `ibv_devices` lists your device.
- [ ] `ibv_devinfo -d <device>` shows detailed information.
- [ ] `hello_verbs` compiles, runs, and prints resource information.
- [ ] If you have a two-node setup, `ibv_rc_pingpong` succeeds between the nodes.
- [ ] Your locked memory limit (`ulimit -l`) is set high enough (or unlimited).

With the development environment verified and the fundamental API patterns mastered, you are ready to send your first RDMA message in Chapter 9.
