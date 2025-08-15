#include "sender.hpp"

namespace uring
{
  sender::sender(io_context& io_ctx, registered_buffer_pool& buf_pool, std::size_t max_writelist_entries)
    : io_ctx_{io_ctx}, buf_pool_{buf_pool}, write_list_{max_writelist_entries}, pending_zc_notif_{max_writelist_entries}
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

  void sender::start_send_operation(void const* data, std::size_t size, buffer_index_type buf_index)
  {
    state_ = state::zc_sending;
    auto& sqe = io_ctx_.create_request(send_handle_, get_socket().get_file_handle(), on_send_completion, this);
    sqe.ioprio |= IORING_SEND_ZC_REPORT_USAGE;
    //::io_uring_prep_send_zc_fixed(&sqe, get_socket().get_fd(), data, size, MSG_WAITALL, 0, buf_index.value());
    ::io_uring_prep_write_fixed(&sqe, get_socket().get_file_handle().get_fd(), data, size, 0, buf_index.value());
    last_sub_seq_ = io_ctx_.get_submit_sequence();
    last_send_sqe_ = &sqe;
  }

  void sender::on_send_completion(::io_uring_cqe const& cqe, void* context)
  {
    auto& self = *static_cast<sender*>(context);

    if (cqe.flags & IORING_CQE_F_NOTIF)
    {
      auto buf_idx = self.pending_zc_notif_.front();
      self.pending_zc_notif_.pop_front();
      self.buf_pool_.release_buffer(buf_idx);
      return;
    }

    if (cqe.res < 0)
    {
      std::cerr << "send failed: " << strerror(-cqe.res) << std::endl;
      self.state_ = state::idle;
      return;
    }

    // If partial send, resend the remaining data
    if (auto bytes_sent = static_cast<std::size_t>(cqe.res); bytes_sent < self.head_buf_.size)
    {
      auto buf = self.buf_pool_.get_buffer(self.head_buf_.index);
      buf += bytes_sent;
      self.head_buf_.size -= bytes_sent;
      self.start_send_operation(buf.data(), self.head_buf_.size, self.head_buf_.index);
      return;
    }

    // Full send complete for head_buf_
    assert(self.head_buf_.size == static_cast<std::size_t>(cqe.res));
    //self.pending_zc_notif_.push_back(self.head_buf_.index);
    self.buf_pool_.release_buffer(self.head_buf_.index);

    // If there are queued buffers, send the next one
    if (!self.write_list_.empty())
    {
      self.head_buf_ = self.write_list_.front();
      self.write_list_.pop_front();
      auto buf = self.buf_pool_.get_buffer(self.head_buf_.index);
      self.start_send_operation(buf.data(), self.head_buf_.size, self.head_buf_.index);
      return;
    }

    // No more data to send, set state to idle
    self.state_ = state::idle;
  }

}