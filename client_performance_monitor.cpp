// 引入客户端性能监控模块头文件（定义PerformanceMonitor类）
#include "client_performance_monitor.h"
// 引入公共模块头文件（包含日志工具、通用宏定义）
#include "common.h"
// 引入文件流库（用于读取/proc文件系统信息）
#include <fstream>
// 引入字符串流库（用于解析/proc文件内容）
#include <sstream>
// 引入资源使用统计头文件（getrusage函数依赖）
#include <sys/resource.h>
// 引入系统配置头文件（sysconf函数依赖）
#include <unistd.h>

/**
 * @brief ClientPerformanceMonitor 类构造函数
 * @note
 * 初始化性能监控的基准数据：记录测试开始时间、初始CPU占用时间、初始内存占用
 *       用于后续计算相对CPU/内存消耗
 */
ClientPerformanceMonitor::ClientPerformanceMonitor() {
  // 记录性能监控的起始时间点（高精度时钟）
  start_time = std::chrono::high_resolution_clock::now();
  // 获取进程初始CPU累计使用时间（单位：秒）
  initial_cpu_usage = get_cpu_usage();
  // 获取进程初始内存峰值占用（单位：KB）
  initial_memory_kb = get_memory_usage_kb();
}

/**
 * @brief 读取当前进程的累计CPU使用时间（用户态+内核态）
 * @return double 进程从启动到当前的CPU总使用时间（秒）
 * @note 通过解析/proc/self/stat文件获取CPU时间信息：
 *       - /proc/self/stat：包含当前进程的详细状态信息
 *       - utime：用户态CPU时间（时钟滴答数）
 *       - stime：内核态CPU时间（时钟滴答数）
 */
double ClientPerformanceMonitor::get_cpu_usage() {
  // 打开当前进程的stat文件（/proc/self/stat是当前进程的状态文件）
  std::ifstream proc_stat("/proc/self/stat");
  if (!proc_stat.is_open()) { // 文件打开失败时返回0
    return 0;
  }

  std::string line;
  std::getline(proc_stat, line); // 读取文件整行内容
  std::istringstream iss(line);  // 将字符串转为流用于解析

  // /proc/self/stat字段解析（仅提取需要的CPU相关字段）
  int pid;                                        // 进程ID
  std::string comm;                               // 进程名（带括号）
  char state;                                     // 进程状态（R/S/D等）
  int ppid, pgrp, session, tty_nr, tpgid;         // 父进程ID、进程组等
  unsigned int flags;                             // 进程标志位
  unsigned long minflt, cminflt, majflt, cmajflt; // 缺页异常次数
  unsigned long utime, stime; // 用户态/内核态CPU时间（时钟滴答数）

  // 按格式读取字段（忽略不需要的字段）
  iss >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >>
      flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime;

  // 获取系统时钟滴答数（每秒的滴答次数，用于转换CPU时间为秒）
  static const double clock_ticks = sysconf(_SC_CLK_TCK);
  // 将用户态+内核态时间转换为秒（总CPU时间=utime+stime / 每秒滴答数）
  return (utime + stime) / clock_ticks;
}

/**
 * @brief 获取当前进程的内存峰值占用（常驻集大小）
 * @return size_t 进程从启动到当前的最大内存占用（单位：KB）
 * @note 使用getrusage系统调用获取进程资源使用信息：
 *       - RUSAGE_SELF：统计当前进程的资源使用
 *       - ru_maxrss：进程使用的最大常驻集大小（Resident Set Size）
 */
size_t ClientPerformanceMonitor::get_memory_usage_kb() {
  struct rusage usage; // 存储进程资源使用信息的结构体
  // 获取当前进程的资源使用数据（RUSAGE_SELF表示当前进程）
  getrusage(RUSAGE_SELF, &usage);
  // 返回最大常驻内存大小（单位：KB，不同系统可能有差异，Linux下为KB）
  return usage.ru_maxrss;
}

/**
 * @brief 记录并打印当前的性能统计信息
 * @param thread_id 线程ID（用于区分不同连接线程的统计）
 * @param msg_count 当前线程已发送的消息数量
 * @note 输出内容包括：运行时长、CPU累计消耗、内存增量、已发送消息数
 *       用于实时监控客户端各线程的性能表现
 */
void ClientPerformanceMonitor::log_current_stats(const std::string &thread_id,
                                                 int msg_count) {
  // 获取当前时间点（用于计算已运行时长）
  auto now = std::chrono::high_resolution_clock::now();
  // 计算从监控开始到当前的时间差（单位：秒）
  std::chrono::duration<double> elapsed = now - start_time;

  // 计算当前CPU累计使用时间（减去初始值，得到测试期间的CPU消耗）
  double current_cpu = get_cpu_usage() - initial_cpu_usage;
  // 计算当前内存增量（减去初始值，得到测试期间的内存增长）
  size_t current_memory = get_memory_usage_kb() - initial_memory_kb;

  // 打印性能日志（结合线程ID、运行时长、CPU/内存消耗、消息发送量）
  log_info("[Thread " + thread_id +
           "] "
           "Elapsed: " +
           std::to_string(elapsed.count()) + // 已运行秒数
           "s | "
           "CPU: " +
           std::to_string(current_cpu) + // CPU累计消耗秒数
           "s | "
           "Memory: " +
           std::to_string(current_memory) + // 内存增量KB
           "KB | "
           "Sent: " +
           std::to_string(msg_count)); // 已发送消息数
}