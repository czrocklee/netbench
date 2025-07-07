#include "io_context.hpp"
#include <cerrno>

namespace bsd
{
  io_context::io_context(unsigned max_events) : events_(max_events)
  {
    epoll_fd_ = epoll_create1(0);

    if (epoll_fd_ == -1)
    {
      throw std::system_error(errno, std::system_category(), "epoll_create1 failed");
    }

    setup_wakeup_event();
  }

  io_context::~io_context()
  {
    if (wakeup_fd_ != -1)
    {
      ::close(wakeup_fd_);
    }

    if (epoll_fd_ != -1)
    {
      ::close(epoll_fd_);
    }
  }

  void io_context::setup_wakeup_event()
  {
    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if (wakeup_fd_ < 0)
    {
      throw std::system_error(errno, std::system_category(), "eventfd failed");
    }
    // Use a raw `this` pointer as a special value to identify the wakeup event.
    add(wakeup_fd_, EPOLLIN, reinterpret_cast<event_data*>(this));
  }

  void io_context::handle_wakeup()
  {
    uint64_t val;
    // Drain the eventfd to reset its state.
    std::ignore = ::read(wakeup_fd_, &val, sizeof(val));
  }

  void io_context::wakeup()
  {
    uint64_t val = 1;
    // A write to the eventfd will trigger an EPOLLIN event.
    std::ignore = ::write(wakeup_fd_, &val, sizeof(val));
  }

  void io_context::add(int fd, uint32_t events, event_data* data)
  {
    struct epoll_event event;
    event.events = events;
    event.data.ptr = data;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1)
    {
      throw std::system_error(errno, std::system_category(), "epoll_ctl ADD failed");
    }
  }

  void io_context::modify(int fd, uint32_t events, event_data* data)
  {
    struct epoll_event event;
    event.events = events;
    event.data.ptr = data;

    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == -1)
    {
      throw std::system_error(errno, std::system_category(), "epoll_ctl MOD failed");
    }
  }

  void io_context::remove(int fd)
  {
    // It's okay if the fd is already closed/removed, so we don't check for errors.
    ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
  }

  void io_context::poll() { run_for(std::chrono::milliseconds{0}); }

  void io_context::poll_wait() { run_for(std::chrono::milliseconds{-1}); }

  void io_context::run_for(std::chrono::milliseconds const& timeout)
  {
    int num_events = ::epoll_wait(epoll_fd_, events_.data(), events_.size(), timeout.count());

    if (num_events == -1)
    {
      if (errno == EINTR)
      {
        return;
      } // Interrupted by a signal, safe to continue.

      throw std::system_error(errno, std::system_category(), "epoll_wait failed");
    }

    for (int i = 0; i < num_events; ++i)
    {
      if (events_[i].data.ptr == this)
      {
        handle_wakeup();
      }
      else if (auto* data = static_cast<event_data*>(events_[i].data.ptr); data && data->handler)
      {
        data->handler(events_[i].events, data->context);
      }
    }
  }
}