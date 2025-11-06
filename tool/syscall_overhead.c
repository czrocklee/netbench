// Use GNU extensions for CLOCK_MONOTONIC_RAW and sched_* APIs when available
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
// #include <stdbool.h> // not needed currently

#define DEFAULT_ITERS 10000000

// Ordered TSC helpers: lfence; rdtsc ... rdtscp; lfence
static inline uint64_t rdtsc_begin(void)
{
  unsigned int lo, hi;
  __asm__ __volatile__("lfence; rdtsc" : "=a"(lo), "=d"(hi)::"memory");
  return ((uint64_t)hi << 32) | lo;
}

static inline uint64_t rdtsc_end(void)
{
  unsigned int lo, hi, aux;
  __asm__ __volatile__("rdtscp" : "=a"(lo), "=d"(hi), "=c"(aux)::"memory");
  __asm__ __volatile__("lfence" ::: "memory");
  return ((uint64_t)hi << 32) | lo;
}

static void pin_to_cpu(int cpu)
{
  if (cpu < 0)
  {
    fprintf(stderr, "[affinity] invalid CPU id: %d\n", cpu);
    return;
  }

  long nproc = sysconf(_SC_NPROCESSORS_CONF);

  if (nproc > 0 && cpu >= nproc)
  {
    fprintf(stderr, "[affinity] CPU id %d out of range [0, %ld)\n", cpu, nproc);
    // continue anyway; kernel will validate too
  }

  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET((unsigned)cpu, &set);

  if (sched_setaffinity(0, sizeof(set), &set) == 0)
  {
    fprintf(stderr, "[affinity] pinned to CPU %d\n", cpu);
  }
  else
  {
    fprintf(stderr, "[affinity] sched_setaffinity(%d) failed: %s\n", cpu, strerror(errno));
  }
}

// Generic helpers to reduce duplicated measurement code
#define MEASURE_WALL(seconds_out, clk, iters, BODY)                                                                    \
  do                                                                                                                   \
  {                                                                                                                    \
    clock_gettime((clk), &start);                                                                                      \
    for (int _i = 0; _i < (iters); ++_i)                                                                               \
    {                                                                                                                  \
      BODY;                                                                                                            \
    }                                                                                                                  \
    clock_gettime((clk), &end);                                                                                        \
    (seconds_out) = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;                                 \
  } while (0)

#define MEASURE_TSC(cycles_out, iters, BODY)                                                                           \
  do                                                                                                                   \
  {                                                                                                                    \
    tsc_start = rdtsc_begin();                                                                                         \
    for (int _i = 0; _i < (iters); ++_i)                                                                               \
    {                                                                                                                  \
      BODY;                                                                                                            \
    }                                                                                                                  \
    tsc_end = rdtsc_end();                                                                                             \
    (cycles_out) = tsc_end - tsc_start;                                                                                \
  } while (0)

// (Stats helpers removed to keep code simple; we run a single iteration set only)

int main(int argc, char** argv)
{
  struct timespec start, end;
  uint64_t tsc_start, tsc_end;
  int volatile dummy = 0;

  // Parse args: -c/--cpu <id>, -n/--iters I
  int desired_cpu = -1;
  int iters = DEFAULT_ITERS;

  for (int i = 1; i < argc; ++i)
  {
    if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "--cpu"))
    {
      if (i + 1 < argc)
      {
        desired_cpu = atoi(argv[++i]);
      }
      else
      {
        fprintf(stderr, "Usage: %s [-c|--cpu <id>] [-n|--iters I]\n", argv[0]);
        return 1;
      }
    }
    else if (!strcmp(argv[i], "-n") || !strcmp(argv[i], "--iters"))
    {
      if (i + 1 < argc)
      {
        iters = atoi(argv[++i]);
        if (iters < 1)
          iters = 1;
      }
      else
      {
        fprintf(stderr, "Usage: %s [-c|--cpu <id>] [-n|--iters I]\n", argv[0]);
        return 1;
      }
    }
    else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
    {
      fprintf(stderr, "Usage: %s [-c|--cpu <id>] [-n|--iters I]\n", argv[0]);
      return 0;
    }
  }

  // Pin to specific CPU if requested; otherwise pin to current CPU
  pin_to_cpu(desired_cpu >= 0 ? desired_cpu : sched_getcpu());

  // Warm up (glibc wrapper path)
  for (int i = 0; i < 1000; ++i)
    (void)getpid();

  // Choose a clock; prefer MONOTONIC_RAW if available
  clockid_t clk = CLOCK_MONOTONIC;
#ifdef CLOCK_MONOTONIC_RAW
  clk = CLOCK_MONOTONIC_RAW;
#endif

  // Single set of measurements (no repeated runs)
  double elapsed_glibc = 0.0, base_elapsed = 0.0, elapsed_sys = 0.0;
  uint64_t cycles_glibc = 0, base_cycles = 0, cycles_sys = 0;

  // Baseline (no syscall)
  MEASURE_WALL(base_elapsed, clk, iters, { dummy += 1; });
  MEASURE_TSC(base_cycles, iters, { dummy += 1; });

  // glibc getpid()
  MEASURE_WALL(elapsed_glibc, clk, iters, { dummy += getpid(); });
  MEASURE_TSC(cycles_glibc, iters, { dummy += getpid(); });

  // Raw syscall warm up (small)
  for (int i = 0; i < 16; ++i) (void)syscall(SYS_getpid);

  // Raw syscall
  MEASURE_WALL(elapsed_sys, clk, iters, { dummy += (int)syscall(SYS_getpid); });
  MEASURE_TSC(cycles_sys, iters, { dummy += (int)syscall(SYS_getpid); });

  // Report
  printf("getpid() (glibc) x %d: %.3f ms, %.1f ns/call\n", iters, elapsed_glibc * 1000.0, (elapsed_glibc * 1e9) / iters);
  printf("getpid() (glibc) x %d: %lu cycles total, %.1f cycles/call\n", iters, (unsigned long)cycles_glibc, (double)cycles_glibc / iters);

  printf("syscall(getpid) x %d: %.3f ms, %.1f ns/call\n", iters, elapsed_sys * 1000.0, (elapsed_sys * 1e9) / iters);
  printf("syscall(getpid) x %d: %lu cycles total, %.1f cycles/call\n", iters, (unsigned long)cycles_sys, (double)cycles_sys / iters);

  double adj_ns = ((elapsed_sys - base_elapsed) * 1e9) / iters;
  double adj_cyc = (double)(cycles_sys - base_cycles) / iters;
  printf("Baseline-subtracted: ~%.1f ns/call, ~%.1f cycles/call\n", adj_ns, adj_cyc);

  // Prevent optimizing out dummy
  if (dummy == 42)
    puts("");
  return 0;
}
