#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define N 10000000

static inline uint64_t rdtsc()
{
  unsigned int lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((uint64_t)hi << 32) | lo;
}

int main()
{
  struct timespec start, end;
  uint64_t tsc_start, tsc_end;
  int volatile dummy = 0;

  // Warm up
  for (int i = 0; i < 1000; ++i)
    getpid();

  // Measure getpid syscall overhead (wall clock)
  clock_gettime(CLOCK_MONOTONIC, &start);
  for (int i = 0; i < N; ++i)
    dummy += getpid();
  clock_gettime(CLOCK_MONOTONIC, &end);
  double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1e9;
  printf("getpid() x %d: %.3f ms, %.1f ns/call\n", N, elapsed * 1000, elapsed * 1e9 / N);

  // Measure getpid syscall overhead (TSC)
  tsc_start = rdtsc();
  for (int i = 0; i < N; ++i)
    dummy += getpid();
  tsc_end = rdtsc();
  printf(
    "getpid() x %d: %lu cycles total, %.1f cycles/call\n", N, tsc_end - tsc_start, (double)(tsc_end - tsc_start) / N);

  // Prevent optimizing out dummy
  if (dummy == 42)
    puts("");
  return 0;
}
