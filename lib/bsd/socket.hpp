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
    socket_exception(const std::string& what);
  };

  class socket
  {
  public:
    socket();
    explicit socket(int fd);
    socket(int domain, int type, int protocol);
    ~socket();

    socket(const socket&) = delete;
    socket& operator=(const socket&) = delete;
    socket(socket&& other) noexcept;
    socket& operator=(socket&& other) noexcept;

    void connect(const std::string& host, const std::string& port);
    void bind(const std::string& address, const std::string& port);
    void listen(int backlog);
    ssize_t send(const void* data, size_t size, int flags);

    template<typename T>
    void set_option(int level, int optname, const T& optval)
    {
      if (::setsockopt(sock_fd_, level, optname, &optval, sizeof(optval)) < 0)
      {
        throw socket_exception("setsockopt failed");
      }
    }

    socket accept();
    int get_fd() const { return sock_fd_; }

  private:
    int sock_fd_;
  };

} // namespace bsd
