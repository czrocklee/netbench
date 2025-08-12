#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <chrono>
#include <list>
#include <map>

#include <boost/container/small_vector.hpp>

namespace bsd
{
  class io_context
  {
  public:
    explicit io_context(unsigned max_events = 64);
    ~io_context();

    io_context(io_context const&) = delete;
    io_context& operator=(io_context const&) = delete;

    class event_handle;
    using handler_type = void (*)(std::uint32_t events, void* context);
    event_handle register_event(int fd, std::uint32_t events, handler_type handler, void* context);

    void poll();
    void poll_wait();
    void run_for(std::chrono::milliseconds const& timeout);
    void wakeup();

  private:
    void setup_wakeup_event();
    void handle_wakeup();

    struct event
    {
      handler_type handler;
      void* context;
    };

    using small_vec_type = boost::container::small_vector<event, 4>;

    struct epoll_data
    {
      int epoll_fd = -1;
      std::uint32_t flags = 0;
      small_vec_type events;
    };

    int epoll_fd_;
    int wakeup_fd_;
    std::vector<::epoll_event> events_;
    std::map<int, epoll_data> active_data_;
  };

  class io_context::event_handle
  {
  public:
    event_handle() = default;
    ~event_handle();
    event_handle(event_handle const&) = delete;
    event_handle& operator=(event_handle const&) = delete;
    event_handle(event_handle&& other) noexcept;
    event_handle& operator=(event_handle&& other) noexcept;
    void rearm();
    void reset();

  private:
    event_handle(std::pair<int const, io_context::epoll_data>* data, std::size_t index);
    std::pair<int const, io_context::epoll_data>* data_ = nullptr;
    std::size_t index_ = 0;
    friend class io_context;
  };
}