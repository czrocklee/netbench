#include "sender.hpp"
#include <magic_enum/magic_enum.hpp>

#include <numeric>

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

  void sender::prepare_send_operation()
  {
    io_ctx_.prepare_request(send_handle_, this, [](::io_uring_sqe& sqe, void* context) {
      static_cast<sender*>(context)->on_submit_send_operation(sqe);
    });
    state_ = state::open;
  }

  void sender::on_submit_send_operation(::io_uring_sqe& sqe)
  {
    /*     std::cout << "Starting send operation with active buffer size: " << active_buf_->size
                  << " pending buffer size: " << (pending_buf_ ? pending_buf_->size : 0) << std::endl; */
    auto& data = write_list_[active_index_];
    auto buf = buf_pool_.get_buffer(data.index) + data.offset;
    auto& file = get_socket().get_file_handle();

    if (flags_ & flags::zerocopy)
    {
      LOG_DEBUG("starting zerocopy send operation: size={}, index={}", data.size, active_index_);
      send_handle_.set_completion_handler(
        this, [](auto const& cqe, void* ctxt) { static_cast<sender*>(ctxt)->on_zc_send_completion(cqe); });

      ::io_uring_prep_send_zc_fixed(&sqe, file.get_fd(), buf.data(), data.size, 0, 0, data.index.value());
      sqe.ioprio |= IORING_SEND_ZC_REPORT_USAGE;
      last_send_sqe_ = &sqe;
    }
    else
    {
      LOG_DEBUG("starting regular send operation: size={}, index={}", data.size, active_index_);
      send_handle_.set_completion_handler(
        this, [](auto const& cqe, void* ctxt) { static_cast<sender*>(ctxt)->on_send_completion(cqe); });
      ::io_uring_prep_write_fixed(&sqe, file.get_fd(), buf.data(), data.size, 0, data.index.value());
      /*       ::io_uring_prep_send(&sqe, file.get_fd(), buf.data(), data.size, 0);
            sqe.ioprio |= IORING_RECVSEND_FIXED_BUF;
            sqe.buf_index = data.index.value(); */
      last_send_sqe_ = &sqe;
    }

    state_ = state::submitted;
  }

  void sender::on_send_completion(::io_uring_cqe const& cqe)
  {
    if (cqe.res < 0)
    {
      send_error_ = -cqe.res;
      std::cerr << "send failed: " << strerror(send_error_) << std::endl;
      return;
    }

    auto const bytes_sent = static_cast<std::size_t>(cqe.res);
    auto& data = write_list_.front();
    data.offset += bytes_sent;
    data.size -= bytes_sent;

    if (data.size > 0)
    {
      LOG_DEBUG(
        "send completion and keep sending: bytes_sent={}, active_size={}, pending_bufs={}",
        bytes_sent,
        data.size,
        write_list_.size());
      prepare_send_operation();
      return;
    }

    buf_pool_.release_buffer(data.index);
    write_list_.pop_front();

    if (!write_list_.empty())
    {
      LOG_DEBUG(
        "send completion and switch to next buffer: bytes_sent={}, active_size={}, pending_bufs={}",
        bytes_sent,
        write_list_.front().size,
        write_list_.size());
      prepare_send_operation();
    }
    else
    {
      state_ = state::idle;
      LOG_DEBUG("send completion and no more data to send: bytes_sent={}", bytes_sent);
    }
  }

  void sender::on_zc_send_completion(::io_uring_cqe const& cqe)
  {
    if (cqe.flags & IORING_CQE_F_NOTIF)
    {
      LOG_TRACE("received zerocopy notify");
      on_zf_notify(cqe);
      return;
    }

    if (cqe.res < 0)
    {
      send_error_ = -cqe.res;
      std::cerr << "send failed: " << strerror(send_error_) << std::endl;
      return;
    }

    auto const bytes_sent = static_cast<std::size_t>(cqe.res);
    auto& data = write_list_[active_index_];
    data.offset += bytes_sent;
    data.size -= bytes_sent;

    /*     std::cout << "send completed: " << cqe.res << " bytes " << " active buffer size: " << self.active_buf_->size
                  << " offset " << self.active_buf_->offset << std::endl; */

    if (data.size > 0)
    {
      LOG_DEBUG(
        "zerocopy send completion and keep sending: bytes_sent={}, active_index={}, active_size={}, pending_bufs={}",
        bytes_sent,
        active_index_,
        data.size,
        write_list_.size() - active_index_);
      prepare_send_operation();
      return;
    }

    if (write_list_.size() > active_index_ + 1)
    {
      ++active_index_;
      LOG_DEBUG(
        "zerocopy completion and switch to next buffer: bytes_sent={}, active_index={}, active_size={}, "
        "pending_bufs={}",
        bytes_sent,
        active_index_,
        write_list_[active_index_].size,
        write_list_.size() - active_index_);
      prepare_send_operation();
    }
    else
    {
      state_ = state::idle;
      LOG_DEBUG(
        "zerocopy send completion and no more data to send: bytes_sent={}, active_index={}", bytes_sent, active_index_);
    }
  }

  void sender::on_zf_notify(::io_uring_cqe const& cqe)
  {
    // std::cout << "notify received " << active_buf_->pending_zf_notify - 1 << std::endl;

    auto& data = write_list_.front();

    if (--data.pending_zf_notify == 0)
    {
      buf_pool_.release_buffer(data.index);
      write_list_.pop_front();

      if (active_index_ > 0)
      {
        --active_index_;
      }

      LOG_DEBUG(
        "zerocopy send notif, front buffer cleared: err={}, front_counter={}, active_index={}, pending_zf_notify={}",
        std::strerror(-cqe.res),
        write_list_.empty() ? 0 : write_list_.front().pending_zf_notify,
        active_index_,
        std::accumulate(
          write_list_.begin(),
          write_list_.begin() + active_index_ + 1,
          0,
          [](std::size_t sum, buffer_data const& bd) { return sum + bd.pending_zf_notify; }));
    }
    else
    {
      LOG_DEBUG(
        "zerocopy send notif: err={}, front_counter={}, state={}, active_index={}, pending_zf_notify={}",
        std::strerror(-cqe.res),
        data.pending_zf_notify,
        magic_enum::enum_name(state_),
        active_index_,
        std::accumulate(
          write_list_.begin(),
          write_list_.begin() + active_index_ + 1,
          0,
          [](std::size_t sum, buffer_data const& bd) { return sum + bd.pending_zf_notify; }));
    }
  }
}
