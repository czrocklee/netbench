#pragma once

#include "../bsd/socket.hpp"
#include "provided_buffer_pool.hpp"
#include "registered_buffer_pool.hpp"
#include <utility/ref_or_own.hpp>

#include <functional>
#include <iostream>
#include <cassert>
#include <deque>

namespace uring
{
  class bundle_sender
  {
  public:
    using buffer_id_type = provided_buffer_pool::buffer_id_type;
    using buffer_group_id_type = provided_buffer_pool::group_id_type;
    using socket_type = utility::ref_or_own<bsd::socket>;

    bundle_sender(
      io_context& io_ctx,
      std::uint16_t max_buf_cnt,
      std::size_t max_buf_size,
      buffer_group_id_type group_id);

    void open(socket_type sock);

    template<typename F>
    void send(F&& f);

    void enable_fixed_buffer_fastpath(registered_buffer_pool& reg_buf_pool);

    [[nodiscard]] bool is_buffer_full() const noexcept { return is_buffer_full_; }

    [[nodiscard]] provided_buffer_pool& get_buffer_pool() noexcept { return buf_pool_; }

    [[nodiscard]] bsd::socket& get_socket() noexcept { return sock_.get(); }

  private:
    static void on_fixed_buffer_send_completion(::io_uring_cqe const& cqe, void* context);
    static void on_bundle_send_completion(::io_uring_cqe const& cqe, void* context);
    static void on_retry_send_completion(::io_uring_cqe const& cqe, void* context);

    void send_fastpath();

    void send_after_fill(std::size_t req_size);

    void consume_buffer(std::size_t size, int flags);

    void start_bundle_send_operation();

    struct fixed_buffer_data
    {
      fixed_buffer_data(registered_buffer_pool& p) : pool{p} {}
      registered_buffer_pool& pool;
      registered_buffer_pool::buffer_index_type idx;
      ::asio::mutable_buffer buf;
      io_context::req_data send_req_data;
    };

    io_context& io_ctx_;
    socket_type sock_;
    std::unique_ptr<fixed_buffer_data> fixed_buf_data_;
    provided_buffer_pool buf_pool_;
    buffer_id_type buf_id_head_{0};
    buffer_id_type buf_id_tail_{0};
    bool is_buffer_full_ = false;
    io_context::req_data send_req_data_;

    enum class state
    {
      idle,
      fastpath,
      fastpath_sending,
      sending,
      retrying
    };

    state state_ = state::idle;
  };

  template<typename F>
  void bundle_sender::send(F&& f)
  {
    assert(!is_buffer_full_);
    
    //std::cout << "bundle_sender::send called with state: " << static_cast<int>(state_) << std::endl;
    if (state_ == state::idle && fixed_buf_data_ && !fixed_buf_data_->pool.is_empty())
    {
      fixed_buf_data_->idx = fixed_buf_data_->pool.acquire_buffer();
      const auto buffer = fixed_buf_data_->pool.get_buffer(fixed_buf_data_->idx);
      std::size_t const req_size = std::invoke(std::forward<F>(f), buffer.data(), buffer.size());
      fixed_buf_data_->buf = ::asio::buffer(buffer.data(), req_size);
      send_fastpath();
      return;
    }

    std::size_t const req_size =
      std::invoke(std::forward<F>(f), buf_pool_.get_buffer_address(buf_id_head_), buf_pool_.get_buffer_size());
    send_after_fill(req_size);
  }
}
