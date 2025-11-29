#include "server_performance_monitor.h"
#include "common.h"
#include <fstream>
#include <sstream>
#include <sys/resource.h>
#include <unistd.h>

/**
 * 获取系统总CPU时间（所有核心的累计时间）
 * 从/proc/stat读取cpu行数据，包含user/nice/system/idle等时间
 */
double ServerPerformanceMonitor::get_system_cpu_total() {
  std::ifstream proc_stat("/proc/stat");
  if (!proc_stat.is_open()) {
    log_error("Failed to open /proc/stat for CPU usage");
    return 0;
  }

  std::string line;
  std::getline(proc_stat, line); // 读取第一行（cpu总信息）
  std::istringstream iss(line);

  std::string cpu_label; // 跳过"cpu"标签
  unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
  iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >>
      softirq >> steal;

  // 转换为秒（sysconf(_SC_CLK_TCK)是每秒的时钟滴答数）
  static const double clock_ticks = sysconf(_SC_CLK_TCK);
  return (user + nice + system + idle + iowait + irq + softirq + steal) /
         clock_ticks;
}

/**
 * 构造函数：初始化监控参数
 */
ServerPerformanceMonitor::ServerPerformanceMonitor(
    std::atomic<long long> &total_msgs, std::atomic<int> &active_conns)
    : total_processed_msgs(total_msgs), active_connections(active_conns) {
  start_time = std::chrono::high_resolution_clock::now();
  initial_cpu_usage = get_cpu_usage();

  // 初始化TPS计算的历史数据
  last_msg_count = total_processed_msgs.load();
  last_check_time = std::chrono::high_resolution_clock::now();

  // 初始化CPU利用率计算的历史数据
  last_sys_cpu = get_system_cpu_total();
  last_proc_cpu = get_cpu_usage();
}

/**
 * 获取进程累计CPU时间（user+system）
 * 从/proc/self/stat读取进程的utime和stime
 */
double ServerPerformanceMonitor::get_cpu_usage() {
  std::ifstream proc_self_stat("/proc/self/stat");
  if (!proc_self_stat.is_open()) {
    log_error("Failed to open /proc/self/stat for process CPU usage");
    return 0;
  }

  std::string line;
  std::getline(proc_self_stat, line);
  std::istringstream iss(line);

  // 解析/proc/self/stat的字段（仅需前14个字段中的utime和stime）
  int pid;
  std::string comm;
  char state;
  int ppid, pgrp, session, tty_nr, tpgid;
  unsigned int flags;
  unsigned long minflt, cminflt, majflt, cmajflt;
  unsigned long utime, stime; // utime=用户态CPU时间，stime=内核态CPU时间

  iss >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >>
      flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime;

  // 转换为秒
  static const double clock_ticks = sysconf(_SC_CLK_TCK);
  return (utime + stime) / clock_ticks;
}

/**
 * 获取进程内存占用（KB）
 * 使用getrusage获取进程的最大驻留集大小（RSS）
 */
size_t ServerPerformanceMonitor::get_memory_usage_kb() {
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == -1) {
    log_error("Failed to getrusage for memory usage");
    return 0;
  }
  // ru_maxrss在Linux下是KB单位
  return usage.ru_maxrss;
}

/**
 * 计算CPU利用率百分比
 * 对比两次采样的进程CPU时间和系统总CPU时间的增量
 */
double ServerPerformanceMonitor::get_cpu_percent() {
  // 获取当前采样值
  double current_sys_cpu = get_system_cpu_total();
  double current_proc_cpu = get_cpu_usage();

  // 计算增量（避免负数，处理采样异常）
  double sys_delta = current_sys_cpu - last_sys_cpu;
  double proc_delta = current_proc_cpu - last_proc_cpu;

  // 更新历史采样值
  last_sys_cpu = current_sys_cpu;
  last_proc_cpu = current_proc_cpu;

  // 避免除零错误（系统时间无变化时返回0）
  if (sys_delta <= 0 || proc_delta < 0) {
    return 0;
  }

  // 计算利用率百分比
  return (proc_delta / sys_delta) * 100.0;
}

/**
 * 计算TPS（每秒处理消息数）
 * 对比两次采样的消息数增量和时间增量
 */
long long ServerPerformanceMonitor::get_current_tps() {
  long long current_msg_count = total_processed_msgs.load();
  auto now = std::chrono::high_resolution_clock::now();
  // 计算时间增量（秒）
  double elapsed = std::chrono::duration<double>(now - last_check_time).count();

  // 避免时间差过小导致的异常（至少0.1秒才计算）
  long long tps = 0;
  if (elapsed >= 0.1) {
    tps =
        static_cast<long long>((current_msg_count - last_msg_count) / elapsed);
  }

  // 更新历史采样值
  last_msg_count = current_msg_count;
  last_check_time = now;
  return tps;
}

/**
 * 打印性能统计日志
 * 包含运行时长、CPU利用率、内存、活跃连接数、总消息数、TPS
 */
void ServerPerformanceMonitor::log_server_stats() {
  auto now = std::chrono::high_resolution_clock::now();
  // 计算服务器运行总时长（秒）
  std::chrono::duration<double> elapsed_total = now - start_time;

  // 获取各项监控指标
  double cpu_percent = get_cpu_percent();
  size_t memory_kb = get_memory_usage_kb();
  long long tps = get_current_tps();
  int active_conns = active_connections.load();
  long long total_msgs = total_processed_msgs.load();

  // 格式化日志输出
  log_info("[SERVER] Elapsed: " + std::to_string(elapsed_total.count()) +
           "s | "
           "CPU: " +
           std::to_string(cpu_percent) +
           "% | "
           "Memory: " +
           std::to_string(memory_kb) +
           "KB | "
           "Active Conns: " +
           std::to_string(active_conns) +
           " | "
           "Total Msgs: " +
           std::to_string(total_msgs) +
           " | "
           "TPS: " +
           std::to_string(tps));
}