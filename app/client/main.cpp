#include <iostream>
#include <CLI/CLI.hpp>

int main(int argc, char** argv)
{
  CLI::App app{"My Awesome App"};

  std::string address;
  app.add_option("-a,--address", address, "Target address (e.g., host:port)")->required();

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

  return 0;
}
