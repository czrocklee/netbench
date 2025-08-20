#include "provided_buffer_pool.hpp"
#include <utility>
#include <cstring>
#include <stdexcept>
#include <cassert>
#include <iostream>

namespace uring
{
  provided_buffer_pool::provided_buffer_pool(
    io_context& io_ctx,
    std::size_t buf_size,
    std::uint16_t buf_cnt,
    group_id_type grp_id)
    : ring_{io_ctx.get_ring()}, buf_array_{buf_size, buf_cnt}, grp_id_{grp_id}
  {
    int ret = 0;
    buf_ring_ = decltype(buf_ring_){
      ::io_uring_setup_buf_ring(&ring_, buf_cnt, grp_id_.value(), 0, &ret),
      [this](::io_uring_buf_ring* ptr) { ::io_uring_unregister_buf_ring(&ring_, grp_id_); }};

    if (!buf_ring_)
    {
      throw std::runtime_error("Failed to setup ::io_uring buffer ring. Error: " + std::string(strerror(-ret)));
    }

    actual_buf_size_ = std::make_unique<std::size_t[]>(buf_cnt);
  }

  provided_buffer_pool::~provided_buffer_pool() = default;

  void provided_buffer_pool::populate_buffers() noexcept
  {
    push_buffers(buffer_id_type{0}, buffer_id_type{get_buffer_count()});
  }

  void provided_buffer_pool::push_buffer(buffer_id_type buf_id) noexcept
  {
    push_buffer(buf_id, get_buffer_size());
  }

  void provided_buffer_pool::push_buffer(buffer_id_type buf_id, std::size_t buf_size) noexcept
  {
    actual_buf_size_[buf_id] = buf_size;
    ::io_uring_buf_ring_add(
      buf_ring_.get(),
      get_buffer_address(buf_id),
      buf_size,
      buf_id.value(),
      ::io_uring_buf_ring_mask(get_buffer_count()),
      0);
    ::io_uring_buf_ring_advance(buf_ring_.get(), 1);
  }

  void provided_buffer_pool::push_buffers(buffer_id_type buf_id_begin, buffer_id_type buf_id_end) noexcept
  {
    int offset = 0;

    if (buf_id_end > buf_id_begin)
    {
      for (auto i = buf_id_begin; i < buf_id_end; ++i)
      {
        ::io_uring_buf_ring_add(
          buf_ring_.get(),
          get_buffer_address(i),
          get_buffer_size(),
          i,
          ::io_uring_buf_ring_mask(get_buffer_count()),
          offset++);
      }
    }
    else
    {
      for (auto i = buf_id_begin; i < get_buffer_count(); ++i)
      {
        ::io_uring_buf_ring_add(
          buf_ring_.get(),
          get_buffer_address(i),
          get_buffer_size(),
          i,
          ::io_uring_buf_ring_mask(get_buffer_count()),
          offset++);
      }

      for (auto i = buffer_id_type{0}; i < buf_id_end; ++i)
      {
        ::io_uring_buf_ring_add(
          buf_ring_.get(),
          get_buffer_address(i),
          get_buffer_size(),
          i,
          ::io_uring_buf_ring_mask(get_buffer_count()),
          offset++);
      }
    }

    /*  std::cout << "Pushing buffers from " << buf_id_begin.value() << " to " << buf_id_end.value()
               << ", total: " << offset << std::endl; */
    ::io_uring_buf_ring_advance(buf_ring_.get(), offset);
  }

  void provided_buffer_pool::adjust_buffer_size(buffer_id_type buf_id, int offset) noexcept
  {
    actual_buf_size_[buf_id] += offset;
    assert(actual_buf_size_[buf_id] >= 0 && actual_buf_size_[buf_id] <= buf_size_);
  }

  std::byte* provided_buffer_pool::get_buffer_address(buffer_id_type buf_id) const noexcept
  {
    assert(buf_id.value() < buf_cnt_);
    return buf_array_.get(buf_id.value());
  }

  ::asio::mutable_buffer provided_buffer_pool::get_buffer(buffer_id_type buf_id) const noexcept
  {
    return ::asio::buffer(get_buffer_address(buf_id), actual_buf_size_[buf_id]);
  }

} // namespace uring