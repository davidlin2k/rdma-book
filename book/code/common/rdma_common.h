/*
 * rdma_common.h - Common helpers for RDMA programming examples
 *
 * This header provides utility macros, data structures, and function
 * declarations used across all chapter 9 code examples.
 */

#ifndef RDMA_COMMON_H
#define RDMA_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <infiniband/verbs.h>

/* --------------------------------------------------------------------------
 * Error checking macros
 * -------------------------------------------------------------------------- */

/* Check a pointer return value; exit on NULL */
#define CHECK_NULL(ptr, msg) do {                                   \
    if (!(ptr)) {                                                    \
        fprintf(stderr, "ERROR %s:%d: %s returned NULL: %s\n",      \
                __FILE__, __LINE__, (msg), strerror(errno));         \
        exit(EXIT_FAILURE);                                          \
    }                                                                \
} while (0)

/* Check an integer return value; exit on non-zero */
#define CHECK_ERRNO(ret, msg) do {                                  \
    if ((ret)) {                                                     \
        fprintf(stderr, "ERROR %s:%d: %s failed: %s\n",             \
                __FILE__, __LINE__, (msg), strerror(errno));         \
        exit(EXIT_FAILURE);                                          \
    }                                                                \
} while (0)

/* Check an integer return value where the return IS the error code */
#define CHECK_RC(ret, msg) do {                                     \
    int _rc = (ret);                                                 \
    if (_rc) {                                                       \
        fprintf(stderr, "ERROR %s:%d: %s failed: %s (rc=%d)\n",     \
                __FILE__, __LINE__, (msg), strerror(_rc), _rc);      \
        exit(EXIT_FAILURE);                                          \
    }                                                                \
} while (0)

/* --------------------------------------------------------------------------
 * Default parameters
 * -------------------------------------------------------------------------- */

#define DEFAULT_PORT      18515   /* TCP port for metadata exchange       */
#define DEFAULT_IB_PORT   1       /* InfiniBand/RoCE port number         */
#define DEFAULT_GID_INDEX 1       /* GID index (typically 1 for RoCEv2)  */
#define DEFAULT_MSG_SIZE  64      /* Default message size in bytes       */
#define DEFAULT_NUM_ITERS 1000    /* Default number of iterations        */
#define CQ_DEPTH          128     /* Completion queue depth              */
#define SQ_DEPTH          16      /* Send queue depth                    */
#define RQ_DEPTH          16      /* Receive queue depth                 */

/* --------------------------------------------------------------------------
 * QP info structure - exchanged out-of-band over TCP
 * -------------------------------------------------------------------------- */

struct qp_info {
    uint32_t      qp_num;   /* Queue pair number                       */
    uint32_t      lid;      /* Local identifier (IB subnet routing)    */
    uint32_t      psn;      /* Packet sequence number (starting)       */
    uint32_t      padding;  /* Alignment padding                       */
    union ibv_gid gid;      /* Global identifier (RoCE / cross-subnet) */
};

/* --------------------------------------------------------------------------
 * MR info structure - exchanged for RDMA Read/Write
 * -------------------------------------------------------------------------- */

struct mr_info {
    uint64_t addr;    /* Virtual address of the remote buffer */
    uint32_t rkey;    /* Remote key for the memory region     */
    uint32_t length;  /* Length of the remote buffer          */
};

/* --------------------------------------------------------------------------
 * TCP helper functions (for out-of-band metadata exchange)
 * -------------------------------------------------------------------------- */

/*
 * tcp_client_connect - Connect to a TCP server.
 * @server_name: Hostname or IP address of the server.
 * @port:        TCP port number.
 * Returns: Socket file descriptor, or -1 on error.
 */
int tcp_client_connect(const char *server_name, int port);

/*
 * tcp_server_listen - Create a listening TCP socket.
 * @port: TCP port number to listen on.
 * Returns: Listening socket file descriptor, or -1 on error.
 */
int tcp_server_listen(int port);

/*
 * tcp_server_accept - Accept a single incoming TCP connection.
 * @listen_fd: Listening socket from tcp_server_listen().
 * Returns: Connected socket file descriptor, or -1 on error.
 */
int tcp_server_accept(int listen_fd);

/* --------------------------------------------------------------------------
 * RDMA metadata exchange functions
 * -------------------------------------------------------------------------- */

/*
 * exchange_qp_info - Exchange QP metadata with a peer over TCP.
 * @sockfd: Connected TCP socket.
 * @local:  Local QP info to send.
 * @remote: Buffer to receive remote QP info.
 * Returns: 0 on success, -1 on error.
 */
int exchange_qp_info(int sockfd, struct qp_info *local,
                     struct qp_info *remote);

/*
 * exchange_mr_info - Exchange MR metadata with a peer over TCP.
 * @sockfd: Connected TCP socket.
 * @local:  Local MR info to send.
 * @remote: Buffer to receive remote MR info.
 * Returns: 0 on success, -1 on error.
 */
int exchange_mr_info(int sockfd, struct mr_info *local,
                     struct mr_info *remote);

/* --------------------------------------------------------------------------
 * Printing helpers
 * -------------------------------------------------------------------------- */

/*
 * print_qp_info - Print QP info to stdout.
 * @label: Descriptive label (e.g., "Local", "Remote").
 * @info:  QP info to print.
 */
void print_qp_info(const char *label, struct qp_info *info);

/* --------------------------------------------------------------------------
 * Completion polling helper
 * -------------------------------------------------------------------------- */

/*
 * poll_completion - Busy-poll a CQ until one completion arrives.
 * @cq: Completion queue to poll.
 * @wc: Work completion structure to fill.
 * Returns: 0 on success, -1 on error.
 */
int poll_completion(struct ibv_cq *cq, struct ibv_wc *wc);

/* --------------------------------------------------------------------------
 * Device helper
 * -------------------------------------------------------------------------- */

/*
 * open_device - Open an RDMA device by name, or the first device if
 *               dev_name is NULL.
 * @dev_name: Device name (e.g., "mlx5_0"), or NULL for first device.
 * Returns: Device context, or NULL on error.
 */
struct ibv_context *open_device(const char *dev_name);

/*
 * get_local_info - Populate a qp_info struct with local QP, port,
 *                  and GID information.
 * @ctx:       Device context.
 * @qp:        Queue pair.
 * @ib_port:   Port number.
 * @gid_index: GID table index.
 * @info:      Output qp_info structure.
 * Returns: 0 on success, -1 on error.
 */
int get_local_info(struct ibv_context *ctx, struct ibv_qp *qp,
                   int ib_port, int gid_index, struct qp_info *info);

#endif /* RDMA_COMMON_H */
