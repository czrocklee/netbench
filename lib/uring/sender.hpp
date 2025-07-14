#pragma once

#include "../bsd/socket.hpp"
#include "provided_buffer_pool.hpp"

#include <functional>
#include <iostream>
#include <cassert>
#include <deque>

namespace uring
{
  class sender
  {
  public:
    using buffer_id_type = provided_buffer_pool::buffer_id_type;
    using buffer_group_id_type = provided_buffer_pool::group_id_type;

    sender(io_context& io_ctx, std::uint16_t max_buf_cnt, std::size_t max_buf_size, buffer_group_id_type group_id);

    void open(bsd::socket sock);

    template<typename F>
    void send(F&& f);

    [[nodiscard]] bool is_buffer_full() const noexcept { return is_buffer_full_; }

    [[nodiscard]] provided_buffer_pool& get_buffer_pool() noexcept { return buf_pool_; }

  private:
    static void on_bundle_send_completion(::io_uring_cqe const& cqe, void* context);
    static void on_retry_send_completion(::io_uring_cqe const& cqe, void* context);

    void send_after_fill(std::size_t req_size);

    void consume_buffer(std::size_t size, int flags);

    void start_bundle_send_operation();

    io_context& io_ctx_;
    bsd::socket sock_;
    provided_buffer_pool buf_pool_;
    buffer_id_type buf_id_head_{0};
    buffer_id_type buf_id_tail_{0};
    bool is_buffer_full_ = false;
    io_context::req_data send_req_data_;

    enum class state
    {
      idle,
      sending,
      retrying
    };

    state state_ = state::idle;
  };

  template<typename F>
  void sender::send(F&& f)
  {
    assert(!is_buffer_full_);
    std::size_t const req_size =
      std::invoke(std::forward<F>(f), buf_pool_.get_buffer_address(buf_id_head_), buf_pool_.get_buffer_size());
    send_after_fill(req_size);
  }
}
