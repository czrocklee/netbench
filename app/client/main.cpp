#include <iostream>
#include <CLI/CLI.hpp>
#include "sender.hpp"
#include <deque>

int main(int argc, char** argv)
{
  CLI::App app{"My Awesome App"};

  std::string address;
  app.add_option("-a,--address", address, "Target host")->required();

  int conns = 1;
  app.add_option("-c,--conns", conns, "Number of connections per sender")->default_val(1);

  int senders = 1;
  app.add_option("-s,--senders", senders, "Number of senders")->default_val(1);

  int msgs_per_sec = 1000;
  app.add_option("-m,--msgs-per-sec", msgs_per_sec, "Messages per second per sender")->default_val(1000);

  int msg_size = 1024;
  app.add_option("-z,--msg-size", msg_size, "Message size in bytes")->default_val(1024);

  CLI11_PARSE(app, argc, argv);

  std::cout << "Target address: " << address << std::endl;
  std::cout << "Connections: " << conns << std::endl;
  std::cout << "Senders per connection: " << senders << std::endl;
  std::cout << "Messages per second per sender: " << msgs_per_sec << std::endl;
  std::cout << "Message size: " << msg_size << " bytes" << std::endl;

  std::string host;
  std::string port;

  if (auto colon_pos = address.find(':'); colon_pos == std::string::npos)
  {
    host = address;
    port = "19005"; // Default port if not specified
  }
  else
  {
    host = address.substr(0, colon_pos);
    port = address.substr(colon_pos + 1);
  }

  auto ss = std::deque<sender>{};

  for (auto i = 0; i < senders; ++i) { ss.emplace_back(msgs_per_sec); }

  for (auto& s : ss) { s.run(host, port, msg_size); }

  std::uint64_t last_total_msgs_sent = 0;
  const auto start_time = std::chrono::steady_clock::now();
  auto last_time_checked = start_time;

  while (true)
  {
    std::this_thread::sleep_for(std::chrono::seconds(5));

    const auto total_msgs_sent =
      std::accumulate(ss.begin(), ss.end(), 0u, [](auto count, auto& s) { return count + s.total_msgs_sent(); });
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed_time = now - start_time;
    const auto rate = total_msgs_sent / std::chrono::round<std::chrono::seconds>(elapsed_time).count();
    std::cout << "total rate: " << rate << " msgs/s, throughput: " << rate * msg_size << " bytes/s" << std::endl;
  
    const auto elapsed_time_since_last_check = now - last_time_checked;
    const auto rate_since_last_check = (total_msgs_sent - last_total_msgs_sent) / std::chrono::round<std::chrono::seconds>(elapsed_time_since_last_check).count();
    std::cout << "current rate: " << rate_since_last_check << " msgs/s, throughput: " << rate_since_last_check * msg_size << " bytes/s" << std::endl;
    last_total_msgs_sent = total_msgs_sent;
    last_time_checked = now;
  }

  return 0;
}
