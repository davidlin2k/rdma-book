/*
 * rdma_common.c - Implementation of common RDMA helper functions
 *
 * Provides TCP-based metadata exchange, device management, and
 * completion polling helpers used by all chapter 9 examples.
 */

#include "rdma_common.h"

/* --------------------------------------------------------------------------
 * TCP helper functions
 * -------------------------------------------------------------------------- */

int tcp_client_connect(const char *server_name, int port)
{
    struct addrinfo hints, *res, *rp;
    char port_str[16];
    int sockfd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;    /* IPv4 or IPv6 */
    hints.ai_socktype = SOCK_STREAM;  /* TCP */

    snprintf(port_str, sizeof(port_str), "%d", port);

    int ret = getaddrinfo(server_name, port_str, &hints, &res);
    if (ret) {
        fprintf(stderr, "getaddrinfo(%s): %s\n",
                server_name, gai_strerror(ret));
        return -1;
    }

    /* Try each address until one succeeds */
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0)
            continue;

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0)
            break;  /* Success */

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(res);

    if (sockfd < 0) {
        fprintf(stderr, "Could not connect to %s:%d\n", server_name, port);
        return -1;
    }

    return sockfd;
}

int tcp_server_listen(int port)
{
    struct sockaddr_in addr;
    int sockfd;
    int optval = 1;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    /* Allow rapid reuse of the port */
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    if (listen(sockfd, 1) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    printf("Listening on TCP port %d...\n", port);
    return sockfd;
}

int tcp_server_accept(int listen_fd)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int connfd = accept(listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (connfd < 0) {
        perror("accept");
        return -1;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    printf("Accepted connection from %s:%d\n",
           client_ip, ntohs(client_addr.sin_port));

    return connfd;
}

/* --------------------------------------------------------------------------
 * RDMA metadata exchange
 * -------------------------------------------------------------------------- */

/*
 * Reliable read/write helpers: loop until all bytes are transferred,
 * handling partial reads/writes.
 */
static int write_all(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = write(fd, p, remaining);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            perror("write");
            return -1;
        }
        p += n;
        remaining -= n;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t len)
{
    char *p = (char *)buf;
    size_t remaining = len;

    while (remaining > 0) {
        ssize_t n = read(fd, p, remaining);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            if (n == 0) {
                fprintf(stderr, "Peer closed connection\n");
                return -1;
            }
            perror("read");
            return -1;
        }
        p += n;
        remaining -= n;
    }
    return 0;
}

int exchange_qp_info(int sockfd, struct qp_info *local,
                     struct qp_info *remote)
{
    if (write_all(sockfd, local, sizeof(*local)) < 0)
        return -1;
    if (read_all(sockfd, remote, sizeof(*remote)) < 0)
        return -1;
    return 0;
}

int exchange_mr_info(int sockfd, struct mr_info *local,
                     struct mr_info *remote)
{
    if (write_all(sockfd, local, sizeof(*local)) < 0)
        return -1;
    if (read_all(sockfd, remote, sizeof(*remote)) < 0)
        return -1;
    return 0;
}

/* --------------------------------------------------------------------------
 * Printing helpers
 * -------------------------------------------------------------------------- */

void print_qp_info(const char *label, struct qp_info *info)
{
    printf("  %s: QPN=0x%06x, LID=0x%04x, PSN=0x%06x\n",
           label, info->qp_num, info->lid, info->psn);

    /* Print the GID in standard format (groups of 4 hex digits) */
    printf("         GID=");
    for (int i = 0; i < 16; i++) {
        printf("%02x", info->gid.raw[i]);
        if (i % 2 == 1 && i < 15)
            printf(":");
    }
    printf("\n");
}

/* --------------------------------------------------------------------------
 * Completion polling
 * -------------------------------------------------------------------------- */

int poll_completion(struct ibv_cq *cq, struct ibv_wc *wc)
{
    int ne;

    do {
        ne = ibv_poll_cq(cq, 1, wc);
        if (ne < 0) {
            fprintf(stderr, "ibv_poll_cq() failed: %d\n", ne);
            return -1;
        }
    } while (ne == 0);

    if (wc->status != IBV_WC_SUCCESS) {
        fprintf(stderr, "Completion error: %s (%d), wr_id=%lu\n",
                ibv_wc_status_str(wc->status), wc->status, wc->wr_id);
        return -1;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 * Device helpers
 * -------------------------------------------------------------------------- */

struct ibv_context *open_device(const char *dev_name)
{
    struct ibv_device **dev_list;
    struct ibv_device *dev = NULL;
    struct ibv_context *ctx = NULL;
    int num_devices;

    dev_list = ibv_get_device_list(&num_devices);
    if (!dev_list) {
        fprintf(stderr, "ibv_get_device_list() failed\n");
        return NULL;
    }

    if (num_devices == 0) {
        fprintf(stderr, "No RDMA devices found\n");
        ibv_free_device_list(dev_list);
        return NULL;
    }

    if (dev_name) {
        /* Find the named device */
        for (int i = 0; i < num_devices; i++) {
            if (strcmp(ibv_get_device_name(dev_list[i]), dev_name) == 0) {
                dev = dev_list[i];
                break;
            }
        }
        if (!dev) {
            fprintf(stderr, "Device '%s' not found\n", dev_name);
            ibv_free_device_list(dev_list);
            return NULL;
        }
    } else {
        /* Use the first device */
        dev = dev_list[0];
    }

    printf("Using device: %s\n", ibv_get_device_name(dev));

    ctx = ibv_open_device(dev);
    if (!ctx) {
        fprintf(stderr, "Failed to open device '%s'\n",
                ibv_get_device_name(dev));
    }

    ibv_free_device_list(dev_list);
    return ctx;
}

int get_local_info(struct ibv_context *ctx, struct ibv_qp *qp,
                   int ib_port, int gid_index, struct qp_info *info)
{
    struct ibv_port_attr port_attr;

    if (ibv_query_port(ctx, ib_port, &port_attr)) {
        fprintf(stderr, "ibv_query_port() failed\n");
        return -1;
    }

    memset(info, 0, sizeof(*info));
    info->qp_num = qp->qp_num;
    info->lid    = port_attr.lid;
    info->psn    = lrand48() & 0xFFFFFF;  /* Random 24-bit PSN */

    if (ibv_query_gid(ctx, ib_port, gid_index, &info->gid)) {
        fprintf(stderr, "ibv_query_gid() failed (index=%d)\n", gid_index);
        return -1;
    }

    return 0;
}
