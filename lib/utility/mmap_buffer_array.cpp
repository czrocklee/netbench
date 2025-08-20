#include "mmap_buffer_array.hpp"
#include <stdexcept>
#include <sys/mman.h>

namespace utility
{
  mmap_buffer_array::mmap_buffer_array(std::size_t buf_size, std::uint16_t buf_cnt)
    : buf_size_{buf_size}, buf_cnt_{buf_cnt}
  {
    if (buf_size == 0 || buf_cnt == 0)
    {
      throw std::invalid_argument("Buffer size and count must be greater than zero");
    }

    auto const total_size = buf_size_ * buf_cnt;

    ptr_ = decltype(ptr_){
      static_cast<std::byte*>(::mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)),
      [total_size](std::byte* p) {
        if (p && p != MAP_FAILED) { ::munmap(p, total_size); }
      }};

    if (ptr_.get() == MAP_FAILED) { throw std::runtime_error("Failed to allocate mmap buffer array"); }
  }

} // namespace utility
