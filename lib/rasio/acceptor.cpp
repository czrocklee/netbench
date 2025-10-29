#include "acceptor.hpp"
#include "handler_allocator.hpp"

#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/socket_base.hpp>
#include <utility>

namespace rasio
{
  struct acceptor::impl
  {
    ::asio::basic_socket_acceptor<::asio::ip::tcp, ::asio::io_context::executor_type> acceptor;
    dynamic_handler_memory handler_memory;
    accept_callback accept_cb;

    explicit impl(::asio::io_context& io_ctx) : acceptor{io_ctx} {}

    void do_accept(std::shared_ptr<impl> self)
    {
      acceptor.async_accept(
        make_custom_alloc_handler(handler_memory, [self = std::move(self)](std::error_code ec, socket sock) {
          if (self->accept_cb)
          {
            self->accept_cb(ec, std::move(sock));
          }

          if (!ec)
          {
            self->do_accept(std::move(self));
          }
        }));
    }
  };

  acceptor::acceptor(::asio::io_context& io_ctx) : impl_{std::make_shared<impl>(io_ctx)}
  {
  }

  acceptor::~acceptor()
  {
    impl_->accept_cb = nullptr;
    impl_->acceptor.close();
  };

  void acceptor::listen(std::string const& address, std::string const& port)
  {
    ::asio::ip::tcp::endpoint endpoint(::asio::ip::make_address(address), static_cast<unsigned short>(std::stoi(port)));
    impl_->acceptor.open(endpoint.protocol());
    impl_->acceptor.set_option(::asio::socket_base::reuse_address{true});
    impl_->acceptor.bind(endpoint);
    impl_->acceptor.listen();
  }

  void acceptor::start(accept_callback cb)
  {
    impl_->accept_cb = std::move(cb);
    impl_->do_accept(impl_);
  }
} // namespace rasio