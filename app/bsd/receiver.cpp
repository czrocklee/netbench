#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>

#define MAX_EVENTS 1024
#define PORT 8080
#define BUFFER_SIZE 16

// Function to set a file descriptor to non-blocking mode
int make_socket_non_blocking(int sfd)
{
  int flags;
  flags = fcntl(sfd, F_GETFL, 0);
  if (flags == -1)
  {
    perror("fcntl F_GETFL");
    return -1;
  }

  flags |= O_NONBLOCK;
  if (fcntl(sfd, F_SETFL, flags) == -1)
  {
    perror("fcntl F_SETFL");
    return -1;
  }
  return 0;
}

int main()
{
  int server_fd, new_socket;
  struct sockaddr_in address;
  int addrlen = sizeof(address);

  int epoll_fd;
  struct epoll_event event, events[MAX_EVENTS];

  // Create a TCP socket
  if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
  {
    perror("socket failed");
    exit(EXIT_FAILURE);
  }

  // Set socket options to reuse address and port
  int opt = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
  {
    perror("setsockopt");
    exit(EXIT_FAILURE);
  }

  // Bind the socket
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(PORT);
  if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0)
  {
    perror("bind failed");
    exit(EXIT_FAILURE);
  }

  // Make server socket non-blocking for accept()
  if (make_socket_non_blocking(server_fd) == -1)
  {
    abort();
  }

  // Listen for incoming connections
  if (listen(server_fd, SOMAXCONN) < 0)
  {
    perror("listen");
    exit(EXIT_FAILURE);
  }

  // Create an epoll instance
  if ((epoll_fd = epoll_create1(0)) < 0)
  {
    perror("epoll_create1 failed");
    exit(EXIT_FAILURE);
  }

  event.data.fd = server_fd;
  event.events = EPOLLIN | EPOLLET; // Monitor for incoming connections (Edge-Triggered)
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0)
  {
    perror("epoll_ctl on server_fd failed");
    exit(EXIT_FAILURE);
  }

  printf("Server listening on port %d\n", PORT);

  while (1)
  {
    int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);

    for (int i = 0; i < num_events; i++)
    {
      if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)))
      {
        // An error has occurred on this fd, or the socket is not ready for reading.
        fprintf(stderr, "epoll error on fd %d\n", events[i].data.fd);
        close(events[i].data.fd);
        continue;
      }
      else if (events[i].data.fd == server_fd)
      {
        // New incoming connection(s)
        while (1)
        {
          struct sockaddr in_addr;
          socklen_t in_len = sizeof(in_addr);
          new_socket = accept(server_fd, &in_addr, &in_len);
          if (new_socket == -1)
          {
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
            {
              // We have processed all incoming connections.
              break;
            }
            else
            {
              perror("accept");
              break;
            }
          }

          make_socket_non_blocking(new_socket);

          event.data.fd = new_socket;
          event.events = EPOLLIN | EPOLLET; // Set ET for the new client socket
          if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_socket, &event) == -1)
          {
            perror("epoll_ctl on new_socket");
            abort();
          }
          printf("New connection accepted on socket fd %d\n", new_socket);
        }
      }
      else
      {
        // Data from an existing connection, must read until EAGAIN
        int client_fd = events[i].data.fd;
        int done = 0;
        while (1)
        {
          char buffer[BUFFER_SIZE];
          ssize_t count = read(client_fd, buffer, sizeof(buffer));
          if (count == -1)
          {
            // If errno is EAGAIN, it means we have read all data.
            if (errno != EAGAIN)
            {
              perror("read error");
              done = 1;
            }
            break;
          }
          else if (count == 0)
          {
            // End of file. The remote has closed the connection.
            done = 1;
            break;
          }

          // Process the received data (e.g., write to stdout)
          buffer[count] = '\0';
          //printf("Received from fd %d: %s", client_fd, buffer);
        }

        if (done)
        {
          printf("Client disconnected or error on fd %d\n", client_fd);
          close(client_fd); // Closing the fd removes it from epoll automatically
        }
      }
    }
  }

  close(server_fd);
  return 0;
}