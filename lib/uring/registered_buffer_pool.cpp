#include "registered_buffer_pool.hpp"
#include <liburing.h>
#include <cstring>
#include <cassert>
#include <stdexcept>

namespace uring
{
  registered_buffer_pool::registered_buffer_pool(io_context& io_ctx, std::uint16_t buf_cnt, std::size_t buf_size)
    : ring_(io_ctx.get_ring()),
      pool_memory_{static_cast<std::byte*>(std::aligned_alloc(buf_size, buf_cnt * buf_size))},
      buf_cnt_{buf_cnt},
      buf_size_{buf_size}
  {
    auto iovecs = std::vector<::iovec>{};
    iovecs.reserve(buf_cnt);
    free_stack_.reserve(buf_cnt);

    for (std::uint16_t i = 0; i < buf_cnt; ++i)
    {
      auto* addr = pool_memory_.get() + i * buf_size_;
      iovecs.push_back(::iovec{.iov_base = addr, .iov_len = buf_size_});
      free_stack_.push_back(buffer_index_type{i});
    }

    if (::io_uring_register_buffers(&ring_, iovecs.data(), buf_cnt_) < 0)
    {
      throw std::runtime_error("io_uring_register_buffers failed");
    }
  }

  registered_buffer_pool::~registered_buffer_pool()
  {
    ::io_uring_unregister_buffers(&ring_);
  }

  registered_buffer_pool::buffer_index_type registered_buffer_pool::acquire_buffer() noexcept
  {
    assert(!free_stack_.empty() && "No free buffers available");
    auto idx = free_stack_.back();
    free_stack_.pop_back();
    return idx;
  }

  void registered_buffer_pool::release_buffer(buffer_index_type buf_idx) noexcept
  {
    assert(buf_idx < buf_cnt_ && "Buffer index out of range");
    free_stack_.push_back(buf_idx);
  }

  ::asio::mutable_buffer registered_buffer_pool::get_buffer(buffer_index_type buf_idx) const noexcept
  {
    assert(buf_idx < buf_cnt_ && "Buffer index out of range");
    auto* addr = pool_memory_.get() + buf_idx * buf_size_;
    return ::asio::buffer(addr, buf_size_);
  }

} // namespace uring
