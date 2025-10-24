#include "socket.hpp"
#include "io_context.hpp"
#include "provided_buffer_pool.hpp"
#include <utility/ref_or_own.hpp>

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
    using buffer_pool_type = utility::ref_or_own<provided_buffer_pool>;

    // Construct with either a referenced or owned buffer pool
    explicit receiver(io_context& io_ctx, buffer_pool_type buffer_pool);
    void open(socket sock);
    void start(data_callback cb);

    socket& get_socket() noexcept { return sock_; }
    io_context& get_io_context() noexcept { return io_ctx_; }

  private:
    void on_multishot_recv(::io_uring_cqe const& cqe);
    void new_multishot_recv_op();

    io_context& io_ctx_;
    socket sock_;
    buffer_pool_type buffer_pool_;
    io_context::request_handle recv_handle_;
    data_callback data_cb_;
  };
}