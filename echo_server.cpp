#include "echo_server.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>

bool EchoServer::set_non_blocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

void EchoServer::add_to_epoll(int fd, uint32_t events)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl add failed");
    }
}

void EchoServer::remove_from_epoll(int fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl del failed");
    }
}

// 处理新连接
void EchoServer::handle_new_connection()
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    
    if (client_fd == -1)
    {
        perror("accept failed");
        return;
    }

    // 设置客户端连接为非阻塞
    if (!set_non_blocking(client_fd)) {
        perror("set non-blocking failed");
        close(client_fd);
        return;
    }

    // 为新客户端分配缓冲区
    char* buffer = new char[BUFFER_SIZE];
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        client_buffers[client_fd] = buffer;
    }

    // 将新客户端添加到epoll，监听读事件
    add_to_epoll(client_fd, EPOLLIN | EPOLLET);  // 边缘触发模式
    std::cout << "New connection from " << inet_ntoa(client_addr.sin_addr) 
              << ", fd: " << client_fd << std::endl;
}

// 处理客户消息
void EchoServer::handle_client_data(int client_fd)
{
    char* buffer;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        auto it = client_buffers.find(client_fd);
        if (it == client_buffers.end()) {
            return;  // 未找到客户端缓冲区，忽略
        }
        buffer = it->second;
    }

    ssize_t bytes_read;
    while (true) {
        bytes_read = read(client_fd, buffer, BUFFER_SIZE);
        
        if (bytes_read > 0) {
            // 回射数据给客户端
            ssize_t bytes_written = write(client_fd, buffer, bytes_read);
            if (bytes_written == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("write failed");
                close_client(client_fd);
                return;
            }
        } 
        else if (bytes_read == 0) {
            // 客户端关闭连接
            std::cout << "Client " << client_fd << " disconnected" << std::endl;
            close_client(client_fd);
            return;
        } 
        else {
            // 读取错误处理
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("read failed");
                close_client(client_fd);
            }
            break;  // 非阻塞模式下无数据可读
        }
    }
}

// 关闭客户连接
void EchoServer::close_client(int client_fd) {
    remove_from_epoll(client_fd);
    close(client_fd);
    
    std::lock_guard<std::mutex> lock(buffer_mutex);
    auto it = client_buffers.find(client_fd);
    if (it != client_buffers.end()) {
        delete[] it->second;
        client_buffers.erase(it);
    }
}

// 1. 服务器启动前的准备
bool EchoServer::init(int port)
{
    // 创建监听套接字
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        perror("socket creation failed");
        return false;
    }

    // 设置端口复用
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        close(listen_fd);
        return false;
    }

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");
        close(listen_fd);
        return false;
    }

    // 开始监听
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen failed");
        close(listen_fd);
        return false;
    }

    // 设置监听套接字为非阻塞
    if (!set_non_blocking(listen_fd)) {
        perror("set non-blocking failed");
        close(listen_fd);
        return false;
    }

    // 创建epoll实例
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create failed");
        close(listen_fd);
        return false;
    }

    // 将监听套接字添加到epoll
    add_to_epoll(listen_fd, EPOLLIN | EPOLLET);  // 边缘触发模式

    std::cout << "Echo server initialized on port " << port << std::endl;
    return true;
}

// 服务器开始工作
void EchoServer::run() {
    if (epoll_fd == -1 || listen_fd == -1) {
        std::cerr << "Server not initialized" << std::endl;
        return;
    }

    struct epoll_event events[MAX_EVENTS];
    
    while (true) {
        // 等待事件发生
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events == -1) {
            perror("epoll_wait failed");
            break;
        }

        // 处理所有就绪事件
        for (int i = 0; i < num_events; ++i) {
            if (events[i].data.fd == listen_fd) {
                // 处理新连接
                handle_new_connection();
            } else if (events[i].events & EPOLLIN) {
                // 处理客户端读事件
                handle_client_data(events[i].data.fd);
            } else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                // 处理错误或断开事件
                std::cerr << "Client " << events[i].data.fd << " error/hangup" << std::endl;
                close_client(events[i].data.fd);
            }
        }
    }
}

void EchoServer::shutdown() {
    if (epoll_fd != -1) {
        close(epoll_fd);
        epoll_fd = -1;
    }
    if (listen_fd != -1) {
        close(listen_fd);
        listen_fd = -1;
    }
    
    // 清理所有客户端缓冲区
    std::lock_guard<std::mutex> lock(buffer_mutex);
    for (auto& pair : client_buffers) {
        close(pair.first);
        delete[] pair.second;
    }
    client_buffers.clear();
}