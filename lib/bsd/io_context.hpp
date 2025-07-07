#pragma once

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <vector>
#include <functional>
#include <stdexcept>
#include <system_error>
#include <chrono>

namespace bsd
{
  class io_context
  {
  public:
    using handler_type = void (*)(uint32_t events, void* context);

    struct event_data
    {
      handler_type handler;
      void* context;
    };

    explicit io_context(unsigned max_events = 64);
    ~io_context();

    io_context(io_context const&) = delete;
    io_context& operator=(io_context const&) = delete;

    void add(int fd, uint32_t events, event_data* data);
    void modify(int fd, uint32_t events, event_data* data);
    void remove(int fd);

    void poll();
    void poll_wait();
    void run_for(std::chrono::milliseconds const& timeout);

    void wakeup();

  private:
    void setup_wakeup_event();
    void handle_wakeup();

    int epoll_fd_;
    int wakeup_fd_;
    std::vector<struct epoll_event> events_;
  };
}