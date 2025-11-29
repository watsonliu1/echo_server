#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <chrono>
#include <string>

class PerformanceMonitor {
private:
  std::chrono::high_resolution_clock::time_point start_time;
  double initial_cpu_usage;
  size_t initial_memory_kb;

  double get_cpu_usage();
  size_t get_memory_usage_kb();

public:
  PerformanceMonitor();
  void log_current_stats(const std::string &thread_id, int msg_count);
};

#endif // PERFORMANCE_MONITOR_H