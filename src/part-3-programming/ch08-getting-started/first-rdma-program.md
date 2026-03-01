# 8.3 Your First RDMA Program

This section presents `hello_verbs.c`, a complete, compilable C program that walks through the fundamental resource allocation path of an RDMA application. It does not transfer data---that comes in Chapter 9. Its purpose is to exercise every core API call, demonstrate proper error handling, and produce output that confirms your environment is working correctly.

## What the Program Does

The program performs the following steps, in order:

1. Enumerates all RDMA devices and opens the first one (or a named one).
2. Queries and prints the device's capabilities.
3. Queries and prints port information, including the GID table.
4. Allocates a Protection Domain.
5. Creates a Completion Queue.
6. Allocates a buffer and registers it as a Memory Region.
7. Creates a Queue Pair (RC type).
8. Prints a summary of all allocated resources.
9. Destroys everything in reverse order.

## The Complete Program

The full source is available in `src/code/ch08-hello-verbs/hello_verbs.c`. Here is the annotated listing:

```c
/*
 * hello_verbs.c - First RDMA program
 *
 * Opens an RDMA device, allocates fundamental resources (PD, CQ, MR, QP),
 * queries device capabilities, and cleans up. No data transfer is performed.
 *
 * Build:  gcc -o hello_verbs hello_verbs.c -libverbs
 * Run:    ./hello_verbs [device_name]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <infiniband/verbs.h>

#define BUFFER_SIZE 4096

/* Print a fatal error message and exit */
#define DIE(fmt, ...) do {                                      \
    fprintf(stderr, "ERROR: " fmt "\n", ##__VA_ARGS__);         \
    exit(EXIT_FAILURE);                                         \
} while (0)

/*
 * Print device capabilities queried via ibv_query_device().
 */
static void print_device_caps(const struct ibv_device_attr *attr)
{
    printf("\n=== Device Capabilities ===\n");
    printf("  Firmware version:     %s\n", attr->fw_ver);
    printf("  Max QPs:              %d\n", attr->max_qp);
    printf("  Max QP WRs:           %d\n", attr->max_qp_wr);
    printf("  Max SGE per WR:       %d\n", attr->max_sge);
    printf("  Max CQs:              %d\n", attr->max_cq);
    printf("  Max CQE per CQ:       %d\n", attr->max_cqe);
    printf("  Max MRs:              %d\n", attr->max_mr);
    printf("  Max PDs:              %d\n", attr->max_pd);
    printf("  Max Multicast Groups: %d\n", attr->max_mcast_grp);
    printf("  Atomic capability:    %d\n", attr->atomic_cap);
    printf("  Max EE:               %d\n", attr->max_ee);
    printf("  Page size cap:        0x%" PRIx64 "\n", attr->page_size_cap);
}

/*
 * Print port information queried via ibv_query_port().
 */
static void print_port_info(const struct ibv_port_attr *port_attr,
                            uint8_t port_num)
{
    const char *state_str;
    switch (port_attr->state) {
    case IBV_PORT_DOWN:       state_str = "DOWN"; break;
    case IBV_PORT_INIT:       state_str = "INIT"; break;
    case IBV_PORT_ARMED:      state_str = "ARMED"; break;
    case IBV_PORT_ACTIVE:     state_str = "ACTIVE"; break;
    default:                  state_str = "UNKNOWN"; break;
    }

    const char *link_str;
    switch (port_attr->link_layer) {
    case IBV_LINK_LAYER_INFINIBAND:  link_str = "InfiniBand"; break;
    case IBV_LINK_LAYER_ETHERNET:    link_str = "Ethernet"; break;
    default:                         link_str = "Unknown"; break;
    }

    printf("\n=== Port %u Information ===\n", port_num);
    printf("  State:        %s\n", state_str);
    printf("  Max MTU:      %d\n", port_attr->max_mtu);
    printf("  Active MTU:   %d\n", port_attr->active_mtu);
    printf("  LID:          %u\n", port_attr->lid);
    printf("  Link layer:   %s\n", link_str);
    printf("  Max msg size: 0x%x\n", port_attr->max_msg_sz);
}

/*
 * Print GID table entries for a port.
 */
static void print_gid_table(struct ibv_context *ctx, uint8_t port_num,
                             int gid_tbl_len)
{
    printf("\n=== GID Table (Port %u) ===\n", port_num);
    for (int i = 0; i < gid_tbl_len && i < 8; i++) {
        union ibv_gid gid;
        if (ibv_query_gid(ctx, port_num, i, &gid)) {
            continue;
        }
        /* Skip all-zero GIDs */
        int all_zero = 1;
        for (int j = 0; j < 16; j++) {
            if (gid.raw[j] != 0) { all_zero = 0; break; }
        }
        if (all_zero) continue;

        printf("  GID[%2d]: %02x%02x:%02x%02x:%02x%02x:%02x%02x:"
               "%02x%02x:%02x%02x:%02x%02x:%02x%02x\n",
               i,
               gid.raw[0],  gid.raw[1],  gid.raw[2],  gid.raw[3],
               gid.raw[4],  gid.raw[5],  gid.raw[6],  gid.raw[7],
               gid.raw[8],  gid.raw[9],  gid.raw[10], gid.raw[11],
               gid.raw[12], gid.raw[13], gid.raw[14], gid.raw[15]);
    }
}

int main(int argc, char *argv[])
{
    const char *dev_name = argc > 1 ? argv[1] : NULL;

    /* ---------------------------------------------------------------
     * Step 1: Get device list and open a device
     * --------------------------------------------------------------- */
    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        DIE("ibv_get_device_list() failed: %s", strerror(errno));
    }

    printf("Found %d RDMA device(s):\n", num_devices);
    for (int i = 0; i < num_devices; i++) {
        printf("  %d: %s\n", i, ibv_get_device_name(dev_list[i]));
    }

    if (num_devices == 0) {
        ibv_free_device_list(dev_list);
        DIE("No RDMA devices found. Is the kernel module loaded?");
    }

    /* Select the requested device, or the first one */
    struct ibv_device *ib_dev = NULL;
    if (dev_name) {
        for (int i = 0; i < num_devices; i++) {
            if (strcmp(ibv_get_device_name(dev_list[i]), dev_name) == 0) {
                ib_dev = dev_list[i];
                break;
            }
        }
        if (!ib_dev) {
            ibv_free_device_list(dev_list);
            DIE("Device '%s' not found", dev_name);
        }
    } else {
        ib_dev = dev_list[0];
    }

    printf("\nOpening device: %s\n", ibv_get_device_name(ib_dev));

    struct ibv_context *ctx = ibv_open_device(ib_dev);
    if (!ctx) {
        ibv_free_device_list(dev_list);
        DIE("ibv_open_device() failed: %s", strerror(errno));
    }

    /* The device list can be freed after opening the device */
    ibv_free_device_list(dev_list);

    /* ---------------------------------------------------------------
     * Step 2: Query device capabilities
     * --------------------------------------------------------------- */
    struct ibv_device_attr dev_attr;
    if (ibv_query_device(ctx, &dev_attr)) {
        DIE("ibv_query_device() failed");
    }
    print_device_caps(&dev_attr);

    /* ---------------------------------------------------------------
     * Step 3: Query port information
     * --------------------------------------------------------------- */
    struct ibv_port_attr port_attr;
    uint8_t port_num = 1;  /* Ports are 1-indexed */
    if (ibv_query_port(ctx, port_num, &port_attr)) {
        DIE("ibv_query_port() failed");
    }
    print_port_info(&port_attr, port_num);
    print_gid_table(ctx, port_num, port_attr.gid_tbl_len);

    /* ---------------------------------------------------------------
     * Step 4: Allocate a Protection Domain
     * --------------------------------------------------------------- */
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd) {
        DIE("ibv_alloc_pd() failed: %s", strerror(errno));
    }
    printf("\nProtection Domain allocated successfully.\n");

    /* ---------------------------------------------------------------
     * Step 5: Create a Completion Queue
     * --------------------------------------------------------------- */
    int cq_size = 16;  /* Number of CQ entries */
    struct ibv_cq *cq = ibv_create_cq(ctx, cq_size, NULL, NULL, 0);
    if (!cq) {
        DIE("ibv_create_cq() failed: %s", strerror(errno));
    }
    printf("Completion Queue created: %d entries\n", cq_size);

    /* ---------------------------------------------------------------
     * Step 6: Allocate and register a Memory Region
     * --------------------------------------------------------------- */
    char *buf = malloc(BUFFER_SIZE);
    if (!buf) {
        DIE("malloc() failed");
    }
    memset(buf, 0, BUFFER_SIZE);

    struct ibv_mr *mr = ibv_reg_mr(pd, buf, BUFFER_SIZE,
                                   IBV_ACCESS_LOCAL_WRITE |
                                   IBV_ACCESS_REMOTE_WRITE |
                                   IBV_ACCESS_REMOTE_READ);
    if (!mr) {
        DIE("ibv_reg_mr() failed: %s", strerror(errno));
    }
    printf("Memory Region registered: addr=%p, length=%d, lkey=0x%08x, rkey=0x%08x\n",
           buf, BUFFER_SIZE, mr->lkey, mr->rkey);

    /* ---------------------------------------------------------------
     * Step 7: Create a Queue Pair (RC type)
     * --------------------------------------------------------------- */
    struct ibv_qp_init_attr qp_init_attr = {
        .send_cq = cq,
        .recv_cq = cq,
        .cap = {
            .max_send_wr  = 16,
            .max_recv_wr  = 16,
            .max_send_sge = 1,
            .max_recv_sge = 1,
        },
        .qp_type = IBV_QPT_RC,
        .sq_sig_all = 1,  /* Signal completion for every send WR */
    };

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        DIE("ibv_create_qp() failed: %s", strerror(errno));
    }
    printf("Queue Pair created: qp_num=%u, type=RC\n", qp->qp_num);

    /* Note: The QP is in RESET state. To send/receive data, you would
     * transition it through INIT -> RTR -> RTS using ibv_modify_qp().
     * We do not do that here---data transfer is covered in Chapter 9. */

    /* ---------------------------------------------------------------
     * Step 8: Print summary
     * --------------------------------------------------------------- */
    printf("\n=== Resource Summary ===\n");
    printf("  Device:    %s\n", ibv_get_device_name(ctx->device));
    printf("  Port:      %u (state: %s)\n", port_num,
           port_attr.state == IBV_PORT_ACTIVE ? "ACTIVE" : "NOT ACTIVE");
    printf("  PD:        allocated\n");
    printf("  CQ:        %d entries\n", cq_size);
    printf("  MR:        %d bytes, lkey=0x%08x, rkey=0x%08x\n",
           BUFFER_SIZE, mr->lkey, mr->rkey);
    printf("  QP:        num=%u, type=RC, state=RESET\n", qp->qp_num);

    /* ---------------------------------------------------------------
     * Step 9: Clean up (reverse order of creation)
     * --------------------------------------------------------------- */
    printf("\nCleaning up...\n");

    if (ibv_destroy_qp(qp)) {
        fprintf(stderr, "Warning: ibv_destroy_qp() failed: %s\n",
                strerror(errno));
    }

    if (ibv_dereg_mr(mr)) {
        fprintf(stderr, "Warning: ibv_dereg_mr() failed: %s\n",
                strerror(errno));
    }

    free(buf);

    if (ibv_destroy_cq(cq)) {
        fprintf(stderr, "Warning: ibv_destroy_cq() failed: %s\n",
                strerror(errno));
    }

    if (ibv_dealloc_pd(pd)) {
        fprintf(stderr, "Warning: ibv_dealloc_pd() failed: %s\n",
                strerror(errno));
    }

    if (ibv_close_device(ctx)) {
        fprintf(stderr, "Warning: ibv_close_device() failed: %s\n",
                strerror(errno));
    }

    printf("All resources freed. Done.\n");
    return 0;
}
```

## Line-by-Line Walkthrough

### Step 1: Device Discovery and Opening

```c
struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
```

`ibv_get_device_list()` returns a NULL-terminated array of pointers to `ibv_device` structures. Each entry represents one RDMA device in the system. The `num_devices` output parameter tells you how many there are. You must call `ibv_free_device_list()` when you are done with the array---but only after you have opened any devices you intend to use.

```c
struct ibv_context *ctx = ibv_open_device(ib_dev);
```

`ibv_open_device()` opens a specific device and returns a context. This context is the handle through which all subsequent API calls interact with the hardware. Internally, it opens `/dev/infiniband/uverbsN` and sets up memory-mapped regions for fast-path operations.

After opening the device, we immediately free the device list. The open context keeps its own reference to the device; the list is no longer needed.

### Step 2: Querying Device Capabilities

```c
ibv_query_device(ctx, &dev_attr);
```

This retrieves the hardware capabilities of the device: how many QPs, CQs, and MRs it supports; the maximum number of work requests per QP; scatter/gather limits; and atomic operation support. These limits are critical for resource planning. For example, if you need to register 10,000 memory regions and the device supports only 8,192, you need a different design.

### Step 3: Querying Port Information

```c
ibv_query_port(ctx, port_num, &port_attr);
```

Ports are 1-indexed (port 1 is the first port). The port attributes tell you the link state, MTU, LID (for InfiniBand), and link layer type (InfiniBand or Ethernet). For RoCE devices, the port state must be ACTIVE for data transfer to work. If it shows DOWN or INIT, the underlying network interface is not up.

The GID table query deserves special attention for RoCE. Unlike InfiniBand, which uses LIDs for addressing, RoCE uses GIDs derived from IP addresses. Selecting the correct GID index is essential for establishing connections. Section 8.4 covers this in detail.

### Step 4: Protection Domain

```c
struct ibv_pd *pd = ibv_alloc_pd(ctx);
```

The PD is the simplest resource to create---it takes only the context as a parameter. Despite its simplicity, it plays a crucial security role: it ensures that only QPs and MRs within the same PD can interact. In multi-tenant systems, separate PDs enforce isolation between tenants.

### Step 5: Completion Queue

```c
struct ibv_cq *cq = ibv_create_cq(ctx, cq_size, NULL, NULL, 0);
```

The parameters are: context, minimum number of entries, a user-defined context pointer (returned in completion events), a completion channel (for event-driven notification, covered in Chapter 9), and a completion vector (for interrupt affinity). We use NULL for the channel and 0 for the vector because this program uses polling, not event notification.

### Step 6: Memory Region

```c
struct ibv_mr *mr = ibv_reg_mr(pd, buf, BUFFER_SIZE,
                               IBV_ACCESS_LOCAL_WRITE |
                               IBV_ACCESS_REMOTE_WRITE |
                               IBV_ACCESS_REMOTE_READ);
```

Memory registration pins the virtual pages in physical memory and programs the NIC's translation tables. The access flags determine what operations are permitted:

- `IBV_ACCESS_LOCAL_WRITE`: The local NIC can write to this buffer (required for receive operations).
- `IBV_ACCESS_REMOTE_WRITE`: Remote peers can RDMA Write to this buffer.
- `IBV_ACCESS_REMOTE_READ`: Remote peers can RDMA Read from this buffer.

<div class="warning">

`IBV_ACCESS_LOCAL_WRITE` must be set whenever you set `IBV_ACCESS_REMOTE_WRITE` or `IBV_ACCESS_REMOTE_READ`. This is a hardware requirement. Omitting it will cause `ibv_reg_mr()` to fail.

</div>

### Step 7: Queue Pair

```c
struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
```

The `ibv_qp_init_attr` structure specifies the QP type, the associated send and receive CQs, and the capacity of the send and receive queues. Setting `sq_sig_all = 1` means every send work request generates a completion, regardless of whether `IBV_SEND_SIGNALED` is set. This simplifies initial development at the cost of higher completion overhead. In performance-critical code, you would set this to 0 and selectively signal only some operations (see Chapter 12).

The QP is created in the RESET state. To actually use it for data transfer, you must transition it through INIT, RTR (Ready to Receive), and RTS (Ready to Send) using `ibv_modify_qp()`. This is covered in Chapter 9.

### Step 9: Clean Teardown

Cleanup proceeds in strict reverse order: QP first (because it references the CQ and PD), then MR (references PD), then CQ, then PD, and finally the device context. Each destroy call is checked for errors, though in practice they should not fail if the ordering is correct and no operations are outstanding.

The `free(buf)` call comes after `ibv_dereg_mr()` because the buffer must remain allocated (and pinned) for as long as it is registered. Freeing the buffer before deregistering it is undefined behavior.

## Expected Output

When run against a Soft-RoCE device, the output looks similar to:

```
Found 1 RDMA device(s):
  0: rxe0

Opening device: rxe0

=== Device Capabilities ===
  Firmware version:
  Max QPs:              32
  Max QP WRs:           16384
  Max SGE per WR:       32
  Max CQs:              1024
  Max CQE per CQ:       1048575
  Max MRs:              2048
  Page size cap:        0xfffff000

=== Port 1 Information ===
  State:        ACTIVE
  Max MTU:      5
  Active MTU:   3
  LID:          0
  Link layer:   Ethernet

=== GID Table (Port 1) ===
  GID[ 0]: fe80:0000:0000:0000:xxxx:xxxx:xxxx:xxxx

Protection Domain allocated successfully.
Completion Queue created: 16 entries
Memory Region registered: addr=0x..., length=4096, lkey=0x00000100, rkey=0x00000100
Queue Pair created: qp_num=17, type=RC

=== Resource Summary ===
  Device:    rxe0
  Port:      1 (state: ACTIVE)
  PD:        allocated
  CQ:        16 entries
  MR:        4096 bytes, lkey=0x00000100, rkey=0x00000100
  QP:        num=17, type=RC, state=RESET

Cleaning up...
All resources freed. Done.
```

The exact numbers will vary depending on your device. Hardware devices will report significantly larger limits than Soft-RoCE. If the program runs to completion without errors, your RDMA environment is correctly configured, and you are ready to proceed to data transfer in Chapter 9.
