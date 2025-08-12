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
    std::uint16_t buf_cnt,
    std::size_t buf_size,
    group_id_type grp_id)
    : ring_{io_ctx.get_ring()}, buf_cnt_{buf_cnt}, buf_size_{buf_size}, grp_id_{grp_id}
  {
    if (buf_cnt == 0) { throw std::invalid_argument{"Buffer count must be greater than zero"}; }

    if (buf_size == 0) { throw std::invalid_argument{"Buffer size must be greater than zero"}; }

    std::size_t pool_size = buf_cnt_ * buf_size_;
    pool_memory_ = decltype(pool_memory_){
      static_cast<std::byte*>(::mmap(nullptr, pool_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)),
      [pool_size](std::byte* ptr) { ::munmap(ptr, pool_size); }};

    if (!pool_memory_) { throw std::runtime_error("Failed to allocate memory for buffer pool"); }

    int ret = 0;
    buf_ring_ = decltype(buf_ring_){
      ::io_uring_setup_buf_ring(&ring_, buf_cnt_, grp_id_.value(), 0, &ret),
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
    push_buffers(buffer_id_type{0}, buffer_id_type{buf_cnt_});
  }

  void provided_buffer_pool::push_buffer(buffer_id_type buf_id) noexcept
  {
    push_buffer(buf_id, buf_size_);
  }

  void provided_buffer_pool::push_buffer(buffer_id_type buf_id, std::size_t buf_size) noexcept
  {
    actual_buf_size_[buf_id] = buf_size;
    ::io_uring_buf_ring_add(
      buf_ring_.get(), get_buffer_address(buf_id), buf_size, buf_id.value(), ::io_uring_buf_ring_mask(buf_cnt_), 0);
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
          buf_ring_.get(), get_buffer_address(i), buf_size_, i, ::io_uring_buf_ring_mask(buf_cnt_), offset++);
      }
    }
    else
    {
      for (auto i = buf_id_begin; i < buf_cnt_; ++i)
      {
        ::io_uring_buf_ring_add(
          buf_ring_.get(), get_buffer_address(i), buf_size_, i, ::io_uring_buf_ring_mask(buf_cnt_), offset++);
      }

      for (auto i = buffer_id_type{0}; i < buf_id_end; ++i)
      {
        ::io_uring_buf_ring_add(
          buf_ring_.get(), get_buffer_address(i), buf_size_, i, ::io_uring_buf_ring_mask(buf_cnt_), offset++);
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
    return pool_memory_.get() + (buf_id.value() * buf_size_);
  }

  ::asio::mutable_buffer provided_buffer_pool::get_buffer(buffer_id_type buf_id) const noexcept
  {
    return ::asio::buffer(get_buffer_address(buf_id), actual_buf_size_[buf_id]);
  }

} // namespace uring