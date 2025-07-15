#include "sender.hpp"

#include <cstring>
#include <iostream>

namespace uring
{
  sender::sender(io_context& io_ctx, std::uint16_t max_buf_cnt, std::size_t max_buf_size, buffer_group_id_type group_id)
    : io_ctx_{io_ctx},
      sock_{AF_INET, SOCK_STREAM, 0},
      buf_pool_{io_ctx, max_buf_cnt, max_buf_size, group_id},
      send_req_data_{nullptr, this}
  {
  }

  void sender::open(bsd::socket sock)
  {
    sock_ = std::move(sock);
  }

  void sender::send_after_fill(std::size_t req_size)
  {
    buf_pool_.push_buffer(buf_id_head_, req_size);

    // std::cout << "sending buffer " << buf_id_head_.value() << std::endl;

    if (++buf_id_head_ == buf_pool_.get_buffer_count()) { buf_id_head_ = buffer_id_type{0}; }

    is_buffer_full_ = (buf_id_head_ == buf_id_tail_);

    if (state_ == state::idle) { start_bundle_send_operation(); }
  }

  void sender::on_bundle_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    // std::cout << "on_bundle_send_completion res: " << cqe.res << ", flags: " << (cqe.flags & 0xFFFF) << std::endl;

    auto& self = *static_cast<sender*>(context);

    if (cqe.res < 0)
    {
      // std::terminate();
      fprintf(stderr, "Send error on fd %d: %s\n", self.sock_.get_fd(), ::strerror(-cqe.res));
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
          ::io_uring_prep_send_bundle(&sqe, self->sock_.get_fd(), 0, 0);
          sqe.buf_group = self->buf_pool_.get_group_id();
          sqe.flags |= IOSQE_BUFFER_SELECT;
          self->state_ = state::sending;
        }
        else { self->state_ = state::idle; } */
  }

  void sender::on_retry_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    // std::cout << "on_retry_send_completion res: " << cqe.res << std::endl;
    auto& self = *static_cast<sender*>(context);

    if (cqe.res < 0)
    {
      fprintf(stderr, "Send error on fd %d: %s\n", self.sock_.get_fd(), ::strerror(-cqe.res));
      return;
    }

    if (cqe.res > 0) { self.consume_buffer(cqe.res, cqe.flags); }
  }

  void sender::consume_buffer(std::size_t bytes, int flags)
  {
    while (bytes > 0)
    {
      auto const cur_buf = buf_pool_.get_buffer(buf_id_tail_);

      if (bytes < cur_buf.size())
      {
        // std::cout << "starting retry send operation " << buf_id_head_.value() << " " << buf_id_tail_.value() <<
        // std::endl;

        state_ = state::retrying;
        send_req_data_.handler = on_retry_send_completion;
        auto& sqe = io_ctx_.create_request(send_req_data_);
        ::io_uring_prep_send(
          &sqe, sock_.get_fd(), static_cast<std::byte const*>(cur_buf.data()) + bytes, cur_buf.size() - bytes, 0);
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

  void sender::start_bundle_send_operation()
  {
    // std::cout << "starting bundle send operation " << buf_id_head_.value() << " " << buf_id_tail_.value() <<
    // std::endl;
    state_ = state::sending;
    send_req_data_.handler = on_bundle_send_completion;
    auto& sqe = io_ctx_.create_request(send_req_data_);
    ::io_uring_prep_send_bundle(&sqe, sock_.get_fd(), 0, 0);
    sqe.buf_group = buf_pool_.get_group_id();
    sqe.flags |= IOSQE_BUFFER_SELECT;
  }

} // namespace uring
