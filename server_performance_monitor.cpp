// 引入服务器性能监控类头文件（定义ServerPerformanceMonitor类）
#include "server_performance_monitor.h"
// 引入公共模块头文件（包含日志函数、全局统计变量定义等）
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
 * 获取系统总CPU时间（所有核心的累计时间）
 * 从/proc/stat读取cpu行数据，包含user/nice/system/idle等时间
 * @return double 系统启动以来所有CPU核心的总累计时间（秒）
 * @note
 * /proc/stat的第一行"cpu"汇总了所有CPU核心的时间统计，包含用户态、内核态、空闲等时间
 */
double ServerPerformanceMonitor::get_system_cpu_total() {
  // 打开系统CPU统计文件/proc/stat
  std::ifstream proc_stat("/proc/stat");
  if (!proc_stat.is_open()) {
    log_error("Failed to open /proc/stat for CPU usage");
    return 0;
  }

  std::string line;
  std::getline(proc_stat, line); // 读取第一行（cpu总信息行）
  std::istringstream iss(line);

  std::string cpu_label; // 用于存储行首的"cpu"标签（无需使用）
  unsigned long user, nice, system, idle, iowait, irq, softirq, steal;
  // 解析cpu行的各字段：user(用户态)、nice(低优先级用户态)、system(内核态)、idle(空闲)、iowait(IO等待)等
  iss >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >>
      softirq >> steal;

  // 将时钟滴答数转换为秒（sysconf(_SC_CLK_TCK)获取每秒的时钟滴答数，通常为100）
  static const double clock_ticks = sysconf(_SC_CLK_TCK);
  // 累加所有CPU时间字段并转换为秒，得到系统总CPU时间
  return (user + nice + system + idle + iowait + irq + softirq + steal) /
         clock_ticks;
}

/**
 * 构造函数：初始化监控参数，记录基准数据
 * @param total_msgs 服务器全局已处理消息数的原子引用（用于统计TPS）
 * @param active_conns 服务器当前活跃连接数的原子引用（用于监控连接状态）
 * @note
 * 初始化启动时间、初始进程CPU时间、TPS计算的历史基准、CPU利用率计算的历史基准
 */
ServerPerformanceMonitor::ServerPerformanceMonitor(
    std::atomic<long long> &total_msgs, std::atomic<int> &active_conns)
    : total_processed_msgs(total_msgs), active_connections(active_conns) {
  // 记录监控启动的时间点（高精度时钟）
  start_time = std::chrono::high_resolution_clock::now();
  // 记录进程初始CPU使用时间（用于后续计算相对增量）
  initial_cpu_usage = get_cpu_usage();

  // 初始化TPS计算的历史数据：记录当前消息数和当前时间
  last_msg_count = total_processed_msgs.load();
  last_check_time = std::chrono::high_resolution_clock::now();

  // 初始化CPU利用率计算的历史数据：记录初始系统总CPU时间和进程CPU时间
  last_sys_cpu = get_system_cpu_total();
  last_proc_cpu = get_cpu_usage();
}

/**
 * 获取进程累计CPU时间（用户态+内核态）
 * 从/proc/self/stat读取进程的utime（用户态）和stime（内核态）时间
 * @return double 进程启动以来的累计CPU时间（秒）
 * @note /proc/self/stat是当前进程的状态文件，包含进程的CPU时间、内存等详细信息
 */
double ServerPerformanceMonitor::get_cpu_usage() {
  // 打开当前进程的状态文件/proc/self/stat
  std::ifstream proc_self_stat("/proc/self/stat");
  if (!proc_self_stat.is_open()) {
    log_error("Failed to open /proc/self/stat for process CPU usage");
    return 0;
  }

  std::string line;
  std::getline(proc_self_stat, line);
  std::istringstream iss(line);

  // 解析/proc/self/stat的前14个字段（仅提取需要的utime和stime）
  int pid;                                        // 进程ID
  std::string comm;                               // 进程名（带括号）
  char state;                                     // 进程状态（R/S/D等）
  int ppid, pgrp, session, tty_nr, tpgid;         // 父进程ID、进程组等
  unsigned int flags;                             // 进程标志位
  unsigned long minflt, cminflt, majflt, cmajflt; // 缺页异常次数
  unsigned long utime,
      stime; // utime=用户态CPU时间（时钟滴答数），stime=内核态CPU时间（时钟滴答数）

  iss >> pid >> comm >> state >> ppid >> pgrp >> session >> tty_nr >> tpgid >>
      flags >> minflt >> cminflt >> majflt >> cmajflt >> utime >> stime;

  // 将时钟滴答数转换为秒，返回进程总CPU时间（用户态+内核态）
  static const double clock_ticks = sysconf(_SC_CLK_TCK);
  return (utime + stime) / clock_ticks;
}

/**
 * 获取进程内存占用（KB）
 * 使用getrusage获取进程的最大驻留集大小（RSS，Resident Set Size）
 * @return size_t 进程当前的最大内存占用（KB）
 * @note ru_maxrss在Linux系统中表示进程使用的最大物理内存（常驻集），单位为KB
 */
size_t ServerPerformanceMonitor::get_memory_usage_kb() {
  struct rusage usage; // 存储进程资源使用信息的结构体
  // 获取当前进程的资源使用数据（RUSAGE_SELF表示当前进程）
  if (getrusage(RUSAGE_SELF, &usage) == -1) {
    log_error("Failed to getrusage for memory usage");
    return 0;
  }
  // 返回进程的最大驻留集大小（KB）
  return usage.ru_maxrss;
}

/**
 * 计算CPU利用率百分比
 * 对比两次采样的进程CPU时间增量和系统总CPU时间增量，计算进程占用的CPU比例
 * @return double 进程当前的CPU利用率（百分比，0-100）
 * @note 公式：(进程CPU增量 / 系统CPU增量) *
 * 100%，系统CPU增量反映所有核心的总时间变化
 */
double ServerPerformanceMonitor::get_cpu_percent() {
  // 获取当前采样的系统总CPU时间和进程CPU时间
  double current_sys_cpu = get_system_cpu_total();
  double current_proc_cpu = get_cpu_usage();

  // 计算两次采样的时间增量（当前值-历史值）
  double sys_delta = current_sys_cpu - last_sys_cpu;
  double proc_delta = current_proc_cpu - last_proc_cpu;

  // 更新历史采样值为当前值，用于下一次计算
  last_sys_cpu = current_sys_cpu;
  last_proc_cpu = current_proc_cpu;

  // 避免除零错误（系统CPU无变化时返回0）
  if (sys_delta <= 0 || proc_delta < 0) {
    return 0;
  }

  // 计算进程CPU利用率百分比并返回
  return (proc_delta / sys_delta) * 100.0;
}

/**
 * 计算TPS（每秒处理消息数）
 * 对比两次采样的消息数增量和时间增量，计算每秒处理的消息数量
 * @return long long 当前的TPS值（每秒处理消息数）
 * @note 为避免抖动，时间增量至少0.1秒才计算TPS
 */
long long ServerPerformanceMonitor::get_current_tps() {
  // 读取当前已处理的总消息数（原子操作保证线程安全）
  long long current_msg_count = total_processed_msgs.load();
  auto now = std::chrono::high_resolution_clock::now();
  // 计算两次采样的时间增量（秒）
  double elapsed = std::chrono::duration<double>(now - last_check_time).count();

  // 初始化TPS为0，时间增量不足0.1秒时不计算（避免分母过小导致TPS异常）
  long long tps = 0;
  if (elapsed >= 0.1) {
    // TPS = 消息数增量 / 时间增量
    tps =
        static_cast<long long>((current_msg_count - last_msg_count) / elapsed);
  }

  // 更新历史采样的消息数和时间，用于下一次计算
  last_msg_count = current_msg_count;
  last_check_time = now;
  return tps;
}

/**
 * 打印服务器性能统计日志
 * 汇总运行时长、CPU利用率、内存占用、活跃连接数、总消息数、TPS等指标并输出
 * @note 定期调用此函数（如每10秒），输出服务器实时性能状态，便于监控
 */
void ServerPerformanceMonitor::log_server_stats() {
  auto now = std::chrono::high_resolution_clock::now();
  // 计算服务器从启动到当前的总运行时长（秒）
  std::chrono::duration<double> elapsed_total = now - start_time;

  // 获取各项性能监控指标
  double cpu_percent = get_cpu_percent();   // 当前CPU利用率（%）
  size_t memory_kb = get_memory_usage_kb(); // 当前内存占用（KB）
  long long tps = get_current_tps(); // 当前TPS（每秒处理消息数）
  int active_conns = active_connections.load(); // 当前活跃连接数（原子读取）
  long long total_msgs = total_processed_msgs.load(); // 累计处理消息总数

  // 格式化输出性能日志，包含关键监控指标
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