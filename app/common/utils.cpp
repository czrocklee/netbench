#include "utils.hpp"
#include "build_info.hpp"
#include <utility/machine_info.hpp>

#include <boost/algorithm/string/join.hpp>
#include <nlohmann/json.hpp>

#include <thread>
#include <pthread.h>
#include <iostream>
#include <cstring>
#include <cerrno>
#include <string>
#include <stdexcept>
#include <csignal>
#include <fstream>

void parse_address(std::string const& full_address, std::string& host, std::string& port)
{
  auto colon_pos = full_address.find(':');

  if (colon_pos == std::string::npos)
  {
    throw std::runtime_error{"Invalid address format. Expected host:port"};
  }

  host = full_address.substr(0, colon_pos);
  port = full_address.substr(colon_pos + 1);
}

void set_thread_cpu_affinity(std::thread::native_handle_type thread_handle, int cpu_id)
{
  if (cpu_id < 0)
  {
    return;
  }

  ::cpu_set_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(cpu_id, &cpuset);

  if (::pthread_setaffinity_np(thread_handle, sizeof(cpu_set_t), &cpuset) != 0)
  {
    throw std::runtime_error{std::format("Failed to set thread affinity for cpu {}: {}", cpu_id, strerror(errno))};
  }
}

void set_thread_cpu_affinity(int cpu_id)
{
  set_thread_cpu_affinity(::pthread_self(), cpu_id);
}

std::atomic<int>& setup_signal_handlers()
{
  static std::atomic<int> shutdown_counter = 1;

  auto signal_handler = [](int /* signum */) { shutdown_counter = -1; };

  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  return shutdown_counter;
}

std::optional<metric_hud> setup_metric_hud(std::chrono::seconds interval, std::move_only_function<metric()> collector)
{
  if (interval.count() > 0 && collector)
  {
    return metric_hud{interval, std::move(collector)};
  }
  else
  {
    return std::nullopt;
  }
}

void dump_run_metadata(
  std::filesystem::path const& path,
  std::vector<std::string> const& cmd_args,
  std::vector<std::string> const& tags)
{
  auto j = nlohmann::json{};
  j["git_describe"] = build_info::git_describe;
  j["git_commit"] = build_info::git_commit;
  j["build_type"] = build_info::build_type;
  j["compiler_id"] = build_info::compiler_id;
  j["compiler_ver"] = build_info::compiler_ver;
  j["build_time_utc"] = build_info::build_time_utc;
  j["cmdline"] = boost::algorithm::join(cmd_args, " ");
  j["tags"] = tags;

  auto const mi = utility::collect_machine_info();
  j["machine"] = {
    {"kernel", mi.kernel},
    {"cpu_model", mi.cpu_model},
    {"hw_threads", mi.hw_threads},
    {"cpuset", mi.cpuset},
    {"os_name", mi.os_name},
    {"os_version", mi.os_version}};

  auto ofs = std::ofstream{path};
  ofs << j.dump(2) << std::endl;
}
