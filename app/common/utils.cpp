#include "utils.hpp"
#include "build_info.hpp"
#include <utility/machine_info.hpp>

#include <boost/algorithm/string/join.hpp>
#include <nlohmann/json.hpp>
#include <hdr/hdr_histogram.h>
#include <hdr/hdr_histogram_log.h>

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

void dump_metrics(std::filesystem::path const& dir, std::vector<metric const*> const& metrics)
{
  auto j = nlohmann::json::array();
  int worker_idx = 0;

  for (auto i = 0u; i < metrics.size(); ++i)
  {
    auto const* m = metrics[i];

    j.push_back({
      {"ops", m->ops},
      {"msgs", m->msgs},
      {"bytes", m->bytes},
      {"begin_ts", m->begin_ts.time_since_epoch().count()},
      {"end_ts", m->end_ts.time_since_epoch().count()},
    });

    if (m->latency_hist && m->latency_hist->total_count > 0)
    {
      auto const hist_log_path = dir / std::to_string(i).append(".hdr");

      if (FILE* fp = ::fopen(hist_log_path.c_str(), "w"); fp != nullptr)
      {
        constexpr auto to_timespec = [](std::chrono::steady_clock::time_point tp) {
          auto const secs = std::chrono::time_point_cast<std::chrono::seconds>(tp);
          auto const ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(tp) -
                          std::chrono::time_point_cast<std::chrono::nanoseconds>(secs);
          return ::timespec{secs.time_since_epoch().count(), ns.count()};
        };

        auto const begin_ts = to_timespec(m->begin_ts);
        auto const end_ts = to_timespec(m->end_ts);
        ::hdr_log_write(nullptr, fp, &begin_ts, &end_ts, m->latency_hist.get());
        ::fclose(fp);
        std::cout << "Lossless latency histogram for worker " << i << " written to " << hist_log_path << std::endl;
      }
      else
      {
        std::cerr << "Failed to open histogram log file " << hist_log_path << " for writing: " << strerror(errno)
                  << std::endl;
      }
    }
  }

  auto const metrics_path = dir / "metrics.json";
  auto ofs = std::ofstream{metrics_path};
  ofs << j.dump(2) << std::endl;
  std::cout << "Metrics written to " << metrics_path << std::endl;
}
