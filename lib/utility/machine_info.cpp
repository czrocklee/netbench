#include "machine_info.hpp"

#include <sys/utsname.h>
#include <sched.h>
#include <unistd.h>

#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
  std::string read_first_cpu_model()
  {
    std::ifstream f("/proc/cpuinfo");

    for (std::string line; std::getline(f, line);)
    {
      if (line.rfind("model name", 0) == 0)
      {

        if (auto pos = line.find(':'); pos != std::string::npos)
        {
          return std::string(line.begin() + pos + 2, line.end());
        }
      }
    }

    return "unknown";
  }

  std::pair<std::string, std::string> read_os_release()
  {
    std::ifstream f("/etc/os-release");
    std::string line, name, ver;

    constexpr auto unquote = [](std::string s) {
      if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
      {
        return s.substr(1, s.size() - 2);
      }

      return s;
    };

    while (std::getline(f, line))
    {
      if (line.rfind("NAME=", 0) == 0)
      {
        name = unquote(line.substr(5));
      }
      else if (line.rfind("VERSION_ID=", 0) == 0)
      {
        ver = unquote(line.substr(11));
      }
    }

    return {name.empty() ? "unknown" : name, ver.empty() ? "unknown" : ver};
  }

  std::vector<int> current_cpuset()
  {
    ::cpu_set_t set;
    CPU_ZERO(&set);
    std::vector<int> cpus;

    if (::sched_getaffinity(0, sizeof(set), &set) == 0)
    {
      auto n = ::sysconf(_SC_NPROCESSORS_CONF);

      for (int i = 0; i < n; ++i)
      {
        if (CPU_ISSET(i, &set))
        {
          cpus.push_back(i);
        }
      }
    }

    return cpus;
  }
}

namespace utility
{
  machine_info collect_machine_info()
  {
    ::utsname u{};
    ::uname(&u);
    auto [name, ver] = read_os_release();

    return machine_info{
      .kernel = u.release,
      .cpu_model = read_first_cpu_model(),
      .hw_threads = std::thread::hardware_concurrency(),
      .cpuset = current_cpuset(),
      .os_name = name,
      .os_version = ver};
  }

} // namespace utility
