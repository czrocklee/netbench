#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace utility
{
  struct machine_info
  {
    std::string kernel;      // e.g. 6.9.9
    std::string cpu_model;   // model name
    unsigned hw_threads = 0; // hardware_concurrency
    std::vector<int> cpuset; // allowed CPUs
    std::string os_name;     // NixOS, Ubuntu
    std::string os_version;  // 25.05, 24.04
  };

  struct RunMetadata
  {
    // build
    std::string git_describe, git_commit, build_type, compiler_id, compiler_ver, build_time_utc;
    // runtime
    std::string cmdline;
    std::unordered_map<std::string, std::string> env;
    machine_info machine;
    // user
    std::vector<std::string> tags;
  };


  machine_info collect_machine_info();

} // namespace utility