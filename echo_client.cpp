#include "echo_client.h"
#include "logger.h"
#include <arpa/inet.h>
#include <cstdlib> // 用于 rand()
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

EchoClient::EchoClient(const std::string &server_ip, uint16_t server_port,
                       int concurrent, size_t msg_size, int test_duration)
    : server_ip_(server_ip), server_port_(server_port), concurrent_(concurrent),
      msg_size_(msg_size), test_duration_(test_duration) {}

EchoClient::~EchoClient() { stop(); }

void EchoClient::start() {
  stop_flag_ = false;
  start_time_ = std::chrono::system_clock::now();

  for (int i = 0; i < concurrent_; ++i) {
    worker_threads_.emplace_back(&EchoClient::worker_thread, this, i);
  }

  LOG_INFO("客户端启动，并发连接数: " + std::to_string(concurrent_) +
           ", 测试时长: " + std::to_string(test_duration_) + "秒");

  // 等待测试结束
  std::this_thread::sleep_for(std::chrono::seconds(test_duration_));
  stop();

  // 输出统计结果
  double elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now() - start_time_)
                       .count();

  LOG_INFO("测试结果: "
           "发送消息=" +
           std::to_string(stats_.sent) +
           ", "
           "接收消息=" +
           std::to_string(stats_.received) +
           ", "
           "错误数=" +
           std::to_string(stats_.errors) +
           ", "
           "发送TPS=" +
           std::to_string(elapsed > 0 ? stats_.sent / elapsed : 0) +
           ", "
           "接收TPS=" +
           std::to_string(elapsed > 0 ? stats_.received / elapsed : 0));
}

void EchoClient::stop() {
  stop_flag_ = true;

  for (auto &thread : worker_threads_) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  worker_threads_.clear();
  LOG_INFO("客户端已停止");
}

void EchoClient::worker_thread(int thread_id) {
  int fd = connect_to_server(thread_id);
  if (fd < 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) + "连接服务器失败");
    stats_.errors++;
    return;
  }

  // 构造测试消息
  std::string msg(msg_size_, 'A');

  while (!stop_flag_) {
    if (!send_message(fd, msg)) {
      stats_.errors++;
      break;
    }
    stats_.sent++;

    if (!recv_message(fd)) {
      stats_.errors++;
      break;
    }
    stats_.received++;

    // 轻微延迟，避免压垮服务器
    std::this_thread::sleep_for(std::chrono::microseconds(10));
  }

  close(fd);
  LOG_DEBUG("线程" + std::to_string(thread_id) + "退出");
}

int EchoClient::connect_to_server(int thread_id) {
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (fd < 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "创建socket失败: " + std::string(strerror(errno)));
    return RET_ERROR;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(server_port_);

  if (inet_pton(AF_INET, server_ip_.c_str(), &server_addr.sin_addr) <= 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "无效的服务器IP: " + server_ip_);
    close(fd);
    return RET_ERROR;
  }

  // 非阻塞connect
  int ret = connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
  if (ret < 0 && errno != EINPROGRESS) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "connect失败: " + std::string(strerror(errno)));
    close(fd);
    return RET_ERROR;
  }

  // 等待连接完成
  fd_set write_fds;
  FD_ZERO(&write_fds);
  FD_SET(fd, &write_fds);

  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;

  if (select(fd + 1, nullptr, &write_fds, nullptr, &timeout) <= 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) + "connect超时");
    close(fd);
    return RET_ERROR;
  }

  int error = 0;
  socklen_t len = sizeof(error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "连接失败: " + std::string(strerror(error)));
    close(fd);
    return RET_ERROR;
  }

  LOG_DEBUG("线程" + std::to_string(thread_id) + "连接成功");
  return fd;
}

int EchoClient::set_nonblock(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return RET_ERROR;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return RET_ERROR;
  }

  return RET_SUCCESS;
}

bool EchoClient::send_message(int fd, const std::string &msg) {
  MessageHeader header;
  header.msg_len = htonl(msg.size());
  header.msg_id = htonl(rand() % 10000);

  // 发送消息头（循环处理非阻塞）
  size_t total_sent = 0;
  const char *header_ptr = reinterpret_cast<const char *>(&header);
  size_t header_len = sizeof(MessageHeader);

  while (total_sent < header_len) {
    ssize_t n = send(fd, header_ptr + total_sent, header_len - total_sent, 0);
    if (n > 0) {
      total_sent += n;
    } else if (n == 0) {
      LOG_ERROR("发送消息头时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("发送消息头失败: " + std::string(strerror(errno)));
        return false;
      }
      // 等待可写
      fd_set write_fds;
      FD_ZERO(&write_fds);
      FD_SET(fd, &write_fds);
      struct timeval timeout = {2, 0}; // 2秒超时
      int ret = select(fd + 1, nullptr, &write_fds, nullptr, &timeout);
      if (ret <= 0) {
        LOG_ERROR("发送消息头超时");
        return false;
      }
    }
  }

  // 发送消息体（循环处理非阻塞）
  total_sent = 0;
  const char *msg_ptr = msg.data();
  size_t msg_len = msg.size();

  while (total_sent < msg_len) {
    ssize_t n = send(fd, msg_ptr + total_sent, msg_len - total_sent, 0);
    if (n > 0) {
      total_sent += n;
    } else if (n == 0) {
      LOG_ERROR("发送消息体时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("发送消息体失败: " + std::string(strerror(errno)));
        return false;
      }
      // 等待可写
      fd_set write_fds;
      FD_ZERO(&write_fds);
      FD_SET(fd, &write_fds);
      struct timeval timeout = {2, 0};
      int ret = select(fd + 1, nullptr, &write_fds, nullptr, &timeout);
      if (ret <= 0) {
        LOG_ERROR("发送消息体超时");
        return false;
      }
    }
  }

  return true;
}

bool EchoClient::recv_message(int fd) {
  // 等待可读（处理非阻塞IO）
  fd_set read_fds;
  FD_ZERO(&read_fds);
  FD_SET(fd, &read_fds);
  struct timeval timeout = {2, 0}; // 2秒超时

  int ret = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
  if (ret < 0) {
    LOG_ERROR("select失败: " + std::string(strerror(errno)));
    return false;
  } else if (ret == 0) {
    LOG_ERROR("接收超时");
    return false;
  }

  // 接收消息头（循环处理拆包）
  MessageHeader header;
  size_t total_read = 0;
  char *header_ptr = reinterpret_cast<char *>(&header);
  size_t header_len = sizeof(MessageHeader);

  while (total_read < header_len) {
    ssize_t n = recv(fd, header_ptr + total_read, header_len - total_read, 0);
    if (n > 0) {
      total_read += n;
    } else if (n == 0) {
      LOG_ERROR("接收消息头时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("接收消息头失败: " + std::string(strerror(errno)));
        return false;
      }
      // 再次等待可读
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(fd, &read_fds);
      struct timeval timeout = {1, 0};
      int ret = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
      if (ret <= 0) {
        LOG_ERROR("接收消息头超时");
        return false;
      }
    }
  }

  // 解析消息长度
  uint32_t msg_len = ntohl(header.msg_len);
  if (msg_len == 0 || msg_len > 1024 * 1024) {
    LOG_ERROR("非法消息长度: " + std::to_string(msg_len));
    return false;
  }

  // 接收消息体（循环处理拆包）
  std::vector<char> buf(msg_len);
  total_read = 0;

  while (total_read < msg_len) {
    ssize_t n = recv(fd, buf.data() + total_read, msg_len - total_read, 0);
    if (n > 0) {
      total_read += n;
    } else if (n == 0) {
      LOG_ERROR("接收消息体时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("接收消息体失败: " + std::string(strerror(errno)));
        return false;
      }
      // 再次等待可读
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(fd, &read_fds);
      struct timeval timeout = {1, 0};
      int ret = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
      if (ret <= 0) {
        LOG_ERROR("接收消息体超时");
        return false;
      }
    }
  }

  return true;
}