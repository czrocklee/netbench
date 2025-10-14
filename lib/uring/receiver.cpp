#include "receiver.hpp"

#include <asio/error.hpp>
#include <asio/buffer.hpp>

#include <utility>
#include <system_error>
#include <iostream>

namespace uring
{
  receiver::receiver(io_context& io_ctx, provided_buffer_pool& buffer_pool) : io_ctx_{io_ctx}, buffer_pool_{buffer_pool}
  {
  }

  void receiver::open(socket sock)
  {
    sock_ = std::move(sock);
  }

  void receiver::start(data_callback cb)
  {
    data_cb_ = std::move(cb);
    new_multishot_recv_op();
  }

  void receiver::on_multishot_recv(::io_uring_cqe const& cqe)
  {
    if (cqe.res <= 0)
    {
      if (cqe.res == -ENOBUFS)
      {
        // Handle the case where no buffers are available

        if (!(cqe.flags & IORING_CQE_F_MORE))
        {
          new_multishot_recv_op();
        }

        return;
      }

      if (cqe.res < 0)
      {
        data_cb_(std::make_error_code(static_cast<std::errc>(-cqe.res)), {});
      }
      else
      {
        data_cb_(::asio::error::make_error_code(::asio::error::eof), {});
      }

      return;
    }

    using buffer_id_type = provided_buffer_pool::buffer_id_type;
    std::size_t bytes_received = static_cast<std::size_t>(cqe.res);
    auto buf_id = buffer_id_type{static_cast<buffer_id_type::value_type>(cqe.flags >> IORING_CQE_BUFFER_SHIFT)};
    std::byte* buffer = buffer_pool_.get_buffer_address(buf_id);

    data_cb_({}, ::asio::const_buffer{buffer, bytes_received});

    buffer_pool_.push_buffer(buf_id);

    if (!(cqe.flags & IORING_CQE_F_MORE))
    {
      new_multishot_recv_op();
    }
  }

  void receiver::new_multishot_recv_op()
  {
    auto const& file_handle = sock_.get_file_handle();
    auto& sqe = io_ctx_.create_request(
      recv_handle_, this, [](auto const& cqe, void* ctxt) { static_cast<receiver*>(ctxt)->on_multishot_recv(cqe); });
    file_handle.update_sqe_flag(sqe);
    ::io_uring_prep_recv_multishot(&sqe, file_handle.get_fd(), nullptr, 0, 0);
    sqe.flags |= IOSQE_BUFFER_SELECT;
    sqe.buf_group = buffer_pool_.get_group_id();
  }

} // namespace uring
