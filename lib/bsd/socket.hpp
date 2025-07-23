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
    socket_exception(std::string const& what);
  };

  class socket
  {
  public:
    socket();
    explicit socket(int fd);
    socket(int domain, int type, int protocol);
    ~socket();

    socket(socket const&) = delete;
    socket& operator=(socket const&) = delete;
    socket(socket&& other) noexcept;
    socket& operator=(socket&& other) noexcept;

    void connect(std::string const& host, std::string const& port);
    void bind(std::string const& address, std::string const& port);
    void listen(int backlog);
    std::size_t recv(void* buffer, std::size_t size, int flags);
    std::size_t send(void const* data, std::size_t size, int flags);

    template<typename T>
    void set_option(int level, int optname, T const& optval)
    {
      if (::setsockopt(sock_fd_, level, optname, &optval, sizeof(optval)) < 0)
      {
        throw socket_exception("setsockopt failed");
      }
    }

    socket accept();
    int get_fd() const { return sock_fd_; }

    void set_nodelay(bool enable);
    void set_nonblocking(bool enable);

    // adapters to asio's socket
    template<typename SettableSocketOption>
    void set_option(SettableSocketOption const& option)
    {
      if (::setsockopt(sock_fd_, option.level(0), option.name(0), option.data(0), option.size(0)) < 0)
      {
        throw socket_exception("setsockopt failed");
      }
    }

    int native_handle() const { return sock_fd_; }
    void non_blocking(bool enable) { set_nonblocking(enable); }

    template<typename MutableBuffer>
    std::size_t receive(MutableBuffer const& buffer, int flags)
    {
      return recv(buffer.data(), buffer.size(), flags);
    }

    template<typename ConstBuffer>
    std::size_t send(ConstBuffer const& buffer, int flags)
    {
      return send(buffer.data(), buffer.size(), flags);
    }

  private:
    int sock_fd_;
  };

} // namespace bsd
