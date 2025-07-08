#pragma once

#include "handler_allocator.hpp"

#include <asio.hpp>
#include <asio/error.hpp>
#include <asio/buffer.hpp>
#include <functional>
#include <system_error>
#include <utility>

namespace rasio
{
  class acceptor
  {
  public:
    using accept_callback = std::function<void(std::error_code, ::asio::ip::tcp::socket)>;

    explicit acceptor(::asio::io_context& io_ctx) : acceptor_{io_ctx} {}

    void listen(std::string const& address, std::string const& port)
    {
      ::asio::ip::tcp::endpoint endpoint(
        ::asio::ip::make_address(address), static_cast<unsigned short>(std::stoi(port)));
      acceptor_.open(endpoint.protocol());
      acceptor_.set_option(::asio::ip::tcp::acceptor::reuse_address(true));
      acceptor_.bind(endpoint);
      acceptor_.listen();
    }

    void start(accept_callback cb)
    {
      accept_cb_ = std::move(cb);
      do_accept();
    }

  private:
    void do_accept()
    {
      acceptor_.async_accept(
        make_custom_alloc_handler(handler_memory_, [this](std::error_code ec, ::asio::ip::tcp::socket sock) {
          accept_cb_(ec, std::move(sock));
          if (!ec)
          {
            do_accept();
          }
        }));
    }

    ::asio::ip::tcp::acceptor acceptor_;
    dynamic_handler_memory handler_memory_;
    accept_callback accept_cb_;
  };
} // namespace rasio
