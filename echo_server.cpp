#include "echo_server.h"
#include "common.h"
#include "server_performance_monitor.h"
#include <arpa/inet.h>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

EchoServer::EchoServer(int port) : port(port), epoll_fd(-1), listen_fd(-1) {}

EchoServer::~EchoServer() {
  if (epoll_fd != -1)
    close(epoll_fd);
  if (listen_fd != -1)
    close(listen_fd);
}

/**
 * 设置文件描述符为非阻塞模式
 * @param fd 目标文件描述符
 */
void EchoServer::set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    throw std::runtime_error("fcntl F_GETFL failed: " +
                             std::string(strerror(errno)));
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    throw std::runtime_error("fcntl F_SETFL failed: " +
                             std::string(strerror(errno)));
  }
}

/**
 * 初始化服务器：创建监听socket、epoll实例
 */
void EchoServer::init() {
  // 创建非阻塞监听socket
  listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listen_fd == -1)
    throw std::runtime_error("socket creation failed: " +
                             std::string(strerror(errno)));

  // 设置SO_REUSEADDR和SO_REUSEPORT，避免端口占用问题
  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    throw std::runtime_error("setsockopt SO_REUSEADDR failed: " +
                             std::string(strerror(errno)));
  }
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) ==
      -1) {
    throw std::runtime_error("setsockopt SO_REUSEPORT failed: " +
                             std::string(strerror(errno)));
  }

  // 绑定端口到所有网卡
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(port);
  if (bind(listen_fd, reinterpret_cast<struct sockaddr *>(&server_addr),
           sizeof(server_addr)) == -1) {
    throw std::runtime_error("bind failed: " + std::string(strerror(errno)));
  }

  // 开始监听（监听队列大小1024）
  if (listen(listen_fd, 1024) == -1)
    throw std::runtime_error("listen failed: " + std::string(strerror(errno)));

  // 创建epoll实例（EPOLL_CLOEXEC保证进程退出时自动关闭）
  epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd == -1)
    throw std::runtime_error("epoll_create1 failed: " +
                             std::string(strerror(errno)));

  // 注册监听fd到epoll（水平触发，检测读事件和连接关闭）
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP;
  ev.data.fd = listen_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
    throw std::runtime_error("epoll_ctl add listen_fd failed: " +
                             std::string(strerror(errno)));
  }

  log_info("Server started successfully");
  log_info("Listening on port: " + std::to_string(port));
  log_info("Epoll instance created, waiting for connections...");
}

/**
 * 处理新的客户端连接
 * 循环accept所有待处理的连接（非阻塞模式）
 */
void EchoServer::handle_new_connection() {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int conn_fd;

  // 循环接受所有待处理连接（避免遗漏）
  while ((conn_fd = accept4(listen_fd,
                            reinterpret_cast<struct sockaddr *>(&client_addr),
                            &client_len, SOCK_NONBLOCK)) != -1) {
    // 获取客户端IP地址
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    // 设置TCP_NODELAY，禁用Nagle算法（减少延迟）
    int opt = 1;
    setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    log_info("New connection from " + std::string(client_ip) + ":" +
             std::to_string(ntohs(client_addr.sin_port)) +
             " (fd: " + std::to_string(conn_fd) + ")");
    g_active_connections++; // 增加活跃连接数

    // 注册连接fd到epoll（EPOLLONESHOT避免多线程同时处理）
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.fd = conn_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
      log_error("epoll_ctl add conn_fd failed (fd: " + std::to_string(conn_fd) +
                "): " + std::string(strerror(errno)));
      close(conn_fd);
      g_active_connections--; // 回滚活跃连接数
      continue;
    }
  }

  // 处理accept错误（EAGAIN/EWOULDBLOCK是正常的，无待处理连接）
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    log_error("accept failed: " + std::string(strerror(errno)));
  }
}

/**
 * 处理客户端数据（读取并回射）
 * @param conn_fd 客户端连接fd
 */
void EchoServer::handle_client_data(int conn_fd) {
  std::vector<char> buffer(8192); // 8KB缓冲区
  ssize_t bytes_read;

  // 循环读取所有可用数据（非阻塞模式）
  while ((bytes_read = read(conn_fd, buffer.data(), buffer.size())) > 0) {
    g_total_processed_msgs++; // 增加总处理消息数
    log_info("Read " + std::to_string(bytes_read) +
             " bytes from fd: " + std::to_string(conn_fd));

    // 原样回射数据（带重试处理）
    ssize_t bytes_sent = 0;
    while (bytes_sent < bytes_read) {
      ssize_t ret =
          write(conn_fd, buffer.data() + bytes_sent, bytes_read - bytes_sent);
      if (ret == -1) {
        // 发送缓冲区满，等待后重试
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          std::this_thread::sleep_for(std::chrono::microseconds(50));
          continue;
        }
        // 其他错误，关闭连接
        log_error("Write failed (fd: " + std::to_string(conn_fd) +
                  "): " + std::string(strerror(errno)));
        close(conn_fd);
        g_active_connections--; // 减少活跃连接数
        return;
      }
      bytes_sent += ret;
      log_info("Wrote " + std::to_string(ret) +
               " bytes to fd: " + std::to_string(conn_fd));
    }
  }

  // 处理读取结果
  if (bytes_read == 0) {
    // 客户端主动断开连接
    log_info("Client disconnected (fd: " + std::to_string(conn_fd) + ")");
    close(conn_fd);
    g_active_connections--;
  } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
    // 读取错误（非缓冲区空）
    log_error("Read failed (fd: " + std::to_string(conn_fd) +
              "): " + std::string(strerror(errno)));
    close(conn_fd);
    g_active_connections--;
  } else {
    // 缓冲区空，重新注册EPOLLIN事件（EPOLLONESHOT需要重新注册）
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.fd = conn_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn_fd, &ev);
  }
}

/**
 * 服务器主循环（epoll事件处理）
 */
void EchoServer::run() {
  const int MAX_EVENTS = 1024; // 每次处理最多1024个事件
  struct epoll_event events[MAX_EVENTS];

  // 初始化性能监控器（每10秒打印一次）
  ServerPerformanceMonitor monitor(g_total_processed_msgs,
                                   g_active_connections);
  const std::chrono::seconds log_interval(10);
  auto last_log_time = std::chrono::high_resolution_clock::now();

  while (true) {
    // 等待epoll事件（超时1秒，用于定期打印监控）
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
    if (nfds == -1) {
      // 忽略中断信号（如SIGINT）
      if (errno == EINTR)
        continue;
      throw std::runtime_error("epoll_wait failed: " +
                               std::string(strerror(errno)));
    }

    // 定期打印性能监控日志
    auto now = std::chrono::high_resolution_clock::now();
    if (now - last_log_time >= log_interval) {
      monitor.log_server_stats();
      last_log_time = now;
    }

    // 处理所有就绪事件
    for (int i = 0; i < nfds; ++i) {
      if (events[i].data.fd == listen_fd) {
        // 监听fd有事件：新连接
        handle_new_connection();
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // 连接关闭或错误
        log_info("Connection closed/error (fd: " +
                 std::to_string(events[i].data.fd) + ")");
        close(events[i].data.fd);
        g_active_connections--;
      } else if (events[i].events & EPOLLIN) {
        // 客户端fd有数据可读
        handle_client_data(events[i].data.fd);
      }
    }
  }
}