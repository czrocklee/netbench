#include "connector.hpp"

namespace bsd
{
  connector::connector(io_context& io_ctx) : io_ctx_{io_ctx} {}

  socket connector::connect(std::string const& host, std::string const& port)
  {
    socket sock{AF_INET, SOCK_STREAM, 0};
    sock.connect(host, port);
    return sock;
  }
} // namespace bsd