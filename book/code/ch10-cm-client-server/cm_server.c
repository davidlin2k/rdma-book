/*
 * cm_server.c - RDMA_CM Server Example
 *
 * Demonstrates a complete RDMA server using librdmacm.
 * The server listens for incoming connections, accepts them,
 * receives a message from the client, sends a reply, and
 * disconnects cleanly.
 *
 * Build: gcc -o cm_server cm_server.c -lrdmacm -libverbs
 * Usage: ./cm_server [port]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

#define DEFAULT_PORT    12345
#define BUFFER_SIZE     1024
#define MAX_WR          16

/* Per-connection context */
struct conn_context {
    struct ibv_pd       *pd;
    struct ibv_cq       *cq;
    struct ibv_mr       *recv_mr;
    struct ibv_mr       *send_mr;
    char                recv_buf[BUFFER_SIZE];
    char                send_buf[BUFFER_SIZE];
};

/* ------------------------------------------------------------------ */
/* Helper: post a receive work request                                 */
/* ------------------------------------------------------------------ */
static int post_receive(struct rdma_cm_id *id)
{
    struct conn_context *ctx = (struct conn_context *)id->context;

    struct ibv_sge sge = {
        .addr   = (uint64_t)(uintptr_t)ctx->recv_buf,
        .length = BUFFER_SIZE,
        .lkey   = ctx->recv_mr->lkey
    };

    struct ibv_recv_wr wr = {
        .wr_id   = 0,
        .sg_list = &sge,
        .num_sge = 1
    };

    struct ibv_recv_wr *bad_wr;
    return ibv_post_recv(id->qp, &wr, &bad_wr);
}

/* ------------------------------------------------------------------ */
/* Helper: post a send work request                                    */
/* ------------------------------------------------------------------ */
static int post_send(struct rdma_cm_id *id, size_t len)
{
    struct conn_context *ctx = (struct conn_context *)id->context;

    struct ibv_sge sge = {
        .addr   = (uint64_t)(uintptr_t)ctx->send_buf,
        .length = len,
        .lkey   = ctx->send_mr->lkey
    };

    struct ibv_send_wr wr = {
        .wr_id      = 1,
        .sg_list    = &sge,
        .num_sge    = 1,
        .opcode     = IBV_WR_SEND,
        .send_flags = IBV_SEND_SIGNALED
    };

    struct ibv_send_wr *bad_wr;
    return ibv_post_send(id->qp, &wr, &bad_wr);
}

/* ------------------------------------------------------------------ */
/* Handle CONNECT_REQUEST: allocate resources and accept               */
/* ------------------------------------------------------------------ */
static int on_connect_request(struct rdma_cm_id *id)
{
    struct conn_context *ctx;
    int ret;

    printf("Received connection request\n");

    /* Allocate per-connection context */
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        perror("calloc");
        return -1;
    }
    id->context = ctx;

    /* Allocate protection domain */
    ctx->pd = ibv_alloc_pd(id->verbs);
    if (!ctx->pd) {
        fprintf(stderr, "ibv_alloc_pd failed\n");
        goto err_free_ctx;
    }

    /* Create completion queue */
    ctx->cq = ibv_create_cq(id->verbs, MAX_WR * 2, NULL, NULL, 0);
    if (!ctx->cq) {
        fprintf(stderr, "ibv_create_cq failed\n");
        goto err_dealloc_pd;
    }

    /* Register memory regions */
    ctx->recv_mr = ibv_reg_mr(ctx->pd, ctx->recv_buf, BUFFER_SIZE,
                              IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->recv_mr) {
        fprintf(stderr, "ibv_reg_mr (recv) failed\n");
        goto err_destroy_cq;
    }

    ctx->send_mr = ibv_reg_mr(ctx->pd, ctx->send_buf, BUFFER_SIZE,
                              IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->send_mr) {
        fprintf(stderr, "ibv_reg_mr (send) failed\n");
        goto err_dereg_recv;
    }

    /* Create queue pair */
    struct ibv_qp_init_attr qp_attr = {
        .send_cq = ctx->cq,
        .recv_cq = ctx->cq,
        .qp_type = IBV_QPT_RC,
        .cap = {
            .max_send_wr  = MAX_WR,
            .max_recv_wr  = MAX_WR,
            .max_send_sge = 1,
            .max_recv_sge = 1
        }
    };

    ret = rdma_create_qp(id, ctx->pd, &qp_attr);
    if (ret) {
        perror("rdma_create_qp");
        goto err_dereg_send;
    }

    /* Post receive buffer BEFORE accepting */
    ret = post_receive(id);
    if (ret) {
        fprintf(stderr, "post_receive failed\n");
        goto err_destroy_qp;
    }

    /* Accept the connection */
    struct rdma_conn_param conn_param = {
        .responder_resources = 1,
        .initiator_depth     = 1,
    };

    ret = rdma_accept(id, &conn_param);
    if (ret) {
        perror("rdma_accept");
        goto err_destroy_qp;
    }

    printf("Connection accepted\n");
    return 0;

err_destroy_qp:
    rdma_destroy_qp(id);
err_dereg_send:
    ibv_dereg_mr(ctx->send_mr);
err_dereg_recv:
    ibv_dereg_mr(ctx->recv_mr);
err_destroy_cq:
    ibv_destroy_cq(ctx->cq);
err_dealloc_pd:
    ibv_dealloc_pd(ctx->pd);
err_free_ctx:
    free(ctx);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Handle ESTABLISHED: connection is ready for data transfer           */
/* ------------------------------------------------------------------ */
static int on_established(struct rdma_cm_id *id)
{
    struct conn_context *ctx = (struct conn_context *)id->context;

    printf("Connection established\n");

    /* Poll for the client's message */
    struct ibv_wc wc;
    int ret;

    while ((ret = ibv_poll_cq(ctx->cq, 1, &wc)) == 0)
        ;  /* busy-wait */

    if (ret < 0) {
        fprintf(stderr, "ibv_poll_cq failed\n");
        return -1;
    }

    if (wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Completion error: %s\n",
                ibv_wc_status_str(wc.status));
        return -1;
    }

    printf("Received from client: %s\n", ctx->recv_buf);

    /* Send reply */
    snprintf(ctx->send_buf, BUFFER_SIZE,
             "Hello from server! Your message was %u bytes.",
             wc.byte_len);

    ret = post_send(id, strlen(ctx->send_buf) + 1);
    if (ret) {
        fprintf(stderr, "post_send failed\n");
        return -1;
    }

    /* Wait for send completion */
    while ((ret = ibv_poll_cq(ctx->cq, 1, &wc)) == 0)
        ;

    if (ret < 0 || wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Send completion error\n");
        return -1;
    }

    printf("Reply sent to client\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Handle DISCONNECTED: clean up resources                             */
/* ------------------------------------------------------------------ */
static int on_disconnected(struct rdma_cm_id *id)
{
    struct conn_context *ctx = (struct conn_context *)id->context;

    printf("Client disconnected\n");

    rdma_destroy_qp(id);
    ibv_dereg_mr(ctx->send_mr);
    ibv_dereg_mr(ctx->recv_mr);
    ibv_destroy_cq(ctx->cq);
    ibv_dealloc_pd(ctx->pd);
    free(ctx);

    rdma_destroy_id(id);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main event loop                                                     */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    uint16_t port = DEFAULT_PORT;
    if (argc > 1)
        port = (uint16_t)atoi(argv[1]);

    /* Create event channel */
    struct rdma_event_channel *channel = rdma_create_event_channel();
    if (!channel) {
        perror("rdma_create_event_channel");
        return 1;
    }

    /* Create listener CM ID */
    struct rdma_cm_id *listener;
    int ret = rdma_create_id(channel, &listener, NULL, RDMA_PS_TCP);
    if (ret) {
        perror("rdma_create_id");
        return 1;
    }

    /* Bind to address */
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    ret = rdma_bind_addr(listener, (struct sockaddr *)&addr);
    if (ret) {
        perror("rdma_bind_addr");
        return 1;
    }

    printf("Server bound to port %u\n", port);

    /* Start listening */
    ret = rdma_listen(listener, 10);
    if (ret) {
        perror("rdma_listen");
        return 1;
    }

    printf("Listening for connections...\n");

    /* Event loop */
    struct rdma_cm_event *event;
    int stop = 0;

    while (!stop && rdma_get_cm_event(channel, &event) == 0) {
        struct rdma_cm_event event_copy = *event;
        rdma_ack_cm_event(event);

        switch (event_copy.event) {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            ret = on_connect_request(event_copy.id);
            if (ret)
                stop = 1;
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            ret = on_established(event_copy.id);
            if (ret)
                stop = 1;
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            on_disconnected(event_copy.id);
            stop = 1;  /* Exit after first client disconnects */
            break;

        default:
            fprintf(stderr, "Unexpected event: %s\n",
                    rdma_event_str(event_copy.event));
            stop = 1;
            break;
        }
    }

    /* Cleanup */
    rdma_destroy_id(listener);
    rdma_destroy_event_channel(channel);

    printf("Server shut down\n");
    return 0;
}
