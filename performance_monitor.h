#ifndef PERFORMANCE_MONITOR_H
#define PERFORMANCE_MONITOR_H

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

class PerformanceMonitor {
public:
  PerformanceMonitor(int interval_sec);
  ~PerformanceMonitor();

  void start();
  void stop();

  // 统计接口
  void increment_message_count();
  void set_connection_count(int count);

private:
  void monitor_loop();
  void collect_cpu_memory(double &cpu_usage, uint64_t &memory_rss);
  uint64_t get_cpu_time();

private:
  int interval_sec_;
  std::thread monitor_thread_;
  std::atomic<bool> stop_flag_ = false;

  // 统计数据
  std::atomic<uint64_t> total_messages_ = 0;
  std::atomic<int> connection_count_ = 0;
  uint64_t last_message_count_ = 0;
  std::chrono::time_point<std::chrono::system_clock> last_time_;

  std::mutex mutex_;
};

#endif // PERFORMANCE_MONITOR_H