#pragma once

#include "bsd/socket.hpp"
#include "io_context.hpp"
#include "provided_buffer_pool.hpp"
#include <utility/ref_or_own.hpp>

#include <asio/buffer.hpp>
#include <asio/error.hpp>

#include <functional>
#include <system_error>
#include <vector>

namespace uring
{
  class bundle_receiver
  {
  public:
    using data_callback = std::function<void(std::error_code, std::vector<::asio::const_buffer> const&)>;

    explicit bundle_receiver(io_context& io_ctx, utility::ref_or_own<provided_buffer_pool> buffer_pool);

    ~bundle_receiver();

    void open(bsd::socket sock);

    void start(data_callback cb);

    bsd::socket& get_socket() noexcept { return sock_; }

    io_context& get_io_context() noexcept { return io_ctx_; }

  private:
    static void on_bundle_recv(::io_uring_cqe const& cqe, void* context);
    void new_bundle_recv_op();

    io_context& io_ctx_;
    bsd::socket sock_;
    utility::ref_or_own<provided_buffer_pool> buffer_pool_;
    io_context::req_data recv_req_data_;
    data_callback data_cb_;
    std::vector<::asio::const_buffer> bundle_;
  };
} // namespace uring
