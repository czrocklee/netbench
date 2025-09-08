#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <linux/errqueue.h>
#include <linux/net_tstamp.h>
#include <linux/socket.h>
#include <arpa/inet.h>
#include <assert.h>

int parse_host_port(char const* input, char* host, size_t hostlen, int* port)
{
  char const* colon = strchr(input, ':');
  if (!colon)
    return -1;
  size_t hlen = colon - input;
  if (hlen >= hostlen)
    return -1;
  strncpy(host, input, hlen);
  host[hlen] = 0;
  *port = atoi(colon + 1);
  if (*port <= 0 || *port > 65535)
    return -1;
  return 0;
}

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    fprintf(stderr, "Usage: %s host:port [msg_size]\n", argv[0]);
    return 1;
  }

  char host[256];
  int port;

  if (parse_host_port(argv[1], host, sizeof(host), &port) != 0)
  {
    fprintf(stderr, "Invalid host:port: %s\n", argv[1]);
    return 1;
  }

  size_t msg_size = 4096;
  if (argc >= 3)
  {
    long val = strtol(argv[2], NULL, 10);
    if (val > 0 && val <= 1 << 24) // 16MB max reasonable
      msg_size = (size_t)val;
    else
      fprintf(stderr, "Invalid msg_size '%s', using default 4096.\n", argv[2]);
  }

  int sock = socket(AF_INET, SOCK_STREAM, 0);

  if (sock == -1)
  {
    perror("socket");
    exit(1);
  }

  // Enable SO_ZEROCOPY
  int enable = 1;

  if (setsockopt(sock, SOL_SOCKET, SO_ZEROCOPY, &enable, sizeof(enable)) < 0)
  {
    perror("setsockopt SO_ZEROCOPY");
    exit(1);
  }

  struct sockaddr_in srv = {0};
  srv.sin_family = AF_INET;
  srv.sin_port = htons(port);

  if (inet_pton(AF_INET, host, &srv.sin_addr) != 1)
  {
    fprintf(stderr, "Invalid IP address: %s\n", host);
    exit(1);
  }

  if (connect(sock, (struct sockaddr*)&srv, sizeof(srv)) < 0)
  {
    perror("connect");
    exit(1);
  }

  // Prepare buffer to send
  char* buffer = malloc(msg_size);

  if (!buffer)
  {
    fprintf(stderr, "Failed to allocate buffer of size %zu\n", msg_size);
    exit(1);
  }

  memset(buffer, 'A', msg_size);
  size_t to_send = msg_size;

  // Send with MSG_ZEROCOPY
  ssize_t sent = send(sock, buffer, to_send, MSG_ZEROCOPY);

  if (sent < 0)
  {
    perror("send MSG_ZEROCOPY");
    exit(1);
  }

  printf("Sent %zd bytes with MSG_ZEROCOPY\n", sent);

  // Now, wait for completion notification (from the error queue)
  struct msghdr msg = {0};
  char cmsgbuf[512];
  struct sock_extended_err* serr;
  struct cmsghdr* cmsg;
  char dummy[1];

  struct iovec iov = {.iov_base = dummy, .iov_len = sizeof(dummy)};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = cmsgbuf;
  msg.msg_controllen = sizeof(cmsgbuf);

  int got_zerocopy = 0;
  while (!got_zerocopy)
  {
    ssize_t ret = recvmsg(sock, &msg, MSG_ERRQUEUE);

    if (ret < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        usleep(10000); // 10ms
        continue;
      }

      perror("recvmsg MSG_ERRQUEUE");
      break;
    }
    for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL; cmsg = CMSG_NXTHDR(&msg, cmsg))
    {
      if (cmsg->cmsg_level == SOL_IP && cmsg->cmsg_type == IP_RECVERR)
      {
        serr = (struct sock_extended_err*)CMSG_DATA(cmsg);

        if (serr->ee_origin == SO_EE_ORIGIN_ZEROCOPY)
        {
          if (serr->ee_code == 0)
          {
            printf("Zero-copy send completed successfully.\n");
          }
          else if (serr->ee_code == SO_EE_CODE_ZEROCOPY_COPIED)
          {
            printf("Send was COPIED (fallback), not zero-copy.\n");
          }
          else
          {
            printf("Zero-copy send completed with ee_code=%u, ee_errno=%u\n", serr->ee_code, serr->ee_errno);
          }
          got_zerocopy = 1;
        }
      }
    }
  }

  close(sock);
  free(buffer);
  return 0;
}