#include "performance_monitor.h"
#include "logger.h"
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#include <sys/sysinfo.h>
#include <unistd.h>

PerformanceMonitor::PerformanceMonitor(int interval_sec)
    : interval_sec_(interval_sec) {}

PerformanceMonitor::~PerformanceMonitor() { stop(); }

void PerformanceMonitor::start() {
  stop_flag_ = false;
  last_time_ = std::chrono::system_clock::now();
  last_message_count_ = 0;
  monitor_thread_ = std::thread(&PerformanceMonitor::monitor_loop, this);
  LOG_INFO("性能监控启动，间隔: " + std::to_string(interval_sec_) + "秒");
}

void PerformanceMonitor::stop() {
  stop_flag_ = true;
  if (monitor_thread_.joinable()) {
    monitor_thread_.join();
  }
  LOG_INFO("性能监控停止");
}

void PerformanceMonitor::increment_message_count() { total_messages_++; }

void PerformanceMonitor::set_connection_count(int count) {
  connection_count_ = count;
}

void PerformanceMonitor::monitor_loop() {
  while (!stop_flag_) {
    std::this_thread::sleep_for(std::chrono::seconds(interval_sec_));

    // 计算TPS
    auto now = std::chrono::system_clock::now();
    double elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_time_)
            .count();
    uint64_t current_messages = total_messages_;
    double tps = (current_messages - last_message_count_) / elapsed;

    // 采集CPU和内存（修复：获取实时内存）
    double cpu_usage;
    uint64_t memory_rss;
    collect_cpu_memory(cpu_usage, memory_rss);

    // 输出监控数据
    LOG_INFO("性能监控: "
             "连接数=" +
             std::to_string(connection_count_) +
             ", "
             "TPS=" +
             std::to_string(tps) +
             ", "
             "累计消息=" +
             std::to_string(current_messages) +
             ", "
             "CPU使用率=" +
             std::to_string(cpu_usage) +
             "%, "
             "内存占用=" +
             std::to_string(memory_rss / 1024 / 1024) + "MB" // 转换为MB
    );

    last_time_ = now;
    last_message_count_ = current_messages;
  }
}

void PerformanceMonitor::collect_cpu_memory(double &cpu_usage,
                                            uint64_t &memory_rss) {
  // ===== 修复：获取当前内存占用（非峰值）=====
  std::ifstream statm("/proc/self/statm");
  std::string line;
  if (statm.is_open() && std::getline(statm, line)) {
    std::istringstream iss(line);
    uint64_t size, resident;
    iss >> size >> resident;
    memory_rss = resident * sysconf(_SC_PAGESIZE); // 当前RSS（字节）
  } else {
    memory_rss = 0;
  }

  // 获取CPU使用率
  static uint64_t last_cpu_time = 0;
  static uint64_t last_system_time = 0;

  uint64_t current_cpu_time = get_cpu_time();
  uint64_t current_system_time =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();

  if (last_cpu_time == 0 || last_system_time == 0) {
    cpu_usage = 0.0;
  } else {
    double cpu_diff = current_cpu_time - last_cpu_time;
    double system_diff = current_system_time - last_system_time;
    cpu_usage = (cpu_diff / system_diff) * 100 * sysconf(_SC_NPROCESSORS_ONLN);
  }

  last_cpu_time = current_cpu_time;
  last_system_time = current_system_time;
}

uint64_t PerformanceMonitor::get_cpu_time() {
  std::ifstream stat_file("/proc/self/stat");
  std::string line;
  std::getline(stat_file, line);
  std::istringstream iss(line);

  std::string pid;
  uint64_t utime, stime;
  iss >> pid >> pid >> pid >> pid >> pid >> pid >> pid >> pid >> pid >> pid >>
      pid >> pid >> pid >> utime >> stime;

  return utime + stime; // jiffies
}