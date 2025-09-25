#include "worker.hpp"
#include "../common/utils.hpp"
#include <utility/metric_hud.hpp>
#include <utility/logger.hpp>

#include <CLI/CLI.hpp>
#include <magic_enum/magic_enum.hpp>

#include <atomic>
#include <csignal>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <future>

std::atomic<int> shutdown_counter = 1;

void signal_handler(int /* signum */)
{
  shutdown_counter = -1;
}

int main(int argc, char** argv)
{
  CLI::App app{"C++ TCP receiver"};

  std::string address_str;
  app.add_option("-a,--address", address_str, "Target address in host:port format")->default_val("0.0.0.0:19004");

  worker::config cfg;

  std::map<std::string, worker::config::echo_mode> echo_mode_map;

  for (auto const& entry : magic_enum::enum_entries<worker::config::echo_mode>())
  {
    echo_mode_map[std::string(entry.second)] = entry.first;
  }

  app.add_option("-e,--echo", cfg.echo, "Enable echo mode")
    ->default_val(worker::config::echo_mode::none)
    ->transform(CLI::CheckedTransformer(echo_mode_map, CLI::ignore_case));

  app.add_option("-b,--buffer-size", cfg.buffer_size, "Size of receive buffer in bytes")
    ->default_val(1024)
    ->transform(CLI::AsSizeValue(false));

  app
    .add_option(
      "-C,--collect-latency-every-n-samples",
      cfg.collect_latency_every_n_samples,
      "Collect latency metrics every n samples")
    ->default_val(0);

  bool busy_spin;
  app.add_option("-s,--busy-spin", busy_spin, "Enable busy spin polling")->default_val(false);

  unsigned num_workers;
  app.add_option("-w,--workers", num_workers, "Number of worker threads to start")->default_val(1);

  std::string log_file;
  app.add_option("-l,--log-file", log_file, "Path to log file")->default_val("/tmp/receiver.log");

  std::string log_level;
  app.add_option("-L,--log-level", log_level, "Minimum log level (trace, debug, info, warn, error, critical)")
    ->default_val("info");

  app
    .add_option(
      "--so-rcvbuf", cfg.socket_recv_buffer_size, "Socket receive buffer size in bytes (0 for system default)")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(false));

  app.add_option("--so-sndbuf", cfg.socket_send_buffer_size, "Socket send buffer size in bytes (0 for system default)")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(false));

  app.add_option("--shutdown-on-disconnect", cfg.shutdown_on_disconnect, "Shutdown server when a client disconnects")
    ->default_val(false);

  bool metrics_hud;
  app.add_option("-M,--metrics-hud", metrics_hud, "Enable metrics HUD display")->default_val(true);

#ifdef IO_URING_API
  app.add_option("-z,--zerocopy", cfg.zerocopy, "Use zerocopy send or not")->default_val(true);

  app.add_option("-c,--buffer-count", cfg.buffer_count, "Number of buffers in pool prepared for each worker")
    ->default_val(2048)
    ->transform(CLI::AsSizeValue(false));

  app.add_option("-d,--uring-depth", cfg.uring_depth, "io_uring queue depth")
    ->default_val(1024 * 16)
    ->transform(CLI::AsSizeValue(false));

#elifdef BSD_API
  app.add_option("-r,--read-limit", cfg.read_limit, "Optional read limit for BSD API (0 for no limit)")
    ->default_val(1024 * 64)
    ->transform(CLI::AsSizeValue(false));
#endif

  CLI11_PARSE(app, argc, argv);

  if (num_workers <= 0)
  {
    std::cerr << "Number of workers must be greater than 0." << std::endl;
    return 1;
  }

  if (cfg.shutdown_on_disconnect)
  {
    shutdown_counter = num_workers;
    cfg.shutdown_counter = &shutdown_counter;
  }

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  try
  {
    utility::init_log_file(log_file);
    utility::set_log_level(utility::from_string(log_level));

    std::string host, port;
    parse_address(address_str, host, port);

    std::vector<std::unique_ptr<worker>> workers;
    std::cout << "Starting " << num_workers << " worker threads on " << address_str << "..." << std::endl;

    net::io_context io_ctx{};

    for (auto i = 0u; i < num_workers; ++i)
    {
#ifdef IO_URING_API
      io_uring_params params{};
      params.cq_entries = 16384;
      // params.flags |= IORING_SETUP_CQSIZE;
      params.flags |= IORING_SETUP_R_DISABLED;
      params.flags |= IORING_SETUP_SINGLE_ISSUER;
      params.flags |= IORING_SETUP_DEFER_TASKRUN;
      params.flags |= (IORING_SETUP_COOP_TASKRUN); //| IORING_SETUP_TASKRUN_FLAG);

      cfg.buffer_group_id = static_cast<std::uint16_t>(i);
      cfg.params = params;
#endif

      workers.emplace_back(std::make_unique<worker>(cfg))->start(busy_spin);
    }

    net::acceptor acceptor{io_ctx};
    acceptor.listen(host, port);
    acceptor.start([&, next_worker_idx = 0](std::error_code ec, net::socket new_sock) mutable {
      if (ec)
      {
        std::cerr << "Error accepting connection: " << ec.message() << std::endl;
        return;
      }

      int accepted_fd = new_sock.native_handle();
      auto& worker = workers[next_worker_idx];

      if (!worker->post([&worker, sock = std::move(new_sock)]() mutable { worker->add_connection(std::move(sock)); }))
      {
        std::cerr << "Main thread FAILED to hand off fd " << accepted_fd << " to worker " << next_worker_idx
                  << " (queue full?)" << std::endl;
      }

      next_worker_idx = (next_worker_idx + 1) % workers.size();
    });

    std::cout << "Main thread acceptor listening on " << address_str << std::endl;

    auto collect_metric = [&workers] {
      utility::metric total_metric{};
      total_metric.init_histogram();

      for (auto& worker_ptr : workers)
      {
        std::promise<void> prom;
        auto fut = prom.get_future();

        worker_ptr->post([&] {
          total_metric.add(worker_ptr->get_metrics());
          prom.set_value();
        });

        fut.get();
      }

      return total_metric;
    };

    auto hud = std::optional<utility::metric_hud>{};

    if (metrics_hud)
    {
      hud.emplace(std::chrono::seconds{5}, collect_metric);
    }

    while (shutdown_counter > 0)
    {
      io_ctx.run_for(std::chrono::milliseconds{1000});

      if (metrics_hud)
      {
        hud->tick();
      }
#ifdef ASIO_API
      io_ctx.restart();
#endif
    }

    if (shutdown_counter < 0)
    {
      std::cout << "Shutdown signal received, stopping server..." << std::endl;

      for (auto& worker : workers)
      {
        worker->stop();
      }
    }
    else
    {
      std::cout << "All clients disconnected, server stopped." << std::endl;
    }
  }
  catch (std::exception const& e)
  {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Server has shut down gracefully." << std::endl;
  return 0;
}
