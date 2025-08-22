#pragma once

#include "socket.hpp"
#include "registered_buffer_pool.hpp"
#include <utility/ref_or_own.hpp>
#include <utility/logger.hpp>

#include <boost/circular_buffer.hpp>
#include <functional>
#include <iostream>
#include <cassert>
#include <deque>

namespace uring
{
  class sender
  {
  public:
    using buffer_index_type = registered_buffer_pool::buffer_index_type;
    using socket_type = utility::ref_or_own<socket>;

    sender(io_context& io_ctx, registered_buffer_pool& buf_pool);

    enum flags : int
    {
      zerocopy = 1 << 0
    };
    void open(socket_type sock, flags f = static_cast<flags>(0));
    template<typename F>
    void send(std::size_t size, F&& f);
    void send(void const* data, std::size_t size);

    [[nodiscard]] socket& get_socket() noexcept { return sock_.get(); }

  private:
    void start_send_operation();
    static void on_send_completion(::io_uring_cqe const& cqe, void* context);
    static void on_zc_send_completion(::io_uring_cqe const& cqe, void* context);
    void on_zf_notify(::io_uring_cqe const& cqe);

    io_context& io_ctx_;
    socket_type sock_;
    registered_buffer_pool& buf_pool_;
    io_context::request_handle send_handle_;

    struct buffer_data
    {
      buffer_index_type index;
      std::size_t offset = 0;
      std::size_t size = 0;
      std::size_t pending_zf_notify = 0;
    };

    flags flags_;
    int send_error_ = 0;
    bool sending_ = false;
    std::optional<buffer_data> active_buf_;
    std::optional<buffer_data> pending_buf_;
    io_context::submit_sequence last_sub_seq_;
    ::io_uring_sqe* last_send_sqe_ = nullptr;
  };

  template<typename F>
  void sender::send(std::size_t size, F&& f)
  {
    if (send_error_ > 0) { throw std::runtime_error("send failed: " + std::string(strerror(send_error_))); }

    if (!active_buf_)
    {
      LOG_TRACE("activating new buffer: size={}, is_sending={}", size, sending_);
      active_buf_.emplace().index = buf_pool_.acquire_buffer();
      auto buf = buf_pool_.get_buffer(active_buf_->index);
      active_buf_->size = std::invoke(std::forward<F>(f), buf.data(), buf.size());
      start_send_operation();
      return;
    }

    if (pending_buf_)
    {
      auto buf = buf_pool_.get_buffer(pending_buf_->index);
      buf += (pending_buf_->offset + pending_buf_->size);
      LOG_TRACE(
        "appending to pending buffer: size={}, active_size={}, pending_size={}",
        size,
        active_buf_->size,
        pending_buf_->size);

      if (buf.size() < size)
      {
        std::terminate();
        throw std::runtime_error("sender: insufficient buffer space");
      }

      pending_buf_->size += std::invoke(std::forward<F>(f), buf.data(), buf.size());
      return;
    }

    auto buf = buf_pool_.get_buffer(active_buf_->index);
    buf += (active_buf_->offset + active_buf_->size);

    if (buf.size() >= size)
    {
      active_buf_->size += std::invoke(std::forward<F>(f), buf.data(), buf.size());

      if (last_sub_seq_ == io_ctx_.get_submit_sequence())
      {
        LOG_TRACE(
          "appending to active buffer and updating last send SQE: size={}, new size: {}", size, active_buf_->size);
        last_send_sqe_->len = active_buf_->size; // update the send length
        return;
      }

      if (!sending_)
      {
        LOG_TRACE(
          "appending to active buffer and starting new send operation: size={}, active_size={}",
          size,
          active_buf_->size);
        start_send_operation();
      }
      else
      {
        LOG_TRACE(
          "appending to active buffer while sending already started: size={}, active_size={}", size, active_buf_->size);
      }

      return;
    }

    // std::terminate();

    pending_buf_.emplace().index = buf_pool_.acquire_buffer();
    buf = buf_pool_.get_buffer(pending_buf_->index);
    pending_buf_->size = std::invoke(std::forward<F>(f), buf.data(), buf.size());
    LOG_TRACE(
      "appending to pending buffer: size={}, pending_size={}, is_sending={}", size, pending_buf_->size, sending_);
  }
}
