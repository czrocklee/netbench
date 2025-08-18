#include "sender.hpp"

namespace uring
{
  sender::sender(io_context& io_ctx, registered_buffer_pool& buf_pool) : io_ctx_{io_ctx}, buf_pool_{buf_pool}
  {
  }

  void sender::open(socket_type sock)
  {
    sock_ = std::move(sock);
  }

  void sender::send(void const* data, std::size_t size)
  {
    send(size, [&](void* buf, std::size_t) {
      std::memcpy(buf, data, size);
      return size;
    });
  }

  void sender::start_send_operation()
  {
    auto& sqe = io_ctx_.create_request(send_handle_, get_socket().get_file_handle(), on_send_completion, this);
    sqe.ioprio |= IORING_SEND_ZC_REPORT_USAGE;
    auto buf = buf_pool_.get_buffer(active_buf_->index) + active_buf_->offset;
    //::io_uring_prep_send_zc_fixed(&sqe, get_socket().get_fd(), data, size, MSG_WAITALL, 0, buf_index.value());
    ::io_uring_prep_write_fixed(
      &sqe, get_socket().get_file_handle().get_fd(), buf.data(), active_buf_->size, 0, active_buf_->index.value());

    last_sub_seq_ = io_ctx_.get_submit_sequence();
    last_send_sqe_ = &sqe;
  }

  void sender::on_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    auto& self = *static_cast<sender*>(context);

    if (cqe.flags & IORING_CQE_F_NOTIF)
    {
      // auto buf_idx = self.pending_zc_notif_.front();
      // self.pending_zc_notif_.pop_front();
      // self.buf_pool_.release_buffer(buf_idx);
      return;
    }

    if (cqe.res < 0)
    {
      std::cerr << "send failed: " << strerror(-cqe.res) << std::endl;
      return;
    }

    // if (cqe.res > 32) std::cout << "completion: " << cqe.res << std::endl;

    if (auto const bytes_sent = static_cast<std::size_t>(cqe.res); (self.active_buf_->size -= bytes_sent) > 0)
    {
      self.active_buf_->offset += bytes_sent;
      self.active_buf_->size -= bytes_sent;
      self.start_send_operation();
      return;
    }

    self.buf_pool_.release_buffer(self.active_buf_->index);
    self.active_buf_.reset();
    std::swap(self.active_buf_, self.pending_buf_);

    if (self.pending_buf_)
    {
      std::swap(self.active_buf_, self.pending_buf_);
      self.start_send_operation();
    }
  }

}
