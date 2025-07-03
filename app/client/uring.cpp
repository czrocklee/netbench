#include "uring_sender.hpp"
#include "utility/metric_hud.hpp"

#include <CLI/CLI.hpp>


int main(int argc, char** argv)
{
  CLI::App app{"io_uring TCP Sender Client"};

  std::string address;
  app.add_option("-a,--address", address, "Target address")->default_val("127.0.0.1:19004");

  int senders = 1;
  app.add_option("-s,--senders", senders, "Number of senders")->default_val(1);

  int msgs_per_sec = 1000;
  app.add_option("-m,--msgs-per-sec", msgs_per_sec, "Messages per second per sender (0 for max)")->default_val(1000);

  int msg_size = 1024;
  app.add_option("-z,--msg-size", msg_size, "Message size in bytes")->default_val(1024);

  CLI11_PARSE(app, argc, argv);

  std::cout << "Target address: " << address << std::endl;
  std::cout << "Senders: " << senders << std::endl;
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

  std::vector<std::unique_ptr<client::uring_sender>> sender_list;
  for (int i = 0; i < senders; ++i)
  {
    try
    {
      sender_list.emplace_back(std::make_unique<client::uring_sender>(i, host, port, msg_size, msgs_per_sec));
    }
    catch (const std::exception& e)
    {
      std::cerr << "Failed to create sender " << i << ": " << e.what() << std::endl;
      return 1;
    }
  }

  for (auto& s : sender_list) { s->start(); }

  auto collect_metric = [&]() {
    auto total_msgs = std::accumulate(
      sender_list.begin(), sender_list.end(), 0ul, [](auto count, auto& s) { return count + s->total_msgs_sent(); });
    auto total_bytes = std::accumulate(
      sender_list.begin(), sender_list.end(), 0ul, [](auto count, auto& s) { return count + s->total_bytes_sent(); });
    return utility::metric_hud::metric{.msgs = total_msgs, .bytes = total_bytes};
  };

  utility::metric_hud hud{std::chrono::seconds{5}, collect_metric};

  while (true)
  {
    hud.tick();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}