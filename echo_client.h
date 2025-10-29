#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H

#include "common.h"
#include <thread>
#include <vector>
#include <atomic>
#include <mutex>
#include <string>

class EchoClient
{
private:
    ClientConfig config;
    std::atomic<int> total_connections{0};   // 成功建立的连接数
    std::atomic<int> total_sent{0};          // 总发送消息数
    std::atomic<int> total_received{0};      // 总接收消息数
    std::atomic<int> total_errors{0};        // 总错误数
    std::mutex cout_mutex;                   // 输出互斥锁

    // 单个连接的处理函数
    void handle_connection();

    // 线程安全的日志输出
    void log_info(const std::string& msg);

    // 线程安全的错误输出
    void log_error(const std::string& msg);

public:
    EchoClient(const ClientConfig& cfg) : config(cfg) {}

    // 运行客户端测试
    void run();
};

#endif