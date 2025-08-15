#pragma once

#include "io_context.hpp"
#include "utility/tagged_integer.hpp"

#include <asio/buffer.hpp>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <sys/mman.h>
#include <memory>
#include <functional>

namespace uring
{
  class registered_buffer_pool
  {
  public:
    using buffer_index_type = utility::tagged_integer<std::uint16_t, struct buffer_index_type, 0>;

    explicit registered_buffer_pool(io_context& io_ctx, std::uint16_t buf_cnt, std::size_t buf_size);
    ~registered_buffer_pool();

    [[nodiscard]] buffer_index_type acquire_buffer() noexcept;
    void release_buffer(buffer_index_type buf_idx) noexcept;

    [[nodiscard]] ::asio::mutable_buffer get_buffer(buffer_index_type buf_idx) const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return free_stack_.empty(); }
    [[nodiscard]] std::size_t get_buffer_size() const noexcept { return buf_size_; }
    [[nodiscard]] std::uint16_t get_buffer_count() const noexcept { return buf_cnt_; }
    [[nodiscard]] std::size_t get_free_buffer_count() const noexcept { return free_stack_.size(); }

  private:
    registered_buffer_pool(registered_buffer_pool const&) = delete;
    registered_buffer_pool& operator=(registered_buffer_pool const&) = delete;
    registered_buffer_pool(registered_buffer_pool&&) = delete;
    registered_buffer_pool& operator=(registered_buffer_pool&&) = delete;

    ::io_uring& ring_;
    std::unique_ptr<std::byte[], void (*)(void*)> pool_memory_;
    std::vector<buffer_index_type> free_stack_; // FILO stack for free buffers
    std::uint16_t const buf_cnt_ = 0;
    std::size_t const buf_size_ = 0;
  };
}
