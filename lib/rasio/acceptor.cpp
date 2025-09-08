#include "acceptor.hpp"

#include <asio/ip/address.hpp>
#include <asio/socket_base.hpp>
#include <utility>

namespace rasio
{
  acceptor::acceptor(::asio::io_context& io_ctx) : acceptor_{io_ctx}
  {
  }

  void acceptor::listen(std::string const& address, std::string const& port)
  {
    ::asio::ip::tcp::endpoint endpoint(::asio::ip::make_address(address), static_cast<unsigned short>(std::stoi(port)));
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(::asio::socket_base::reuse_address{true});
    acceptor_.bind(endpoint);
    acceptor_.listen();
  }

  void acceptor::start(accept_callback cb)
  {
    accept_cb_ = std::move(cb);
    do_accept();
  }

  void acceptor::do_accept()
  {
    acceptor_.async_accept(make_custom_alloc_handler(handler_memory_, [this](std::error_code ec, socket sock) {
      accept_cb_(ec, std::move(sock));

      if (!ec)
      {
        do_accept();
      }
    }));
  }
} // namespace rasio