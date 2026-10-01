// Developer-only deterministic workload timing; never linked into a daemon.
#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace bench {
using Clock = std::chrono::steady_clock;
inline void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
inline uint64_t hash(const std::string &s) {
  uint64_t h = 14695981039346656037ull;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ull;
  }
  return h;
}
inline double ms(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}
inline void report(const char *name, std::vector<double> times,
                   uint64_t checksum = 0) {
  std::sort(times.begin(), times.end());
  std::cout << name << " median_ms=" << times[times.size() / 2] << " p95_ms="
            << times[std::min(times.size() - 1, times.size() * 95 / 100)]
            << " samples=" << times.size() << " checksum=" << checksum << '\n';
}
template <class F> void measure(const char *name, int count, F work) {
  work();
  std::vector<double> times;
  uint64_t checksum = 0;
  for (int i = 0; i < count; ++i) {
    auto start = Clock::now();
    auto value = work();
    times.push_back(ms(start));
    checksum ^= hash(value);
  }
  report(name, std::move(times), checksum);
}
} // namespace bench
