#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

#include "common.h"
#include <unordered_map>
#include <mutex>
#include <memory>
#include <sys/epoll.h>

class EchoServer {
private:
    int server_fd;                  // 监听套接字
    int epoll_fd;                   // epoll句柄
    int port;                       // 端口
    bool running;                   // 运行状态
    std::unordered_map<int, std::unique_ptr<char[]>> client_buffers;  // 客户端缓冲区
    std::mutex buffer_mutex;        // 保护缓冲区映射的锁
    std::unordered_map<int, std::mutex> fd_mutexes;  // 每个FD独立互斥锁（关键）
    std::mutex fd_mutex_map_mutex;  // 保护fd_mutexes的锁

    // 设置非阻塞
    int set_nonblocking(int fd);
    // 关闭客户端连接
    void close_client(int client_fd);
    // 获取FD对应的互斥锁（确保同一FD串行处理）
    std::mutex& get_fd_mutex(int client_fd);

public:
    EchoServer(int port);
    ~EchoServer();
    void start();
    void handle_client_data(int client_fd);  // 处理客户端数据
};

#endif // ECHO_SERVER_H