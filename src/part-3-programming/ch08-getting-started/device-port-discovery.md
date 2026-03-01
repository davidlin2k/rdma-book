# 8.4 Device and Port Discovery

Every RDMA application begins with the same question: which devices are available, and what can they do? The libibverbs API provides a set of query functions that answer this question comprehensively. This section explores these functions in detail and develops utility code that you can reuse across all your RDMA programs.

## Enumerating Devices

### ibv_get_device_list()

The entry point to device discovery is `ibv_get_device_list()`:

```c
struct ibv_device **ibv_get_device_list(int *num_devices);
```

It returns a NULL-terminated array of pointers to `ibv_device` structures. Each structure represents one RDMA device visible to user space. The `num_devices` output parameter receives the count.

The returned array is allocated by the library and must be freed with `ibv_free_device_list()`. However, the individual `ibv_device` pointers within the array remain valid until `ibv_free_device_list()` is called---so you must open any device you want to use before freeing the list.

```c
int num_devices;
struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
if (!dev_list) {
    perror("ibv_get_device_list");
    return -1;
}

for (int i = 0; i < num_devices; i++) {
    printf("Device %d: %s\n", i, ibv_get_device_name(dev_list[i]));
}

/* Open a device before freeing the list */
struct ibv_context *ctx = ibv_open_device(dev_list[0]);
ibv_free_device_list(dev_list);
```

### ibv_get_device_name() and ibv_get_device_guid()

Two accessors provide basic device identity:

```c
const char *ibv_get_device_name(struct ibv_device *device);
__be64 ibv_get_device_guid(struct ibv_device *device);
```

The device name is a short string like `mlx5_0`, `rxe0`, or `siw0`. It matches the name shown by `ibv_devices` and `rdma link show`. The GUID (Globally Unique Identifier) is a 64-bit value assigned by the manufacturer, useful for identifying a specific physical device across reboots or name changes.

```c
printf("Name: %s\n", ibv_get_device_name(dev));
printf("GUID: %016" PRIx64 "\n", ibv_get_device_guid(dev));
```

### ibv_open_device()

```c
struct ibv_context *ibv_open_device(struct ibv_device *device);
```

Opening a device creates a context that serves as the handle for all subsequent operations. Internally, this opens the `/dev/infiniband/uverbsN` character device, maps shared memory pages for the fast-path (doorbell) operations, and initializes provider-specific state.

You can open the same device multiple times. Each context is independent: resources created through one context are not visible through another (unless they share a PD via cross-context sharing mechanisms, which are rare).

## Querying Device Capabilities

### ibv_query_device()

```c
int ibv_query_device(struct ibv_context *context,
                     struct ibv_device_attr *device_attr);
```

This function fills a `struct ibv_device_attr` with the device's capabilities. The most important fields are:

```c
struct ibv_device_attr {
    char            fw_ver[64];       /* Firmware version string */
    uint64_t        node_guid;        /* Node GUID */
    uint64_t        sys_image_guid;   /* System image GUID */
    uint64_t        max_mr_size;      /* Largest MR size */
    uint64_t        page_size_cap;    /* Supported page sizes (bitmask) */
    uint32_t        vendor_id;        /* IEEE vendor ID */
    uint32_t        vendor_part_id;   /* Vendor part number */
    uint32_t        hw_ver;           /* Hardware version */
    int             max_qp;           /* Max Queue Pairs */
    int             max_qp_wr;        /* Max WRs per QP */
    int             max_sge;          /* Max SGE per WR */
    int             max_sge_rd;       /* Max SGE per RDMA Read WR */
    int             max_cq;           /* Max Completion Queues */
    int             max_cqe;          /* Max CQE per CQ */
    int             max_mr;           /* Max Memory Regions */
    int             max_pd;           /* Max Protection Domains */
    int             max_qp_rd_atom;   /* Max outstanding RDMA Reads/Atomics */
    int             max_ee_rd_atom;
    int             max_res_rd_atom;
    int             max_qp_init_rd_atom;
    int             max_ee_init_rd_atom;
    enum ibv_atomic_cap atomic_cap;   /* Atomic operation support level */
    int             max_ee;
    int             max_rdd;
    int             max_mw;           /* Max Memory Windows */
    int             max_raw_ipv6_qp;
    int             max_raw_ethy_qp;
    int             max_mcast_grp;    /* Max multicast groups */
    int             max_mcast_qp_attach;
    int             max_total_mcast_qp_attach;
    int             max_ah;           /* Max Address Handles */
    int             max_srq;          /* Max Shared Receive Queues */
    int             max_srq_wr;       /* Max WRs per SRQ */
    int             max_srq_sge;      /* Max SGE per SRQ WR */
    uint16_t        max_pkeys;
    /* ... additional fields ... */
};
```

Several of these fields deserve careful attention:

**`max_qp_wr`**: The maximum number of work requests that can be posted to any single QP. If you need deeper queues, you must redesign your application. Typical hardware values range from 16,384 to 32,768. Soft-RoCE reports 16,384.

**`max_sge`**: The maximum number of scatter/gather entries per work request. Each SGE points to a separate buffer. For simple applications that use a single contiguous buffer per operation, 1 is sufficient. High-performance applications may use multiple SGEs to gather data from non-contiguous memory.

**`max_qp_rd_atom`** and **`max_qp_init_rd_atom`**: These control how many RDMA Read or Atomic operations can be in flight simultaneously on a single QP. The responder side advertises `max_qp_rd_atom` (how many it can service), and the initiator side uses `max_qp_init_rd_atom` (how many it can issue). These values must be correctly propagated during connection setup, as covered in Chapter 7.

**`atomic_cap`**: Three possible values:
- `IBV_ATOMIC_NONE`: No atomic operations supported.
- `IBV_ATOMIC_HCA`: Atomic operations are guaranteed to be atomic with respect to other RDMA operations on the same NIC, but not with respect to CPU memory operations.
- `IBV_ATOMIC_GLOB`: Atomic operations are globally atomic---atomic with respect to both other RDMA operations and CPU memory accesses.

### ibv_query_device_ex()

The extended query function provides access to newer capabilities not present in the original `ibv_device_attr`:

```c
int ibv_query_device_ex(struct ibv_context *context,
                        const struct ibv_query_device_ex_input *input,
                        struct ibv_device_attr_ex *attr);
```

The extended attributes include fields for TSO (TCP Segmentation Offload) support, RSS (Receive Side Scaling) capabilities, completion timestamp support, and other modern features. For basic applications, `ibv_query_device()` is sufficient.

## Querying Port Information

### ibv_query_port()

```c
int ibv_query_port(struct ibv_context *context,
                   uint8_t port_num,
                   struct ibv_port_attr *port_attr);
```

Port numbers are 1-indexed. Most RDMA devices have a single port (port 1), but dual-port NICs and InfiniBand switches have more. The number of ports is available in `ibv_device_attr.phys_port_cnt`.

The key fields in `ibv_port_attr`:

```c
struct ibv_port_attr {
    enum ibv_port_state     state;          /* DOWN, INIT, ARMED, ACTIVE */
    enum ibv_mtu            max_mtu;        /* Maximum supported MTU */
    enum ibv_mtu            active_mtu;     /* Currently active MTU */
    int                     gid_tbl_len;    /* Size of GID table */
    uint32_t                port_cap_flags; /* Capability flags */
    uint32_t                max_msg_sz;     /* Max message size */
    uint16_t                pkey_tbl_len;   /* Size of partition key table */
    uint16_t                lid;            /* Local Identifier (IB only) */
    uint16_t                sm_lid;         /* Subnet Manager LID (IB) */
    uint8_t                 lmc;            /* LID Mask Count */
    uint8_t                 max_vl_num;     /* Max virtual lanes */
    uint8_t                 active_width;   /* Active link width */
    uint8_t                 active_speed;   /* Active link speed */
    uint8_t                 phys_state;     /* Physical port state */
    uint8_t                 link_layer;     /* IB or Ethernet */
};
```

### Port States

The port state tells you whether the device is ready for data transfer:

| State | Value | Meaning |
|-------|-------|---------|
| `IBV_PORT_DOWN` | 1 | The physical link is down. No traffic is possible. |
| `IBV_PORT_INIT` | 2 | The port is initialized but not yet configured by the Subnet Manager (IB) or the link is not fully negotiated. |
| `IBV_PORT_ARMED` | 3 | The port is configured but not yet activated for traffic. Rare to see in practice. |
| `IBV_PORT_ACTIVE` | 4 | The port is fully operational. Data transfer is possible. |

For RoCE devices (Soft-RoCE or hardware RoCE), the port state mirrors the underlying Ethernet interface state. If the Ethernet interface is up and has an IP address, the RDMA port will be ACTIVE. If the interface is down, the port will be DOWN.

### Link Layer

The `link_layer` field is critical for determining how to address the remote end:

```c
switch (port_attr.link_layer) {
case IBV_LINK_LAYER_INFINIBAND:
    printf("InfiniBand: use LID %u for addressing\n", port_attr.lid);
    break;
case IBV_LINK_LAYER_ETHERNET:
    printf("Ethernet (RoCE/iWARP): use GID for addressing\n");
    break;
default:
    printf("Unknown link layer\n");
    break;
}
```

InfiniBand uses LID (Local Identifier) for subnet addressing. RoCE and iWARP use GIDs (derived from IP addresses). Your connection establishment code must handle both cases.

## The GID Table

### ibv_query_gid()

```c
int ibv_query_gid(struct ibv_context *context,
                  uint8_t port_num,
                  int index,
                  union ibv_gid *gid);
```

The GID (Global Identifier) table is an array of 128-bit addresses associated with each port. For InfiniBand, GIDs are formed from the port GUID and the subnet prefix. For RoCE, GIDs are derived from the IP addresses assigned to the underlying network interface.

Each entry in the GID table corresponds to a different address or address type. The GID index you choose when establishing a connection determines the network path that will be used.

### RoCEv1 vs RoCEv2 GID Types

For RoCE devices, the GID table contains entries of different types, and selecting the correct one is essential:

```c
/*
 * Enumerate GID table and identify GID types.
 * This requires the extended GID query, available in newer rdma-core versions.
 */
#include <infiniband/verbs.h>

void enumerate_gids(struct ibv_context *ctx, uint8_t port_num, int gid_tbl_len)
{
    for (int i = 0; i < gid_tbl_len; i++) {
        union ibv_gid gid;
        if (ibv_query_gid(ctx, port_num, i, &gid))
            continue;

        /* Skip zero GIDs */
        uint64_t subnet = 0, ifid = 0;
        memcpy(&subnet, gid.raw, 8);
        memcpy(&ifid, gid.raw + 8, 8);
        if (subnet == 0 && ifid == 0)
            continue;

        /* Determine GID type by examining the prefix */
        const char *type_str;
        if (gid.raw[0] == 0xfe && gid.raw[1] == 0x80) {
            type_str = "Link-local (RoCEv1)";
        } else if (gid.raw[0] == 0x00 && gid.raw[1] == 0x00 &&
                   gid.raw[10] == 0xff && gid.raw[11] == 0xff) {
            type_str = "IPv4-mapped (RoCEv2)";
        } else {
            type_str = "IPv6 (RoCEv2)";
        }

        printf("GID[%2d] type=%-24s  ", i, type_str);
        for (int j = 0; j < 16; j++) {
            printf("%02x", gid.raw[j]);
            if (j % 2 == 1 && j < 15) printf(":");
        }
        printf("\n");
    }
}
```

<div class="note">

On modern rdma-core, you can also query the GID type explicitly using the sysfs interface:

```bash
cat /sys/class/infiniband/rxe0/ports/1/gids/0
cat /sys/class/infiniband/rxe0/ports/1/gid_attrs/types/0
```

The type will be one of `IB/RoCE v1`, `RoCE v2`, or `IB/RoCE v1` depending on how the GID was formed.

</div>

### Choosing the Right GID Index

For **RoCEv2** (the most common deployment), you want a GID index whose type is `RoCE v2`. These are formed from IPv4-mapped or IPv6 addresses and carry the information needed for UDP/IP routing.

A practical strategy for GID selection:

1. Iterate through all GID table entries.
2. Skip entries that are all zeros.
3. For IPv4-based networks, look for IPv4-mapped entries (prefix `::ffff:x.x.x.x`).
4. For IPv6 networks, look for global unicast entries (prefix other than `fe80::`).
5. Avoid link-local entries (`fe80::`) for RoCEv2, as they correspond to RoCEv1 behavior.

```c
/*
 * Find a suitable GID index for RoCEv2.
 * Returns the index, or -1 if none found.
 */
int find_rocev2_gid_index(struct ibv_context *ctx, uint8_t port_num,
                          int gid_tbl_len)
{
    for (int i = 0; i < gid_tbl_len; i++) {
        union ibv_gid gid;
        if (ibv_query_gid(ctx, port_num, i, &gid))
            continue;

        /* Skip zero GIDs */
        int all_zero = 1;
        for (int j = 0; j < 16; j++) {
            if (gid.raw[j] != 0) { all_zero = 0; break; }
        }
        if (all_zero) continue;

        /* Skip link-local (fe80::) -- these are RoCEv1 */
        if (gid.raw[0] == 0xfe && gid.raw[1] == 0x80)
            continue;

        /* This is a routable GID (RoCEv2 IPv4-mapped or IPv6 global) */
        return i;
    }
    return -1;
}
```

<div class="warning">

GID index selection is one of the most common sources of "silent" connection failures in RoCE. If you use a RoCEv1 GID index on a network that expects RoCEv2 (or vice versa), the connection will time out with no useful error message. When debugging connection issues, always verify the GID type and ensure both sides are using compatible GID indices.

</div>

## Practical Device Enumeration Utility

The following function combines all the discovery APIs into a single reusable utility that prints everything you need to know about the RDMA devices on a system:

```c
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <infiniband/verbs.h>

void enumerate_rdma_devices(void)
{
    int num_devices;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        perror("ibv_get_device_list");
        return;
    }

    printf("Found %d RDMA device(s)\n\n", num_devices);

    for (int d = 0; d < num_devices; d++) {
        struct ibv_device *dev = dev_list[d];
        printf("────────────────────────────────────\n");
        printf("Device %d: %s\n", d, ibv_get_device_name(dev));
        printf("  GUID: %016" PRIx64 "\n",
               (uint64_t)ibv_get_device_guid(dev));
        printf("  Node type: %d\n", dev->node_type);
        printf("  Transport: %d\n", dev->transport_type);

        struct ibv_context *ctx = ibv_open_device(dev);
        if (!ctx) {
            printf("  (failed to open device)\n");
            continue;
        }

        /* Device attributes */
        struct ibv_device_attr attr;
        if (ibv_query_device(ctx, &attr) == 0) {
            printf("  FW version:    %s\n", attr.fw_ver);
            printf("  Max QP:        %d\n", attr.max_qp);
            printf("  Max QP WR:     %d\n", attr.max_qp_wr);
            printf("  Max SGE:       %d\n", attr.max_sge);
            printf("  Max CQ:        %d\n", attr.max_cq);
            printf("  Max CQE:       %d\n", attr.max_cqe);
            printf("  Max MR:        %d\n", attr.max_mr);
            printf("  Max PD:        %d\n", attr.max_pd);
            printf("  Phys ports:    %d\n", attr.phys_port_cnt);
            printf("  Atomic cap:    %s\n",
                   attr.atomic_cap == IBV_ATOMIC_NONE ? "NONE" :
                   attr.atomic_cap == IBV_ATOMIC_HCA  ? "HCA" : "GLOB");
        }

        /* Port attributes */
        for (uint8_t p = 1; p <= attr.phys_port_cnt; p++) {
            struct ibv_port_attr port_attr;
            if (ibv_query_port(ctx, p, &port_attr))
                continue;

            printf("\n  Port %u:\n", p);
            printf("    State:       %s\n",
                   port_attr.state == IBV_PORT_ACTIVE ? "ACTIVE" :
                   port_attr.state == IBV_PORT_DOWN   ? "DOWN" :
                   port_attr.state == IBV_PORT_INIT   ? "INIT" : "OTHER");
            printf("    Link layer:  %s\n",
                   port_attr.link_layer == IBV_LINK_LAYER_ETHERNET
                       ? "Ethernet" : "InfiniBand");
            printf("    Active MTU:  %d\n", port_attr.active_mtu);
            printf("    LID:         %u\n", port_attr.lid);
            printf("    GID entries: %d\n", port_attr.gid_tbl_len);

            /* Print non-zero GIDs */
            for (int g = 0; g < port_attr.gid_tbl_len && g < 16; g++) {
                union ibv_gid gid;
                if (ibv_query_gid(ctx, p, g, &gid))
                    continue;
                int all_zero = 1;
                for (int j = 0; j < 16; j++) {
                    if (gid.raw[j]) { all_zero = 0; break; }
                }
                if (all_zero) continue;

                printf("    GID[%2d]: ", g);
                for (int j = 0; j < 16; j++) {
                    printf("%02x", gid.raw[j]);
                    if (j % 2 == 1 && j < 15) printf(":");
                }
                printf("\n");
            }
        }

        ibv_close_device(ctx);
    }

    ibv_free_device_list(dev_list);
}
```

This utility is useful as a diagnostic tool and as a starting point for device selection logic in your applications. In production code, you might extend it to filter devices by transport type, link layer, or port state, and to select a device based on command-line arguments or a configuration file.

## Device Selection Strategies

When a system has multiple RDMA devices---for example, a dual-port ConnectX NIC (which appears as two ports on one device, or two separate devices depending on firmware configuration), plus a Soft-RoCE device for testing---you need a strategy for selecting the right one.

Common approaches include:

1. **By name**: The user or configuration file specifies `mlx5_0` or `rxe0`. This is the simplest and most explicit approach.

2. **By port state**: Iterate through all devices and ports; select the first one that is in ACTIVE state. This works for systems where only one link is connected.

3. **By link layer**: If your application targets RoCE, skip InfiniBand devices and vice versa.

4. **By NUMA affinity**: For performance-sensitive applications, select the device that is local to the NUMA node where the application is running. The device's NUMA node is available via sysfs at `/sys/class/infiniband/<device>/device/numa_node`. Chapter 12 covers NUMA-aware RDMA programming in detail.

5. **By GID match**: If the application needs to communicate over a specific IP subnet, iterate through GID tables and find the device/port/GID index whose GID corresponds to an address on the target subnet.

<div class="tip">

For development and testing, hardcoding the device name (with a command-line override) is perfectly fine. For production, consider using the RDMA CM (`rdma_resolve_addr`), which handles device and port selection automatically based on IP addresses---you specify the destination IP, and the CM figures out which device, port, and GID to use. This approach is covered in Chapter 10.

</div>

With device discovery mastered, the final piece of the puzzle is building and running your RDMA programs reliably.
