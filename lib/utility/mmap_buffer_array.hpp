#pragma once

#include <functional>
#include <cstddef>
#include <memory>

namespace utility
{
  class mmap_buffer_array
  {
  public:
    mmap_buffer_array(std::size_t buf_size, std::uint16_t buf_cnt);

    std::byte* get(std::size_t index) const noexcept { return ptr_.get() + (index * buf_size_); }
    std::uint16_t get_buffer_count() const noexcept { return buf_cnt_; }
    std::size_t get_buffer_size() const noexcept { return buf_size_; }

  private:
    std::size_t const buf_size_ = 0;
    std::uint16_t const buf_cnt_ = 0;
    std::unique_ptr<std::byte, std::function<void(std::byte*)>> ptr_;
  };
} // namespace utility
