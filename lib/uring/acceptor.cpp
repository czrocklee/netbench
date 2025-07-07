#include "acceptor.hpp"
#include <cstring>
#include <unistd.h>
#include <cstdio>

namespace uring
{
  acceptor::acceptor(io_context& io_ctx) : io_ctx_{io_ctx}, accept_req_data_{on_multishot_accept, this} {}

  void acceptor::listen(std::string const& address, std::string const& port, int backlog)
  {
    listen_sock_ = bsd::socket(AF_INET, SOCK_STREAM, 0);
    int optval = 1;
    listen_sock_.set_option(SOL_SOCKET, SO_REUSEADDR, optval);
    listen_sock_.bind(address, port);
    listen_sock_.listen(backlog);
  }

  void acceptor::start(accept_callback accept_cb)
  {
    accept_cb_ = std::move(accept_cb);
    new_multishot_accept_op();
  }

  void acceptor::on_multishot_accept(::io_uring_cqe const& cqe, void* context)
  {
    auto* self = static_cast<acceptor*>(context);

    if (cqe.res >= 0)
    {
      int new_conn_fd = cqe.res;
      self->accept_cb_({}, bsd::socket{new_conn_fd});
    }
    else
    {
      self->accept_cb_(std::make_error_code(static_cast<std::errc>(-cqe.res)), bsd::socket{});
    }
    
    if (!(cqe.flags & IORING_CQE_F_MORE))
    {
      self->new_multishot_accept_op();
    }
  }

  void acceptor::new_multishot_accept_op()
  {
    auto& sqe = io_ctx_.create_request(accept_req_data_);
    ::io_uring_prep_multishot_accept(&sqe, listen_sock_.get_fd(), nullptr, nullptr, 0);
  }

} // namespace uring
