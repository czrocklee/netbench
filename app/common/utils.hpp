#pragma once

#include "metric_hud.hpp"

#include <filesystem>
#include <thread>
#include <string>
#include <optional>
#include <functional>
#include <chrono>

void parse_address(std::string const& full_address, std::string& host, std::string& port);

void set_thread_cpu_affinity(std::thread::native_handle_type thread_handle, int cpu_id);

void set_thread_cpu_affinity(int cpu_id);

std::atomic<int>& setup_signal_handlers();

std::optional<metric_hud> setup_metric_hud(std::chrono::seconds interval, std::move_only_function<metric()> collector);

void dump_run_metadata(
  std::filesystem::path const& path,
  std::vector<std::string> const& cmd_args,
  std::vector<std::string> const& tags);
