#include "receiver.hpp"
#include "acceptor.hpp"
#include "../metric_hud.hpp"

#include <CLI/CLI.hpp>
#include <atomic>
#include <csignal>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <future>

std::atomic<bool> shutdown_flag = false;

void signal_handler(int /* signum */) { shutdown_flag = true; }

namespace
{
  void parse_address(const std::string& full_address, std::string& host, std::string& port)
  {
    auto colon_pos = full_address.find(':');
    if (colon_pos == std::string::npos)
    {
      throw std::runtime_error{"Invalid address format. Expected host:port"};
    }
    host = full_address.substr(0, colon_pos);
    port = full_address.substr(colon_pos + 1);
  }
} // namespace

int main(int argc, char** argv)
{
  CLI::App app{"C++ TCP epoll Multi-threaded Server"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:8080");

  unsigned buffer_size;
  app.add_option("-s,--buffer-size", buffer_size, "Size of each receive buffer in bytes")->default_val(1024);

  unsigned epoll_size;
  app.add_option("-e,--epoll-size", epoll_size, "Max events for epoll_wait per receiver")->default_val(128);

  unsigned num_threads;
  app.add_option("-t,--threads", num_threads, "Number of receiver threads to start")->default_val(1);

  CLI11_PARSE(app, argc, argv);

  if (num_threads <= 0)
  {
    std::cerr << "Number of threads must be greater than 0." << std::endl;
    return 1;
  }

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  try
  {
    std::string host, port;
    parse_address(address_str, host, port);

    std::vector<std::unique_ptr<bsd::receiver>> receivers;
    std::cout << "Starting " << num_threads << " receiver threads on " << address_str << "..." << std::endl;

    for (auto i = 0u; i < num_threads; ++i)
    {
      bsd::receiver::config cfg = {.epoll_size = epoll_size, .buffer_size = buffer_size};
      receivers.emplace_back(std::make_unique<bsd::receiver>(cfg))->start();
    }

    bsd::io_context main_io_ctx{16};

    auto on_accept = [&, next_receiver_idx = 0](bsd::socket&& new_sock) mutable {
      if (!receivers[next_receiver_idx]->add_connection(std::move(new_sock)))
      {
        std::cerr << "Main thread FAILED to hand off fd to receiver " << next_receiver_idx << std::endl;
      }
      next_receiver_idx = (next_receiver_idx + 1) % receivers.size();
    };

    bsd::acceptor acceptor{main_io_ctx, on_accept};
    acceptor.listen(host, port);
    acceptor.start();
    std::cout << "Main thread acceptor listening on " << address_str << std::endl;

    auto collect_metric = [&receivers]() -> metric_hud::metric {
      std::vector<std::future<metric_hud::metric>> futures;
      futures.reserve(receivers.size());
      for (auto& receiver_ptr : receivers)
      {
        auto p = std::make_shared<std::promise<metric_hud::metric>>();
        futures.emplace_back(p->get_future());
        receiver_ptr->post([p, receiver = receiver_ptr.get()]() { p->set_value(receiver->get_metrics()); });
      }
      metric_hud::metric total_metric{};
      for (auto& f : futures)
      {
        auto m = f.get();
        total_metric.msgs += m.msgs;
        total_metric.bytes += m.bytes;
      }
      return total_metric;
    };

    metric_hud hud{std::chrono::seconds{5}, collect_metric};

    while (!shutdown_flag)
    {
      main_io_ctx.run_for(std::chrono::milliseconds{100});
      hud.tick();
    }

    std::cout << "\nShutting down..." << std::endl;
    for (auto& receiver : receivers) { receiver->stop(); }
  }
  catch (const std::exception& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Server has shut down gracefully." << std::endl;
  return 0;
}
