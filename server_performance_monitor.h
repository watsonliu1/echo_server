#ifndef SERVER_PERFORMANCE_MONITOR_H
#define SERVER_PERFORMANCE_MONITOR_H

#include <atomic>
#include <chrono>
#include <string>

/**
 * 服务器性能监控类
 * 功能：实时采集服务器CPU利用率(百分比)、内存占用、TPS、活跃连接数等核心指标
 */
class ServerPerformanceMonitor {
private:
  // 监控起始时间（用于计算运行时长）
  std::chrono::high_resolution_clock::time_point start_time;
  // 进程初始CPU占用时间（秒）
  double initial_cpu_usage;
  // 全局统计引用：总处理消息数（原子变量保证线程安全）
  std::atomic<long long> &total_processed_msgs;
  // 全局统计引用：当前活跃连接数
  std::atomic<int> &active_connections;

  // TPS计算相关：上次统计的消息数
  long long last_msg_count;
  // TPS计算相关：上次统计的时间点
  std::chrono::high_resolution_clock::time_point last_check_time;

  // CPU利用率计算相关：上次采样的系统总CPU时间
  double last_sys_cpu;
  // CPU利用率计算相关：上次采样的进程CPU时间
  double last_proc_cpu;

  /**
   * 获取进程累计CPU占用时间（秒）
   * @return 进程从启动到现在占用的CPU时间（user+system）
   */
  double get_cpu_usage();

  /**
   * 获取系统总CPU时间（所有核心的累计时间，秒）
   * @return 系统启动到现在的总CPU时间（user+nice+system+idle+...）
   */
  double get_system_cpu_total();

  /**
   * 获取进程当前内存占用（KB）
   * @return 进程的最大驻留集大小（RSS）
   */
  size_t get_memory_usage_kb();

  /**
   * 计算每秒处理消息数（TPS）
   * @return 当前TPS值（避免时间差过小导致的异常）
   */
  long long get_current_tps();

  /**
   * 计算进程CPU利用率（百分比）
   * 原理：(进程CPU时间增量 / 系统总CPU时间增量) * 100%
   * @return CPU利用率百分比（0-100*核心数）
   */
  double get_cpu_percent();

public:
  /**
   * 构造函数：初始化监控器
   * @param total_msgs 全局总处理消息数引用
   * @param active_conns 全局活跃连接数引用
   */
  ServerPerformanceMonitor(std::atomic<long long> &total_msgs,
                           std::atomic<int> &active_conns);

  /**
   * 打印服务器性能统计信息（每10秒调用一次）
   */
  void log_server_stats();
};

#endif // SERVER_PERFORMANCE_MONITOR_H