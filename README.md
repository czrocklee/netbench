
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

3. **Configure the build:**
   ```sh
   cmake -B build -G Ninja
   ```
   - To enable spdlog logging:
     ```sh
     cmake -B build -G Ninja -DSPDLOG_LOGGER=ON
     ```
   - To enable stderr logging:
     ```sh
     cmake -B build -G Ninja -DSTDERR_LOGGER=ON
     ```

4. **Build the project:**
   ```sh
   cmake --build build
   ```

### Notes

- You can use CMake presets or VS Code CMake Tools for advanced configuration.
- For io_uring support, ensure your system has a recent Linux kernel; the Nix shell provides the necessary development headers.
- For custom logger backends, see `lib/utility/logger.hpp`.

## TOOLING

Understanding io_uring applications cannot be achieved easily by tracing syscalls alone. eBPF-based tools such as [bpftrace](https://bpftrace.org/) are good candidates for deeper inspection.

```sh
   sudo bpftrace tool/io_uring_trace.bt uring_receiver
```

###

- You may need root privileges (`sudo`) to run bpftrace scripts.
- Tracepoints are not a stable interface and may change across kernel versions.
