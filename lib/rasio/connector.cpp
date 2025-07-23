#include "connector.hpp"

#include <asio/connect.hpp>
#include <asio/ip/tcp.hpp>

namespace rasio
{
  connector::connector(::asio::io_context& io_ctx) : io_ctx_{io_ctx} {}

  ::asio::ip::tcp::socket connector::connect(std::string const& host, std::string const& port)
  {
    ::asio::ip::tcp::resolver resolver{io_ctx_};
    auto endpoints = resolver.resolve(host, port);
    ::asio::ip::tcp::socket sock{io_ctx_};
    ::asio::connect(sock, endpoints);
    return sock;
  }
} // namespace rasio