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
/*     std::cout << "Starting send operation with active buffer size: " << active_buf_->size
              << " pending buffer size: " << (pending_buf_ ? pending_buf_->size : 0) << std::endl; */
    auto& sqe = io_ctx_.create_request(send_handle_, get_socket().get_file_handle(), on_send_completion, this);
    auto buf = buf_pool_.get_buffer(active_buf_->index) + active_buf_->offset;
    ::io_uring_prep_send_zc_fixed(
      &sqe, get_socket().get_file_handle().get_fd(), buf.data(), active_buf_->size, 0, 0, active_buf_->index.value());
    sqe.ioprio |= IORING_SEND_ZC_REPORT_USAGE;
    ++active_buf_->pending_zf_notify;
    sending_ = true;

    //::io_uring_prep_write_fixed(
    //&sqe, get_socket().get_file_handle().get_fd(), buf.data(), active_buf_->size, 0, active_buf_->index.value());

    last_sub_seq_ = io_ctx_.get_submit_sequence();
    last_send_sqe_ = &sqe;
  }

  void sender::on_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    auto& self = *static_cast<sender*>(context);

    if (cqe.flags & IORING_CQE_F_NOTIF)
    {
      /* std::cout << "notify received active buffer size: " << self.active_buf_->size
                << " pending buffer size: " << (self.pending_buf_ ? self.pending_buf_->size : 0) << std::endl; */
      //std::cout << "notif res: " << cqe.res << std::endl;
      self.on_zf_notify();
      return;
    }

    if (cqe.res < 0)
    {
      self.send_error_ = -cqe.res;
      std::cerr << "send failed: " << strerror(self.send_error_) << std::endl;
      return;
    }

    auto const bytes_sent = static_cast<std::size_t>(cqe.res);
    self.active_buf_->offset += bytes_sent;
    self.active_buf_->size -= bytes_sent;

/*     std::cout << "send completed: " << cqe.res << " bytes " << " active buffer size: " << self.active_buf_->size
              << " offset " << self.active_buf_->offset << std::endl; */

    if (self.active_buf_->size > 0) { self.start_send_operation(); }
    else { self.sending_ = false; }
  }

  void sender::on_zf_notify()
  {
    //std::cout << "notify received " << active_buf_->pending_zf_notify - 1 << std::endl;

    if (--active_buf_->pending_zf_notify > 0) { return; }

    if (send_error_ > 0)
    {
      buf_pool_.release_buffer(active_buf_->index);
      active_buf_.reset();

      if (pending_buf_)
      {
        buf_pool_.release_buffer(pending_buf_->index);
        pending_buf_.reset();
      }

      return;
    }

/*     if (active_buf_->size > 0 && !sending_)
    {
      start_send_operation();
    } */

    buf_pool_.release_buffer(active_buf_->index);
    active_buf_.reset();

    if (pending_buf_)
    {
      std::swap(active_buf_, pending_buf_);
      start_send_operation();
    }
  }
}
