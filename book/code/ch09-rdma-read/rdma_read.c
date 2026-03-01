/*
 * rdma_read.c - RC RDMA Read Example
 *
 * Demonstrates one-sided RDMA Read:
 *   - Server fills a buffer with data and shares its MR info
 *   - Client reads data from the server's buffer via RDMA Read
 *   - Demonstrates the metadata-read-then-data-read pattern
 *   - Server's CPU is not involved in the data transfer
 *
 * Usage:
 *   Server:  ./rdma_read -s [-d <device>] [-p <port>] [-m <size>]
 *   Client:  ./rdma_read -c <server_ip> [-d <device>] [-p <port>] [-m <size>]
 *
 * Build:
 *   make
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <inttypes.h>
#include <time.h>

#include "../common/rdma_common.h"

#define NOTIFY_SIZE  16

/*
 * Data header structure placed at the beginning of the server's buffer.
 * The client reads this header first to discover the payload layout.
 */
struct data_header {
    uint32_t magic;        /* Validity marker: 0xRDMA0001 */
    uint32_t payload_len;  /* Length of payload data */
    uint64_t sequence;     /* Sequence number */
};

#define HEADER_MAGIC  0x52444D41  /* "RDMA" in hex */

/* --------------------------------------------------------------------------
 * QP state transition helpers
 * -------------------------------------------------------------------------- */

static int modify_qp_to_init(struct ibv_qp *qp, int ib_port)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = ib_port;
    /* Allow remote reads from this QP */
    attr.qp_access_flags = IBV_ACCESS_REMOTE_READ;

    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_PKEY_INDEX |
        IBV_QP_PORT  | IBV_QP_ACCESS_FLAGS);
}

static int modify_qp_to_rtr(struct ibv_qp *qp, int ib_port,
                              struct qp_info *remote, int gid_index)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state               = IBV_QPS_RTR;
    attr.path_mtu               = IBV_MTU_1024;
    attr.dest_qp_num            = remote->qp_num;
    attr.rq_psn                 = remote->psn;
    /* Allow up to 4 outstanding RDMA Read responses */
    attr.max_dest_rd_atomic     = 4;
    attr.min_rnr_timer          = 12;
    attr.ah_attr.is_global      = 1;
    attr.ah_attr.grh.dgid       = remote->gid;
    attr.ah_attr.grh.sgid_index = gid_index;
    attr.ah_attr.grh.hop_limit  = 1;
    attr.ah_attr.dlid           = remote->lid;
    attr.ah_attr.sl             = 0;
    attr.ah_attr.src_path_bits  = 0;
    attr.ah_attr.port_num       = ib_port;

    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
        IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
        IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
}

static int modify_qp_to_rts(struct ibv_qp *qp, uint32_t local_psn)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state      = IBV_QPS_RTS;
    attr.sq_psn        = local_psn;
    attr.timeout       = 14;
    attr.retry_cnt     = 7;
    attr.rnr_retry     = 7;
    /* Allow up to 4 outstanding RDMA Read requests */
    attr.max_rd_atomic = 4;

    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
        IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
        IBV_QP_MAX_QP_RD_ATOMIC);
}

/* --------------------------------------------------------------------------
 * RDMA Read and Send/Recv helpers
 * -------------------------------------------------------------------------- */

static int post_rdma_read(struct ibv_qp *qp, void *local_buf,
                           uint32_t len, struct ibv_mr *mr,
                           uint64_t remote_addr, uint32_t rkey,
                           uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr   = (uintptr_t)local_buf,
        .length = len,
        .lkey   = mr->lkey,
    };
    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id              = wr_id;
    wr.sg_list            = &sge;
    wr.num_sge            = 1;
    wr.opcode             = IBV_WR_RDMA_READ;
    wr.send_flags         = IBV_SEND_SIGNALED;
    wr.wr.rdma.remote_addr = remote_addr;
    wr.wr.rdma.rkey        = rkey;

    struct ibv_send_wr *bad_wr;
    return ibv_post_send(qp, &wr, &bad_wr);
}

static int post_send(struct ibv_qp *qp, void *buf, uint32_t len,
                      struct ibv_mr *mr)
{
    struct ibv_sge sge = {
        .addr   = (uintptr_t)buf,
        .length = len,
        .lkey   = mr->lkey,
    };
    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.sg_list    = &sge;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;

    struct ibv_send_wr *bad_wr;
    return ibv_post_send(qp, &wr, &bad_wr);
}

static int post_receive(struct ibv_qp *qp, void *buf, uint32_t len,
                         struct ibv_mr *mr)
{
    struct ibv_sge sge = {
        .addr   = (uintptr_t)buf,
        .length = len,
        .lkey   = mr->lkey,
    };
    struct ibv_recv_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.sg_list = &sge;
    wr.num_sge = 1;

    struct ibv_recv_wr *bad_wr;
    return ibv_post_recv(qp, &wr, &bad_wr);
}

/* --------------------------------------------------------------------------
 * Usage
 * -------------------------------------------------------------------------- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  Server: %s -s [-d device] [-p port] [-m size]\n"
        "  Client: %s -c <server_ip> [-d device] [-p port] [-m size]\n",
        prog, prog);
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int is_server = -1;
    char *server_name = NULL;
    char *dev_name    = NULL;
    int tcp_port      = DEFAULT_PORT;
    int buf_size      = 4096;
    int ib_port       = DEFAULT_IB_PORT;
    int gid_index     = DEFAULT_GID_INDEX;
    int opt;

    while ((opt = getopt(argc, argv, "sc:d:p:m:g:")) != -1) {
        switch (opt) {
        case 's': is_server = 1; break;
        case 'c': is_server = 0; server_name = optarg; break;
        case 'd': dev_name = optarg; break;
        case 'p': tcp_port = atoi(optarg); break;
        case 'm': buf_size = atoi(optarg); break;
        case 'g': gid_index = atoi(optarg); break;
        default:  usage(argv[0]); exit(EXIT_FAILURE);
        }
    }

    if (is_server < 0) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    srand48(getpid() * time(NULL));

    printf("=== RC RDMA Read Example ===\n");
    printf("Role: %s, Buffer size: %d bytes\n",
           is_server ? "Server" : "Client", buf_size);

    /* ---- Create RDMA resources ---- */
    struct ibv_context *ctx = open_device(dev_name);
    CHECK_NULL(ctx, "open_device");

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    CHECK_NULL(pd, "ibv_alloc_pd");

    struct ibv_cq *cq = ibv_create_cq(ctx, CQ_DEPTH, NULL, NULL, 0);
    CHECK_NULL(cq, "ibv_create_cq");

    struct ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_RC;
    qp_init_attr.cap.max_send_wr  = SQ_DEPTH;
    qp_init_attr.cap.max_recv_wr  = RQ_DEPTH;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    CHECK_NULL(qp, "ibv_create_qp");

    /* Data buffer */
    char *data_buf = calloc(1, buf_size);
    CHECK_NULL(data_buf, "calloc data_buf");

    int mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ;
    struct ibv_mr *data_mr = ibv_reg_mr(pd, data_buf, buf_size, mr_flags);
    CHECK_NULL(data_mr, "ibv_reg_mr (data)");

    /* Notification buffer */
    char *notify_buf = calloc(1, NOTIFY_SIZE);
    CHECK_NULL(notify_buf, "calloc notify_buf");
    struct ibv_mr *notify_mr = ibv_reg_mr(pd, notify_buf, NOTIFY_SIZE,
                                           IBV_ACCESS_LOCAL_WRITE);
    CHECK_NULL(notify_mr, "ibv_reg_mr (notify)");

    /* ---- Gather local info ---- */
    struct qp_info local_qp_info, remote_qp_info;
    CHECK_ERRNO(get_local_info(ctx, qp, ib_port, gid_index, &local_qp_info),
                "get_local_info");

    struct mr_info local_mr_info, remote_mr_info;
    local_mr_info.addr   = (uint64_t)(uintptr_t)data_mr->addr;
    local_mr_info.rkey   = data_mr->rkey;
    local_mr_info.length = buf_size;

    /* ---- TCP connection and metadata exchange ---- */
    int sockfd;
    if (is_server) {
        int listen_fd = tcp_server_listen(tcp_port);
        CHECK_NULL((listen_fd >= 0) ? (void *)1 : NULL, "tcp_server_listen");
        sockfd = tcp_server_accept(listen_fd);
        CHECK_NULL((sockfd >= 0) ? (void *)1 : NULL, "tcp_server_accept");
        close(listen_fd);
    } else {
        sockfd = tcp_client_connect(server_name, tcp_port);
        CHECK_NULL((sockfd >= 0) ? (void *)1 : NULL, "tcp_client_connect");
    }

    CHECK_ERRNO(exchange_qp_info(sockfd, &local_qp_info, &remote_qp_info),
                "exchange_qp_info");
    printf("Exchanged QP info:\n");
    print_qp_info("Local ", &local_qp_info);
    print_qp_info("Remote", &remote_qp_info);

    CHECK_ERRNO(exchange_mr_info(sockfd, &local_mr_info, &remote_mr_info),
                "exchange_mr_info");
    printf("Remote MR: addr=0x%" PRIx64 ", rkey=0x%x, len=%u\n",
           remote_mr_info.addr, remote_mr_info.rkey, remote_mr_info.length);

    /* ---- QP state transitions ---- */
    CHECK_RC(modify_qp_to_init(qp, ib_port), "QP INIT");
    CHECK_RC(modify_qp_to_rtr(qp, ib_port, &remote_qp_info, gid_index),
             "QP RTR");
    CHECK_RC(modify_qp_to_rts(qp, local_qp_info.psn), "QP RTS");
    printf("QP state: RESET -> INIT -> RTR -> RTS\n");

    /* ---- Synchronize ---- */
    char sync = 'R';
    write(sockfd, &sync, 1);
    read(sockfd, &sync, 1);

    /* ---- Perform RDMA Read ---- */
    struct ibv_wc wc;

    if (is_server) {
        /*
         * Server: fill buffer with a header + payload, then wait
         * for the client to signal it has finished reading.
         */
        struct data_header *hdr = (struct data_header *)data_buf;
        char *payload = data_buf + sizeof(struct data_header);
        const char *message = "This data was read from the server's memory "
                              "by the client using RDMA Read. The server's "
                              "CPU did no work for this transfer.";

        hdr->magic       = HEADER_MAGIC;
        hdr->payload_len = strlen(message) + 1;
        hdr->sequence    = 42;
        strncpy(payload, message, buf_size - sizeof(struct data_header));

        printf("\nServer: buffer initialized with header + payload.\n");
        printf("  Header: magic=0x%x, payload_len=%u, seq=%lu\n",
               hdr->magic, hdr->payload_len, (unsigned long)hdr->sequence);
        printf("  Payload: \"%s\"\n", payload);

        /* Wait for client's "done" notification */
        CHECK_ERRNO(post_receive(qp, notify_buf, NOTIFY_SIZE, notify_mr),
                    "post_receive");
        printf("Server: waiting for client to finish reading...\n");
        CHECK_ERRNO(poll_completion(cq, &wc), "poll notification");
        printf("Server: client finished reading.\n");
    } else {
        /*
         * Client: perform two-phase RDMA Read.
         * Phase 1: Read the header to discover payload layout.
         * Phase 2: Read the payload based on header info.
         */
        printf("\nClient: performing RDMA Read (metadata-then-data)...\n");

        /* Phase 1: Read the header */
        struct data_header local_hdr;
        /* We need an MR for the header read target */
        struct ibv_mr *hdr_mr = ibv_reg_mr(pd, &local_hdr,
                                            sizeof(local_hdr),
                                            IBV_ACCESS_LOCAL_WRITE);
        CHECK_NULL(hdr_mr, "ibv_reg_mr (header)");

        CHECK_ERRNO(post_rdma_read(qp, &local_hdr, sizeof(local_hdr),
                                    hdr_mr, remote_mr_info.addr,
                                    remote_mr_info.rkey, 10),
                    "post_rdma_read (header)");
        CHECK_ERRNO(poll_completion(cq, &wc), "poll header read");

        printf("Client: read header from server:\n");
        printf("  magic=0x%x, payload_len=%u, sequence=%lu\n",
               local_hdr.magic, local_hdr.payload_len,
               (unsigned long)local_hdr.sequence);

        if (local_hdr.magic != HEADER_MAGIC) {
            fprintf(stderr, "Invalid magic! Expected 0x%x, got 0x%x\n",
                    HEADER_MAGIC, local_hdr.magic);
            exit(EXIT_FAILURE);
        }

        /* Phase 2: Read the payload */
        uint64_t payload_remote_addr = remote_mr_info.addr +
                                       sizeof(struct data_header);
        uint32_t payload_len = local_hdr.payload_len;

        printf("Client: reading %u bytes of payload from remote addr 0x%"
               PRIx64 "...\n", payload_len, payload_remote_addr);

        memset(data_buf, 0, buf_size);

        CHECK_ERRNO(post_rdma_read(qp, data_buf, payload_len, data_mr,
                                    payload_remote_addr,
                                    remote_mr_info.rkey, 11),
                    "post_rdma_read (payload)");
        CHECK_ERRNO(poll_completion(cq, &wc), "poll payload read");

        printf("Client: read payload: \"%s\"\n", data_buf);

        /* Notify server that we are done */
        strncpy(notify_buf, "READ_DONE", NOTIFY_SIZE);
        CHECK_ERRNO(post_send(qp, notify_buf, NOTIFY_SIZE, notify_mr),
                    "post_send (done)");
        CHECK_ERRNO(poll_completion(cq, &wc), "poll send done");

        ibv_dereg_mr(hdr_mr);
    }

    /* ---- Cleanup ---- */
    close(sockfd);
    ibv_destroy_qp(qp);
    ibv_dereg_mr(data_mr);
    ibv_dereg_mr(notify_mr);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    free(data_buf);
    free(notify_buf);

    printf("\nDone.\n");
    return 0;
}
