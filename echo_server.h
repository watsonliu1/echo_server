#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

#include "common.h"
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unordered_map>
#include <mutex>
#include <string>

class EchoServer
{
private:
    int epoll_fd;
    int listen_fd;
    std::unordered_map<int, char*> client_buffers;  // 客户端缓冲区映射
    std::mutex buffer_mutex;                        // 缓冲区互斥锁

    // 设置文件描述符为非阻塞模式
    bool set_non_blocking(int fd);

    // 添加文件描述符到epoll
    void add_to_epoll(int fd, uint32_t events);

    // 从epoll移除文件描述符
    void remove_from_epoll(int fd);

    // 处理新连接
    void handle_new_connection();

    // 处理客户端数据
    void handle_client_data(int client_fd);

    // 关闭客户端连接
    void close_client(int client_fd);

public:
    EchoServer() : epoll_fd(-1), listen_fd(-1) {}
    ~EchoServer() { shutdown(); }

    // 初始化服务器
    bool init(int port = DEFAULT_PORT);

    // 运行服务器事件循环
    void run();

    // 关闭服务器
    void shutdown();
};

#endif