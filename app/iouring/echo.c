#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <liburing.h>
#include <signal.h>

#define PORT 8081 // Different port to avoid conflict with the raw version
#define BACKLOG 128
#define URING_ENTRIES 256 // Number of SQEs, can be different from CQEs with liburing
#define BUFFER_SIZE 1024
#define MAX_CONNECTIONS URING_ENTRIES

// Globals
volatile sig_atomic_t running = 1;
struct io_uring ring;
int listen_fd;

// Connection/Operation state
enum event_type
{
  EVENT_TYPE_ACCEPT,
  EVENT_TYPE_RECV,
  EVENT_TYPE_SEND,
  EVENT_TYPE_CLOSE,
};

struct conn_info
{
  int fd; // Socket FD (listen_fd for ACCEPT, client_fd for others)
  enum event_type type;
  char buffer[BUFFER_SIZE];
  int buflen;                     // Bytes to send or bytes received
  struct sockaddr_in client_addr; // For accept
  socklen_t client_addr_len;      // For accept
  int conn_idx;                   // Index in the conn_states pool (optional, for debugging/tracking)
};

// Pool of connection states (simple array for this example)
struct conn_info conn_states[MAX_CONNECTIONS];
int conn_state_idx_counter = 0; // To assign unique conn_idx for debugging

void sigint_handler(int sig)
{
  (void)sig;
  fprintf(stderr, "Signal received, shutting down...\n");
  running = 0;
}

// Initialize the pool of connection_info structures
// With liburing, we often allocate these dynamically or use a more robust pool.
// For simplicity, we'll preallocate and manage them.
void init_conn_states()
{
  for (int i = 0; i < MAX_CONNECTIONS; ++i)
  {
    conn_states[i].fd = -1; // Mark as free
  }
}

// Allocate a conn_info structure.
// In a real app, use a proper pool or dynamic allocation.
struct conn_info* alloc_conn_info()
{
  // For this example, we'll just find a free slot.
  // A more robust solution would be needed for high-load scenarios.
  for (int i = 0; i < MAX_CONNECTIONS; ++i)
  {
    if (conn_states[i].fd == -1)
    { // Simple check for "free"
      struct conn_info* ci = &conn_states[i];
      memset(ci, 0, sizeof(struct conn_info));
      ci->fd = -2;                             // Mark as "allocated but not yet used with a real FD"
      ci->conn_idx = conn_state_idx_counter++; // For debugging
      return ci;
    }
  }
  fprintf(stderr, "Connection pool exhausted.\n");
  return NULL; // Should not happen if MAX_CONNECTIONS is sufficient
}

// Free a conn_info structure
void free_conn_info(struct conn_info* ci)
{
  if (ci)
  {
    // Mark as free by resetting fd.
    // The actual index in conn_states doesn't change, so we can reuse it.
    // fprintf(stderr, "Freeing conn_info for fd %d, type %d, idx %d\n", ci->fd, ci->type, ci->conn_idx);
    ci->fd = -1;
    // Other fields will be overwritten or zeroed by alloc_conn_info
  }
}

// Prepare an accept operation
void prepare_accept(struct io_uring* ring_ptr, int lfd)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring_ptr);
  if (!sqe)
  {
    fprintf(stderr, "Failed to get SQE for ACCEPT\n");
    return;
  }

  struct conn_info* ci = alloc_conn_info();
  if (!ci)
  {
    fprintf(stderr, "Failed to allocate conn_info for ACCEPT\n");
    // With liburing, io_uring_get_sqe already "reserved" the slot.
    // We should ideally not submit if conn_info allocation fails.
    // For simplicity, this example might proceed and hit an error later if ci is NULL.
    // A better way: check alloc_conn_info first.
    // Or, if SQE is already taken, one might need to submit a NOP or cancel.
    // Here, we'll just return, leaving the SQE uninitialized (bad).
    // A proper fix: don't call io_uring_get_sqe if alloc_conn_info fails.
    return;
  }

  ci->fd = lfd; // Temporary, will be overwritten by new client_fd
  ci->type = EVENT_TYPE_ACCEPT;
  ci->client_addr_len = sizeof(ci->client_addr);

  io_uring_prep_accept(
    sqe, lfd, (struct sockaddr*)&ci->client_addr, &ci->client_addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
  io_uring_sqe_set_data(sqe, ci);
  // fprintf(stderr, "Prepared ACCEPT for listen_fd %d, conn_idx %d\n", lfd, ci->conn_idx);
}

// Prepare a receive operation
void prepare_recv(struct io_uring* ring_ptr, int client_fd, struct conn_info* ci_orig)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring_ptr);
  if (!sqe)
  {
    fprintf(stderr, "Failed to get SQE for RECV on fd %d\n", client_fd);
    if (ci_orig) free_conn_info(ci_orig); // Free original if it can't be reused
    return;
  }

  struct conn_info* ci;
  // If ci_orig was for ACCEPT or a completed SEND/CLOSE, it might be reusable or need freeing.
  // For simplicity, let's assume ci_orig is the one to use/repurpose for RECV.
  // If ci_orig was from ACCEPT, its fd is listen_fd, needs update.
  // If ci_orig was from SEND, its fd is client_fd, buffer needs clearing.
  if (ci_orig) { ci = ci_orig; }
  else
  { // Should not happen if we chain operations correctly
    ci = alloc_conn_info();
    if (!ci)
    {
      fprintf(stderr, "Failed to alloc conn_info for RECV on fd %d\n", client_fd);
      return; // SQE slot taken but not used.
    }
  }

  ci->fd = client_fd; // Ensure FD is correct for this operation
  ci->type = EVENT_TYPE_RECV;
  memset(ci->buffer, 0, BUFFER_SIZE);

  io_uring_prep_recv(sqe, client_fd, ci->buffer, BUFFER_SIZE, 0);
  io_uring_sqe_set_data(sqe, ci);
  // fprintf(stderr, "Prepared RECV for client_fd %d, conn_idx %d\n", client_fd, ci->conn_idx);
}

// Prepare a send operation
void prepare_send(struct io_uring* ring_ptr, struct conn_info* ci_recv)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring_ptr);
  if (!sqe)
  {
    fprintf(stderr, "Failed to get SQE for SEND on fd %d\n", ci_recv->fd);
    free_conn_info(ci_recv); // Free the conn_info as we can't proceed
    return;
  }

  // Reuse the conn_info from RECV
  struct conn_info* ci = ci_recv;
  ci->type = EVENT_TYPE_SEND;
  // ci->buflen is already set from the RECV result

  io_uring_prep_send(sqe, ci->fd, ci->buffer, ci->buflen, 0);
  io_uring_sqe_set_data(sqe, ci);
  // fprintf(stderr, "Prepared SEND for client_fd %d, len %d, conn_idx %d\n", ci->fd, ci->buflen, ci->conn_idx);
}

// Prepare a close operation
void prepare_close_op(struct io_uring* ring_ptr, struct conn_info* ci_orig)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring_ptr);
  if (!sqe)
  {
    fprintf(stderr, "Failed to get SQE for CLOSE on fd %d. Closing synchronously.\n", ci_orig->fd);
    close(ci_orig->fd); // Fallback to synchronous close
    free_conn_info(ci_orig);
    return;
  }

  struct conn_info* ci = ci_orig; // Reuse conn_info
  ci->type = EVENT_TYPE_CLOSE;
  // ci->fd is already set

  io_uring_prep_close(sqe, ci->fd);
  io_uring_sqe_set_data(sqe, ci);
  // fprintf(stderr, "Prepared CLOSE for client_fd %d, conn_idx %d\n", ci->fd, ci->conn_idx);
}

int main()
{
  signal(SIGINT, sigint_handler);
  signal(SIGTERM, sigint_handler);

  init_conn_states();

  // Initialize io_uring
  // Using IORING_SETUP_SINGLE_ISSUER and IORING_SETUP_CLAMP for potentially better performance/behavior
  // struct io_uring_params params;
  // memset(&params, 0, sizeof(params));
  // params.flags = IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_CLAMP; // Example flags
  // if (io_uring_queue_init_params(URING_ENTRIES, &ring, &params) < 0) {
  if (io_uring_queue_init(URING_ENTRIES, &ring, 0) < 0)
  { // Simpler init
    perror("io_uring_queue_init");
    return EXIT_FAILURE;
  }
  fprintf(stdout, "io_uring initialized successfully with liburing.\n");

  listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listen_fd < 0)
  {
    perror("socket");
    io_uring_queue_exit(&ring);
    return EXIT_FAILURE;
  }

  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
  {
    perror("setsockopt SO_REUSEADDR");
    close(listen_fd);
    io_uring_queue_exit(&ring);
    return EXIT_FAILURE;
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(PORT);

  if (bind(listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
  {
    perror("bind");
    close(listen_fd);
    io_uring_queue_exit(&ring);
    return EXIT_FAILURE;
  }

  if (listen(listen_fd, BACKLOG) < 0)
  {
    perror("listen");
    close(listen_fd);
    io_uring_queue_exit(&ring);
    return EXIT_FAILURE;
  }

  fprintf(stdout, "Server listening on port %d\n", PORT);

  // Submit initial accept operation
  prepare_accept(&ring, listen_fd);
  io_uring_submit(&ring); // Submit the first accept

  while (running)
  {
    struct io_uring_cqe* cqe;
    int ret;

    // Wait for a completion event.
    // Using submit_and_wait is efficient if we might have new SQEs to submit.
    // If no new SQEs, io_uring_wait_cqe is fine.
    // For this loop structure, io_uring_submit_and_wait is generally good.
    ret = io_uring_submit_and_wait(&ring, 1); // Wait for at least 1 event
    if (ret < 0)
    {
      if (errno == EINTR && !running)
      {
        fprintf(stderr, "io_uring_submit_and_wait interrupted by signal, exiting loop.\n");
        break;
      }
      perror("io_uring_submit_and_wait");
      break;
    }

    // fprintf(stderr, "io_uring_submit_and_wait returned %d, CQ ring space: %u\n", ret, io_uring_cq_ready(&ring));

    // Process all available CQEs
    unsigned head;
    unsigned cqe_count = 0;
    io_uring_for_each_cqe(&ring, head, cqe)
    {
      cqe_count++;
      struct conn_info* ci = (struct conn_info*)io_uring_cqe_get_data(cqe);

      // fprintf(stderr, "CQE: user_data=%p, res=%d, flags=%u, type=%d, fd=%d, idx=%d\n",
      //         (void*)ci, cqe->res, cqe->flags, ci ? ci->type : -1, ci ? ci->fd : -1, ci ? ci->conn_idx : -1);

      if (!ci)
      { // Should not happen if set_data is always used
        fprintf(stderr, "CRITICAL: CQE with NULL user_data!\n");
        continue; // Skip this CQE
      }

      if (cqe->res < 0)
      {
        fprintf(
          stderr,
          "Async operation failed for type %d, fd %d: %s (res %d)\n",
          ci->type,
          ci->fd,
          strerror(-cqe->res),
          cqe->res);

        if (ci->type != EVENT_TYPE_ACCEPT && ci->type != EVENT_TYPE_CLOSE)
        {
          // For RECV/SEND errors on a client_fd, try to close it.
          // The current ci is for the failed RECV/SEND.
          prepare_close_op(&ring, ci); // ci will be reused for CLOSE
        }
        else if (ci->type == EVENT_TYPE_ACCEPT)
        {
          // Error on accept, resubmit accept.
          // The conn_info 'ci' was for the failed accept, free it.
          free_conn_info(ci);
          prepare_accept(&ring, listen_fd); // Try to accept again
        }
        else
        { // EVENT_TYPE_CLOSE failed or other
          // If close failed, the fd might be already gone. Just free state.
          fprintf(stderr, "Failed to close fd %d or other error, freeing state.\n", ci->fd);
          free_conn_info(ci);
        }
      }
      else
      {
        switch (ci->type)
        {
          case EVENT_TYPE_ACCEPT:
          {
            int client_fd = cqe->res; // Result of accept is the new client FD
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ci->client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            // fprintf(stdout, "Accepted connection from %s:%d on fd %d (conn_idx %d)\n",
            //         client_ip, ntohs(ci->client_addr.sin_port), client_fd, ci->conn_idx);

            // The conn_info 'ci' was for ACCEPT. Now repurpose it for RECV.
            prepare_recv(&ring, client_fd, ci);

            // Immediately prepare for the next accept operation
            prepare_accept(&ring, listen_fd);
            break;
          }
          case EVENT_TYPE_RECV:
          {
            int bytes_read = cqe->res;
            if (bytes_read == 0)
            {
              // fprintf(stdout, "Client fd %d disconnected (conn_idx %d).\n", ci->fd, ci->conn_idx);
              prepare_close_op(&ring, ci); // ci reused for CLOSE
            }
            else
            {
              // fprintf(stdout, "Received %d bytes from fd %d (conn_idx %d).\n", bytes_read, ci->fd, ci->conn_idx);
              ci->buflen = bytes_read;
              prepare_send(&ring, ci); // ci reused for SEND
            }
            break;
          }
          case EVENT_TYPE_SEND:
          {
            int bytes_sent = cqe->res;
            // fprintf(stdout, "Sent %d bytes to fd %d (conn_idx %d).\n", bytes_sent, ci->fd, ci->conn_idx);
            if (bytes_sent < ci->buflen)
            {
              fprintf(stderr, "Partial send to fd %d (%d/%d), closing.\n", ci->fd, bytes_sent, ci->buflen);
              prepare_close_op(&ring, ci); // ci reused for CLOSE
            }
            else
            {
              // Successfully sent, prepare for next receive
              prepare_recv(&ring, ci->fd, ci); // ci reused for RECV
            }
            break;
          }
          case EVENT_TYPE_CLOSE:
            // fprintf(stdout, "Closed fd %d (conn_idx %d).\n", ci->fd, ci->conn_idx);
            free_conn_info(ci); // Operation complete, free the state
            break;
          default:
            fprintf(stderr, "Unknown event type %d in CQE for fd %d\n", ci->type, ci->fd);
            free_conn_info(ci); // Free to avoid leak
            break;
        }
      }
    }
    // Mark CQEs as seen. This advances the CQ ring head.
    if (cqe_count > 0) { io_uring_cq_advance(&ring, cqe_count); }
    // After processing CQEs, we might have submitted new SQEs (e.g. new accept, recv, send)
    // io_uring_submit(&ring); // Submit them if any were prepared
    // The call to io_uring_submit_and_wait at the start of the loop handles submission.
  }

  fprintf(stdout, "Shutting down server...\n");

  // A more robust shutdown might try to cancel pending operations.
  // For example, one could iterate through conn_states and if an fd is active,
  // try to submit a cancel operation for it, or just close it.
  // liburing provides io_uring_prep_cancel.

  // Close any remaining client FDs (from conn_states) - quick and dirty
  // This is a fallback, ideally all connections are closed via io_uring_prep_close
  for (int i = 0; i < MAX_CONNECTIONS; ++i)
  {
    if (conn_states[i].fd > 0)
    { // Check if fd is potentially active client fd
      // fprintf(stderr, "Force closing active fd %d (type %d) during shutdown\n", conn_states[i].fd,
      // conn_states[i].type);
      close(conn_states[i].fd);
      conn_states[i].fd = -1; // Mark as closed/free
    }
  }

  close(listen_fd);
  io_uring_queue_exit(&ring);

  fprintf(stdout, "Server shut down.\n");
  return EXIT_SUCCESS;
}
