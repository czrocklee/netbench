#include "io_context.hpp"
#include <cerrno>
#include <iostream>

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

  io_context::event_handle io_context::register_event(int fd, std::uint32_t events, handler_type handler, void* context)
  {
    auto data_iter = active_data_.find(fd);
    std::uint32_t flags = 0;

    if (data_iter == active_data_.end())
    {
      data_iter = active_data_.emplace(fd, epoll_data{.epoll_fd = epoll_fd_, .flags = events}).first;
    }
    else
    {
      flags = data_iter->second.flags;
      events |= flags;
    }

    auto& vec = data_iter->second.events;

    if (flags != events)
    {
      data_iter->second.flags = events;

      if (flags == 0)
      {
        if (::epoll_event ev{.events = events, .data = {.ptr = &vec}};
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1)
        {
          throw std::system_error(errno, std::system_category(), "epoll_ctl ADD failed");
        }
      }
      else
      {
        if (::epoll_event ev{.events = events, .data = {.ptr = &vec}};
            ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == -1)
        {
          throw std::system_error(errno, std::system_category(), "epoll_ctl MOD failed");
        }
      }
    }

    auto node_iter = std::find_if(vec.begin(), vec.end(), [](event const& e) { return e.handler == nullptr; });

    if (node_iter == vec.end())
    {
      vec.emplace_back(event{handler, context});
      return event_handle{&*data_iter, vec.size() - 1};
    }

    *node_iter = event{handler, context};

    return event_handle{&*data_iter, static_cast<std::size_t>(std::distance(vec.begin(), node_iter))};
  }

  void io_context::setup_wakeup_event()
  {
    wakeup_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);

    if (wakeup_fd_ < 0)
    {
      throw std::system_error(errno, std::system_category(), "eventfd failed");
    }
    // Use a raw `this` pointer as a special value to identify the wakeup event.
    // add(wakeup_fd_, EPOLLIN, reinterpret_cast<event_data*>(this));
    if (::epoll_event ev{.events = EPOLLIN, .data = {.ptr = this}};
        ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev) == -1)
    {
      throw std::system_error(errno, std::system_category(), "epoll_ctl ADD failed");
    }
  }

  void io_context::handle_wakeup()
  {
    std::uint64_t val;
    // Drain the eventfd to reset its state.
    std::ignore = ::read(wakeup_fd_, &val, sizeof(val));
  }

  void io_context::wakeup()
  {
    std::uint64_t val = 1;
    // A write to the eventfd will trigger an EPOLLIN event.
    std::ignore = ::write(wakeup_fd_, &val, sizeof(val));
  }

  void io_context::poll()
  {
    run_for(std::chrono::milliseconds{0});
  }

  void io_context::poll_wait()
  {
    run_for(std::chrono::milliseconds{-1});
  }

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
        continue;
      }

      auto* events = static_cast<small_vec_type*>(events_[i].data.ptr);

      for (auto& event : *events)
      {
        if (event.handler != nullptr) [[likely]]
        {
          event.handler(events_[i].events, event.context);
        }
      }
    }
  }

  io_context::event_handle::~event_handle()
  {
    reset();
  }

  io_context::event_handle::event_handle(std::pair<int const, epoll_data>* data, std::size_t index)
    : data_{data}, index_{index}
  {
  }

  io_context::event_handle::event_handle(event_handle&& other) noexcept : data_{other.data_}, index_{other.index_}
  {
    other.data_ = nullptr;
    other.index_ = 0;
  }

  io_context::event_handle& io_context::event_handle::operator=(event_handle&& other) noexcept
  {
    if (this != &other)
    {
      data_ = other.data_;
      index_ = other.index_;
      other.data_ = nullptr;
      other.index_ = 0;
    }

    return *this;
  }

  void io_context::event_handle::reset()
  {
    if (data_ != nullptr)
    {
      data_->second.events[index_].handler = nullptr;

      if (std::all_of(data_->second.events.begin(), data_->second.events.end(), [](event const& e) {
            return e.handler == nullptr;
          }))
      {
        data_->second.flags = 0;

        try
        {
          if (::epoll_ctl(data_->second.epoll_fd, EPOLL_CTL_DEL, data_->first, nullptr) == -1)
          {
            throw std::system_error(errno, std::system_category(), "epoll_ctl DEL failed");
          }
        }
        catch (std::system_error const& e)
        {
          std::cerr << "Error removing event: " << e.what() << std::endl;
        }
      }
    }

    data_ = nullptr;
    index_ = 0;
  }

  void io_context::event_handle::rearm()
  {
    if (data_ != nullptr)
    {
      if (::epoll_event ev{.events = data_->second.flags, .data = {.ptr = &data_->second.events}};
          ::epoll_ctl(data_->second.epoll_fd, EPOLL_CTL_MOD, data_->first, &ev) == -1)
      {
        throw std::system_error(errno, std::system_category(), "epoll_ctl MOD failed");
      }
    }
  }
}