
## BUILD

### Prerequisites

- [Nix](https://nixos.org/download.html) package manager
- `nix-shell` (comes with Nix)
- C++23 compatible compiler (provided by Nix shell)
- CMake 3.19 or newer (provided by Nix shell)
- Ninja (provided by Nix shell)

### Build Steps

1. **Clone the repository:**
   ```sh
   git clone https://github.com/czrocklee/netbench.git
   cd netbench
   ```

2. **Enter the Nix development shell:**
   ```sh
   nix-shell
   ```
   This command provides all required dependencies and tools.

3. **Configure with CMake presets (recommended):**
   ```sh
   cmake --preset=release
   ```

4. **Build using the preset:**
   ```sh
   cmake --build --preset=release
   ```

This produces binaries under `build/release/app/`.

### Notes

- CMake presets are provided in `CMakePresets.json` (e.g., `debug`, `release`).
- For io_uring support, ensure your system has a recent Linux kernel; the Nix shell provides the necessary development headers.
- For custom logger backends, see `lib/utility/logger.hpp`.

## BENCHMARK

This repository includes a simple scenario runner to automate local (or remote) end-to-end tests and plotting.

- Entrypoint: `benchmark/run_scenarios.py`
- Output: artifacts under `results/<scenario_timestamp>/...` and optional plots

Quick start (local machine):

```sh
# From repo root, after building (binaries at build/release/app)
python3 benchmark/run_scenarios.py --auto-plot
```

Common, minimal examples:

- Run a single scenario and auto-plot:
   ```sh
   python3 benchmark/run_scenarios.py \
      --scenario receive_throughput_by_buffer_size \
      --auto-plot
   ```

- Limit implementations (e.g., BSD and io_uring only):
   ```sh
   python3 benchmark/run_scenarios.py \
      --scenario echo_throughput_by_buffer_size \
      --impl bsd --impl uring \
      --auto-plot
   ```

- Override a few fixed parameters (no need to enumerate all options):
   ```sh
   python3 benchmark/run_scenarios.py \
      --scenario receive_latency_by_message_rate \
      --fixed duration_sec=15 --fixed msg_size=64 \
      --auto-plot
   ```

- Ping-pong RTT example:
   ```sh
   python3 benchmark/run_scenarios.py \
      --scenario pingpong_latency_by_msg_size \
      --impl uring_sqpoll_zc \
      --auto-plot
   ```

Notes:
- By default, binaries are discovered under `build/release/app`. You can override with `--app-root` if needed.
- Results and plots are written to `results/` by default. Use your image viewer or open the printed file:// URLs.
- For convenience on multi-core machines, add `--auto-cpu-ids` to let the runner pick non-overlapping CPU affinities.

## TOOLING

Understanding io_uring applications cannot be achieved easily by tracing syscalls alone. eBPF-based tools such as [bpftrace](https://bpftrace.org/) are good candidates for deeper inspection.

```sh
   sudo bpftrace tool/io_uring_trace.bt uring_receiver
```

###

- You may need root privileges (`sudo`) to run bpftrace scripts.
- Tracepoints are not a stable interface and may change across kernel versions.
