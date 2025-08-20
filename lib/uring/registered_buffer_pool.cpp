#include "registered_buffer_pool.hpp"
#include <liburing.h>
#include <cstring>
#include <cassert>
#include <stdexcept>

namespace uring
{
  registered_buffer_pool::registered_buffer_pool(io_context& io_ctx, std::size_t buf_size, std::uint16_t buf_cnt)
    : ring_{io_ctx.get_ring()}, buf_array_{buf_size, buf_cnt}
  {
    auto iovecs = std::vector<::iovec>{};
    iovecs.reserve(buf_cnt);
    free_stack_.reserve(buf_cnt);

    for (std::uint16_t i = 0; i < buf_cnt; ++i)
    {
      auto* addr = buf_array_.get(i);
      iovecs.push_back(::iovec{.iov_base = addr, .iov_len = get_buffer_size()});
      free_stack_.push_back(buffer_index_type{i});
    }

    if (::io_uring_register_buffers(&ring_, iovecs.data(), get_buffer_count()) < 0)
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
    auto* addr = buf_array_.get(buf_idx);
    return ::asio::buffer(addr, get_buffer_size());
  }

} // namespace uring
