/*
 * srq_example.c - Shared Receive Queue (SRQ) Example
 *
 * Demonstrates SRQ shared by multiple QPs using loopback.
 * Creates an SRQ, associates multiple RC QPs with it,
 * sends messages on different QPs, and receives them all
 * through the shared SRQ. Also demonstrates SRQ limit events.
 *
 * Build: gcc -o srq_example srq_example.c -libverbs -lpthread
 * Usage: ./srq_example [device_name]
 *
 * Note: Requires RDMA hardware or SoftRoCE (rxe). For loopback
 * testing, the QPs connect to each other on the same device.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>

#define NUM_QPS             4
#define SRQ_MAX_WR          64
#define SRQ_LIMIT           16
#define BUFFER_SIZE         256
#define NUM_SRQ_BUFFERS     32
#define CQ_SIZE             128
#define GRH_SIZE            40

/* Global resources */
struct {
    struct ibv_context      *ctx;
    struct ibv_pd           *pd;
    struct ibv_cq           *cq;
    struct ibv_srq          *srq;
    struct ibv_qp           *send_qps[NUM_QPS];
    struct ibv_qp           *recv_qps[NUM_QPS];
    struct ibv_mr           *pool_mr;
    struct ibv_mr           *send_mr;
    char                    *recv_pool;
    char                    send_bufs[NUM_QPS][BUFFER_SIZE];
    uint16_t                lid;
    uint8_t                 port_num;
    volatile int            async_running;
} g;

/* ------------------------------------------------------------------ */
/* Helper: get buffer pointer from index                               */
/* ------------------------------------------------------------------ */
static inline char *get_buffer(int idx)
{
    return g.recv_pool + idx * BUFFER_SIZE;
}

/* ------------------------------------------------------------------ */
/* Helper: post a receive WR to the SRQ                                */
/* ------------------------------------------------------------------ */
static int post_srq_recv(int buf_idx)
{
    struct ibv_sge sge = {
        .addr   = (uint64_t)(uintptr_t)get_buffer(buf_idx),
        .length = BUFFER_SIZE,
        .lkey   = g.pool_mr->lkey
    };

    struct ibv_recv_wr wr = {
        .wr_id   = (uint64_t)buf_idx,
        .sg_list = &sge,
        .num_sge = 1
    };

    struct ibv_recv_wr *bad_wr;
    return ibv_post_srq_recv(g.srq, &wr, &bad_wr);
}

/* ------------------------------------------------------------------ */
/* Helper: modify QP to a given state                                  */
/* ------------------------------------------------------------------ */
static int modify_qp_to_init(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr = {
        .qp_state        = IBV_QPS_INIT,
        .pkey_index      = 0,
        .port_num        = g.port_num,
        .qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                           IBV_ACCESS_REMOTE_WRITE
    };
    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_PKEY_INDEX |
        IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
}

static int modify_qp_to_rtr(struct ibv_qp *qp, uint32_t dest_qpn)
{
    struct ibv_qp_attr attr = {
        .qp_state           = IBV_QPS_RTR,
        .path_mtu           = IBV_MTU_1024,
        .dest_qp_num        = dest_qpn,
        .rq_psn             = 0,
        .max_dest_rd_atomic = 1,
        .min_rnr_timer      = 12,
        .ah_attr = {
            .dlid       = g.lid,
            .sl         = 0,
            .src_path_bits = 0,
            .is_global  = 0,
            .port_num   = g.port_num
        }
    };
    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
        IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
        IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
}

static int modify_qp_to_rts(struct ibv_qp *qp)
{
    struct ibv_qp_attr attr = {
        .qp_state      = IBV_QPS_RTS,
        .sq_psn        = 0,
        .timeout       = 14,
        .retry_cnt     = 7,
        .rnr_retry     = 7,
        .max_rd_atomic = 1
    };
    return ibv_modify_qp(qp, &attr,
        IBV_QP_STATE | IBV_QP_SQ_PSN | IBV_QP_TIMEOUT |
        IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
        IBV_QP_MAX_QP_RD_ATOMIC);
}

/* ------------------------------------------------------------------ */
/* Async event thread: monitors SRQ limit events                       */
/* ------------------------------------------------------------------ */
static void *async_event_thread(void *arg)
{
    (void)arg;

    while (g.async_running) {
        struct ibv_async_event event;
        int ret = ibv_get_async_event(g.ctx, &event);
        if (ret)
            break;

        switch (event.event_type) {
        case IBV_EVENT_SRQ_LIMIT_REACHED:
            printf("[ASYNC] SRQ limit reached! "
                   "Replenishing buffers...\n");

            /* Re-arm the SRQ limit */
            {
                struct ibv_srq_attr attr = {
                    .srq_limit = SRQ_LIMIT
                };
                ibv_modify_srq(g.srq, &attr, IBV_SRQ_LIMIT);
            }
            printf("[ASYNC] SRQ limit re-armed to %d\n", SRQ_LIMIT);
            break;

        default:
            printf("[ASYNC] Event: %d\n", event.event_type);
            break;
        }

        ibv_ack_async_event(&event);
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Initialize RDMA resources                                           */
/* ------------------------------------------------------------------ */
static int init_resources(const char *dev_name)
{
    struct ibv_device **dev_list;
    struct ibv_device *device = NULL;
    int num_devices;

    /* Get device list */
    dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list || num_devices == 0) {
        fprintf(stderr, "No RDMA devices found\n");
        return -1;
    }

    /* Find requested device or use first one */
    if (dev_name) {
        for (int i = 0; i < num_devices; i++) {
            if (strcmp(ibv_get_device_name(dev_list[i]),
                       dev_name) == 0) {
                device = dev_list[i];
                break;
            }
        }
        if (!device) {
            fprintf(stderr, "Device %s not found\n", dev_name);
            ibv_free_device_list(dev_list);
            return -1;
        }
    } else {
        device = dev_list[0];
    }

    printf("Using device: %s\n", ibv_get_device_name(device));

    /* Open device */
    g.ctx = ibv_open_device(device);
    ibv_free_device_list(dev_list);
    if (!g.ctx) {
        fprintf(stderr, "ibv_open_device failed\n");
        return -1;
    }

    /* Query port for LID */
    g.port_num = 1;
    struct ibv_port_attr port_attr;
    if (ibv_query_port(g.ctx, g.port_num, &port_attr)) {
        fprintf(stderr, "ibv_query_port failed\n");
        return -1;
    }
    g.lid = port_attr.lid;
    printf("Local LID: %u\n", g.lid);

    /* Allocate PD */
    g.pd = ibv_alloc_pd(g.ctx);
    if (!g.pd) {
        fprintf(stderr, "ibv_alloc_pd failed\n");
        return -1;
    }

    /* Create CQ (shared by all QPs) */
    g.cq = ibv_create_cq(g.ctx, CQ_SIZE, NULL, NULL, 0);
    if (!g.cq) {
        fprintf(stderr, "ibv_create_cq failed\n");
        return -1;
    }

    /* Create SRQ */
    struct ibv_srq_init_attr srq_attr = {
        .attr = {
            .max_wr  = SRQ_MAX_WR,
            .max_sge = 1
        }
    };

    g.srq = ibv_create_srq(g.pd, &srq_attr);
    if (!g.srq) {
        fprintf(stderr, "ibv_create_srq failed\n");
        return -1;
    }

    printf("SRQ created (max_wr=%u)\n", srq_attr.attr.max_wr);

    /* Set SRQ limit for notification */
    struct ibv_srq_attr limit_attr = {
        .srq_limit = SRQ_LIMIT
    };
    if (ibv_modify_srq(g.srq, &limit_attr, IBV_SRQ_LIMIT)) {
        fprintf(stderr, "ibv_modify_srq (set limit) failed\n");
        return -1;
    }
    printf("SRQ limit set to %d\n", SRQ_LIMIT);

    /* Allocate and register receive buffer pool */
    g.recv_pool = aligned_alloc(4096,
                                NUM_SRQ_BUFFERS * BUFFER_SIZE);
    if (!g.recv_pool) {
        fprintf(stderr, "aligned_alloc failed\n");
        return -1;
    }
    memset(g.recv_pool, 0, NUM_SRQ_BUFFERS * BUFFER_SIZE);

    g.pool_mr = ibv_reg_mr(g.pd, g.recv_pool,
                           NUM_SRQ_BUFFERS * BUFFER_SIZE,
                           IBV_ACCESS_LOCAL_WRITE);
    if (!g.pool_mr) {
        fprintf(stderr, "ibv_reg_mr (pool) failed\n");
        return -1;
    }

    /* Register send buffers */
    g.send_mr = ibv_reg_mr(g.pd, g.send_bufs,
                           sizeof(g.send_bufs),
                           IBV_ACCESS_LOCAL_WRITE);
    if (!g.send_mr) {
        fprintf(stderr, "ibv_reg_mr (send) failed\n");
        return -1;
    }

    /* Pre-fill SRQ with receive buffers */
    for (int i = 0; i < NUM_SRQ_BUFFERS; i++) {
        if (post_srq_recv(i)) {
            fprintf(stderr, "post_srq_recv failed at index %d\n", i);
            return -1;
        }
    }
    printf("Posted %d receive buffers to SRQ\n", NUM_SRQ_BUFFERS);

    /* Create QP pairs (send QP -> recv QP, loopback) */
    for (int i = 0; i < NUM_QPS; i++) {
        /* Send QP (no SRQ - sends only) */
        struct ibv_qp_init_attr send_attr = {
            .send_cq = g.cq,
            .recv_cq = g.cq,
            .qp_type = IBV_QPT_RC,
            .cap = {
                .max_send_wr  = 16,
                .max_recv_wr  = 1,   /* Minimal - not used for recv */
                .max_send_sge = 1,
                .max_recv_sge = 1
            }
        };
        g.send_qps[i] = ibv_create_qp(g.pd, &send_attr);
        if (!g.send_qps[i]) {
            fprintf(stderr, "ibv_create_qp (send %d) failed\n", i);
            return -1;
        }

        /* Recv QP (with SRQ) */
        struct ibv_qp_init_attr recv_attr = {
            .send_cq = g.cq,
            .recv_cq = g.cq,
            .srq     = g.srq,       /* Shared Receive Queue */
            .qp_type = IBV_QPT_RC,
            .cap = {
                .max_send_wr  = 16,
                .max_recv_wr  = 0,   /* Receives go through SRQ */
                .max_send_sge = 1,
                .max_recv_sge = 0
            }
        };
        g.recv_qps[i] = ibv_create_qp(g.pd, &recv_attr);
        if (!g.recv_qps[i]) {
            fprintf(stderr, "ibv_create_qp (recv %d) failed\n", i);
            return -1;
        }
    }

    printf("Created %d send QPs and %d recv QPs (with SRQ)\n",
           NUM_QPS, NUM_QPS);

    /* Connect QP pairs via loopback: send_qps[i] -> recv_qps[i] */
    for (int i = 0; i < NUM_QPS; i++) {
        /* Transition send QP */
        if (modify_qp_to_init(g.send_qps[i]) ||
            modify_qp_to_rtr(g.send_qps[i],
                             g.recv_qps[i]->qp_num) ||
            modify_qp_to_rts(g.send_qps[i])) {
            fprintf(stderr, "Failed to transition send QP %d\n", i);
            return -1;
        }

        /* Transition recv QP */
        if (modify_qp_to_init(g.recv_qps[i]) ||
            modify_qp_to_rtr(g.recv_qps[i],
                             g.send_qps[i]->qp_num) ||
            modify_qp_to_rts(g.recv_qps[i])) {
            fprintf(stderr, "Failed to transition recv QP %d\n", i);
            return -1;
        }
    }

    printf("All QP pairs connected (loopback)\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Run the SRQ demonstration                                           */
/* ------------------------------------------------------------------ */
static int run_demo(void)
{
    int ret;

    printf("\n=== Sending messages on %d different QPs ===\n", NUM_QPS);

    /* Send a message on each QP */
    for (int i = 0; i < NUM_QPS; i++) {
        snprintf(g.send_bufs[i], BUFFER_SIZE,
                 "Message from QP %d (qpn=%u)",
                 i, g.send_qps[i]->qp_num);

        struct ibv_sge sge = {
            .addr   = (uint64_t)(uintptr_t)g.send_bufs[i],
            .length = strlen(g.send_bufs[i]) + 1,
            .lkey   = g.send_mr->lkey
        };

        struct ibv_send_wr wr = {
            .wr_id      = (uint64_t)(100 + i),
            .sg_list    = &sge,
            .num_sge    = 1,
            .opcode     = IBV_WR_SEND,
            .send_flags = IBV_SEND_SIGNALED
        };

        struct ibv_send_wr *bad_wr;
        ret = ibv_post_send(g.send_qps[i], &wr, &bad_wr);
        if (ret) {
            fprintf(stderr, "ibv_post_send failed on QP %d\n", i);
            return -1;
        }

        printf("Sent on QP %d (send qpn=%u -> recv qpn=%u)\n",
               i, g.send_qps[i]->qp_num, g.recv_qps[i]->qp_num);
    }

    /* Poll for completions (send + receive) */
    int expected = NUM_QPS * 2;  /* N sends + N receives */
    int completed = 0;
    int recv_count = 0;

    printf("\nPolling for %d completions...\n", expected);

    while (completed < expected) {
        struct ibv_wc wc;
        ret = ibv_poll_cq(g.cq, 1, &wc);
        if (ret < 0) {
            fprintf(stderr, "ibv_poll_cq failed\n");
            return -1;
        }
        if (ret == 0)
            continue;

        if (wc.status != IBV_WC_SUCCESS) {
            fprintf(stderr, "Completion error: %s (wr_id=%lu)\n",
                    ibv_wc_status_str(wc.status), wc.wr_id);
            return -1;
        }

        completed++;

        if (wc.opcode == IBV_WC_RECV) {
            recv_count++;
            int buf_idx = (int)wc.wr_id;
            printf("  [RECV] Buffer %d, %u bytes, "
                   "received on qpn=%u: \"%s\"\n",
                   buf_idx, wc.byte_len, wc.qp_num,
                   get_buffer(buf_idx));

            /* Repost the buffer to the SRQ */
            memset(get_buffer(buf_idx), 0, BUFFER_SIZE);
            post_srq_recv(buf_idx);
        } else if (wc.opcode == IBV_WC_SEND) {
            printf("  [SEND] Completed (wr_id=%lu)\n", wc.wr_id);
        }
    }

    printf("\nAll completions received: %d sends, %d receives\n",
           NUM_QPS, recv_count);
    printf("All %d receives came through the shared SRQ!\n",
           recv_count);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Cleanup                                                             */
/* ------------------------------------------------------------------ */
static void cleanup(void)
{
    for (int i = 0; i < NUM_QPS; i++) {
        if (g.recv_qps[i]) ibv_destroy_qp(g.recv_qps[i]);
        if (g.send_qps[i]) ibv_destroy_qp(g.send_qps[i]);
    }
    if (g.send_mr)   ibv_dereg_mr(g.send_mr);
    if (g.pool_mr)   ibv_dereg_mr(g.pool_mr);
    if (g.srq)       ibv_destroy_srq(g.srq);
    if (g.cq)        ibv_destroy_cq(g.cq);
    if (g.pd)        ibv_dealloc_pd(g.pd);
    if (g.ctx)       ibv_close_device(g.ctx);
    if (g.recv_pool) free(g.recv_pool);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    const char *dev_name = (argc > 1) ? argv[1] : NULL;

    memset(&g, 0, sizeof(g));

    if (init_resources(dev_name)) {
        fprintf(stderr, "Failed to initialize resources\n");
        cleanup();
        return 1;
    }

    /* Start async event monitoring thread */
    pthread_t async_thread;
    g.async_running = 1;
    pthread_create(&async_thread, NULL, async_event_thread, NULL);

    /* Run the demonstration */
    int ret = run_demo();

    /* Stop async thread */
    g.async_running = 0;
    /* Note: ibv_get_async_event is blocking; in a production app
       you would use a pipe or eventfd to signal the thread. */

    /* Cleanup */
    cleanup();

    printf("\n%s\n", ret ? "FAILED" : "SUCCESS");
    return ret ? 1 : 0;
}
