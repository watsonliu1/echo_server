#ifndef CLIENT_PERFORMANCE_MONITOR_H
#define CLIENT_PERFORMANCE_MONITOR_H

// 引入C++时间库（用于高精度计时）
#include <chrono>
// 引入字符串库（用于线程ID、日志信息拼接）
#include <string>

/**
 * @class PerformanceMonitor
 * @brief
 * 客户端性能监控类，用于实时采集并记录进程的CPU、内存、运行时长及消息发送量等指标
 * @note 设计为每个客户端/线程独立实例化，或全局单例使用，支持分线程统计性能数据
 */
class ClientPerformanceMonitor {
private:
  /**
   * @brief 性能监控的起始时间点（高精度时钟）
   * @note 用于计算测试/线程运行的累计时长，在构造函数中初始化
   */
  std::chrono::high_resolution_clock::time_point start_time;

  /**
   * @brief 监控起始时的CPU累计使用时间（单位：秒）
   * @note 存储进程初始CPU消耗基准值，后续通过差值计算测试期间的CPU增量
   */
  double initial_cpu_usage;

  /**
   * @brief 监控起始时的内存峰值占用（单位：KB）
   * @note 存储进程初始内存占用基准值，后续通过差值计算测试期间的内存增量
   */
  size_t initial_memory_kb;

  /**
   * @brief 私有方法：获取当前进程的累计CPU使用时间（用户态+内核态）
   * @return double 进程从启动到当前的CPU总使用时间（秒）
   * @note
   * 内部调用，通过解析/proc/self/stat文件实现，为log_current_stats提供CPU数据
   */
  double get_cpu_usage();

  /**
   * @brief 私有方法：获取当前进程的内存峰值占用（常驻集大小）
   * @return size_t 进程从启动到当前的最大内存占用（单位：KB）
   * @note 内部调用，通过getrusage系统调用实现，为log_current_stats提供内存数据
   */
  size_t get_memory_usage_kb();

public:
  /**
   * @brief 构造函数：初始化性能监控的基准数据
   * @note
   * 记录监控起始时间、初始CPU使用时间、初始内存占用，为后续增量计算提供基准
   */
  ClientPerformanceMonitor();

  /**
   * @brief 记录并打印当前的性能统计信息（线程级/全局）
   * @param thread_id 线程标识字符串（用于区分不同连接线程的统计数据）
   * @param msg_count 当前线程/客户端已发送的消息数量
   * @note
   * 输出内容包含：运行时长、CPU增量、内存增量、已发送消息数，通过log_info宏打印日志
   */
  void log_current_stats(const std::string &thread_id, int msg_count);
};

// 头文件保护宏结束
#endif // CLIENT_PERFORMANCE_MONITOR_H