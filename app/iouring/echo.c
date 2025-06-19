#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <liburing.h>
#include <errno.h>

#define PORT 8080
#define QUEUE_DEPTH 256
#define BUFFER_SIZE 4096

// Each connection needs a state structure
typedef struct conn_info
{
  int fd;
  char buffer[BUFFER_SIZE];
} conn_info;

// Each request we submit needs a type to identify its completion
typedef enum
{
  EVENT_TYPE_ACCEPT,
  EVENT_TYPE_RECV,
} event_type;

typedef struct request
{
  event_type type;
  conn_info* client;
} request;

void add_multishot_accept(struct io_uring* ring, int server_fd);
void add_multishot_recv(struct io_uring* ring, struct conn_info* client);
int check_for_op_support(struct io_uring_probe* probe, int op);

int main()
{
  int server_fd;
  struct sockaddr_in serv_addr;
  struct io_uring ring;

  // 1. Create and bind the server socket
  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0)
  {
    perror("socket");
    exit(EXIT_FAILURE);
  }

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  serv_addr.sin_addr.s_addr = INADDR_ANY;

  if (bind(server_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
  {
    perror("bind");
    exit(EXIT_FAILURE);
  }

  if (listen(server_fd, 128) < 0)
  {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  // 2. Initialize io_uring
  if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) < 0)
  {
    perror("io_uring_queue_init");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d (multishot mode)\n", PORT);

  // 3. Submit the first multishot accept request
  add_multishot_accept(&ring, server_fd);
  io_uring_submit(&ring);

  // 4. The Event Loop
  while (1)
  {
    struct io_uring_cqe* cqe;

    int ret = io_uring_wait_cqe(&ring, &cqe);
    if (ret < 0)
    {
      perror("io_uring_wait_cqe");
      continue;
    }

    struct request* req = (struct request*)cqe->user_data;

    if (cqe->res < 0)
    {
      // ENOBUFS is expected when the kernel runs out of provided buffers for multishot recv
      if (cqe->res != -ENOBUFS)
      {
        fprintf(
          stderr, "Async request failed: %s (for fd: %d)\n", strerror(-cqe->res), req->client ? req->client->fd : -1);
      }
      if (req && req->client)
      {
        close(req->client->fd);
        free(req->client);
        free(req);
      }
    }
    else
    {
      switch (req->type)
      {
        case EVENT_TYPE_ACCEPT:
        {
          int client_fd = cqe->res;
          printf("New connection accepted, fd: %d\n", client_fd);
          struct conn_info* client_info = malloc(sizeof(struct conn_info));
          client_info->fd = client_fd;
          add_multishot_recv(&ring, client_info);
          // No need to free req for multishot accept, it's reused by the kernel
          break;
        }
        case EVENT_TYPE_RECV:
        {
          int bytes_read = cqe->res;
          if (bytes_read == 0)
          {
            printf("Client disconnected, fd: %d\n", req->client->fd);
            close(req->client->fd);
            free(req->client);
            free(req);
          }
          else
          {
            printf("Received %d bytes from fd %d: %.*s", bytes_read, req->client->fd, bytes_read, req->client->buffer);
            // Multishot is automatically re-armed. Nothing to do.
          }
          break;
        }
      }
    }
    io_uring_cqe_seen(&ring, cqe);
  }
  io_uring_submit(&ring);

  close(server_fd);
  io_uring_queue_exit(&ring);
  return 0;
}

// Helper to check if an opcode is supported by the kernel
int check_for_op_support(struct io_uring_probe* probe, int op) { return io_uring_opcode_supported(probe, op); }

void add_multishot_accept(struct io_uring* ring, int server_fd)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  io_uring_prep_multishot_accept(sqe, server_fd, NULL, NULL, 0);

  struct request* req = malloc(sizeof(struct request));
  req->type = EVENT_TYPE_ACCEPT;
  req->client = NULL;
  io_uring_sqe_set_data(sqe, req);
}

void add_multishot_recv(struct io_uring* ring, struct conn_info* client)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  io_uring_prep_recv_multishot(sqe, client->fd, client->buffer, BUFFER_SIZE, 0);

  struct request* req = malloc(sizeof(struct request));
  req->type = EVENT_TYPE_RECV;
  req->client = client;
  io_uring_sqe_set_data(sqe, req);
}