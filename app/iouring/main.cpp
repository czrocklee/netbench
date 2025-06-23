
#include <CLI/CLI.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <liburing.h>
#include <iostream>

// Configuration
#define PORT 8080
int BUFFER_SIZE;
#define BUFFER_COUNT 2048
#define BUFFER_GROUP_ID 0

struct request_data;
// Forward declarations
static int setup_listen_socket(int port);
static void setup_buffer_pool(struct io_uring* ring, char** buffers, struct io_uring_buf_ring** br, int gid);
static void add_accept(struct io_uring* ring, int listen_fd);
static void add_recv(struct io_uring* ring, request_data* req);
static void handle_completion(
  struct io_uring_cqe* cqe,
  struct io_uring* ring,
  char* buffers,
  struct io_uring_buf_ring* br,
  int listen_fd);

// To distinguish between accept and recv completions
typedef enum
{
  EVENT_TYPE_ACCEPT,
  EVENT_TYPE_RECV,
} event_type_t;

// Data associated with each request
typedef struct request_data
{
  event_type_t type;
  int client_fd;
  struct io_uring_buf_ring* br;
  char* buffers;
} request_data_t;

int main(int argc, char** argv)
{
  CLI::App app{"TCP io_uring server"};

  std::string address;
  app.add_option("-a,--address", address, "Target address")->default_val("127.0.0.1:19004");

  app.add_option("-b,--buffer-size", BUFFER_SIZE, "Buffer size in bytes")->default_val(1024);

  int uring_depth;
  app.add_option("-d,--uring-depth", uring_depth, "Queue depth of uring")->default_val(512);

  CLI11_PARSE(app, argc, argv);

  struct io_uring ring;
  char* buffers = NULL;
  struct io_uring_buf_ring* br = NULL;

  // 1. Setup listening socket
  int listen_fd = setup_listen_socket(PORT);
  if (listen_fd < 0)
  {
    return 1;
  }

  // 2. Initialize io_uring
  if (io_uring_queue_init(uring_depth, &ring, 0) < 0)
  {
    perror("io_uring_queue_init");
    close(listen_fd);
    return 1;
  }

  // 3. Setup provided buffer pool
  // setup_buffer_pool(&ring, &buffers, &br);
  printf("Buffer pool with %d buffers of %d bytes initialized.\n", BUFFER_COUNT, BUFFER_SIZE);

  // 4. Start accepting connections
  add_accept(&ring, listen_fd);
  printf("Server listening on port %d, waiting for connections...\n", PORT);

  // 5. Event loop
  while (1)
  {
    int ret = io_uring_submit_and_wait(&ring, 1);
    if (ret < 0)
    {
      perror("io_uring_submit_and_wait");
      break;
    }

    struct io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&ring, head, cqe)
    {
      count++;
      handle_completion(cqe, &ring, buffers, br, listen_fd);
    }

    // std::cout << count << std::endl;
    io_uring_cq_advance(&ring, count);
  }

  // 6. Cleanup
  io_uring_queue_exit(&ring);
  free(buffers);
  close(listen_fd);

  return 0;
}

static void handle_completion(
  struct io_uring_cqe* cqe,
  struct io_uring* ring,
  char* buffers,
  struct io_uring_buf_ring* br,
  int listen_fd)
{
  request_data_t* req = (request_data_t*)io_uring_cqe_get_data(cqe);
  if (!req)
  {
    return;
  }

  switch (req->type)
  {
    case EVENT_TYPE_ACCEPT:
    {
      int client_fd = cqe->res;
      if (client_fd >= 0)
      {
        printf("New connection accepted, fd: %d\n", client_fd);
        request_data_t* req = (request_data_t*)malloc(sizeof(request_data_t));
        req->type = EVENT_TYPE_RECV;
        req->client_fd = client_fd;
        setup_buffer_pool(ring, &req->buffers, &req->br, client_fd);

        add_recv(ring, req);
      }
      else
      {
        fprintf(stderr, "Accept error: %s\n", strerror(-cqe->res));
      }

      // If the multishot accept were to stop, we would re-arm it here.
      if (!(cqe->flags & IORING_CQE_F_MORE))
      {
        add_accept(ring, listen_fd);
      }
      break;
    }

    case EVENT_TYPE_RECV:
    {
      if (cqe->res <= 0)
      {
        // Connection closed or error
        if (cqe->res < 0)
        {
          fprintf(stderr, "Recv error on fd %d: %s\n", req->client_fd, strerror(-cqe->res));
        }
        else
        {
          printf("Client disconnected, fd: %d\n", req->client_fd);
        }
        close(req->client_fd);
        io_uring_unregister_buf_ring(ring, req->client_fd);
        free(req->buffers);
        free(req); // Free the request data for this connection
        break;
      }

      // Successful receive
      if (!(cqe->flags & IORING_CQE_F_BUFFER))
      {
        fprintf(stderr, "Error: RECV op completed without a buffer on fd %d.\n", req->client_fd);
        break;
      }

      int bytes_received = cqe->res;
      int buffer_id = cqe->flags >> IORING_CQE_BUFFER_SHIFT;
      char* buffer = req->buffers + (buffer_id * BUFFER_SIZE);

      // printf("Received %d bytes on fd %d (buffer %d)\n", bytes_received, req->client_fd, buffer_id);

      // Reprovide the buffer back to the ring for reuse.
      io_uring_buf_ring_add(req->br, buffer, BUFFER_SIZE, buffer_id, io_uring_buf_ring_mask(BUFFER_COUNT), 0);
      io_uring_buf_ring_advance(req->br, 1);

      // If multishot receive stops, we need to re-arm it.
      if (!(cqe->flags & IORING_CQE_F_MORE))
      {
        // printf("Multishot recv ended for fd %d, re-arming.\n", req->client_fd);

        add_recv(ring, req);
      }
      break;
    }
  }
}

static int setup_listen_socket(int port)
{
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0)
  {
    perror("socket");
    return -1;
  }

  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
  {
    perror("setsockopt");
    close(listen_fd);
    return -1;
  }

  struct sockaddr_in addr = {0};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
  {
    perror("bind");
    close(listen_fd);
    return -1;
  }

  if (listen(listen_fd, SOMAXCONN) < 0)
  {
    perror("listen");
    close(listen_fd);
    return -1;
  }

  return listen_fd;
}

static void setup_buffer_pool(struct io_uring* ring, char** buffers, struct io_uring_buf_ring** br, int gid)
{
  size_t pool_size = BUFFER_COUNT * BUFFER_SIZE;
  *buffers = (char*)aligned_alloc(4096, pool_size);
  if (!*buffers)
  {
    perror("Failed to allocate buffer pool");
    exit(1);
  }

  int ret;
  *br = io_uring_setup_buf_ring(ring, BUFFER_COUNT, gid, 0, &ret);
  if (!*br)
  {
    fprintf(stderr, "Failed to setup buffer ring: %s\n", strerror(-ret));
    exit(1);
  }

  for (int i = 0; i < BUFFER_COUNT; i++)
  {
    io_uring_buf_ring_add(*br, *buffers + (i * BUFFER_SIZE), BUFFER_SIZE, i, io_uring_buf_ring_mask(BUFFER_COUNT), i);
  }
  io_uring_buf_ring_advance(*br, BUFFER_COUNT);

  printf("Buffer pool %d with %d buffers of %d bytes initialized.\n", gid, BUFFER_COUNT, BUFFER_SIZE);
}

static void add_accept(struct io_uring* ring, int listen_fd)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  if (!sqe)
  {
    return;
  }
  static request_data_t accept_req = {.type = EVENT_TYPE_ACCEPT};
  io_uring_prep_multishot_accept(sqe, listen_fd, NULL, NULL, 0);
  io_uring_sqe_set_data(sqe, &accept_req);
}

static void add_recv(struct io_uring* ring, request_data_t* req)
{
  struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
  if (!sqe)
  {
    close(req->client_fd);
    return;
  }

  io_uring_prep_recv_multishot(sqe, req->client_fd, NULL, 0, 0);
  sqe->flags |= IOSQE_BUFFER_SELECT;
  sqe->buf_group = req->client_fd;
  io_uring_sqe_set_data(sqe, req);
}
