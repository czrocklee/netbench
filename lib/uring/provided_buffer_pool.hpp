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
  class provided_buffer_pool
  {
  public:
    using buffer_id_type = utility::tagged_integer<std::uint16_t, struct buffer_id_type, 0>;
    using group_id_type = utility::tagged_integer<std::uint16_t, struct group_id_type, 0>;

    explicit provided_buffer_pool(io_context& io_ctx, std::uint16_t buf_cnt, std::size_t buf_size, group_id_type grp_id);
    ~provided_buffer_pool();

    provided_buffer_pool(provided_buffer_pool const&) = delete;
    provided_buffer_pool& operator=(provided_buffer_pool const&) = delete;
    provided_buffer_pool(provided_buffer_pool&&) = delete;
    provided_buffer_pool& operator=(provided_buffer_pool&&) = delete;

    void populate_buffers() noexcept;
    void push_buffer(buffer_id_type buf_id) noexcept;
    void push_buffer(buffer_id_type buf_id, std::size_t buf_size) noexcept;
    void push_buffers(buffer_id_type buf_id_begin, buffer_id_type buf_id_end) noexcept;
    void adjust_buffer_size(buffer_id_type buf_id, int offset) noexcept;

    [[nodiscard]] std::byte* get_buffer_address(buffer_id_type buf_id) const noexcept;
    [[nodiscard]] ::asio::const_buffer get_buffer(buffer_id_type buf_id) const noexcept;
    [[nodiscard]] std::uint16_t get_group_id() const noexcept { return grp_id_.value(); }
    [[nodiscard]] std::size_t get_buffer_size() const noexcept { return buf_size_; }
    [[nodiscard]] std::uint16_t get_buffer_count() const noexcept { return buf_cnt_; }

  private:
    ::io_uring& ring_;
    std::unique_ptr<::io_uring_buf_ring, std::function<void(::io_uring_buf_ring*)>> buf_ring_;
    std::unique_ptr<std::byte[], std::function<void(::std::byte*)>> pool_memory_;
    std::unique_ptr<std::size_t[]> actual_buf_size_;
    std::uint16_t buf_cnt_ = 0;
    std::size_t buf_size_ = 0;
    group_id_type grp_id_{};
  };
}