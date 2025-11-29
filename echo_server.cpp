// 引入Echo服务器类定义头文件
#include "echo_server.h"
// 引入公共模块头文件（包含全局统计变量、日志函数、常量定义等）
#include "common.h"
// 引入服务器性能监控类头文件（用于统计服务器CPU/内存/消息处理量）
#include "server_performance_monitor.h"
// 引入IP地址转换函数库（inet_ntop等）
#include <arpa/inet.h>
// 引入时间库（用于计时、性能监控日志间隔控制）
#include <chrono>
// 引入字符串操作库（memset等）
#include <cstring>
// 引入错误码定义（errno相关）
#include <errno.h>
// 引入文件控制库（fcntl设置非阻塞模式）
#include <fcntl.h>
// 引入输入输出流库（用于控制台输出）
#include <iostream>
// 引入IPv4地址结构定义（sockaddr_in）
#include <netinet/in.h>
// 引入TCP协议选项定义（TCP_NODELAY）
#include <netinet/tcp.h>
// 引入异常类库（std::runtime_error）
#include <stdexcept>
// 引入epoll库（用于IO多路复用）
#include <sys/epoll.h>
// 引入Socket编程库（socket/bind/listen/accept等）
#include <sys/socket.h>
// 引入线程库（用于线程休眠）
#include <thread>
// 引入系统调用库（close/usleep等）
#include <unistd.h>
// 引入向量容器库（用于缓冲区、事件数组等）
#include <vector>

/**
 * @brief EchoServer构造函数：初始化服务器端口、epoll文件描述符和监听文件描述符
 * @param port 服务器监听端口
 * @note
 * 初始化成员变量：port为传入的监听端口，epoll_fd和listen_fd初始化为-1（表示未创建）
 */
EchoServer::EchoServer(int port) : port(port), epoll_fd(-1), listen_fd(-1) {}

/**
 * @brief EchoServer析构函数：释放服务器资源
 * @note 关闭epoll实例和监听socket，避免资源泄漏
 */
EchoServer::~EchoServer() {
  if (epoll_fd != -1)
    close(epoll_fd); // 关闭epoll文件描述符
  if (listen_fd != -1)
    close(listen_fd); // 关闭监听socket文件描述符
}

/**
 * 设置文件描述符为非阻塞模式
 * @param fd 目标文件描述符
 * @note 通过fcntl修改文件描述符的标志位，添加O_NONBLOCK属性，用于实现非阻塞IO
 */
void EchoServer::set_nonblocking(int fd) {
  // 获取文件描述符当前的标志位
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1)
    // 获取失败时抛出异常，携带错误信息
    throw std::runtime_error("fcntl F_GETFL failed: " +
                             std::string(strerror(errno)));
  // 设置文件描述符为非阻塞模式（添加O_NONBLOCK标志）
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    throw std::runtime_error("fcntl F_SETFL failed: " +
                             std::string(strerror(errno)));
  }
}

/**
 * 初始化服务器：创建监听socket、epoll实例，完成端口绑定和监听
 * @note 包含socket创建、选项设置、地址绑定、监听启动、epoll实例创建及监听fd注册
 */
void EchoServer::init() {
  // 创建非阻塞TCP监听socket（AF_INET：IPv4，SOCK_STREAM：TCP，SOCK_NONBLOCK：非阻塞）
  listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if (listen_fd == -1)
    throw std::runtime_error("socket creation failed: " +
                             std::string(strerror(errno)));

  // 设置SO_REUSEADDR选项：允许端口释放后立即被重用，避免TIME_WAIT状态导致的端口占用
  int opt = 1;
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) ==
      -1) {
    throw std::runtime_error("setsockopt SO_REUSEADDR failed: " +
                             std::string(strerror(errno)));
  }
  // 设置SO_REUSEPORT选项：允许多个进程/线程绑定到同一端口（提升并发）
  if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) ==
      -1) {
    throw std::runtime_error("setsockopt SO_REUSEPORT failed: " +
                             std::string(strerror(errno)));
  }

  // 初始化服务器地址结构
  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr)); // 内存清零
  server_addr.sin_family = AF_INET;             // IPv4协议族
  server_addr.sin_addr.s_addr =
      INADDR_ANY; // 绑定到所有网卡（接收任意IP的连接）
  server_addr.sin_port = htons(port); // 端口号转为网络字节序

  // 将监听socket绑定到指定端口和地址
  if (bind(listen_fd, reinterpret_cast<struct sockaddr *>(&server_addr),
           sizeof(server_addr)) == -1) {
    throw std::runtime_error("bind failed: " + std::string(strerror(errno)));
  }

  // 启动监听（监听队列大小1024，即最多同时处理1024个待连接请求）
  if (listen(listen_fd, 1024) == -1)
    throw std::runtime_error("listen failed: " + std::string(strerror(errno)));

  // 创建epoll实例（EPOLL_CLOEXEC：进程执行exec时自动关闭epoll_fd，避免泄漏）
  epoll_fd = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd == -1)
    throw std::runtime_error("epoll_create1 failed: " +
                             std::string(strerror(errno)));

  // 注册监听fd到epoll实例，监听读事件（EPOLLIN）和连接关闭事件（EPOLLRDHUP）
  struct epoll_event ev;
  ev.events = EPOLLIN | EPOLLRDHUP; // 关注的事件类型
  ev.data.fd = listen_fd;           // 关联的文件描述符为监听fd
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) {
    throw std::runtime_error("epoll_ctl add listen_fd failed: " +
                             std::string(strerror(errno)));
  }

  // 打印服务器初始化成功日志
  log_info("Server started successfully");
  log_info("Listening on port: " + std::to_string(port));
  log_info("Epoll instance created, waiting for connections...");
}

/**
 * 处理新的客户端连接
 * 循环accept所有待处理的连接（非阻塞模式）
 * @note 非阻塞模式下需循环accept直到返回EAGAIN/EWOULDBLOCK，避免遗漏连接；
 *       每个新连接设置为非阻塞，注册到epoll并更新活跃连接数
 */
void EchoServer::handle_new_connection() {
  struct sockaddr_in client_addr;             // 客户端地址结构
  socklen_t client_len = sizeof(client_addr); // 地址结构长度
  int conn_fd;                                // 新连接的文件描述符

  // 循环接受所有待处理的连接（非阻塞accept，直到无新连接）
  while ((conn_fd = accept4(listen_fd,
                            reinterpret_cast<struct sockaddr *>(&client_addr),
                            &client_len, SOCK_NONBLOCK)) != -1) {
    // 将客户端IP地址转为字符串格式
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);

    // 设置TCP_NODELAY选项：禁用Nagle算法，减少小数据包的传输延迟
    int opt = 1;
    setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // 打印新连接日志（客户端IP、端口、连接fd）
    log_info("New connection from " + std::string(client_ip) + ":" +
             std::to_string(ntohs(client_addr.sin_port)) +
             " (fd: " + std::to_string(conn_fd) + ")");
    g_active_connections++; // 增加全局活跃连接数统计

    // 注册新连接fd到epoll：关注读事件、连接关闭事件，使用EPOLLONESHOT避免多线程同时处理
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.fd = conn_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, conn_fd, &ev) == -1) {
      log_error("epoll_ctl add conn_fd failed (fd: " + std::to_string(conn_fd) +
                "): " + std::string(strerror(errno)));
      close(conn_fd);         // 注册失败则关闭连接
      g_active_connections--; // 回滚活跃连接数
      continue;
    }
  }

  // 处理accept错误：EAGAIN/EWOULDBLOCK表示无新连接（正常情况），其他错误需记录
  if (errno != EAGAIN && errno != EWOULDBLOCK) {
    log_error("accept failed: " + std::string(strerror(errno)));
  }
}

/**
 * 处理客户端数据（读取并回射）
 * @param conn_fd 客户端连接fd
 * @note
 * 非阻塞读取客户端数据，读取后原样回射；处理连接关闭、读取错误等情况，更新全局统计
 */
void EchoServer::handle_client_data(int conn_fd) {
  std::vector<char> buffer(8192); // 8KB大小的缓冲区，用于存储读取的数据
  ssize_t bytes_read;             // 实际读取的字节数

  // 循环读取所有可用数据（非阻塞模式，直到无数据可读）
  while ((bytes_read = read(conn_fd, buffer.data(), buffer.size())) > 0) {
    g_total_processed_msgs++; // 增加全局已处理消息数统计
    log_info("Read " + std::to_string(bytes_read) +
             " bytes from fd: " + std::to_string(conn_fd));

    // 原样回射数据给客户端（循环发送确保所有数据发送完成）
    ssize_t bytes_sent = 0;
    while (bytes_sent < bytes_read) {
      ssize_t ret =
          write(conn_fd, buffer.data() + bytes_sent, bytes_read - bytes_sent);
      if (ret == -1) {
        // 发送缓冲区满（EAGAIN/EWOULDBLOCK），休眠50微秒后重试
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          std::this_thread::sleep_for(std::chrono::microseconds(50));
          continue;
        }
        // 其他发送错误，关闭连接并更新活跃连接数
        log_error("Write failed (fd: " + std::to_string(conn_fd) +
                  "): " + std::string(strerror(errno)));
        close(conn_fd);
        g_active_connections--;
        return;
      }
      bytes_sent += ret; // 累加已发送字节数
      log_info("Wrote " + std::to_string(ret) +
               " bytes to fd: " + std::to_string(conn_fd));
    }
  }

  // 处理读取结果
  if (bytes_read == 0) {
    // bytes_read为0表示客户端主动关闭连接
    log_info("Client disconnected (fd: " + std::to_string(conn_fd) + ")");
    close(conn_fd);
    g_active_connections--; // 减少活跃连接数
  } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
    // 读取错误（非缓冲区空），关闭连接并更新统计
    log_error("Read failed (fd: " + std::to_string(conn_fd) +
              "): " + std::string(strerror(errno)));
    close(conn_fd);
    g_active_connections--;
  } else {
    // 缓冲区空（EAGAIN/EWOULDBLOCK），重新注册EPOLLIN事件（因EPOLLONESHOT需重新注册）
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
    ev.data.fd = conn_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, conn_fd, &ev);
  }
}

/**
 * 服务器主循环（epoll事件处理）
 * @note
 * 循环等待epoll事件，处理新连接、客户端数据、连接关闭等事件；定期打印性能监控日志
 */
void EchoServer::run() {
  const int MAX_EVENTS = 1024; // 每次epoll_wait最多处理1024个事件
  struct epoll_event events[MAX_EVENTS]; // 存储就绪事件的数组

  // 初始化服务器性能监控器（传入全局统计变量，用于监控总消息数和活跃连接数）
  ServerPerformanceMonitor monitor(g_total_processed_msgs,
                                   g_active_connections);
  const std::chrono::seconds log_interval(10); // 性能日志打印间隔（10秒）
  auto last_log_time =
      std::chrono::high_resolution_clock::now(); // 上一次打印日志的时间

  while (true) { // 服务器主循环（一直运行直到异常退出）
    // 等待epoll事件（超时时间1000ms，用于定期触发性能日志打印）
    int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
    if (nfds == -1) {
      // 忽略中断信号（如Ctrl+C导致的EINTR），继续循环
      if (errno == EINTR)
        continue;
      // 其他错误抛出异常
      throw std::runtime_error("epoll_wait failed: " +
                               std::string(strerror(errno)));
    }

    // 定期打印性能监控日志（每10秒一次）
    auto now = std::chrono::high_resolution_clock::now();
    if (now - last_log_time >= log_interval) {
      monitor.log_server_stats(); // 打印服务器性能统计
      last_log_time = now;        // 更新最后日志时间
    }

    // 遍历所有就绪的epoll事件并处理
    for (int i = 0; i < nfds; ++i) {
      if (events[i].data.fd == listen_fd) {
        // 监听fd就绪：处理新客户端连接
        handle_new_connection();
      } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
        // 连接关闭（EPOLLRDHUP）、挂起（EPOLLHUP）或错误（EPOLLERR）：关闭连接
        log_info("Connection closed/error (fd: " +
                 std::to_string(events[i].data.fd) + ")");
        close(events[i].data.fd);
        g_active_connections--; // 减少活跃连接数
      } else if (events[i].events & EPOLLIN) {
        // 客户端fd就绪（有数据可读）：处理客户端数据
        handle_client_data(events[i].data.fd);
      }
    }
  }
}