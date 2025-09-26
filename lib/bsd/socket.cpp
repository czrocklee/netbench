#include "socket.hpp"

#include <netdb.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h> // Required for TCP_NODELAY
#include <fcntl.h>

#include <cstring>
#include <errno.h>
#include <memory>
#include <string_view>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace bsd
{

  socket_exception::socket_exception(std::string const& what) : std::system_error{errno, std::system_category(), what}
  {
  }

  socket::socket() : sock_fd_{-1}
  {
  }

  socket::socket(int fd) : sock_fd_{fd}
  {
  }

  socket::socket(int domain, int type, int protocol)
  {
    sock_fd_ = ::socket(domain, type, protocol);

    if (sock_fd_ < 0)
    {
      throw socket_exception{"socket creation failed"};
    }
  }

  socket::~socket()
  {
    close();
  }

  socket::socket(socket&& other) noexcept : sock_fd_{std::exchange(other.sock_fd_, -1)}
  {
  }

  socket& socket::operator=(socket&& other) noexcept
  {
    if (this != &other)
    {
      if (sock_fd_ >= 0)
      {
        ::close(sock_fd_);
      }

      sock_fd_ = std::exchange(other.sock_fd_, -1);
    }

    return *this;
  }

  void socket::close()
  {
    if (sock_fd_ >= 0)
    {
      ::close(sock_fd_);
      sock_fd_ = -1;
    }
  }

  void socket::connect(std::string const& host, std::string const& port)
  {
    ::addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    ::addrinfo* raw_result = nullptr;

    if (int s = ::getaddrinfo(host.c_str(), port.c_str(), &hints, &raw_result); s != 0)
    {
      throw std::runtime_error{std::string{"getaddrinfo failed: "} + gai_strerror(s)};
    }

    std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> result{raw_result, ::freeaddrinfo};

    for (auto rp = result.get(); rp != nullptr; rp = rp->ai_next)
    {
      if (::connect(sock_fd_, rp->ai_addr, rp->ai_addrlen) == 0)
      {
        return;
      }
    }

    throw socket_exception{"connect failed to all addresses"};
  }

  void socket::bind(std::string const& address, std::string const& port)
  {
    int port_num = std::stoi(port);

    ::sockaddr_in addr4{};
    addr4.sin_family = AF_INET;
    addr4.sin_port = htons(port_num);

    if (address.empty() || address == "0.0.0.0")
    {
      addr4.sin_addr.s_addr = INADDR_ANY;
    }
    else if (::inet_pton(AF_INET, address.c_str(), &addr4.sin_addr) != 1)
    {
      throw std::runtime_error{"Invalid IPv4 address"};
    }

    if (::bind(sock_fd_, reinterpret_cast<sockaddr*>(&addr4), sizeof(addr4)) < 0)
    {
      throw socket_exception{"bind failed"};
    }
  }

  void socket::listen(int backlog)
  {
    if (::listen(sock_fd_, backlog) < 0)
    {
      throw socket_exception{"listen failed"};
    }
  }

  std::size_t socket::recv(void* buffer, std::size_t size, int flags)
  {
    if (auto bytes_read = ::recv(sock_fd_, buffer, size, flags); bytes_read < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return 0; // Non-blocking send, no data sent
      }
      else
      {
        throw socket_exception{"recv failed"};
      }
    }
    else
    {
      return bytes_read;
    }
  }

  std::size_t socket::send(void const* data, size_t size, int flags)
  {
    if (auto bytes_sent = ::send(sock_fd_, data, size, flags); bytes_sent < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return 0; // Non-blocking send, no data sent
      }
      else
      {
        throw socket_exception{"send failed"};
      }
    }
    else
    {
      return bytes_sent;
    }
  }

  std::size_t socket::send(::iovec const* iov, std::size_t iovcnt, int flags)
  {
    ::msghdr msg{};
    msg.msg_iov = const_cast<::iovec*>(iov);
    msg.msg_iovlen = iovcnt;

    if (auto bytes_sent = ::sendmsg(sock_fd_, &msg, flags); bytes_sent < 0)
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK)
      {
        return 0; // Non-blocking send, no data sent
      }
      else
      {
        throw socket_exception{"sendv failed"};
      }
    }
    else
    {
      return bytes_sent;
    }
  }

  socket socket::accept()
  {
    ::sockaddr_storage their_addr{};
    ::socklen_t addr_size = sizeof(their_addr);

    if (int new_fd = ::accept(sock_fd_, reinterpret_cast<sockaddr*>(&their_addr), &addr_size); new_fd < 0)
    {
      throw socket_exception{"accept failed"};
    }
    else
    {
      return socket{new_fd};
    }
  }

  void socket::set_nodelay(bool enable)
  {
    int flag = enable ? 1 : 0;

    if (::setsockopt(sock_fd_, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0)
    {
      throw socket_exception{"setsockopt TCP_NODELAY failed"};
    }
  }

  void socket::set_nonblocking(bool enable)
  {
    int flags = ::fcntl(sock_fd_, F_GETFL, 0);

    if (flags < 0)
    {
      throw socket_exception{"fcntl F_GETFL failed"};
    }

    if (enable)
    {
      flags |= O_NONBLOCK;
    }
    else
    {
      flags &= ~O_NONBLOCK;
    }

    if (::fcntl(sock_fd_, F_SETFL, flags) < 0)
    {
      throw socket_exception{"fcntl F_SETFL failed"};
    }
  }

} // namespace bsd
