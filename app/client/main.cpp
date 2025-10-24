#include "sender.hpp"
#include "metric_hud.hpp"
#include "utils.hpp"

#include <CLI/CLI.hpp>

#include <iostream>
#include <deque>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

int main(int argc, char** argv)
{
  auto app = CLI::App{"TCP sender client"};
  sender::config cfg;

  std::string address;
  app.add_option("-a,--address", address, "Target address")->default_val("127.0.0.1:19004");

  std::string bind_address;
  app.add_option("-b,--bind-address", bind_address, "Bind address")->default_val("");

  app.add_option("-c,--conns-per-sender", cfg.conns, "Number of connections per sender")->default_val(1);

  int senders = 1;
  app.add_option("-s,--senders", senders, "Number of senders")->default_val(1);

  int msgs_per_sec;
  app.add_option("-m,--msgs-per-sec", msgs_per_sec, "Messages per second per sender (0 = unlimited)")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(true));

  app.add_option("-z,--msg-size", cfg.msg_size, "Message size in bytes")
    ->default_val(1024)
    ->transform(CLI::AsSizeValue(false));

  app.add_flag("-n,--nodelay", cfg.nodelay, "Enable TCP_NODELAY");

  app.add_flag("-d,--drain", cfg.drain, "Enable receive buffer draining");

  app.add_option("-S,--socket-buffer-size", cfg.socket_buffer_size, "Socket buffer size in bytes")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(false));

  app.add_option("--stop-after-n-msgs", cfg.stop_after_n_messages, "Stop after n messages")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(true));

  app.add_option("--stop-after-n-secs", cfg.stop_after_n_seconds, "Stop after n seconds")->default_val(0);

  app.add_option("--max-batch-size", cfg.max_batch_size, "Max batch size for send operations")->default_val(IOV_MAX);

  int metric_hud_interval_secs;
  app
    .add_option(
      "-M,--metric-hud-interval-secs", metric_hud_interval_secs, "Metric HUD interval in seconds, 0 for disabled")
    ->default_val(5);

  // Optional CPU pinning for sender threads (comma-separated list)
  std::vector<int> sender_cpus; // size <= senders; others unpinned
  app.add_option("--sender-cpus", sender_cpus, "Comma-separated CPU IDs for each sender (optional)")->delimiter(',');

  CLI11_PARSE(app, argc, argv);

  cfg.msgs_per_sec = msgs_per_sec > 0 ? (msgs_per_sec / senders) : 0;

  std::cout << "Target address: " << address << std::endl;
  std::cout << "Bind address: " << (bind_address.empty() ? "not set" : bind_address) << std::endl;
  std::cout << "Senders: " << senders << std::endl;
  std::cout << "Connections per sender: " << cfg.conns << std::endl;
  std::cout << "Message size: " << cfg.msg_size << " bytes" << std::endl;
  std::cout << "Messages per second per sender: "
            << (cfg.msgs_per_sec > 0 ? std::to_string(cfg.msgs_per_sec) : std::string("unlimited")) << std::endl;
  std::cout << "Stop after n messages: "
            << (cfg.stop_after_n_messages > 0 ? std::to_string(cfg.stop_after_n_messages) : "disabled") << std::endl;
  std::cout << "Stop after n seconds: "
            << (cfg.stop_after_n_seconds > 0 ? std::to_string(cfg.stop_after_n_seconds) : "disabled") << std::endl;
  std::cout << "Nodelay: " << (cfg.nodelay ? "enabled" : "disabled") << std::endl;
  std::cout << "Drain: " << (cfg.drain ? "enabled" : "disabled") << std::endl;
  std::cout << "Socket buffer size: "
            << (cfg.socket_buffer_size > 0 ? std::to_string(cfg.socket_buffer_size) + " bytes" : "system default")
            << std::endl;
  std::cout << "Max batch size: " << cfg.max_batch_size << std::endl;

  std::string host;
  std::string port;

  if (auto colon_pos = address.find(':'); colon_pos == std::string::npos)
  {
    host = address;
    port = "19004"; // Default port if not specified
  }
  else
  {
    host = address.substr(0, colon_pos);
    port = address.substr(colon_pos + 1);
  }

  std::atomic<int>& shutdown_counter = setup_signal_handlers();

  auto ss = std::deque<sender>{};

  for (auto i = 0; i < senders; ++i)
  {
    auto& s = ss.emplace_back(i, cfg);
    s.connect(host, port, bind_address);

    int cpu_id = -1;

    if (!sender_cpus.empty() && i < static_cast<int>(sender_cpus.size()))
    {
      cpu_id = sender_cpus[i];
    }

    s.start(shutdown_counter, cpu_id);
  }

  auto metric_hud = setup_metric_hud(std::chrono::seconds{metric_hud_interval_secs}, [&ss, &cfg] {
    auto total_metric = metric{};
    total_metric.init_histogram();

    for (auto& s : ss)
    {
      total_metric.ops += s.total_send_ops();
      total_metric.msgs += s.total_msgs_sent();
    }

    total_metric.bytes = total_metric.msgs * cfg.msg_size;
    return total_metric;
  });

  while (shutdown_counter > 0)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (metric_hud)
    {
      metric_hud->tick();
    }
  }

  if (shutdown_counter < 0)
  {
    std::cout << "Shutdown signal received, stopping senders..." << std::endl;
  }
  else
  {
    std::cout << "All senders stopped." << std::endl;
  }

  for (auto& s : ss)
  {
    s.stop();
  }

  return 0;
}
