#include "sender.hpp"
#include "utility/metric_hud.hpp"

#include <CLI/CLI.hpp>

#include <iostream>
#include <deque>

int main(int argc, char** argv)
{
  CLI::App app{"TCP sender client"};

  std::string address;
  app.add_option("-a,--address", address, "Target address")->default_val("127.0.0.1:19004");

  std::string bind_address;
  app.add_option("-b,--bind-address", bind_address, "Bind address")->default_val("");

  int conns = 1;
  app.add_option("-c,--conns", conns, "Number of connections per sender")->default_val(1);

  int senders = 1;
  app.add_option("-s,--senders", senders, "Number of senders")->default_val(1);

  int msgs_per_sec;
  app.add_option("-m,--msgs-per-sec", msgs_per_sec, "Messages per second per sender")->default_val(1000);

  int msg_size;
    app.add_option("-z,--msg-size", msg_size, "Message size in bytes")->default_val(1024);

  bool nodelay;
  app.add_option("-n,--nodelay", nodelay, "Enable TCP_NODELAY")->default_val(false);

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
    port = "19004"; // Default port if not specified
  }
  else
  {
    host = address.substr(0, colon_pos);
    port = address.substr(colon_pos + 1);
  }

  auto ss = std::deque<sender>{};

  for (auto i = 0; i < senders; ++i) { ss.emplace_back(i, conns, msgs_per_sec / senders); }

  for (auto& s : ss) { s.start(host, port, bind_address, msg_size, nodelay); }

  auto collect_metric = [&] {
    auto total_msgs_sent =
      std::accumulate(ss.begin(), ss.end(), 0ul, [](auto count, auto& s) { return count + s.total_msgs_sent(); });
    return utility::metric_hud::metric{.ops = total_msgs_sent, .bytes = total_msgs_sent * msg_size};
  };

  utility::metric_hud hud{std::chrono::seconds{5}, collect_metric};

  while (true)
  {
    hud.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}
