/*
 * hello_verbs.c - First RDMA program
 *
 * Demonstrates the fundamental RDMA resource allocation lifecycle:
 *   1. Device discovery and opening
 *   2. Device and port capability queries
 *   3. Protection Domain allocation
 *   4. Completion Queue creation
 *   5. Memory Region registration
 *   6. Queue Pair creation (RC type)
 *   7. Clean teardown in reverse order
 *
 * No data transfer is performed. This program verifies that the RDMA
 * environment is correctly set up and that all core API calls succeed.
 *
 * Build:
 *   gcc -Wall -Wextra -O2 -g -std=c11 -o hello_verbs hello_verbs.c -libverbs
 *
 * Usage:
 *   ./hello_verbs [device_name]
 *
 * If device_name is not specified, the first available device is used.
 *
 * Chapter 8 of "RDMA: The Complete Guide"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>
#include <infiniband/verbs.h>

/* Size of the buffer to register as a Memory Region */
#define BUFFER_SIZE 4096

/* Maximum number of GID table entries to display */
#define MAX_GID_DISPLAY 16

/* --------------------------------------------------------------------------
 * Error handling macro: print message and exit.
 * In a real application you would use a cleanup-and-return pattern instead
 * of exit(), but for a self-contained demo this keeps the code focused.
 * -------------------------------------------------------------------------- */
#define DIE(fmt, ...) do {                                          \
    fprintf(stderr, "ERROR [%s:%d]: " fmt "\n",                     \
            __FILE__, __LINE__, ##__VA_ARGS__);                     \
    exit(EXIT_FAILURE);                                             \
} while (0)

/* --------------------------------------------------------------------------
 * Helper: print device capabilities
 * -------------------------------------------------------------------------- */
static void print_device_capabilities(const struct ibv_device_attr *attr)
{
    printf("\n=== Device Capabilities ===\n");
    printf("  Firmware version:      %s\n", attr->fw_ver);
    printf("  Node GUID:             %016" PRIx64 "\n", attr->node_guid);
    printf("  System image GUID:     %016" PRIx64 "\n", attr->sys_image_guid);
    printf("  Max MR size:           0x%" PRIx64 "\n", attr->max_mr_size);
    printf("  Page size cap:         0x%" PRIx64 "\n", attr->page_size_cap);
    printf("  Vendor ID:             0x%04x\n", attr->vendor_id);
    printf("  Vendor part ID:        %u\n", attr->vendor_part_id);
    printf("  HW version:            %u\n", attr->hw_ver);
    printf("  Max QPs:               %d\n", attr->max_qp);
    printf("  Max QP WRs:            %d\n", attr->max_qp_wr);
    printf("  Max SGE per WR:        %d\n", attr->max_sge);
    printf("  Max SGE per RD WR:     %d\n", attr->max_sge_rd);
    printf("  Max CQs:               %d\n", attr->max_cq);
    printf("  Max CQE per CQ:        %d\n", attr->max_cqe);
    printf("  Max MRs:               %d\n", attr->max_mr);
    printf("  Max PDs:               %d\n", attr->max_pd);
    printf("  Max QP RD atoms:       %d\n", attr->max_qp_rd_atom);
    printf("  Max QP init RD atoms:  %d\n", attr->max_qp_init_rd_atom);
    printf("  Max SRQs:              %d\n", attr->max_srq);
    printf("  Max SRQ WRs:           %d\n", attr->max_srq_wr);
    printf("  Max SRQ SGE:           %d\n", attr->max_srq_sge);
    printf("  Max multicast groups:  %d\n", attr->max_mcast_grp);
    printf("  Physical port count:   %d\n", attr->phys_port_cnt);

    const char *atomic_str;
    switch (attr->atomic_cap) {
    case IBV_ATOMIC_NONE: atomic_str = "NONE"; break;
    case IBV_ATOMIC_HCA:  atomic_str = "HCA-local"; break;
    case IBV_ATOMIC_GLOB: atomic_str = "Global"; break;
    default:              atomic_str = "Unknown"; break;
    }
    printf("  Atomic capability:     %s\n", atomic_str);
}

/* --------------------------------------------------------------------------
 * Helper: print port information
 * -------------------------------------------------------------------------- */
static void print_port_info(const struct ibv_port_attr *attr, uint8_t port_num)
{
    /* Port state */
    const char *state_str;
    switch (attr->state) {
    case IBV_PORT_DOWN:   state_str = "DOWN"; break;
    case IBV_PORT_INIT:   state_str = "INIT"; break;
    case IBV_PORT_ARMED:  state_str = "ARMED"; break;
    case IBV_PORT_ACTIVE: state_str = "ACTIVE"; break;
    default:              state_str = "UNKNOWN"; break;
    }

    /* Link layer */
    const char *link_str;
    switch (attr->link_layer) {
    case IBV_LINK_LAYER_INFINIBAND: link_str = "InfiniBand"; break;
    case IBV_LINK_LAYER_ETHERNET:   link_str = "Ethernet"; break;
    default:                        link_str = "Unknown"; break;
    }

    /* MTU enum to bytes */
    const char *mtu_str;
    switch (attr->active_mtu) {
    case IBV_MTU_256:  mtu_str = "256"; break;
    case IBV_MTU_512:  mtu_str = "512"; break;
    case IBV_MTU_1024: mtu_str = "1024"; break;
    case IBV_MTU_2048: mtu_str = "2048"; break;
    case IBV_MTU_4096: mtu_str = "4096"; break;
    default:           mtu_str = "Unknown"; break;
    }

    printf("\n=== Port %u Information ===\n", port_num);
    printf("  State:             %s\n", state_str);
    printf("  Link layer:        %s\n", link_str);
    printf("  Active MTU:        %s bytes\n", mtu_str);
    printf("  LID:               %u\n", attr->lid);
    printf("  SM LID:            %u\n", attr->sm_lid);
    printf("  Max message size:  0x%x\n", attr->max_msg_sz);
    printf("  GID table length:  %d\n", attr->gid_tbl_len);
    printf("  PKey table length: %u\n", attr->pkey_tbl_len);
}

/* --------------------------------------------------------------------------
 * Helper: print GID table entries (up to MAX_GID_DISPLAY non-zero entries)
 * -------------------------------------------------------------------------- */
static void print_gid_table(struct ibv_context *ctx, uint8_t port_num,
                             int gid_tbl_len)
{
    int shown = 0;
    printf("\n=== GID Table (Port %u) ===\n", port_num);

    int limit = gid_tbl_len < MAX_GID_DISPLAY ? gid_tbl_len : MAX_GID_DISPLAY;
    for (int i = 0; i < limit; i++) {
        union ibv_gid gid;
        if (ibv_query_gid(ctx, port_num, i, &gid)) {
            continue;
        }

        /* Skip all-zero GIDs */
        int all_zero = 1;
        for (int j = 0; j < 16; j++) {
            if (gid.raw[j] != 0) {
                all_zero = 0;
                break;
            }
        }
        if (all_zero)
            continue;

        /* Identify GID type heuristically */
        const char *type;
        if (gid.raw[0] == 0xfe && gid.raw[1] == 0x80) {
            type = "link-local";
        } else if (gid.raw[0] == 0x00 && gid.raw[1] == 0x00 &&
                   gid.raw[10] == 0xff && gid.raw[11] == 0xff) {
            type = "IPv4-mapped";
        } else {
            type = "IPv6";
        }

        printf("  GID[%2d] (%s): ", i, type);
        for (int j = 0; j < 16; j++) {
            printf("%02x", gid.raw[j]);
            if (j % 2 == 1 && j < 15) printf(":");
        }
        printf("\n");
        shown++;
    }

    if (shown == 0) {
        printf("  (no non-zero GID entries found)\n");
    }
}

/* --------------------------------------------------------------------------
 * Main program
 * -------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    const char *requested_dev = (argc > 1) ? argv[1] : NULL;

    /*
     * Step 1: Enumerate RDMA devices and open one.
     *
     * ibv_get_device_list() returns a NULL-terminated array of ibv_device
     * pointers. We iterate through it to find the requested device (or
     * use the first one). The list must be freed after opening the device.
     */
    int num_devices = 0;
    struct ibv_device **dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        DIE("ibv_get_device_list() failed: %s", strerror(errno));
    }

    printf("Found %d RDMA device(s):\n", num_devices);
    for (int i = 0; i < num_devices; i++) {
        printf("  [%d] %s (GUID: %016" PRIx64 ")\n",
               i,
               ibv_get_device_name(dev_list[i]),
               (uint64_t)ibv_get_device_guid(dev_list[i]));
    }

    if (num_devices == 0) {
        ibv_free_device_list(dev_list);
        DIE("No RDMA devices found. Is the kernel module loaded?\n"
            "  Try: sudo modprobe rdma_rxe && "
            "sudo rdma link add rxe0 type rxe netdev eth0");
    }

    /* Select the device */
    struct ibv_device *ib_dev = NULL;
    if (requested_dev) {
        for (int i = 0; i < num_devices; i++) {
            if (strcmp(ibv_get_device_name(dev_list[i]), requested_dev) == 0) {
                ib_dev = dev_list[i];
                break;
            }
        }
        if (!ib_dev) {
            ibv_free_device_list(dev_list);
            DIE("Requested device '%s' not found.", requested_dev);
        }
    } else {
        ib_dev = dev_list[0];
        printf("\nNo device specified; using first device: %s\n",
               ibv_get_device_name(ib_dev));
    }

    printf("\nOpening device: %s\n", ibv_get_device_name(ib_dev));

    struct ibv_context *ctx = ibv_open_device(ib_dev);
    if (!ctx) {
        ibv_free_device_list(dev_list);
        DIE("ibv_open_device(%s) failed: %s",
            ibv_get_device_name(ib_dev), strerror(errno));
    }

    /* Safe to free the device list now; ctx holds its own reference */
    ibv_free_device_list(dev_list);
    dev_list = NULL;

    /*
     * Step 2: Query device capabilities.
     *
     * ibv_query_device() fills an ibv_device_attr structure with hardware
     * limits: max QPs, CQs, MRs, SGE count, atomic support, etc.
     */
    struct ibv_device_attr dev_attr;
    memset(&dev_attr, 0, sizeof(dev_attr));
    if (ibv_query_device(ctx, &dev_attr)) {
        DIE("ibv_query_device() failed");
    }
    print_device_capabilities(&dev_attr);

    /*
     * Step 3: Query port information and GID table.
     *
     * Ports are 1-indexed. Most devices have a single port.
     * The port state must be ACTIVE for data transfer.
     */
    uint8_t port_num = 1;
    struct ibv_port_attr port_attr;
    memset(&port_attr, 0, sizeof(port_attr));
    if (ibv_query_port(ctx, port_num, &port_attr)) {
        DIE("ibv_query_port(port=%u) failed", port_num);
    }
    print_port_info(&port_attr, port_num);
    print_gid_table(ctx, port_num, port_attr.gid_tbl_len);

    if (port_attr.state != IBV_PORT_ACTIVE) {
        printf("\nWARNING: Port %u is not ACTIVE (state=%d). "
               "Data transfer will not work.\n", port_num, port_attr.state);
    }

    /*
     * Step 4: Allocate a Protection Domain.
     *
     * The PD is the isolation boundary. QPs and MRs must share a PD
     * to interact. Most applications create one PD.
     */
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    if (!pd) {
        DIE("ibv_alloc_pd() failed: %s", strerror(errno));
    }
    printf("\nProtection Domain: allocated\n");

    /*
     * Step 5: Create a Completion Queue.
     *
     * The CQ collects completions from send and receive operations.
     * Parameters: context, min entries, user context (NULL), completion
     * channel (NULL for polling mode), completion vector (0).
     */
    int cq_entries = 16;
    struct ibv_cq *cq = ibv_create_cq(ctx, cq_entries, NULL, NULL, 0);
    if (!cq) {
        DIE("ibv_create_cq(%d entries) failed: %s", cq_entries, strerror(errno));
    }
    printf("Completion Queue:  %d entries (actual: %d)\n",
           cq_entries, cq->cqe);

    /*
     * Step 6: Allocate a buffer and register it as a Memory Region.
     *
     * Memory registration pins pages in physical memory and programs
     * the NIC's translation tables. The access flags control what
     * operations are allowed on this memory:
     *   - LOCAL_WRITE:  local NIC can write (required for recv buffers)
     *   - REMOTE_WRITE: remote peers can RDMA Write
     *   - REMOTE_READ:  remote peers can RDMA Read
     *
     * LOCAL_WRITE must be set whenever REMOTE_WRITE or REMOTE_READ is set.
     */
    char *buf = malloc(BUFFER_SIZE);
    if (!buf) {
        DIE("malloc(%d) failed", BUFFER_SIZE);
    }
    memset(buf, 0, BUFFER_SIZE);

    int access_flags = IBV_ACCESS_LOCAL_WRITE |
                       IBV_ACCESS_REMOTE_WRITE |
                       IBV_ACCESS_REMOTE_READ;

    struct ibv_mr *mr = ibv_reg_mr(pd, buf, BUFFER_SIZE, access_flags);
    if (!mr) {
        DIE("ibv_reg_mr() failed: %s\n"
            "  Hint: check 'ulimit -l' (locked memory limit).\n"
            "  Set to unlimited: ulimit -l unlimited", strerror(errno));
    }
    printf("Memory Region:     addr=%p, len=%d, lkey=0x%08x, rkey=0x%08x\n",
           (void *)buf, BUFFER_SIZE, mr->lkey, mr->rkey);

    /*
     * Step 7: Create a Queue Pair (Reliable Connected).
     *
     * The QP init attributes specify:
     *   - send_cq / recv_cq: CQs for send and receive completions
     *   - cap: queue depths and SGE limits
     *   - qp_type: IBV_QPT_RC for Reliable Connected
     *   - sq_sig_all: if 1, every send WR generates a completion
     *
     * After creation the QP is in RESET state. To transfer data, it must
     * be moved through INIT -> RTR -> RTS (covered in Chapter 9).
     */
    struct ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq  = cq;
    qp_init_attr.recv_cq  = cq;
    qp_init_attr.qp_type  = IBV_QPT_RC;
    qp_init_attr.sq_sig_all = 1;
    qp_init_attr.cap.max_send_wr  = 16;
    qp_init_attr.cap.max_recv_wr  = 16;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    if (!qp) {
        DIE("ibv_create_qp(RC) failed: %s", strerror(errno));
    }
    printf("Queue Pair:        qp_num=%u, type=RC, state=RESET\n", qp->qp_num);

    /* Print actual capabilities (may differ from requested) */
    printf("  Actual send WR:  %d\n", qp_init_attr.cap.max_send_wr);
    printf("  Actual recv WR:  %d\n", qp_init_attr.cap.max_recv_wr);
    printf("  Actual send SGE: %d\n", qp_init_attr.cap.max_send_sge);
    printf("  Actual recv SGE: %d\n", qp_init_attr.cap.max_recv_sge);

    /*
     * Step 8: Summary
     */
    printf("\n");
    printf("=== Resource Summary ===\n");
    printf("  Device:  %s\n", ibv_get_device_name(ctx->device));
    printf("  Port:    %u (%s, %s)\n",
           port_num,
           port_attr.state == IBV_PORT_ACTIVE ? "ACTIVE" : "NOT ACTIVE",
           port_attr.link_layer == IBV_LINK_LAYER_ETHERNET
               ? "Ethernet" : "InfiniBand");
    printf("  PD:      allocated\n");
    printf("  CQ:      %d entries\n", cq->cqe);
    printf("  MR:      %d bytes, lkey=0x%08x, rkey=0x%08x\n",
           BUFFER_SIZE, mr->lkey, mr->rkey);
    printf("  QP:      num=%u, type=RC, state=RESET\n", qp->qp_num);

    /*
     * Step 9: Clean teardown (reverse order of creation).
     *
     * The order matters: a resource cannot be destroyed while other
     * resources still reference it. QP references CQ and PD. MR
     * references PD. So we destroy: QP -> MR -> CQ -> PD -> context.
     *
     * The malloc'd buffer is freed after deregistering the MR.
     */
    printf("\nCleaning up resources...\n");

    /* Destroy QP first (references CQ and PD) */
    if (ibv_destroy_qp(qp)) {
        fprintf(stderr, "  WARNING: ibv_destroy_qp failed: %s\n",
                strerror(errno));
    } else {
        printf("  QP destroyed\n");
    }

    /* Deregister MR (references PD) */
    if (ibv_dereg_mr(mr)) {
        fprintf(stderr, "  WARNING: ibv_dereg_mr failed: %s\n",
                strerror(errno));
    } else {
        printf("  MR deregistered\n");
    }

    /* Free the buffer (only safe after MR deregistration) */
    free(buf);
    buf = NULL;
    printf("  Buffer freed\n");

    /* Destroy CQ */
    if (ibv_destroy_cq(cq)) {
        fprintf(stderr, "  WARNING: ibv_destroy_cq failed: %s\n",
                strerror(errno));
    } else {
        printf("  CQ destroyed\n");
    }

    /* Deallocate PD (must be after QP and MR are gone) */
    if (ibv_dealloc_pd(pd)) {
        fprintf(stderr, "  WARNING: ibv_dealloc_pd failed: %s\n",
                strerror(errno));
    } else {
        printf("  PD deallocated\n");
    }

    /* Close device context */
    if (ibv_close_device(ctx)) {
        fprintf(stderr, "  WARNING: ibv_close_device failed: %s\n",
                strerror(errno));
    } else {
        printf("  Device closed\n");
    }

    printf("\nAll resources freed successfully. Done.\n");
    return 0;
}
