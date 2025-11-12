#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

#include "common.h"               // 包含公共常量、结构体和日志函数
#include <unordered_map>          // 用于存储客户端缓冲区和互斥锁的映射
#include <mutex>                  // 用于线程同步
#include <memory>                 // 用于智能指针（管理动态内存）
#include <sys/epoll.h>            // 用于epoll事件驱动机制

// 回声服务器类：实现基于epoll的高性能TCP服务器，接收客户端消息并回射
class EchoServer {
private:
    int server_fd;                  // 监听套接字文件描述符（用于接受新连接）
    int epoll_fd;                   // epoll句柄（用于管理IO事件）
    int port;                       // 服务器监听端口
    bool running;                   // 服务器运行状态标志（控制事件循环）
    // 客户端缓冲区映射：key为客户端fd，value为该客户端的读写缓冲区（智能指针管理）
    std::unordered_map<int, std::unique_ptr<char[]>> client_buffers;
    std::mutex buffer_mutex;        // 保护client_buffers的互斥锁（多线程安全）
    // 客户端fd对应的互斥锁：确保同一客户端的IO操作串行化（避免数据竞争）
    std::unordered_map<int, std::mutex> fd_mutexes;
    std::mutex fd_mutex_map_mutex;  // 保护fd_mutexes的互斥锁（多线程安全访问映射）

    // 设置文件描述符为非阻塞模式
    // 参数：fd - 目标文件描述符
    // 返回：0成功，-1失败
    int set_nonblocking(int fd);

    // 关闭客户端连接并清理相关资源
    // 参数：client_fd - 客户端文件描述符
    void close_client(int client_fd);

    // 获取客户端fd对应的互斥锁（不存在则创建）
    // 参数：client_fd - 客户端文件描述符
    // 返回：该fd对应的互斥锁引用
    std::mutex& get_fd_mutex(int client_fd);

public:
    // 构造函数：初始化服务器端口
    // 参数：port - 监听端口
    EchoServer(int port);

    // 析构函数：停止服务器并释放资源
    ~EchoServer();

    // 启动服务器：初始化套接字、epoll，进入事件循环
    void start();

    // 处理客户端数据：读取、校验、回射消息
    // 参数：client_fd - 客户端文件描述符
    void handle_client_data(int client_fd);
};

#endif // ECHO_SERVER_H