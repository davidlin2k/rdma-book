/*
 * ud_send.c - UD (Unreliable Datagram) Send/Receive Example
 *
 * Demonstrates connectionless UD messaging:
 *   - Sender creates an Address Handle (AH) for the receiver
 *   - Sender sends datagrams to the receiver's QP
 *   - Receiver posts receive buffers with space for the 40-byte GRH
 *   - No connection setup required between QPs
 *
 * Usage:
 *   Receiver: ./ud_send -s [-d <device>] [-p <port>] [-n <msgs>]
 *   Sender:   ./ud_send -c <receiver_ip> [-d <device>] [-p <port>] [-n <msgs>]
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
#include <endian.h>

#include "../common/rdma_common.h"

#define GRH_SIZE      40       /* Global Route Header is always 40 bytes */
#define MAX_MSG_SIZE  256      /* Maximum message payload size */
#define BUF_SIZE      (GRH_SIZE + MAX_MSG_SIZE)
#define NUM_RECV_BUFS 16       /* Number of pre-posted receive buffers */
#define DEFAULT_MSGS  10       /* Default number of messages to send */
#define QKEY          0x11111111

/* UD-specific info exchanged between sender and receiver */
struct ud_info {
    uint32_t      qp_num;
    uint32_t      lid;
    union ibv_gid gid;
};

/* --------------------------------------------------------------------------
 * QP state transitions for UD
 * -------------------------------------------------------------------------- */

static int modify_ud_qp_to_init(struct ibv_qp *qp, int ib_port)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state   = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num   = ib_port;
    attr.qkey       = QKEY;    /* Q_Key for UD access control */

    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY);
}

static int modify_ud_qp_to_rtr(struct ibv_qp *qp)
{
    /* UD RTR transition is simple: no remote peer info needed */
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;

    return ibv_modify_qp(qp, &attr, IBV_QP_STATE);
}

static int modify_ud_qp_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn   = 0;

    return ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN);
}

/* --------------------------------------------------------------------------
 * UD send/receive helpers
 * -------------------------------------------------------------------------- */

static int post_ud_receive(struct ibv_qp *qp, void *buf,
                            struct ibv_mr *mr, uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr   = (uintptr_t)buf,
        .length = BUF_SIZE,    /* Include space for GRH */
        .lkey   = mr->lkey,
    };
    struct ibv_recv_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id   = wr_id;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    struct ibv_recv_wr *bad_wr;
    return ibv_post_recv(qp, &wr, &bad_wr);
}

static int post_ud_send(struct ibv_qp *qp, void *buf, uint32_t len,
                          struct ibv_mr *mr, struct ibv_ah *ah,
                          uint32_t remote_qpn, uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr   = (uintptr_t)buf,
        .length = len,
        .lkey   = mr->lkey,
    };
    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id              = wr_id;
    wr.sg_list            = &sge;
    wr.num_sge            = 1;
    wr.opcode             = IBV_WR_SEND;
    wr.send_flags         = IBV_SEND_SIGNALED;
    wr.wr.ud.ah           = ah;
    wr.wr.ud.remote_qpn   = remote_qpn;
    wr.wr.ud.remote_qkey  = QKEY;

    struct ibv_send_wr *bad_wr;
    return ibv_post_send(qp, &wr, &bad_wr);
}

/* --------------------------------------------------------------------------
 * UD info exchange over TCP
 * -------------------------------------------------------------------------- */

static int exchange_ud_info(int sockfd, struct ud_info *local,
                             struct ud_info *remote)
{
    if (write(sockfd, local, sizeof(*local)) != sizeof(*local))
        return -1;
    if (read(sockfd, remote, sizeof(*remote)) != sizeof(*remote))
        return -1;
    return 0;
}

/* --------------------------------------------------------------------------
 * Usage
 * -------------------------------------------------------------------------- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  Receiver: %s -s [-d device] [-p port] [-n msgs]\n"
        "  Sender:   %s -c <receiver_ip> [-d device] [-p port] [-n msgs]\n",
        prog, prog);
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    int is_receiver = -1;
    char *server_name = NULL;
    char *dev_name    = NULL;
    int tcp_port      = DEFAULT_PORT;
    int num_msgs      = DEFAULT_MSGS;
    int ib_port       = DEFAULT_IB_PORT;
    int gid_index     = DEFAULT_GID_INDEX;
    int opt;

    while ((opt = getopt(argc, argv, "sc:d:p:n:g:")) != -1) {
        switch (opt) {
        case 's': is_receiver = 1; break;
        case 'c': is_receiver = 0; server_name = optarg; break;
        case 'd': dev_name = optarg; break;
        case 'p': tcp_port = atoi(optarg); break;
        case 'n': num_msgs = atoi(optarg); break;
        case 'g': gid_index = atoi(optarg); break;
        default:  usage(argv[0]); exit(EXIT_FAILURE);
        }
    }

    if (is_receiver < 0) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    printf("=== UD Messaging Example ===\n");
    printf("Role: %s, Messages: %d\n",
           is_receiver ? "Receiver" : "Sender", num_msgs);

    /* ---- Create RDMA resources ---- */
    struct ibv_context *ctx = open_device(dev_name);
    CHECK_NULL(ctx, "open_device");

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    CHECK_NULL(pd, "ibv_alloc_pd");

    struct ibv_cq *cq = ibv_create_cq(ctx, CQ_DEPTH, NULL, NULL, 0);
    CHECK_NULL(cq, "ibv_create_cq");

    /* Create a UD QP */
    struct ibv_qp_init_attr qp_init_attr;
    memset(&qp_init_attr, 0, sizeof(qp_init_attr));
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.qp_type = IBV_QPT_UD;  /* Unreliable Datagram */
    qp_init_attr.cap.max_send_wr  = SQ_DEPTH;
    qp_init_attr.cap.max_recv_wr  = NUM_RECV_BUFS + 4;
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;

    struct ibv_qp *qp = ibv_create_qp(pd, &qp_init_attr);
    CHECK_NULL(qp, "ibv_create_qp (UD)");

    /* Send buffer (no GRH needed for sends) */
    char *send_buf = calloc(1, MAX_MSG_SIZE);
    CHECK_NULL(send_buf, "calloc send_buf");
    struct ibv_mr *send_mr = ibv_reg_mr(pd, send_buf, MAX_MSG_SIZE,
                                         IBV_ACCESS_LOCAL_WRITE);
    CHECK_NULL(send_mr, "ibv_reg_mr (send)");

    /*
     * Receive buffers: must be BUF_SIZE = GRH_SIZE + MAX_MSG_SIZE.
     * The first 40 bytes of each received message contain the GRH.
     */
    char *recv_bufs[NUM_RECV_BUFS];
    for (int i = 0; i < NUM_RECV_BUFS; i++) {
        recv_bufs[i] = calloc(1, BUF_SIZE);
        CHECK_NULL(recv_bufs[i], "calloc recv_buf");
    }

    /* Register a single large MR covering all receive buffers.
     * For simplicity, allocate a contiguous block instead. */
    char *recv_pool = calloc(NUM_RECV_BUFS, BUF_SIZE);
    CHECK_NULL(recv_pool, "calloc recv_pool");
    struct ibv_mr *recv_mr = ibv_reg_mr(pd, recv_pool,
                                         NUM_RECV_BUFS * BUF_SIZE,
                                         IBV_ACCESS_LOCAL_WRITE);
    CHECK_NULL(recv_mr, "ibv_reg_mr (recv)");

    /* Update recv_bufs pointers to the contiguous pool */
    for (int i = 0; i < NUM_RECV_BUFS; i++) {
        free(recv_bufs[i]);  /* Free the individually allocated ones */
        recv_bufs[i] = recv_pool + i * BUF_SIZE;
    }

    /* ---- QP state transitions (simpler for UD) ---- */
    CHECK_RC(modify_ud_qp_to_init(qp, ib_port), "UD QP INIT");
    CHECK_RC(modify_ud_qp_to_rtr(qp), "UD QP RTR");
    CHECK_RC(modify_ud_qp_to_rts(qp), "UD QP RTS");
    printf("UD QP state: RESET -> INIT -> RTR -> RTS\n");

    /* ---- Gather local UD info ---- */
    struct ibv_port_attr port_attr;
    CHECK_ERRNO(ibv_query_port(ctx, ib_port, &port_attr),
                "ibv_query_port");

    union ibv_gid local_gid;
    CHECK_ERRNO(ibv_query_gid(ctx, ib_port, gid_index, &local_gid),
                "ibv_query_gid");

    struct ud_info local_ud_info, remote_ud_info;
    local_ud_info.qp_num = qp->qp_num;
    local_ud_info.lid    = port_attr.lid;
    local_ud_info.gid    = local_gid;

    printf("Local UD info: QPN=0x%06x, LID=0x%04x\n",
           local_ud_info.qp_num, local_ud_info.lid);

    /* ---- TCP connection and info exchange ---- */
    int sockfd;
    if (is_receiver) {
        int listen_fd = tcp_server_listen(tcp_port);
        CHECK_NULL((listen_fd >= 0) ? (void *)1 : NULL, "tcp_server_listen");
        sockfd = tcp_server_accept(listen_fd);
        CHECK_NULL((sockfd >= 0) ? (void *)1 : NULL, "tcp_server_accept");
        close(listen_fd);
    } else {
        sockfd = tcp_client_connect(server_name, tcp_port);
        CHECK_NULL((sockfd >= 0) ? (void *)1 : NULL, "tcp_client_connect");
    }

    CHECK_ERRNO(exchange_ud_info(sockfd, &local_ud_info, &remote_ud_info),
                "exchange_ud_info");
    printf("Remote UD info: QPN=0x%06x, LID=0x%04x\n",
           remote_ud_info.qp_num, remote_ud_info.lid);

    /* ---- Synchronize ---- */
    char sync = 'R';
    write(sockfd, &sync, 1);
    read(sockfd, &sync, 1);

    /* ---- Send/Receive ---- */
    struct ibv_wc wc;

    if (is_receiver) {
        printf("\nReceiver: posting %d receive buffers...\n", NUM_RECV_BUFS);

        /* Pre-post all receive buffers */
        for (int i = 0; i < NUM_RECV_BUFS; i++) {
            CHECK_ERRNO(post_ud_receive(qp, recv_bufs[i], recv_mr, i),
                        "post_ud_receive");
        }

        /* Receive messages */
        printf("Receiver: waiting for %d messages...\n\n", num_msgs);

        for (int i = 0; i < num_msgs; i++) {
            CHECK_ERRNO(poll_completion(cq, &wc), "poll recv");

            int buf_idx = wc.wr_id;
            char *grh_start = recv_bufs[buf_idx];
            char *msg_data  = grh_start + GRH_SIZE;

            /* Extract source GID from the GRH */
            struct ibv_grh *grh = (struct ibv_grh *)grh_start;
            (void)grh;  /* Available for inspection if needed */

            printf("  Received[%d]: \"%s\" (%u bytes, src_qp=0x%x)\n",
                   i, msg_data, wc.byte_len - GRH_SIZE, wc.src_qp);

            /* Re-post the receive buffer */
            CHECK_ERRNO(post_ud_receive(qp, recv_bufs[buf_idx],
                                         recv_mr, buf_idx),
                        "re-post_ud_receive");
        }

        printf("\nReceiver: all %d messages received.\n", num_msgs);
    } else {
        /* Create Address Handle for the receiver */
        struct ibv_ah_attr ah_attr;
        memset(&ah_attr, 0, sizeof(ah_attr));
        ah_attr.is_global      = 1;
        ah_attr.grh.dgid       = remote_ud_info.gid;
        ah_attr.grh.sgid_index = gid_index;
        ah_attr.grh.hop_limit  = 1;
        ah_attr.dlid           = remote_ud_info.lid;
        ah_attr.sl             = 0;
        ah_attr.src_path_bits  = 0;
        ah_attr.port_num       = ib_port;

        struct ibv_ah *ah = ibv_create_ah(pd, &ah_attr);
        CHECK_NULL(ah, "ibv_create_ah");

        printf("\nSender: sending %d messages...\n\n", num_msgs);

        for (int i = 0; i < num_msgs; i++) {
            snprintf(send_buf, MAX_MSG_SIZE, "UD datagram #%d from sender", i);
            uint32_t msg_len = strlen(send_buf) + 1;

            CHECK_ERRNO(post_ud_send(qp, send_buf, msg_len, send_mr,
                                      ah, remote_ud_info.qp_num, i),
                        "post_ud_send");
            CHECK_ERRNO(poll_completion(cq, &wc), "poll send");

            printf("  Sent[%d]: \"%s\" (%u bytes)\n", i, send_buf, msg_len);
        }

        printf("\nSender: all %d messages sent.\n", num_msgs);

        ibv_destroy_ah(ah);
    }

    /* ---- Cleanup ---- */
    close(sockfd);
    ibv_destroy_qp(qp);
    ibv_dereg_mr(send_mr);
    ibv_dereg_mr(recv_mr);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    free(send_buf);
    free(recv_pool);

    printf("\nDone.\n");
    return 0;
}
