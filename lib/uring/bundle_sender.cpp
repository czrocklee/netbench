#include "bundle_sender.hpp"

#include <cstring>
#include <iostream>
#include <utility/time.hpp>

namespace uring
{
  bundle_sender::bundle_sender(
    io_context& io_ctx,
    std::uint16_t max_buf_cnt,
    std::size_t max_buf_size,
    buffer_group_id_type group_id)
    : io_ctx_{io_ctx}, buf_pool_{io_ctx, max_buf_cnt, max_buf_size, group_id}
  {
  }

  void bundle_sender::open(socket_type sock)
  {
    sock_ = std::move(sock);
  }

  void bundle_sender::send_after_fill(std::size_t req_size)
  {
    buf_pool_.push_buffer(buf_id_head_, req_size);

    // std::cout << "sending buffer " << buf_id_head_.value() << std::endl;

    if (++buf_id_head_ == buf_pool_.get_buffer_count()) { buf_id_head_ = buffer_id_type{0}; }

    is_buffer_full_ = (buf_id_head_ == buf_id_tail_);

    if (state_ == state::idle || state_ == state::fastpath_sending) { start_bundle_send_operation(); }
  }

  void bundle_sender::on_bundle_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    // std::cout << "on_bundle_send_completion res: " << cqe.res << ", flags: " << (cqe.flags & 0xFFFF) << std::endl;

    auto& self = *static_cast<bundle_sender*>(context);

    if (cqe.res < 0)
    {
      // std::terminate();
      fprintf(stderr, "Send error on fd %d: %s\n", self.get_socket().get_fd(), ::strerror(-cqe.res));
      return;
    }

    auto buf_id = buffer_id_type::cast_from(cqe.flags >> IORING_CQE_BUFFER_SHIFT);

    if (buf_id != self.buf_id_tail_)
    {
      fprintf(stderr, "invalid buffer id %d, expected %d\n", buf_id.value(), self.buf_id_tail_.value());
    }

    // std::cout << "bytes sent: " << cqe.res << ", buffer id: " << buf_id << std::endl;

    if (cqe.res > 0)
    {
      self.is_buffer_full_ = false;
      self.consume_buffer(cqe.res, cqe.flags);
    }

    /*     if (!self->pending_buffers_size_.empty())
        {
          auto& sqe = self->io_ctx_.create_request(self->send_req_data_);
          ::io_uring_prep_send_bundle(&sqe, self->get_socket().get_fd(), 0, 0);
          sqe.buf_group = self->buf_pool_.get_group_id();
          sqe.flags |= IOSQE_BUFFER_SELECT;
          self->state_ = state::sending;
        }
        else { self->state_ = state::idle; } */
  }

  void bundle_sender::on_retry_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    // std::cout << "on_retry_send_completion res: " << cqe.res << std::endl;
    auto& self = *static_cast<bundle_sender*>(context);

    if (cqe.res < 0)
    {
      fprintf(stderr, "Send error on fd %d: %s\n", self.get_socket().get_fd(), ::strerror(-cqe.res));
      return;
    }

    if (cqe.res > 0) { self.consume_buffer(cqe.res, cqe.flags); }
  }

  void bundle_sender::enable_fixed_buffer_fastpath(registered_buffer_pool& reg_buf_pool)
  {
    fixed_buf_data_ = std::make_unique<fixed_buffer_data>(reg_buf_pool);
  }

  void bundle_sender::send_fastpath()
  {
    state_ = state::fastpath;
    fixed_buf_data_->send_handle = io_ctx_.create_request(on_fixed_buffer_send_completion, this);
    auto& sqe = fixed_buf_data_->send_handle.get_sqe();
    sqe.ioprio |= IORING_SEND_ZC_REPORT_USAGE;
    ::io_uring_prep_send_zc_fixed(
      &sqe,
      get_socket().get_fd(),
      fixed_buf_data_->buf.data(),
      fixed_buf_data_->buf.size(),
      MSG_WAITALL,
      0,
      fixed_buf_data_->idx.value());
  }

  void bundle_sender::on_fixed_buffer_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    // std::cout << utility::nanos_since_epoch() << " on_fixed_buffer_send_completion res: " << cqe.res << ", flags: "
    // << (cqe.flags & 0xFFFF) << std::endl;

    auto& self = *static_cast<bundle_sender*>(context);

    if (cqe.flags & IORING_CQE_F_NOTIF)
    {
      /*       if (self.state_ == state::fastpath)
            {
              std::cout << "Fast path send completed successfully." << std::endl;
              self.state_ = state::idle;
              self.fixed_buf_data_->pool.release_buffer(self.fixed_buf_data_->idx);
            } */

      if (self.state_ == state::fastpath_sending)
      {
        // std::cout << "Fast path send completed successfully." << std::endl;
        self.state_ = state::idle;
      }

      self.fixed_buf_data_->pool.release_buffer(self.fixed_buf_data_->idx);
      return;
    }

    if (cqe.res > 0)
    {
      assert(cqe.res == self.fixed_buf_data_->buf.size());

      // fast path confirmed, start bundle send operation if there are buffers to send
      if (self.buf_id_head_ != self.buf_id_tail_ || self.is_buffer_full_) { self.start_bundle_send_operation(); }
      else { self.state_ = state::fastpath_sending; }
      /*       else
            {
              // std::cout << "Fast path send completed successfully." << std::endl;
              self.state_ = state::idle;
              self.fixed_buf_data_->pool.release_buffer(self.fixed_buf_data_->idx);
            } */
    }
  }

  void bundle_sender::consume_buffer(std::size_t bytes, int flags)
  {
    while (bytes > 0)
    {
      auto const cur_buf = buf_pool_.get_buffer(buf_id_tail_);

      if (bytes < cur_buf.size())
      {
        state_ = state::retrying;
        send_handle_ = io_ctx_.create_request(on_retry_send_completion, this);
        auto& sqe = send_handle_.get_sqe();
        ::io_uring_prep_send(
          &sqe,
          get_socket().get_fd(),
          static_cast<std::byte const*>(cur_buf.data()) + bytes,
          cur_buf.size() - bytes,
          0);
        break;
      }

      bytes -= cur_buf.size();
      // std::cout << "consuming buffer " << buf_id_tail_.value() << std::endl;

      if (++buf_id_tail_ == buf_pool_.get_buffer_count()) { buf_id_tail_ = buffer_id_type{0}; }
    }

    if (buf_id_head_ == buf_id_tail_) { state_ = state::idle; }
    else if (!(flags & IORING_CQE_F_MORE)) { start_bundle_send_operation(); }
    else { /*std::cout << "more completion comming" << std::endl;*/ }
  }

  void bundle_sender::start_bundle_send_operation()
  {
    // std::cout << "starting bundle send operation " << buf_id_head_.value() << " " << buf_id_tail_.value() <<
    // std::endl;
    state_ = state::sending;
    send_handle_ = io_ctx_.create_request(on_bundle_send_completion, this);
    auto& sqe = send_handle_.get_sqe();
    ::io_uring_prep_send_bundle(&sqe, get_socket().get_fd(), 0, 0);
    sqe.buf_group = buf_pool_.get_group_id();
    sqe.flags |= IOSQE_BUFFER_SELECT;
  }
} // namespace uring
