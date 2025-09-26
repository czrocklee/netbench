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

  std::string address;
  app.add_option("-a,--address", address, "Target address")->default_val("127.0.0.1:19004");

  std::string bind_address;
  app.add_option("-b,--bind-address", bind_address, "Bind address")->default_val("");

  int conns = 1;
  app.add_option("-c,--conns", conns, "Number of connections per sender")->default_val(1);

  int senders = 1;
  app.add_option("-s,--senders", senders, "Number of senders")->default_val(1);

  int msgs_per_sec;
  app.add_option("-m,--msgs-per-sec", msgs_per_sec, "Messages per second per sender")
    ->default_val(1000)
    ->transform(CLI::AsSizeValue(true));

  int msg_size;
  app.add_option("-z,--msg-size", msg_size, "Message size in bytes")
    ->default_val(1024)
    ->transform(CLI::AsSizeValue(false));

  bool nodelay;
  app.add_flag("-n,--nodelay", nodelay, "Enable TCP_NODELAY")->default_val(false);

  bool drain;
  app.add_flag("-d,--drain", drain, "Enable receive buffer draining")->default_val(false);

  int socket_buffer_size;
  app.add_option("-S,--socket-buffer-size", socket_buffer_size, "Socket buffer size in bytes")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(false));

  std::uint64_t stop_after_n_msgs;
  app.add_option("--stop-after-n-msgs", stop_after_n_msgs, "Stop after n messages")
    ->default_val(0)
    ->transform(CLI::AsSizeValue(true));

  std::uint64_t stop_after_n_secs;
  app.add_option("--stop-after-n-secs", stop_after_n_secs, "Stop after n seconds")->default_val(0);

  int metric_hud_interval_secs;
  app
    .add_option(
      "-M,--metric-hud-interval-secs", metric_hud_interval_secs, "Metric HUD interval in seconds, 0 for disabled")
    ->default_val(5);

  CLI11_PARSE(app, argc, argv);

  std::cout << "Target address: " << address << std::endl;
  std::cout << "Bind address: " << (bind_address.empty() ? "not set" : bind_address) << std::endl;
  std::cout << "Senders: " << senders << std::endl;
  std::cout << "Connections per sender: " << conns << std::endl;
  std::cout << "Message size: " << msg_size << " bytes" << std::endl;
  std::cout << "Messages per second per sender: " << msgs_per_sec << std::endl;
  std::cout << "Stop after n messages: " << (stop_after_n_msgs > 0 ? std::to_string(stop_after_n_msgs) : "disabled")
            << std::endl;
  std::cout << "Stop after n seconds: " << (stop_after_n_secs > 0 ? std::to_string(stop_after_n_secs) : "disabled")
            << std::endl;
  std::cout << "Nodelay: " << (nodelay ? "enabled" : "disabled") << std::endl;
  std::cout << "Drain: " << (drain ? "enabled" : "disabled") << std::endl;
  std::cout << "Socket buffer size: "
            << (socket_buffer_size > 0 ? std::to_string(socket_buffer_size) + " bytes" : "system default") << std::endl;

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
    auto& s = ss.emplace_back(i, conns, msg_size);

    if (stop_after_n_msgs > 0)
    {
      s.stop_after_n_messages(stop_after_n_msgs);
    }
    else if (stop_after_n_secs > 0)
    {
      s.stop_after_n_seconds(stop_after_n_secs);
    }
    else
    {
      s.set_message_rate(msgs_per_sec / senders);
    }

    if (socket_buffer_size > 0)
    {
      s.set_socket_buffer_size(socket_buffer_size);
    }

    if (drain)
    {
      s.enable_drain();
    }

    if (nodelay)
    {
      s.set_nodelay(true);
    }

    s.connect(host, port, bind_address);
    s.start(shutdown_counter);
  }

  auto metric_hud = setup_metric_hud(std::chrono::seconds{metric_hud_interval_secs}, [&ss, msg_size] {
    auto total_metric = metric{};
    total_metric.init_histogram();

    for (auto& s : ss)
    {
      total_metric.ops += s.total_send_ops();
      total_metric.msgs += s.total_msgs_sent();
    }

    total_metric.bytes = total_metric.msgs * msg_size;
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
