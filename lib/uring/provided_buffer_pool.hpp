#pragma once

#include "io_context.hpp"
#include "utility/tagged_integer.hpp"
#include "utility/mmap_buffer_array.hpp"

#include <asio/buffer.hpp>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <sys/mman.h>

namespace uring
{
  class provided_buffer_pool
  {
  public:
    using buffer_id_type = utility::tagged_integer<std::uint16_t, struct buffer_id_type, 0>;
    using group_id_type = utility::tagged_integer<std::uint16_t, struct group_id_type, 0>;

    explicit provided_buffer_pool(
      io_context& io_ctx,
      std::size_t buf_size,
      std::uint16_t buf_cnt,
      group_id_type grp_id);
    ~provided_buffer_pool();
    provided_buffer_pool(provided_buffer_pool&&) = default;
    provided_buffer_pool& operator=(provided_buffer_pool&&) = default;

    void populate_buffers() noexcept;
    void push_buffer(buffer_id_type buf_id) noexcept;
    void push_buffer(buffer_id_type buf_id, std::size_t buf_size) noexcept;
    void push_buffers(buffer_id_type buf_id_begin, buffer_id_type buf_id_end) noexcept;
    void adjust_buffer_size(buffer_id_type buf_id, int offset) noexcept;

    [[nodiscard]] std::byte* get_buffer_address(buffer_id_type buf_id) const noexcept;
    [[nodiscard]] ::asio::mutable_buffer get_buffer(buffer_id_type buf_id) const noexcept;
    [[nodiscard]] std::uint16_t get_group_id() const noexcept { return grp_id_.value(); }
    [[nodiscard]] std::size_t get_buffer_size() const noexcept { return buf_array_.get_buffer_size(); }
    [[nodiscard]] std::uint16_t get_buffer_count() const noexcept { return buf_array_.get_buffer_count(); }

  private:
    provided_buffer_pool(provided_buffer_pool const&) = delete;
    provided_buffer_pool& operator=(provided_buffer_pool const&) = delete;

    ::io_uring& ring_;
    std::unique_ptr<::io_uring_buf_ring, std::function<void(::io_uring_buf_ring*)>> buf_ring_;
    utility::mmap_buffer_array buf_array_;
    std::unique_ptr<std::size_t[]> actual_buf_size_;
    group_id_type const grp_id_{};
  };
}