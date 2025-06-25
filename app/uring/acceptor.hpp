#pragma once

#include "../bsd/socket.hpp"
#include "io_context.hpp"

#include <functional>
#include <string>
#include <utility>

namespace uring
{
  class acceptor
  {
  public:
    using on_accept_callback = std::function<void(bsd::socket&&)>;

    explicit acceptor(io_context& io_ctx, on_accept_callback on_accept_cb)
      : io_ctx_{io_ctx}, on_accept_cb_{std::move(on_accept_cb)}, accept_req_data_{on_multishot_accept, this}
    {
    }

    void listen(const std::string& address, const std::string& port, int backlog = SOMAXCONN)
    {
      listen_sock_ = bsd::socket(AF_INET, SOCK_STREAM, 0);

      // Allow the socket to be reused immediately after it's closed.
      int optval = 1;
      listen_sock_.set_option(SOL_SOCKET, SO_REUSEADDR, optval);

      listen_sock_.bind(address, port);
      listen_sock_.listen(backlog);
    }

    void start() { new_multishot_accept_op(); }

  private:
    static void on_multishot_accept(const ::io_uring_cqe& cqe, void* context)
    {
      auto* self = static_cast<acceptor*>(context);

      if (cqe.res >= 0)
      {
        // Successful accept
        int new_conn_fd = cqe.res;
        if (self->on_accept_cb_)
        {
          // Create a socket wrapper for the new fd and move it to the callback.
          self->on_accept_cb_(bsd::socket{new_conn_fd});
        }
        else
        {
          // If there's no callback, we must close the socket to prevent a resource leak.
          ::close(new_conn_fd);
        }
      }
      else
      {
        // An error occurred during accept.
        fprintf(stderr, "Accept error: %s\n", ::strerror(-cqe.res));
      }

      // If the IORING_CQE_F_MORE flag is not set, the multishot operation
      // has terminated (e.g., due to an error) and must be re-armed.
      if (!(cqe.flags & IORING_CQE_F_MORE)) { self->new_multishot_accept_op(); }
    }

    void new_multishot_accept_op()
    {
      auto& sqe = io_ctx_.create_request(accept_req_data_);
      ::io_uring_prep_multishot_accept(&sqe, listen_sock_.get_fd(), nullptr, nullptr, 0);
    }

    io_context& io_ctx_;
    bsd::socket listen_sock_;
    on_accept_callback on_accept_cb_;
    io_context::req_data accept_req_data_;
  };
} // namespace uring