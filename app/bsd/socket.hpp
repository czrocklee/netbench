#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace bsd
{
  class socket_exception : public std::system_error
  {
  public:
    socket_exception(const std::string& what) : std::system_error{errno, std::system_category(), what} {}
  };

  class socket
  {
  public:
    socket() : sock_fd_(-1) {}

    socket(int domain, int type, int protocol)
    {
      sock_fd_ = ::socket(domain, type, protocol);
      if (sock_fd_ < 0)
      {
        throw socket_exception("socket creation failed");
      }
    }

    ~socket()
    {
      if (sock_fd_ >= 0)
      {
        ::close(sock_fd_);
      }
    }

    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;

    socket(socket&& other) noexcept : sock_fd_(other.sock_fd_) { other.sock_fd_ = -1; }

    socket& operator=(socket&& other) noexcept
    {
      if (this != &other)
      {
        if (sock_fd_ >= 0)
        {
          ::close(sock_fd_);
        }
        sock_fd_ = other.sock_fd_;
        other.sock_fd_ = -1;
      }
      return *this;
    }

    void connect(const std::string& host, const std::string& port)
    {
      ::addrinfo hints;
      ::memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;

      ::addrinfo* result;
      if (int s = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &result); s != 0)
      {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(s));
      }

      ::addrinfo* rp;
      for (rp = result; rp != nullptr; rp = rp->ai_next)
      {
        if (::connect(sock_fd_, rp->ai_addr, rp->ai_addrlen) == 0)
        {
          break;
        }
      }

      ::freeaddrinfo(result);

      if (rp == nullptr)
      {
        throw socket_exception("connect failed to all addresses");
      }
    }

    void bind(const std::string& address, const std::string& port)
    {
      ::addrinfo hints;
      ::memset(&hints, 0, sizeof(hints));
      hints.ai_family = AF_UNSPEC;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_flags = AI_PASSIVE; // For wildcard IP address

      ::addrinfo* result;
      const char* host_cstr = address.empty() ? nullptr : address.c_str();
      if (int s = ::getaddrinfo(host_cstr, port.c_str(), &hints, &result); s != 0)
      {
        throw std::runtime_error(std::string("getaddrinfo failed: ") + gai_strerror(s));
      }

      ::addrinfo* rp;
      for (rp = result; rp != nullptr; rp = rp->ai_next)
      {
        if (::bind(sock_fd_, rp->ai_addr, rp->ai_addrlen) == 0)
        {
          break;
        }
      }

      ::freeaddrinfo(result);

      if (rp == nullptr)
      {
        throw socket_exception("bind failed");
      }
    }

    void listen(int backlog)
    {
      if (::listen(sock_fd_, backlog) < 0)
      {
        throw socket_exception("listen failed");
      }
    }

    ssize_t send(const void* data, size_t size, int flags)
    {
      ssize_t bytes_sent = ::send(sock_fd_, data, size, flags);
      if (bytes_sent < 0)
      {
        throw socket_exception("send failed");
      }
      return bytes_sent;
    }

    template<typename T>
    void set_option(int level, int optname, const T& optval)
    {
      if (::setsockopt(sock_fd_, level, optname, &optval, sizeof(optval)) < 0)
      {
        throw socket_exception("setsockopt failed");
      }
    }

    socket accept()
    {
      struct sockaddr_storage their_addr;
      socklen_t addr_size = sizeof(their_addr);
      int new_fd = ::accept(sock_fd_, (struct sockaddr*)&their_addr, &addr_size);
      if (new_fd < 0)
      {
        throw socket_exception("accept failed");
      }
      return socket(new_fd);
    }

    int get_fd() const { return sock_fd_; }

    // This constructor is public to allow creating a socket wrapper from a raw
    // file descriptor, which is useful for APIs like io_uring's accept that
    // return a raw fd. It is explicit to prevent accidental conversions.
    explicit socket(int fd) : sock_fd_(fd) {}

  private:
    int sock_fd_;
  };

} // namespace bsd
