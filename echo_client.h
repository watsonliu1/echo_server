#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H

#include "common.h"       // 包含公共常量、配置结构体和日志函数
#include <thread>         // 用于多线程
#include <atomic>         // 用于原子变量（多线程安全的计数器）

// 回射客户端类：实现多线程客户端，向服务器发送消息并验证回射
class EchoClient
{
private:
    ClientConfig config;  // 客户端配置（从命令行参数解析）

    // 静态原子变量：用于多线程统计（原子操作确保线程安全）
    static std::atomic<int> total_connections;  // 总连接数
    static std::atomic<int> total_sent;         // 总发送消息数
    static std::atomic<int> total_received;     // 总接收（验证成功）消息数
    static std::atomic<int> total_errors;       // 总错误数

    // 获取当前线程ID的后3位（用于日志区分线程）
    // 返回：线程ID的字符串表示（后3位）
    std::string get_thread_id();

    // 处理正常连接：同步发送消息（每条消息等待回射后再发下一条）
    void handle_normal_connection();

public:
    // 构造函数：初始化客户端配置
    // 参数：cfg - 客户端配置
    EchoClient(const ClientConfig& cfg);

    // 析构函数：默认（无需额外资源释放）
    ~EchoClient() = default;

    // 运行客户端：根据配置创建线程并执行
    void run();

    // 打印统计信息（总连接、发送、接收、错误数）
    void print_stats();
};

#endif // ECHO_CLIENT_H