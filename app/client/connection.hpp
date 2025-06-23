#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>


class connection
{
public:
  connection(int conn_id) : conn_id_{conn_id}
  {
    if ((sock_fd_ = ::socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      std::cerr << "Connection " << conn_id_ << ": Socket creation error: " << ::strerror(errno) << std::endl;
      std::terminate();
    }
  }

  void connect(const std::string& host, const std::string& port)
  {
    ::addrinfo hints;
    ::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;     // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // Stream socket
    hints.ai_flags = 0;              // No flags
    hints.ai_protocol = 0;           // Any protocol

    ::addrinfo* result;

    if (int s = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &result); s != 0)
    {
      std::cerr << "Connection " << conn_id_ << ": getaddrinfo: " << ::gai_strerror(s) << std::endl;
      std::terminate();
    }

    ::addrinfo* rp;

    for (rp = result; rp != nullptr; rp = rp->ai_next)
    {
      if (::connect(sock_fd_, rp->ai_addr, rp->ai_addrlen) == 0)
      {
        // Success
        break;
      }
      else
      {
        std::cerr << "Connection " << conn_id_ << ": Connect failed for address: " << ::strerror(errno) << std::endl;
      }
    }

    ::freeaddrinfo(result); // Free the memory allocated by getaddrinfo

    if (rp == nullptr)
    {
      std::cerr << "Connection " << conn_id_ << ": Could not connect to any address." << std::endl;
      std::terminate();
    }
  }

  void send(const char* data, std::size_t size)
  {
    ssize_t bytes_sent = ::send(sock_fd_, data, size, 0);

    if (bytes_sent < 0)
    {
      std::cerr << "Connection " << conn_id_ << ": Send failed: " << strerror(errno) << std::endl;
      std::terminate();
    }

    if (static_cast<std::size_t>(bytes_sent) != size)
    {
      std::cerr << "Connection " << conn_id_ << ": Partial send. Sent " << bytes_sent << " of " << size << " bytes."
                << std::endl;
    }
  }

private:
  int conn_id_;
  int sock_fd_;
};
