#include "bundle_receiver.hpp"
#include <utility>
#include <cstring>
#include <iostream>

namespace uring
{

  bundle_receiver::bundle_receiver(io_context& io_ctx, provided_buffer_pool& buffer_pool)
    : io_ctx_{io_ctx}, buffer_pool_{buffer_pool}, recv_req_data_{on_bundle_recv, this}
  {
    bundle_.reserve(buffer_pool.get_buffer_count());
  }

  void bundle_receiver::open(bsd::socket sock) { sock_ = std::move(sock); }

  void bundle_receiver::start(data_callback cb)
  {
    data_cb_ = std::move(cb);
    new_bundle_recv_op();
  }

  void bundle_receiver::on_bundle_recv(::io_uring_cqe const& cqe, void* context)
  {
    auto* self = static_cast<bundle_receiver*>(context);
    self->bundle_.clear();

    if (cqe.res <= 0)
    {
      std::error_code ec = (cqe.res < 0) ? std::make_error_code(static_cast<std::errc>(-cqe.res))
                                         : ::asio::error::make_error_code(::asio::error::eof);
      self->data_cb_(ec, self->bundle_);
      return;
    }

    int buffer_id = cqe.flags >> IORING_CQE_BUFFER_SHIFT;
    int buffer_id_start = buffer_id;
    std::size_t bytes_received = static_cast<std::size_t>(cqe.res);

    std::cout << "Received bundle with " << bytes_received << " bytes, starting from buffer ID " << buffer_id_start
              << std::endl;

    do {
      std::uint8_t const* address = self->buffer_pool_.get_buffer_address(buffer_id);
      std::size_t const size = std::min(bytes_received, self->buffer_pool_.get_buffer_size());
      self->bundle_.emplace_back(address, size);
      bytes_received -= size;

      if (++buffer_id == self->buffer_pool_.get_buffer_count())
      {
        buffer_id = 0;
      }

    } while (bytes_received > 0);

    self->data_cb_({}, self->bundle_);

    self->buffer_pool_.reprovide_buffers(buffer_id_start, buffer_id);

    if (!(cqe.flags & IORING_CQE_F_MORE))
    {
      self->new_bundle_recv_op();
    }
  }

  void bundle_receiver::new_bundle_recv_op()
  {
    auto& sqe = io_ctx_.create_request(recv_req_data_);
    ::io_uring_sqe_set_flags(&sqe, IOSQE_BUFFER_SELECT | IORING_RECVSEND_BUNDLE);
    sqe.buf_group = buffer_pool_.get_group_id();
    io_uring_prep_recv_multishot(&sqe, sock_.get_fd(), NULL, 0, 0);
  }

} // namespace uring
