#include "echo_server.h"
#include "config.h"
#include "logger.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

EchoServer::EchoServer(uint16_t port, size_t thread_pool_size,
                       int monitor_interval)
    : port_(port), thread_pool_(std::make_unique<ThreadPool>(thread_pool_size)),
      monitor_(std::make_unique<PerformanceMonitor>(monitor_interval)) {}

EchoServer::~EchoServer() { stop(); }

int EchoServer::start() {
  listen_fd_ = init_listen_fd();
  if (listen_fd_ < 0) {
    LOG_ERROR("初始化监听fd失败");
    return RET_ERROR;
  }

  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  if (epoll_fd_ < 0) {
    LOG_ERROR("epoll_create失败: " + std::string(strerror(errno)));
    close(listen_fd_);
    return RET_ERROR;
  }

  if (add_epoll_event(listen_fd_, EPOLLIN) != RET_SUCCESS) {
    close(listen_fd_);
    close(epoll_fd_);
    return RET_ERROR;
  }

  monitor_->start();

  LOG_INFO("服务器启动成功，监听端口: " + std::to_string(port_));

  while (!stop_flag_) {
    handle_events();
  }

  return RET_SUCCESS;
}

void EchoServer::stop() {
  stop_flag_ = true;

  if (monitor_) {
    monitor_->stop();
  }

  if (listen_fd_ >= 0) {
    close(listen_fd_);
  }

  if (epoll_fd_ >= 0) {
    close(epoll_fd_);
  }

  if (thread_pool_) {
    thread_pool_->stop();
  }

  std::lock_guard<std::mutex> lock(conn_map_mutex_);
  for (auto &pair : conn_map_) {
    clear_conn_context(&pair.second);
    close(pair.first);
  }
  conn_map_.clear();

  LOG_INFO("服务器已停止");
}

int EchoServer::init_listen_fd() {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    LOG_ERROR("创建socket失败: " + std::string(strerror(errno)));
    return RET_ERROR;
  }

  int opt = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
                 sizeof(opt)) < 0) {
    LOG_ERROR("setsockopt失败: " + std::string(strerror(errno)));
    close(fd);
    return RET_ERROR;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);

  if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG_ERROR("bind失败: " + std::string(strerror(errno)));
    close(fd);
    return RET_ERROR;
  }

  if (listen(fd, SOMAXCONN) < 0) {
    LOG_ERROR("listen失败: " + std::string(strerror(errno)));
    close(fd);
    return RET_ERROR;
  }

  return fd;
}

int EchoServer::set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    LOG_ERROR("fcntl F_GETFL失败: " + std::string(strerror(errno)));
    return RET_ERROR;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    LOG_ERROR("fcntl F_SETFL失败: " + std::string(strerror(errno)));
    return RET_ERROR;
  }

  return RET_SUCCESS;
}

void EchoServer::handle_events() {
  struct epoll_event events[MAX_EVENTS];
  int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, 1000);

  if (n < 0) {
    if (errno == EINTR) {
      LOG_DEBUG("epoll_wait被信号中断");
      return;
    }
    LOG_ERROR("epoll_wait失败: " + std::string(strerror(errno)));
    return;
  }

  if (n == 0) {
    monitor_->set_connection_count(conn_map_.size());
    return;
  }

  for (int i = 0; i < n; ++i) {
    int fd = events[i].data.fd;
    uint32_t revents = events[i].events;

    if (fd == listen_fd_) {
      handle_accept();
    } else if (revents & (EPOLLIN | EPOLLPRI)) {
      // ===== 优化：仅查找conn时加锁，处理时释放 =====
      ConnContext *ctx = nullptr;
      {
        std::lock_guard<std::mutex> lock(conn_map_mutex_);
        auto it = conn_map_.find(fd);
        if (it != conn_map_.end())
          ctx = &it->second;
      }
      if (ctx)
        handle_read(fd, ctx); // 传递ctx，避免重复加锁
    } else if (revents & EPOLLOUT) {
      ConnContext *ctx = nullptr;
      {
        std::lock_guard<std::mutex> lock(conn_map_mutex_);
        auto it = conn_map_.find(fd);
        if (it != conn_map_.end())
          ctx = &it->second;
      }
      if (ctx)
        handle_write(fd, ctx);
    } else if (revents & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
      close_conn(fd);
    }
  }

  monitor_->set_connection_count(conn_map_.size());
}

void EchoServer::handle_accept() {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);

  while (true) {
    int conn_fd = accept4(listen_fd_, (struct sockaddr *)&client_addr,
                          &client_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (conn_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      LOG_ERROR("accept失败: " + std::string(strerror(errno)));
      break;
    }

    if (add_epoll_event(conn_fd, EPOLLIN | EPOLLRDHUP | EPOLLET) !=
        RET_SUCCESS) {
      close(conn_fd);
      continue;
    }

    {
      std::lock_guard<std::mutex> lock(conn_map_mutex_);
      conn_map_[conn_fd] = {conn_fd, std::vector<char>(), std::vector<char>(),
                            false};
    }

    LOG_INFO("新连接: " + std::string(inet_ntoa(client_addr.sin_addr)) + ":" +
             std::to_string(ntohs(client_addr.sin_port)));
  }
}

// ===== 修改：直接传递ctx，减小锁粒度 =====
void EchoServer::handle_read(int fd, ConnContext *ctx) {
  if (ctx->is_closed)
    return;

  char buf[BUFFER_SIZE];
  while (true) {
    ssize_t n = recv(fd, buf, sizeof(buf), 0);
    if (n > 0) {
      if (ctx->recv_buf.size() + n > 1024 * 1024) {
        LOG_ERROR("fd=" + std::to_string(fd) + " 接收缓冲区超限，关闭连接");
        close_conn(fd);
        return;
      }
      ctx->recv_buf.insert(ctx->recv_buf.end(), buf, buf + n);
      LOG_DEBUG("fd=" + std::to_string(fd) + " 接收数据: " + std::to_string(n) +
                "字节，缓冲区大小: " + std::to_string(ctx->recv_buf.size()));
    } else if (n == 0) {
      close_conn(fd);
      break;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        parse_message(ctx); // 解析消息（无锁）
        break;
      }
      LOG_ERROR("recv失败: fd=" + std::to_string(fd) +
                ", err=" + std::string(strerror(errno)));
      close_conn(fd);
      break;
    }
  }
}

// ===== 修改：直接传递ctx，减小锁粒度 =====
void EchoServer::handle_write(int fd, ConnContext *ctx) {
  if (ctx->is_closed || ctx->send_buf.empty())
    return;

  while (true) {
    ssize_t n = send(fd, ctx->send_buf.data(), ctx->send_buf.size(), 0);
    if (n > 0) {
      ctx->send_buf.erase(ctx->send_buf.begin(), ctx->send_buf.begin() + n);
      LOG_DEBUG("fd=" + std::to_string(fd) + " 发送数据: " + std::to_string(n) +
                "字节，剩余缓冲区: " + std::to_string(ctx->send_buf.size()));
    } else if (n == 0) {
      break;
    } else {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        mod_epoll_event(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
        break;
      }
      LOG_ERROR("send失败: fd=" + std::to_string(fd) +
                ", err=" + std::string(strerror(errno)));
      close_conn(fd);
      break;
    }
  }

  if (ctx->send_buf.empty()) {
    mod_epoll_event(fd, EPOLLIN | EPOLLRDHUP | EPOLLET);
    ctx->send_buf.shrink_to_fit();
  }
}

void EchoServer::parse_message(ConnContext *ctx) {
  while (ctx->recv_buf.size() >= sizeof(MessageHeader)) {
    MessageHeader header;
    memcpy(&header, ctx->recv_buf.data(), sizeof(MessageHeader));
    uint32_t msg_len = ntohl(header.msg_len);

    if (msg_len == 0 || msg_len > 1024 * 1024) {
      LOG_ERROR("非法消息长度: " + std::to_string(msg_len) +
                ", fd=" + std::to_string(ctx->fd));
      clear_conn_context(ctx);
      close_conn(ctx->fd);
      return;
    }

    size_t total_len = sizeof(MessageHeader) + msg_len;
    if (ctx->recv_buf.size() < total_len) {
      break; // 数据不足，等待下次读取
    }

    std::string msg_body(ctx->recv_buf.data() + sizeof(MessageHeader), msg_len);
    LOG_DEBUG("解析到完整消息: fd=" + std::to_string(ctx->fd) +
              ", msg_id=" + std::to_string(ntohl(header.msg_id)));

    // 立即清理已解析数据
    ctx->recv_buf.erase(ctx->recv_buf.begin(),
                        ctx->recv_buf.begin() + total_len);
    ctx->recv_buf.shrink_to_fit();

    // 提交任务到线程池（无锁）
    thread_pool_->submit(
        [this, msg_body, fd = ctx->fd]() { process_echo(msg_body, fd); });
  }
}

void EchoServer::process_echo(const std::string &msg, int fd) {
  std::string response = "Echo: " + msg;

  MessageHeader header;
  header.msg_len = htonl(response.size());
  header.msg_id = htonl(rand() % 10000);

  // 仅修改conn时加锁
  std::lock_guard<std::mutex> lock(conn_map_mutex_);
  auto it = conn_map_.find(fd);
  if (it == conn_map_.end() || it->second.is_closed) {
    return;
  }

  ConnContext *ctx = &it->second;
  if (ctx->send_buf.size() + sizeof(header) + response.size() > 1024 * 1024) {
    LOG_ERROR("fd=" + std::to_string(fd) + " 发送缓冲区超限，关闭连接");
    close_conn(fd);
    return;
  }

  ctx->send_buf.insert(ctx->send_buf.end(), (char *)&header,
                       (char *)&header + sizeof(MessageHeader));
  ctx->send_buf.insert(ctx->send_buf.end(), response.begin(), response.end());

  mod_epoll_event(fd, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
  monitor_->increment_message_count(); // 累计消息数
}

int EchoServer::add_epoll_event(int fd, uint32_t events) {
  struct epoll_event ev;
  ev.data.fd = fd;
  ev.events = events | EPOLLET;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) < 0) {
    LOG_ERROR("epoll_ctl ADD失败: fd=" + std::to_string(fd) +
              ", err=" + std::string(strerror(errno)));
    return RET_ERROR;
  }
  return RET_SUCCESS;
}

int EchoServer::mod_epoll_event(int fd, uint32_t events) {
  struct epoll_event ev;
  ev.data.fd = fd;
  ev.events = events | EPOLLET;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) < 0) {
    LOG_ERROR("epoll_ctl MOD失败: fd=" + std::to_string(fd) +
              ", err=" + std::string(strerror(errno)));
    return RET_ERROR;
  }
  return RET_SUCCESS;
}

int EchoServer::del_epoll_event(int fd) {
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) < 0) {
    LOG_ERROR("epoll_ctl DEL失败: fd=" + std::to_string(fd) +
              ", err=" + std::string(strerror(errno)));
    return RET_ERROR;
  }
  return RET_SUCCESS;
}

void EchoServer::clear_conn_context(ConnContext *ctx) {
  if (ctx) {
    ctx->recv_buf.clear();
    ctx->recv_buf.shrink_to_fit();
    ctx->send_buf.clear();
    ctx->send_buf.shrink_to_fit();
  }
}

void EchoServer::close_conn(int fd) {
  LOG_INFO("关闭连接: fd=" + std::to_string(fd));

  std::lock_guard<std::mutex> lock(conn_map_mutex_);
  auto it = conn_map_.find(fd);
  if (it != conn_map_.end()) {
    clear_conn_context(&it->second);
  }

  del_epoll_event(fd);
  close(fd);
  conn_map_.erase(fd);
}