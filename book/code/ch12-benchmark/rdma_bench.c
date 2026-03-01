/*
 * rdma_bench.c - Simple RDMA Write Bandwidth Benchmark
 *
 * Measures RDMA Write bandwidth and message rate with configurable
 * message size, iteration count, and QP count.
 *
 * Usage:
 *   Server: ./rdma_bench -d mlx5_0 -s 4096 -n 100000
 *   Client: ./rdma_bench -d mlx5_0 -s 4096 -n 100000 <server_ip>
 *
 * Build: make
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>

#include <infiniband/verbs.h>

/* Defaults */
#define DEFAULT_MSG_SIZE    4096
#define DEFAULT_ITERS       100000
#define DEFAULT_WARMUP      5000
#define DEFAULT_QP_COUNT    1
#define DEFAULT_SQ_DEPTH    512
#define DEFAULT_CQ_DEPTH    1024
#define DEFAULT_PORT        18515
#define DEFAULT_IB_PORT     1
#define DEFAULT_GID_INDEX   0
#define SIGNAL_INTERVAL     32

/* Exchange data for connection setup */
struct conn_info {
    uint64_t addr;
    uint32_t rkey;
    uint32_t qpn;
    uint16_t lid;
    uint8_t  gid[16];
};

/* Benchmark configuration */
struct bench_config {
    const char *ib_devname;
    const char *server_name;  /* NULL for server mode */
    int         msg_size;
    int         iterations;
    int         warmup;
    int         qp_count;
    int         sq_depth;
    int         ib_port;
    int         gid_index;
    int         tcp_port;
    int         is_server;
};

/* Benchmark context */
struct bench_ctx {
    struct ibv_context    *ctx;
    struct ibv_pd         *pd;
    struct ibv_mr         *mr;
    struct ibv_cq         *cq;
    struct ibv_qp        **qps;
    void                  *buf;
    size_t                 buf_size;
    struct bench_config   *cfg;
    struct conn_info       local_info;
    struct conn_info       remote_info;
};

/* Statistics */
struct bench_stats {
    double bw_gbps;
    double msg_rate_mpps;
    double elapsed_sec;
    int    iterations;
    int    msg_size;
};

/* ---------- Helper Functions ---------- */

static double get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void die(const char *msg)
{
    fprintf(stderr, "ERROR: %s (errno=%d: %s)\n", msg, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

/* ---------- TCP Exchange ---------- */

static int tcp_server_listen(int port)
{
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) die("socket");

    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        die("bind");
    if (listen(sockfd, 1) < 0)
        die("listen");

    int connfd = accept(sockfd, NULL, NULL);
    if (connfd < 0) die("accept");

    close(sockfd);
    return connfd;
}

static int tcp_client_connect(const char *host, int port)
{
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        struct hostent *he = gethostbyname(host);
        if (!he) die("gethostbyname");
        memcpy(&addr.sin_addr, he->h_addr, he->h_length);
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) die("socket");

    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        die("connect");

    return sockfd;
}

static void exchange_info(int sockfd, struct conn_info *local,
                          struct conn_info *remote)
{
    if (write(sockfd, local, sizeof(*local)) != sizeof(*local))
        die("write conn_info");
    if (read(sockfd, remote, sizeof(*remote)) != sizeof(*remote))
        die("read conn_info");
}

/* ---------- RDMA Setup ---------- */

static struct ibv_context *open_device(const char *devname)
{
    struct ibv_device **dev_list;
    struct ibv_context *ctx = NULL;
    int num_devs;

    dev_list = ibv_get_device_list(&num_devs);
    if (!dev_list || num_devs == 0)
        die("No IB devices found");

    for (int i = 0; i < num_devs; i++) {
        if (!devname || strcmp(ibv_get_device_name(dev_list[i]), devname) == 0) {
            ctx = ibv_open_device(dev_list[i]);
            break;
        }
    }

    ibv_free_device_list(dev_list);
    if (!ctx) die("Failed to open IB device");
    return ctx;
}

static void init_ctx(struct bench_ctx *bctx, struct bench_config *cfg)
{
    bctx->cfg = cfg;

    /* Open device */
    bctx->ctx = open_device(cfg->ib_devname);

    /* Allocate PD */
    bctx->pd = ibv_alloc_pd(bctx->ctx);
    if (!bctx->pd) die("ibv_alloc_pd");

    /* Allocate and register buffer */
    bctx->buf_size = (size_t)cfg->msg_size * cfg->qp_count;
    bctx->buf = calloc(1, bctx->buf_size);
    if (!bctx->buf) die("calloc");

    bctx->mr = ibv_reg_mr(bctx->pd, bctx->buf, bctx->buf_size,
                           IBV_ACCESS_LOCAL_WRITE |
                           IBV_ACCESS_REMOTE_WRITE |
                           IBV_ACCESS_REMOTE_READ);
    if (!bctx->mr) die("ibv_reg_mr");

    /* Create CQ */
    bctx->cq = ibv_create_cq(bctx->ctx, DEFAULT_CQ_DEPTH, NULL, NULL, 0);
    if (!bctx->cq) die("ibv_create_cq");

    /* Create QPs */
    bctx->qps = calloc(cfg->qp_count, sizeof(struct ibv_qp *));
    if (!bctx->qps) die("calloc qps");

    for (int i = 0; i < cfg->qp_count; i++) {
        struct ibv_qp_init_attr qp_attr = {
            .send_cq = bctx->cq,
            .recv_cq = bctx->cq,
            .cap = {
                .max_send_wr  = cfg->sq_depth,
                .max_recv_wr  = 1,
                .max_send_sge = 1,
                .max_recv_sge = 1,
                .max_inline_data = 64,
            },
            .qp_type = IBV_QPT_RC,
        };

        bctx->qps[i] = ibv_create_qp(bctx->pd, &qp_attr);
        if (!bctx->qps[i]) die("ibv_create_qp");
    }

    /* Prepare local connection info (use first QP for exchange) */
    struct ibv_port_attr port_attr;
    if (ibv_query_port(bctx->ctx, cfg->ib_port, &port_attr))
        die("ibv_query_port");

    bctx->local_info.addr = (uint64_t)(uintptr_t)bctx->buf;
    bctx->local_info.rkey = bctx->mr->rkey;
    bctx->local_info.qpn  = bctx->qps[0]->qp_num;
    bctx->local_info.lid  = port_attr.lid;

    union ibv_gid gid;
    if (ibv_query_gid(bctx->ctx, cfg->ib_port, cfg->gid_index, &gid))
        die("ibv_query_gid");
    memcpy(bctx->local_info.gid, &gid, 16);
}

static void modify_qp_to_init(struct ibv_qp *qp, int ib_port)
{
    struct ibv_qp_attr attr = {
        .qp_state        = IBV_QPS_INIT,
        .pkey_index      = 0,
        .port_num        = ib_port,
        .qp_access_flags = IBV_ACCESS_REMOTE_WRITE |
                           IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_LOCAL_WRITE,
    };

    if (ibv_modify_qp(qp, &attr,
                       IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                       IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
        die("modify_qp_to_init");
}

static void modify_qp_to_rtr(struct ibv_qp *qp, struct conn_info *remote,
                              int ib_port, int gid_index)
{
    struct ibv_qp_attr attr = {
        .qp_state           = IBV_QPS_RTR,
        .path_mtu           = IBV_MTU_1024,
        .dest_qp_num        = remote->qpn,
        .rq_psn             = 0,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer      = 12,
        .ah_attr = {
            .dlid          = remote->lid,
            .sl            = 0,
            .src_path_bits = 0,
            .port_num      = ib_port,
            .is_global     = 1,
            .grh = {
                .dgid.global.subnet_prefix = 0,
                .dgid.global.interface_id  = 0,
                .flow_label  = 0,
                .hop_limit   = 1,
                .sgid_index  = gid_index,
                .traffic_class = 0,
            },
        },
    };
    memcpy(&attr.ah_attr.grh.dgid, remote->gid, 16);

    if (ibv_modify_qp(qp, &attr,
                       IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                       IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                       IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER))
        die("modify_qp_to_rtr");
}

static void modify_qp_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr = {
        .qp_state      = IBV_QPS_RTS,
        .timeout       = 14,
        .retry_cnt     = 7,
        .rnr_retry     = 7,
        .sq_psn        = 0,
        .max_rd_atomic = 1,
    };

    if (ibv_modify_qp(qp, &attr,
                       IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                       IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                       IBV_QP_MAX_RD_ATOMIC))
        die("modify_qp_to_rts");
}

static void connect_qps(struct bench_ctx *bctx, int sockfd)
{
    /* Exchange connection info */
    exchange_info(sockfd, &bctx->local_info, &bctx->remote_info);

    /* Transition all QPs: RESET -> INIT -> RTR -> RTS */
    for (int i = 0; i < bctx->cfg->qp_count; i++) {
        modify_qp_to_init(bctx->qps[i], bctx->cfg->ib_port);
        modify_qp_to_rtr(bctx->qps[i], &bctx->remote_info,
                          bctx->cfg->ib_port, bctx->cfg->gid_index);
        modify_qp_to_rts(bctx->qps[i]);
    }
}

/* ---------- Benchmark Core ---------- */

static int post_rdma_write(struct bench_ctx *bctx, int qp_idx, int signaled)
{
    struct ibv_sge sge = {
        .addr   = (uint64_t)(uintptr_t)bctx->buf + qp_idx * bctx->cfg->msg_size,
        .length = bctx->cfg->msg_size,
        .lkey   = bctx->mr->lkey,
    };

    struct ibv_send_wr wr = {
        .wr_id      = qp_idx,
        .sg_list    = &sge,
        .num_sge    = 1,
        .opcode     = IBV_WR_RDMA_WRITE,
        .send_flags = signaled ? IBV_SEND_SIGNALED : 0,
        .wr.rdma = {
            .remote_addr = bctx->remote_info.addr + qp_idx * bctx->cfg->msg_size,
            .rkey        = bctx->remote_info.rkey,
        },
    };

    /* Use inline for small messages */
    if (bctx->cfg->msg_size <= 64)
        wr.send_flags |= IBV_SEND_INLINE;

    struct ibv_send_wr *bad_wr;
    return ibv_post_send(bctx->qps[qp_idx], &wr, &bad_wr);
}

static int poll_completions(struct bench_ctx *bctx, int count)
{
    struct ibv_wc wc[32];
    int total = 0;

    while (total < count) {
        int n = ibv_poll_cq(bctx->cq, 32, wc);
        if (n < 0) die("ibv_poll_cq");

        for (int i = 0; i < n; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                fprintf(stderr, "Completion error: %s (status=%d)\n",
                        ibv_wc_status_str(wc[i].status), wc[i].status);
                return -1;
            }
        }
        total += n;
    }
    return total;
}

static struct bench_stats run_benchmark(struct bench_ctx *bctx)
{
    struct bench_config *cfg = bctx->cfg;
    int total_ops = cfg->iterations * cfg->qp_count;
    int signal_cnt = 0;
    int outstanding_cqes = 0;

    /* Warm-up phase */
    printf("Warm-up: %d iterations...\n", cfg->warmup);
    for (int i = 0; i < cfg->warmup; i++) {
        for (int q = 0; q < cfg->qp_count; q++) {
            int signaled = ((i % SIGNAL_INTERVAL) == (SIGNAL_INTERVAL - 1));
            if (post_rdma_write(bctx, q, signaled))
                die("post_rdma_write (warmup)");
            if (signaled)
                outstanding_cqes++;
        }
    }
    /* Drain warm-up completions */
    if (outstanding_cqes > 0)
        poll_completions(bctx, outstanding_cqes);

    /* Measurement phase */
    printf("Measuring: %d iterations, %d QPs, %d byte messages...\n",
           cfg->iterations, cfg->qp_count, cfg->msg_size);

    outstanding_cqes = 0;
    signal_cnt = 0;

    double start = get_time_sec();

    for (int i = 0; i < cfg->iterations; i++) {
        for (int q = 0; q < cfg->qp_count; q++) {
            int signaled = ((signal_cnt % SIGNAL_INTERVAL) == (SIGNAL_INTERVAL - 1));
            signal_cnt++;

            if (post_rdma_write(bctx, q, signaled))
                die("post_rdma_write");

            if (signaled)
                outstanding_cqes++;

            /* Prevent send queue overflow: poll when half-full */
            if (outstanding_cqes >= cfg->sq_depth / (2 * SIGNAL_INTERVAL)) {
                poll_completions(bctx, 1);
                outstanding_cqes--;
            }
        }
    }

    /* Drain remaining completions */
    if (outstanding_cqes > 0)
        poll_completions(bctx, outstanding_cqes);

    double end = get_time_sec();

    /* Calculate statistics */
    struct bench_stats stats;
    stats.elapsed_sec = end - start;
    stats.iterations = cfg->iterations;
    stats.msg_size = cfg->msg_size;
    stats.msg_rate_mpps = (double)total_ops / stats.elapsed_sec / 1e6;
    stats.bw_gbps = (double)total_ops * cfg->msg_size * 8.0
                    / stats.elapsed_sec / 1e9;

    return stats;
}

static void print_stats(struct bench_stats *stats)
{
    printf("\n");
    printf("=== RDMA Write Benchmark Results ===\n");
    printf("Message size:    %d bytes\n", stats->msg_size);
    printf("Iterations:      %d\n", stats->iterations);
    printf("Elapsed time:    %.3f seconds\n", stats->elapsed_sec);
    printf("Bandwidth:       %.2f Gbps (%.2f GB/s)\n",
           stats->bw_gbps, stats->bw_gbps / 8.0);
    printf("Message rate:    %.2f Mpps\n", stats->msg_rate_mpps);
    printf("====================================\n");
}

/* ---------- Cleanup ---------- */

static void cleanup(struct bench_ctx *bctx)
{
    if (bctx->qps) {
        for (int i = 0; i < bctx->cfg->qp_count; i++) {
            if (bctx->qps[i])
                ibv_destroy_qp(bctx->qps[i]);
        }
        free(bctx->qps);
    }
    if (bctx->cq) ibv_destroy_cq(bctx->cq);
    if (bctx->mr) ibv_dereg_mr(bctx->mr);
    if (bctx->pd) ibv_dealloc_pd(bctx->pd);
    if (bctx->buf) free(bctx->buf);
    if (bctx->ctx) ibv_close_device(bctx->ctx);
}

/* ---------- Main ---------- */

static void print_usage(const char *prog)
{
    printf("Usage: %s [options] [server_ip]\n", prog);
    printf("Options:\n");
    printf("  -d <dev>    IB device name (default: first device)\n");
    printf("  -s <size>   Message size in bytes (default: %d)\n", DEFAULT_MSG_SIZE);
    printf("  -n <iters>  Number of iterations (default: %d)\n", DEFAULT_ITERS);
    printf("  -q <count>  Number of QPs (default: %d)\n", DEFAULT_QP_COUNT);
    printf("  -p <port>   TCP port for exchange (default: %d)\n", DEFAULT_PORT);
    printf("  -w <warmup> Warm-up iterations (default: %d)\n", DEFAULT_WARMUP);
    printf("  -h          Show this help\n");
    printf("\n");
    printf("Run without server_ip for server mode.\n");
    printf("Run with server_ip for client mode.\n");
}

int main(int argc, char *argv[])
{
    struct bench_config cfg = {
        .ib_devname = NULL,
        .server_name = NULL,
        .msg_size   = DEFAULT_MSG_SIZE,
        .iterations = DEFAULT_ITERS,
        .warmup     = DEFAULT_WARMUP,
        .qp_count   = DEFAULT_QP_COUNT,
        .sq_depth   = DEFAULT_SQ_DEPTH,
        .ib_port    = DEFAULT_IB_PORT,
        .gid_index  = DEFAULT_GID_INDEX,
        .tcp_port   = DEFAULT_PORT,
        .is_server  = 1,
    };

    int opt;
    while ((opt = getopt(argc, argv, "d:s:n:q:p:w:h")) != -1) {
        switch (opt) {
        case 'd': cfg.ib_devname = optarg; break;
        case 's': cfg.msg_size   = atoi(optarg); break;
        case 'n': cfg.iterations = atoi(optarg); break;
        case 'q': cfg.qp_count  = atoi(optarg); break;
        case 'p': cfg.tcp_port  = atoi(optarg); break;
        case 'w': cfg.warmup    = atoi(optarg); break;
        case 'h':
            print_usage(argv[0]);
            return 0;
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    /* Remaining argument is server IP (client mode) */
    if (optind < argc) {
        cfg.server_name = argv[optind];
        cfg.is_server = 0;
    }

    printf("RDMA Write Bandwidth Benchmark\n");
    printf("Mode: %s\n", cfg.is_server ? "Server" : "Client");
    printf("Device: %s, Port: %d\n",
           cfg.ib_devname ? cfg.ib_devname : "auto", cfg.ib_port);
    printf("Message size: %d, Iterations: %d, QPs: %d\n",
           cfg.msg_size, cfg.iterations, cfg.qp_count);

    /* Initialize RDMA context */
    struct bench_ctx bctx = {};
    init_ctx(&bctx, &cfg);

    /* Establish TCP connection and exchange RDMA info */
    int sockfd;
    if (cfg.is_server) {
        printf("Waiting for client connection on port %d...\n", cfg.tcp_port);
        sockfd = tcp_server_listen(cfg.tcp_port);
    } else {
        printf("Connecting to %s:%d...\n", cfg.server_name, cfg.tcp_port);
        sockfd = tcp_client_connect(cfg.server_name, cfg.tcp_port);
    }

    connect_qps(&bctx, sockfd);
    printf("QPs connected.\n");

    /* Synchronize before starting benchmark */
    char sync = 'R';
    write(sockfd, &sync, 1);
    read(sockfd, &sync, 1);

    /* Run benchmark (client sends, server receives) */
    if (!cfg.is_server) {
        struct bench_stats stats = run_benchmark(&bctx);
        print_stats(&stats);
    } else {
        printf("Server waiting for client to complete...\n");
        /* Wait for client to signal completion */
        read(sockfd, &sync, 1);
        printf("Client completed.\n");
    }

    /* Signal completion */
    if (!cfg.is_server) {
        write(sockfd, &sync, 1);
    }

    /* Cleanup */
    close(sockfd);
    cleanup(&bctx);

    return 0;
}
