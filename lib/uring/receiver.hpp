#include "socket.hpp"
#include "io_context.hpp"
#include "provided_buffer_pool.hpp"

#include <asio/buffer.hpp>
#include <asio/error.hpp>
#include <functional>
#include <system_error>

namespace uring
{
  class receiver
  {
  public:
    using data_callback = std::move_only_function<void(std::error_code, ::asio::const_buffer)>;

    explicit receiver(io_context& io_ctx, provided_buffer_pool& buffer_pool);

    void open(socket sock);

    void start(data_callback cb);

    socket& get_socket() noexcept { return sock_; }

    io_context& get_io_context() noexcept { return io_ctx_; }

  private:
    static void on_multishot_recv(const ::io_uring_cqe& cqe, void* context);
    void new_multishot_recv_op();

    io_context& io_ctx_;
    socket sock_;
    provided_buffer_pool& buffer_pool_;
    io_context::request_handle recv_handle_;
    data_callback data_cb_;
  };
}