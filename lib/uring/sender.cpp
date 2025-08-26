#include "sender.hpp"

namespace uring
{
  sender::sender(io_context& io_ctx, registered_buffer_pool& buf_pool, std::size_t max_buf_size)
    : io_ctx_{io_ctx}, buf_pool_{buf_pool}, write_list_{max_buf_size / buf_pool.get_buffer_size()}
  {
    if (write_list_.capacity() == 0)
    {
      throw std::runtime_error("sender: max_buf_size is too small for the buffer pool size");
    }

    LOG_INFO(
      "sender initialized: max_buf_size={}, buffer_size={}, max_buffer_count={}",
      max_buf_size,
      buf_pool.get_buffer_size(),
      write_list_.capacity());
  }

  void sender::open(socket_type sock, flags f)
  {
    sock_ = std::move(sock);
    flags_ = f;

    if (flags_ & flags::zerocopy)
    {
      int zc_enable = 1;
      sock_.get().set_option(SOL_SOCKET, SO_ZEROCOPY, zc_enable);
    }
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
    auto& data = write_list_[active_index_];
    auto buf = buf_pool_.get_buffer(data.index) + data.offset;
    auto& file = get_socket().get_file_handle();

    if (flags_ & flags::zerocopy)
    {
      LOG_DEBUG("starting zerocopy send operation: size={}, index={}", data.size, active_index_);
      auto& sqe = io_ctx_.create_request(send_handle_, file, on_zc_send_completion, this);
      ::io_uring_prep_send_zc_fixed(&sqe, file.get_fd(), buf.data(), data.size, 0, 0, data.index.value());
      sqe.ioprio |= IORING_SEND_ZC_REPORT_USAGE;
      ++data.pending_zf_notify;
      ++pending_zf_notify_;
      last_send_sqe_ = &sqe;
    }
    else
    {
      LOG_DEBUG("starting regular send operation: size={}, index={}", data.size, active_index_);
      auto& sqe = io_ctx_.create_request(send_handle_, file, on_send_completion, this);
      ::io_uring_prep_write_fixed(&sqe, file.get_fd(), buf.data(), data.size, 0, data.index.value());
      /*       ::io_uring_prep_send(&sqe, file.get_fd(), buf.data(), data.size, 0);
            sqe.ioprio |= IORING_RECVSEND_FIXED_BUF;
            sqe.buf_index = data.index.value(); */
      last_send_sqe_ = &sqe;
    }

    sending_ = true;
    last_sub_seq_ = io_ctx_.get_submit_sequence();
  }

  void sender::on_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    auto& self = *static_cast<sender*>(context);

    if (cqe.res < 0)
    {
      self.send_error_ = -cqe.res;
      std::cerr << "send failed: " << strerror(self.send_error_) << std::endl;
      return;
    }

    auto const bytes_sent = static_cast<std::size_t>(cqe.res);
    auto& data = self.write_list_.front();
    data.offset += bytes_sent;
    data.size -= bytes_sent;

    if (data.size > 0)
    {
      LOG_DEBUG(
        "send completion and keep sending: bytes_sent={}, active_size={}, pending_bufs={}",
        bytes_sent,
        data.size,
        self.write_list_.size());
      self.start_send_operation();
      return;
    }

    self.buf_pool_.release_buffer(data.index);
    self.write_list_.pop_front();
    self.sending_ = false;

    if (!self.write_list_.empty())
    {
      LOG_DEBUG(
        "send completion and switch to next buffer: bytes_sent={}, active_size={}, pending_bufs={}",
        bytes_sent,
        self.write_list_.front().size,
        self.write_list_.size());
      self.start_send_operation();
    }
    else { LOG_DEBUG("send completion and no more data to send: bytes_sent={}", bytes_sent); }
  }

  void sender::on_zc_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    auto& self = *static_cast<sender*>(context);

    if (cqe.flags & IORING_CQE_F_NOTIF)
    {
      self.on_zf_notify(cqe);
      return;
    }

    if (cqe.res < 0)
    {
      self.send_error_ = -cqe.res;
      std::cerr << "send failed: " << strerror(self.send_error_) << std::endl;
      return;
    }

    auto const bytes_sent = static_cast<std::size_t>(cqe.res);
    auto& data = self.write_list_[self.active_index_];
    data.offset += bytes_sent;
    data.size -= bytes_sent;

    /*     std::cout << "send completed: " << cqe.res << " bytes " << " active buffer size: " << self.active_buf_->size
                  << " offset " << self.active_buf_->offset << std::endl; */

    if (data.size > 0)
    {
      LOG_DEBUG(
        "zerocopy send completion and keep sending: bytes_sent={}, active_index={}, active_size={}, pending_bufs={}",
        bytes_sent,
        self.active_index_,
        data.size,
        self.write_list_.size() - self.active_index_);
      self.start_send_operation();
      return;
    }

    self.sending_ = false;

    if (self.write_list_.size() > self.active_index_ + 1)
    {
      ++self.active_index_;
      LOG_DEBUG(
        "zerocopy completion and switch to next buffer: bytes_sent={}, active_index={}, active_size={}, pending_bufs={}",
        bytes_sent,
        self.active_index_,
        self.write_list_[self.active_index_].size,
        self.write_list_.size() - self.active_index_);
      self.start_send_operation();
    }
    else
    {
      LOG_DEBUG(
        "zerocopy send completion and no more data to send: bytes_sent={}, active_index={}", bytes_sent, self.active_index_);
    }
  }

  void sender::on_zf_notify(::io_uring_cqe const& cqe)
  {
    // std::cout << "notify received " << active_buf_->pending_zf_notify - 1 << std::endl;

    --pending_zf_notify_;
    auto& data = write_list_.front();

    if (--data.pending_zf_notify == 0)
    {
      buf_pool_.release_buffer(data.index);
      write_list_.pop_front();
      --active_index_;
      LOG_DEBUG(
        "zerocopy send notif, front buffer cleared: err={}, front_counter={}, active_index={}, pending_zf_notify={}",
        std::strerror(-cqe.res),
        data.pending_zf_notify,
        active_index_,
        pending_zf_notify_);
    }
    else
    {
      LOG_DEBUG(
        "zerocopy send notif: err={}, front_counter={}, is_sending={}, active_index={}, pending_zf_notify={}",
        std::strerror(-cqe.res),
        data.pending_zf_notify,
        sending_,
        active_index_,
        pending_zf_notify_);
    }
  }
}
