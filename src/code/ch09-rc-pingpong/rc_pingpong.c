/*
 * rc_pingpong.c - RC Send/Receive Ping-Pong Example
 *
 * Demonstrates the fundamental RC Send/Receive pattern:
 *   - Server and client roles in a single binary
 *   - TCP-based out-of-band QP metadata exchange
 *   - Manual QP state transitions: RESET -> INIT -> RTR -> RTS
 *   - Alternating send/receive (ping-pong) for N iterations
 *   - Latency measurement and statistics
 *
 * Usage:
 *   Server:  ./rc_pingpong -s [-d <device>] [-p <port>] [-n <iters>] [-m <size>]
 *   Client:  ./rc_pingpong -c <server_ip> [-d <device>] [-p <port>] [-n <iters>] [-m <size>]
 *
 * Build:
 *   make    (or: gcc -o rc_pingpong rc_pingpong.c ../common/rdma_common.c -libverbs)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <inttypes.h>

#include "../common/rdma_common.h"

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
    attr.qp_access_flags = 0;  /* No remote access needed for Send/Recv */

    int flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                IBV_QP_PORT  | IBV_QP_ACCESS_FLAGS;
    int ret = ibv_modify_qp(qp, &attr, flags);
    if (ret)
        fprintf(stderr, "modify_qp_to_init failed: %s\n", strerror(ret));
    return ret;
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
    attr.max_dest_rd_atomic     = 0;
    attr.min_rnr_timer          = 12;
    attr.ah_attr.is_global      = 1;
    attr.ah_attr.grh.dgid       = remote->gid;
    attr.ah_attr.grh.sgid_index = gid_index;
    attr.ah_attr.grh.hop_limit  = 1;
    attr.ah_attr.dlid           = remote->lid;
    attr.ah_attr.sl             = 0;
    attr.ah_attr.src_path_bits  = 0;
    attr.ah_attr.port_num       = ib_port;

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    int ret = ibv_modify_qp(qp, &attr, flags);
    if (ret)
        fprintf(stderr, "modify_qp_to_rtr failed: %s\n", strerror(ret));
    return ret;
}

static int modify_qp_to_rts(struct ibv_qp *qp, uint32_t local_psn)
{
    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state      = IBV_QPS_RTS;
    attr.sq_psn        = local_psn;
    attr.timeout       = 14;   /* ~67 ms */
    attr.retry_cnt     = 7;    /* Max retries */
    attr.rnr_retry     = 7;    /* Infinite RNR retries */
    attr.max_rd_atomic = 0;

    int flags = IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
                IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                IBV_QP_MAX_QP_RD_ATOMIC;
    int ret = ibv_modify_qp(qp, &attr, flags);
    if (ret)
        fprintf(stderr, "modify_qp_to_rts failed: %s\n", strerror(ret));
    return ret;
}

/* --------------------------------------------------------------------------
 * Post send / receive helpers
 * -------------------------------------------------------------------------- */

static int post_receive(struct ibv_qp *qp, void *buf, uint32_t len,
                         struct ibv_mr *mr, uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr   = (uintptr_t)buf,
        .length = len,
        .lkey   = mr->lkey,
    };
    struct ibv_recv_wr wr = {
        .wr_id   = wr_id,
        .sg_list = &sge,
        .num_sge = 1,
        .next    = NULL,
    };
    struct ibv_recv_wr *bad_wr;
    return ibv_post_recv(qp, &wr, &bad_wr);
}

static int post_send(struct ibv_qp *qp, void *buf, uint32_t len,
                      struct ibv_mr *mr, uint64_t wr_id)
{
    struct ibv_sge sge = {
        .addr   = (uintptr_t)buf,
        .length = len,
        .lkey   = mr->lkey,
    };
    struct ibv_send_wr wr = {
        .wr_id      = wr_id,
        .sg_list    = &sge,
        .num_sge    = 1,
        .opcode     = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED,
        .next       = NULL,
    };
    struct ibv_send_wr *bad_wr;
    return ibv_post_send(qp, &wr, &bad_wr);
}

/* --------------------------------------------------------------------------
 * Time helpers
 * -------------------------------------------------------------------------- */

static uint64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* --------------------------------------------------------------------------
 * Usage
 * -------------------------------------------------------------------------- */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage:\n"
        "  Server: %s -s [-d device] [-p port] [-n iters] [-m size]\n"
        "  Client: %s -c <server_ip> [-d device] [-p port] [-n iters] [-m size]\n"
        "\n"
        "Options:\n"
        "  -s              Run as server\n"
        "  -c <server_ip>  Run as client, connect to server\n"
        "  -d <device>     RDMA device name (default: first device)\n"
        "  -p <port>       TCP port for metadata exchange (default: %d)\n"
        "  -n <iters>      Number of ping-pong iterations (default: %d)\n"
        "  -m <size>       Message size in bytes (default: %d)\n"
        "  -g <gid_idx>    GID index (default: %d)\n",
        prog, prog, DEFAULT_PORT, DEFAULT_NUM_ITERS,
        DEFAULT_MSG_SIZE, DEFAULT_GID_INDEX);
}

/* --------------------------------------------------------------------------
 * Main
 * -------------------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    /* Parse command-line arguments */
    int is_server = -1;  /* -1 = unset */
    char *server_name = NULL;
    char *dev_name    = NULL;
    int tcp_port      = DEFAULT_PORT;
    int num_iters     = DEFAULT_NUM_ITERS;
    int msg_size      = DEFAULT_MSG_SIZE;
    int ib_port       = DEFAULT_IB_PORT;
    int gid_index     = DEFAULT_GID_INDEX;
    int opt;

    while ((opt = getopt(argc, argv, "sc:d:p:n:m:g:")) != -1) {
        switch (opt) {
        case 's': is_server = 1; break;
        case 'c': is_server = 0; server_name = optarg; break;
        case 'd': dev_name = optarg; break;
        case 'p': tcp_port = atoi(optarg); break;
        case 'n': num_iters = atoi(optarg); break;
        case 'm': msg_size = atoi(optarg); break;
        case 'g': gid_index = atoi(optarg); break;
        default:  usage(argv[0]); exit(EXIT_FAILURE);
        }
    }

    if (is_server < 0) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Seed random number generator for PSN */
    srand48(getpid() * time(NULL));

    printf("=== RC Send/Receive Ping-Pong ===\n");
    printf("Role: %s, Message size: %d bytes, Iterations: %d\n",
           is_server ? "Server" : "Client", msg_size, num_iters);

    /* ---- Step 1: Open RDMA device ---- */
    struct ibv_context *ctx = open_device(dev_name);
    CHECK_NULL(ctx, "open_device");

    /* ---- Step 2: Allocate Protection Domain ---- */
    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    CHECK_NULL(pd, "ibv_alloc_pd");

    /* ---- Step 3: Create Completion Queue ---- */
    struct ibv_cq *cq = ibv_create_cq(ctx, CQ_DEPTH, NULL, NULL, 0);
    CHECK_NULL(cq, "ibv_create_cq");

    /* ---- Step 4: Create Queue Pair ---- */
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

    /* ---- Step 5: Register Memory Region ---- */
    char *buf = calloc(1, msg_size);
    CHECK_NULL(buf, "calloc");

    struct ibv_mr *mr = ibv_reg_mr(pd, buf, msg_size,
                                    IBV_ACCESS_LOCAL_WRITE);
    CHECK_NULL(mr, "ibv_reg_mr");

    /* ---- Step 6: Gather local QP info ---- */
    struct qp_info local_info, remote_info;
    CHECK_ERRNO(get_local_info(ctx, qp, ib_port, gid_index, &local_info),
                "get_local_info");

    /* ---- Step 7: Establish TCP connection and exchange QP info ---- */
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

    CHECK_ERRNO(exchange_qp_info(sockfd, &local_info, &remote_info),
                "exchange_qp_info");

    printf("Exchanged QP info:\n");
    print_qp_info("Local ", &local_info);
    print_qp_info("Remote", &remote_info);

    /* ---- Step 8: Transition QP: RESET -> INIT -> RTR -> RTS ---- */
    CHECK_RC(modify_qp_to_init(qp, ib_port), "QP INIT");
    printf("QP state: RESET -> INIT\n");

    CHECK_RC(modify_qp_to_rtr(qp, ib_port, &remote_info, gid_index),
             "QP RTR");
    printf("QP state: INIT -> RTR\n");

    CHECK_RC(modify_qp_to_rts(qp, local_info.psn), "QP RTS");
    printf("QP state: RTR -> RTS\n");

    /* ---- Step 9: Synchronize with peer (barrier via TCP) ---- */
    char sync_byte = 'R';
    write(sockfd, &sync_byte, 1);
    read(sockfd, &sync_byte, 1);
    printf("Peer synchronized.\n\n");

    /* ---- Step 10: Ping-pong loop ---- */
    struct ibv_wc wc;
    uint64_t start_ns, end_ns;
    uint64_t total_ns = 0;

    printf("Starting ping-pong: %d iterations, %d bytes...\n",
           num_iters, msg_size);

    start_ns = get_time_ns();

    for (int i = 0; i < num_iters; i++) {
        if (is_server) {
            /*
             * Server: receive first, then send back.
             * Post receive, wait for incoming message, then echo it back.
             */
            CHECK_ERRNO(post_receive(qp, buf, msg_size, mr, i),
                        "post_receive");
            CHECK_ERRNO(poll_completion(cq, &wc), "poll recv completion");

            /* Echo the received data back */
            CHECK_ERRNO(post_send(qp, buf, msg_size, mr, i),
                        "post_send");
            CHECK_ERRNO(poll_completion(cq, &wc), "poll send completion");
        } else {
            /*
             * Client: send first, then receive.
             * Pre-post receive buffer, send the message, wait for
             * send completion, then wait for the reply.
             */
            snprintf(buf, msg_size, "ping-%06d", i);

            /* Pre-post receive for the reply */
            CHECK_ERRNO(post_receive(qp, buf, msg_size, mr, i),
                        "post_receive");

            /* Send the ping */
            CHECK_ERRNO(post_send(qp, buf, msg_size, mr, i),
                        "post_send");
            CHECK_ERRNO(poll_completion(cq, &wc), "poll send completion");

            /* Wait for the pong */
            CHECK_ERRNO(poll_completion(cq, &wc), "poll recv completion");
        }
    }

    end_ns = get_time_ns();
    total_ns = end_ns - start_ns;

    /* ---- Step 11: Print statistics ---- */
    double total_us = (double)total_ns / 1000.0;
    double avg_rtt_us = total_us / num_iters;
    double avg_one_way_us = avg_rtt_us / 2.0;

    printf("\n=== Results ===\n");
    printf("Iterations:         %d\n", num_iters);
    printf("Message size:       %d bytes\n", msg_size);
    printf("Total time:         %.2f us\n", total_us);
    printf("Avg round-trip:     %.2f us\n", avg_rtt_us);
    printf("Avg one-way:        %.2f us\n", avg_one_way_us);

    if (!is_server) {
        double msgs_per_sec = (double)num_iters * 2.0 /
                              ((double)total_ns / 1e9);
        double bw_mbps = msgs_per_sec * msg_size * 8.0 / 1e6;
        printf("Messages/sec:       %.0f\n", msgs_per_sec);
        printf("Bandwidth:          %.2f Mbps\n", bw_mbps);
    }

    /* ---- Step 12: Cleanup ---- */
    close(sockfd);
    ibv_destroy_qp(qp);
    ibv_dereg_mr(mr);
    ibv_destroy_cq(cq);
    ibv_dealloc_pd(pd);
    ibv_close_device(ctx);
    free(buf);

    printf("\nDone.\n");
    return 0;
}
