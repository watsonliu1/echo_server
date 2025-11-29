// 头文件保护宏：防止该头文件被重复包含，避免编译时的重定义错误
#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H

// 引入公共模块头文件（包含ClientConfig配置结构体、日志函数、常量定义等）
#include "common.h"
// 引入原子操作库（用于多线程环境下的安全统计计数）
#include <atomic>
// 引入字符串库（用于线程ID处理、日志信息拼接等）
#include <string>
// 引入线程库（用于多线程连接管理）
#include <thread>
// 引入向量容器库（用于存储线程对象）
#include <vector>

/**
 * @class EchoClient
 * @brief Echo客户端核心类，负责管理多线程连接、发送消息、接收响应及统计测试结果
 * @note
 * 采用多线程模型，每个线程处理一个TCP连接，支持高并发测试，通过原子变量保证多线程统计的线程安全
 */
class EchoClient {
private:
  /**
   * @brief 客户端配置结构体实例，存储命令行解析后的连接参数、测试参数等
   * @note
   * 包含服务器IP、端口、并发连接数、消息大小、测试时长等配置项（定义在common.h的ClientConfig）
   */
  ClientConfig config;

  /**
   * @brief 私有方法：获取当前线程的ID字符串（简化格式，便于日志区分）
   * @return std::string 简化后的线程ID字符串（如后三位）
   * @note 用于多线程日志中标识不同连接线程，避免日志混淆
   */
  std::string get_thread_id();

  /**
   * @brief
   * 私有方法：处理单个TCP连接的完整生命周期（创建连接、发送消息、接收响应、性能监控）
   * @note
   * 每个线程独立执行此方法，负责单个连接的消息收发逻辑，包含异常处理和错误统计
   */
  void handle_normal_connection();

public:
  /**
   * @brief 静态原子统计变量（多线程共享，原子操作保证线程安全）
   */
  static std::atomic<int> total_connections; // 成功建立的总连接数
  static std::atomic<int> total_sent;        // 客户端发送的总消息数
  static std::atomic<int> total_received;    // 客户端接收的总响应数
  static std::atomic<int> total_errors; // 测试过程中出现的总错误数

  /**
   * @brief 构造函数：初始化EchoClient实例，绑定客户端配置
   * @param cfg ClientConfig配置结构体实例（包含服务器地址、测试参数等）
   * @note 从外部传入解析后的配置，初始化客户端核心参数
   */
  EchoClient(const ClientConfig &cfg);

  /**
   * @brief 客户端主运行方法：启动多线程连接、管理线程生命周期
   * @note
   * 分批创建线程（避免瞬间资源耗尽），等待所有线程执行完成后调用print_stats打印最终统计
   */
  void run();

  /**
   * @brief 打印客户端测试的最终统计结果（总连接数、消息收发数、错误数等）
   * @note 测试结束后调用，输出汇总信息便于分析测试结果
   */
  void print_stats();
};

// 头文件保护宏结束
#endif // ECHO_CLIENT_H