#pragma once

#include "../bsd/socket.hpp"
#include "../metric_hud.hpp"
#include "io_context.hpp"
#include <vector>

namespace bsd
{
  class connection
  {
  public:
    explicit connection(io_context& io_ctx, size_t buffer_size);
    
    ~connection();


    void open(bsd::socket&& sock);
    void start();

    bool is_closed() const { return is_closed_; }
    const metric_hud::metric& get_metrics() const { return metrics_; }

  private:
    static void on_events(uint32_t events, void* context);
    void handle_events(uint32_t events);
    void do_read();

    io_context& io_ctx_;
    bsd::socket sock_;
    std::vector<char> buffer_;
    bool is_closed_;
    metric_hud::metric metrics_{};
    io_context::event_data event_data_;
  };
}