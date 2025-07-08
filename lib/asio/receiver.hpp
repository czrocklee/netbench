#pragma once

#include <asio.hpp>
#include <asio/error.hpp>
#include <asio/buffer.hpp>
#include <functional>
#include <system_error>
#include <array>
#include <cstddef>

namespace masio  {

// Fixed-size buffer allocator for stack allocation
// (for demonstration, buffer size is a template parameter)
template <std::size_t BufferSize>
class stack_buffer_allocator {
public:
    using value_type = char;
    stack_buffer_allocator() = default;
    char* allocate(std::size_t n) { return buffer_; }
    void deallocate(char*, std::size_t) {}
    char* data() { return buffer_; }
    std::size_t size() const { return BufferSize; }
private:
    char buffer_[BufferSize];
};

class receiver {
public:
    using data_callback = std::function<void(std::error_code, ::asio::const_buffer)>;

    template <std::size_t BufferSize>
    explicit receiver(asio::io_context& io_ctx, int fd, stack_buffer_allocator<BufferSize>& allocator)
        : socket_{io_ctx}
        , allocator_{reinterpret_cast<char*>(allocator.data())}
        , buffer_size_{BufferSize}
    {
        socket_.assign(asio::ip::tcp::v4(), fd);
    }

    void open(int fd) {
        socket_.assign(asio::ip::tcp::v4(), fd);
    }

    void start(data_callback cb) {
        data_cb_ = std::move(cb);
        do_read();
    }

private:
    void do_read() {
        auto self = this;
        socket_.async_read_some(
            asio::buffer(allocator_, buffer_size_),
            [self](std::error_code ec, std::size_t n) {
                if (self->data_cb_) {
                    self->data_cb_(ec, ::asio::const_buffer{self->allocator_, n});
                }
                if (!ec) {
                    self->do_read();
                }
            }
        );
    }

    asio::ip::tcp::socket socket_;
    char* allocator_;
    std::size_t buffer_size_;
    data_callback data_cb_;
};

} // namespace asio_receiver
