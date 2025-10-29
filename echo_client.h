#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H
/**
 * @file echo_client.h
 * @brief 回声客户端核心类定义，实现多线程并发连接测试功能
 * @details 该类封装了客户端与回声服务器交互的全流程，支持按配置创建多线程并发连接，
 *          自动发送测试消息、验证回射数据，并统计连接/消息/错误等关键指标，
 *          是测试回声服务器性能与稳定性的核心组件
 */

#include "common.h"           // 引入公共配置（如ClientConfig结构体、BUFFER_SIZE等）
#include <thread>             // C++多线程库（std::thread、std::this_thread）
#include <vector>             // 容器库，用于存储管理线程对象（std::vector<std::thread>）
#include <atomic>             // 原子变量库，确保多线程安全访问统计数据
#include <mutex>              // 互斥锁库，保证日志输出的线程安全
#include <string>             // 字符串库，用于日志消息和测试数据处理

/**
 * @class EchoClient
 * @brief 回声客户端核心类，封装多线程并发测试的所有功能
 * @details 负责按配置（ClientConfig）创建指定数量的连接线程，每个线程独立完成"连接服务器→发送消息→
 *          接收回射→验证数据"流程，同时线程安全地统计测试结果并输出日志
 */
class EchoClient
{
private:
    /**
     * @brief 客户端测试配置结构体
     * @details 存储客户端连接服务器的所有参数（IP、端口、连接数、消息数量/大小等），
     *          从外部传入（如main函数解析命令行参数后构建），确保配置灵活可配置
     */
    ClientConfig config;

    /**
     * @brief 成功建立的连接数（原子变量）
     * @details 统计所有线程中成功与服务器建立TCP连接的数量，使用std::atomic确保多线程并发更新时计数准确，
     *          避免线程安全问题（如数据竞争导致的计数错误）
     */
    std::atomic<int> total_connections{0};

    /**
     * @brief 总发送消息数（原子变量）
     * @details 统计所有线程中成功发送到服务器的消息总数，每个线程发送一条消息则累加1
     */
    std::atomic<int> total_sent{0};

    /**
     * @brief 总接收消息数（原子变量）
     * @details 统计所有线程中成功从服务器接收的回射消息总数，仅当接收数据与发送数据验证一致时才累加
     */
    std::atomic<int> total_received{0};

    /**
     * @brief 总错误数（原子变量）
     * @details 统计所有线程中发生的错误总数，包括连接失败、发送失败、接收失败、数据验证失败等场景
     */
    std::atomic<int> total_errors{0};

    /**
     * @brief 日志输出互斥锁
     * @details 保护标准输出流（cout/cerr），避免多线程同时输出日志导致的内容混杂（如A线程日志与B线程日志交错），
     *          确保每条日志的完整性和可读性
     */
    std::mutex cout_mutex;

    /**
     * @brief 单个连接的全流程处理函数（核心业务逻辑）
     * @details 每个连接线程会调用此函数，独立完成：创建TCP套接字→连接服务器→发送指定数量的测试消息→
     *          接收回射数据并验证→清理资源，同时更新对应的统计变量（如total_connections、total_sent）
     */
    void handle_connection();

    /**
     * @brief 线程安全的信息级日志输出函数
     * @details 输出客户端运行过程中的常规信息（如连接成功、测试完成），自动添加"[INFO]"标识，
     *          通过cout_mutex保证多线程环境下的输出原子性
     * @param msg 要输出的信息日志内容
     */
    void log_info(const std::string& msg);

    /**
     * @brief 线程安全的错误级日志输出函数
     * @details 输出客户端运行过程中的错误信息（如连接失败、数据不匹配），自动添加"[ERROR]"标识，
     *          使用std::cerr（无缓冲，立即输出），通过cout_mutex保证输出不混杂
     * @param msg 要输出的错误日志内容
     */
    void log_error(const std::string& msg);

public:
    /**
     * @brief 构造函数，初始化客户端配置
     * @param cfg 客户端测试配置结构体（包含服务器IP、端口、连接数等参数）
     * @details 从外部接收配置参数，初始化config成员变量，确保客户端按指定规则执行测试
     */
    EchoClient(const ClientConfig& cfg) : config(cfg) {}

    /**
     * @brief 客户端测试入口函数（总控制器）
     * @details 负责启动客户端测试的全流程：记录测试开始时间→创建指定数量的连接线程→控制连接创建速率→
     *          等待所有线程执行完成→计算测试耗时并输出统计结果（连接数、消息量、吞吐量等）
     */
    void run();
};

#endif  // ECHO_CLIENT_H  防止头文件被重复包含的宏定义结束