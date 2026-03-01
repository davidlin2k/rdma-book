/*
 * cm_client.c - RDMA_CM Client Example
 *
 * Demonstrates a complete RDMA client using librdmacm.
 * The client connects to the server, sends a message,
 * receives a reply, and disconnects cleanly.
 *
 * Build: gcc -o cm_client cm_client.c -lrdmacm -libverbs
 * Usage: ./cm_client <server-ip> [port]
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

/* Connection context */
struct conn_context {
    struct ibv_pd       *pd;
    struct ibv_cq       *cq;
    struct ibv_mr       *recv_mr;
    struct ibv_mr       *send_mr;
    char                recv_buf[BUFFER_SIZE];
    char                send_buf[BUFFER_SIZE];
    int                 connected;
};

static struct conn_context *g_ctx = NULL;

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
/* Handle ADDR_RESOLVED: resolve route next                            */
/* ------------------------------------------------------------------ */
static int on_addr_resolved(struct rdma_cm_id *id)
{
    printf("Address resolved\n");
    return rdma_resolve_route(id, 2000);
}

/* ------------------------------------------------------------------ */
/* Handle ROUTE_RESOLVED: create resources and connect                 */
/* ------------------------------------------------------------------ */
static int on_route_resolved(struct rdma_cm_id *id)
{
    struct conn_context *ctx;
    int ret;

    printf("Route resolved\n");

    /* Allocate connection context */
    ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        perror("calloc");
        return -1;
    }
    id->context = ctx;
    g_ctx = ctx;

    /* Allocate protection domain */
    ctx->pd = ibv_alloc_pd(id->verbs);
    if (!ctx->pd) {
        fprintf(stderr, "ibv_alloc_pd failed\n");
        goto err_free;
    }

    /* Create completion queue */
    ctx->cq = ibv_create_cq(id->verbs, MAX_WR * 2, NULL, NULL, 0);
    if (!ctx->cq) {
        fprintf(stderr, "ibv_create_cq failed\n");
        goto err_pd;
    }

    /* Register memory regions */
    ctx->recv_mr = ibv_reg_mr(ctx->pd, ctx->recv_buf, BUFFER_SIZE,
                              IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->recv_mr) {
        fprintf(stderr, "ibv_reg_mr (recv) failed\n");
        goto err_cq;
    }

    ctx->send_mr = ibv_reg_mr(ctx->pd, ctx->send_buf, BUFFER_SIZE,
                              IBV_ACCESS_LOCAL_WRITE);
    if (!ctx->send_mr) {
        fprintf(stderr, "ibv_reg_mr (send) failed\n");
        goto err_recv_mr;
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
        goto err_send_mr;
    }

    /* Post receive buffer BEFORE connecting */
    ret = post_receive(id);
    if (ret) {
        fprintf(stderr, "post_receive failed\n");
        goto err_qp;
    }

    /* Connect to server */
    struct rdma_conn_param conn_param = {
        .responder_resources = 1,
        .initiator_depth     = 1,
        .retry_count         = 7,
        .rnr_retry_count     = 7,
    };

    ret = rdma_connect(id, &conn_param);
    if (ret) {
        perror("rdma_connect");
        goto err_qp;
    }

    return 0;

err_qp:
    rdma_destroy_qp(id);
err_send_mr:
    ibv_dereg_mr(ctx->send_mr);
err_recv_mr:
    ibv_dereg_mr(ctx->recv_mr);
err_cq:
    ibv_destroy_cq(ctx->cq);
err_pd:
    ibv_dealloc_pd(ctx->pd);
err_free:
    free(ctx);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Handle ESTABLISHED: send message and receive reply                  */
/* ------------------------------------------------------------------ */
static int on_established(struct rdma_cm_id *id)
{
    struct conn_context *ctx = (struct conn_context *)id->context;
    int ret;

    printf("Connection established\n");
    ctx->connected = 1;

    /* Send a message to the server */
    snprintf(ctx->send_buf, BUFFER_SIZE,
             "Hello from RDMA_CM client!");

    ret = post_send(id, strlen(ctx->send_buf) + 1);
    if (ret) {
        fprintf(stderr, "post_send failed\n");
        return -1;
    }

    /* Wait for send completion */
    struct ibv_wc wc;
    while ((ret = ibv_poll_cq(ctx->cq, 1, &wc)) == 0)
        ;

    if (ret < 0 || wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Send completion error: %s\n",
                ibv_wc_status_str(wc.status));
        return -1;
    }

    printf("Message sent to server\n");

    /* Wait for server's reply */
    while ((ret = ibv_poll_cq(ctx->cq, 1, &wc)) == 0)
        ;

    if (ret < 0 || wc.status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Recv completion error: %s\n",
                ibv_wc_status_str(wc.status));
        return -1;
    }

    printf("Received from server: %s\n", ctx->recv_buf);

    /* Disconnect */
    rdma_disconnect(id);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Handle DISCONNECTED: clean up resources                             */
/* ------------------------------------------------------------------ */
static int on_disconnected(struct rdma_cm_id *id)
{
    struct conn_context *ctx = (struct conn_context *)id->context;

    printf("Disconnected from server\n");

    rdma_destroy_qp(id);
    ibv_dereg_mr(ctx->send_mr);
    ibv_dereg_mr(ctx->recv_mr);
    ibv_destroy_cq(ctx->cq);
    ibv_dealloc_pd(ctx->pd);
    free(ctx);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <server-ip> [port]\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    uint16_t port = DEFAULT_PORT;
    if (argc > 2)
        port = (uint16_t)atoi(argv[2]);

    /* Create event channel */
    struct rdma_event_channel *channel = rdma_create_event_channel();
    if (!channel) {
        perror("rdma_create_event_channel");
        return 1;
    }

    /* Create CM ID */
    struct rdma_cm_id *conn;
    int ret = rdma_create_id(channel, &conn, NULL, RDMA_PS_TCP);
    if (ret) {
        perror("rdma_create_id");
        return 1;
    }

    /* Resolve server address */
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons(port)
    };
    inet_pton(AF_INET, server_ip, &addr.sin_addr);

    ret = rdma_resolve_addr(conn, NULL, (struct sockaddr *)&addr, 2000);
    if (ret) {
        perror("rdma_resolve_addr");
        return 1;
    }

    printf("Resolving address %s:%u...\n", server_ip, port);

    /* Event loop */
    struct rdma_cm_event *event;
    int stop = 0;

    while (!stop && rdma_get_cm_event(channel, &event) == 0) {
        struct rdma_cm_event event_copy = *event;
        rdma_ack_cm_event(event);

        switch (event_copy.event) {
        case RDMA_CM_EVENT_ADDR_RESOLVED:
            ret = on_addr_resolved(event_copy.id);
            if (ret) stop = 1;
            break;

        case RDMA_CM_EVENT_ROUTE_RESOLVED:
            ret = on_route_resolved(event_copy.id);
            if (ret) stop = 1;
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            ret = on_established(event_copy.id);
            if (ret) stop = 1;
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            on_disconnected(event_copy.id);
            stop = 1;
            break;

        case RDMA_CM_EVENT_REJECTED:
            fprintf(stderr, "Connection rejected (status=%d)\n",
                    event_copy.status);
            stop = 1;
            break;

        case RDMA_CM_EVENT_ADDR_ERROR:
        case RDMA_CM_EVENT_ROUTE_ERROR:
        case RDMA_CM_EVENT_CONNECT_ERROR:
        case RDMA_CM_EVENT_UNREACHABLE:
            fprintf(stderr, "Error event: %s (status=%d)\n",
                    rdma_event_str(event_copy.event),
                    event_copy.status);
            stop = 1;
            break;

        default:
            fprintf(stderr, "Unexpected event: %s\n",
                    rdma_event_str(event_copy.event));
            stop = 1;
            break;
        }
    }

    /* Cleanup */
    rdma_destroy_id(conn);
    rdma_destroy_event_channel(channel);

    printf("Client shut down\n");
    return 0;
}
