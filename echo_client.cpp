#include "echo_client.h"
#include "config.h"
#include "logger.h"
#include <algorithm> // for max
#include <arpa/inet.h>
#include <cstdlib>
#include <ctime>
#include <errno.h>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <poll.h>
#include <random>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// 线程安全随机数
static std::mutex rand_mutex;
static std::mt19937 rand_engine(std::random_device{}());

EchoClient::EchoClient(const std::string &server_ip, uint16_t server_port,
                       int concurrent, size_t msg_size, int test_duration)
    : server_ip_(server_ip), server_port_(server_port), concurrent_(concurrent),
      msg_size_(msg_size), test_duration_(test_duration) {
  // 限制消息大小
  if (msg_size_ > 1024 * 1024) {
    msg_size_ = 1024 * 1024;
    LOG_INFO("消息大小超过1MB限制，自动调整为1MB");
  }
}

EchoClient::~EchoClient() { stop(); }

void EchoClient::start() {
  stop_flag_ = false;
  start_time_ = std::chrono::system_clock::now();

  LOG_INFO("客户端启动，并发连接数: " + std::to_string(concurrent_) +
           ", 消息大小: " + std::to_string(msg_size_) +
           "字节, "
           "测试时长: " +
           std::to_string(test_duration_) + "秒");

  // 启动工作线程
  for (int i = 0; i < concurrent_; ++i) {
    worker_threads_.emplace_back(&EchoClient::worker_thread, this, i);
  }

  // 等待测试结束（确保等待足够时长）
  std::this_thread::sleep_for(std::chrono::seconds(test_duration_));

  // 停止所有线程
  stop();

  // 输出统计
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
  int fd = -1;
  int retry_count = 3; // 连接重试次数

  // 连接重试逻辑
  while (retry_count-- > 0 && !stop_flag_) {
    fd = connect_to_server(thread_id);
    if (fd >= 0)
      break;
    LOG_INFO("线程" + std::to_string(thread_id) +
             "连接重试，剩余次数: " + std::to_string(retry_count));
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 重试间隔
  }

  if (fd < 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "连接服务器失败（重试耗尽）");
    stats_.errors++;
    return;
  }

  std::string msg(msg_size_, 'A');
  LOG_DEBUG("线程" + std::to_string(thread_id) + "开始发送消息");

  while (!stop_flag_) {
    // 发送消息（允许重试）
    bool send_ok = false;
    for (int i = 0; i < 2; ++i) { // 发送重试2次
      if (send_message(fd, msg, thread_id)) {
        send_ok = true;
        stats_.sent++;
        break;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    if (!send_ok) {
      LOG_ERROR("线程" + std::to_string(thread_id) + "发送消息失败");
      stats_.errors++;
      break;
    }

    // 接收消息（允许重试）
    bool recv_ok = false;
    for (int i = 0; i < 2; ++i) { // 接收重试2次
      if (recv_message(fd, thread_id)) {
        recv_ok = true;
        stats_.received++;
        break;
      }
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    if (!recv_ok) {
      LOG_ERROR("线程" + std::to_string(thread_id) + "接收消息失败");
      stats_.errors++;
      break;
    }

    // 动态延迟（根据并发调整）
    std::this_thread::sleep_for(
        std::chrono::microseconds(std::max(10, concurrent_ / 100)));
  }

  if (fd >= 0)
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

  // poll等待连接完成（延长超时到10秒）
  struct pollfd pfd = {fd, POLLOUT, 0};
  ret = poll(&pfd, 1, 10000); // 10秒超时
  if (ret < 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "poll失败: " + std::string(strerror(errno)));
    close(fd);
    return RET_ERROR;
  } else if (ret == 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) + "connect超时");
    close(fd);
    return RET_ERROR;
  }

  // 检查连接是否成功
  int error = 0;
  socklen_t len = sizeof(error);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "连接失败: " + std::string(strerror(error)));
    close(fd);
    return RET_ERROR;
  }

  LOG_DEBUG("线程" + std::to_string(thread_id) +
            "连接成功，fd=" + std::to_string(fd));
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

bool EchoClient::send_message(int fd, const std::string &msg, int thread_id) {
  MessageHeader header __attribute__((aligned(4)));
  memset(&header, 0, sizeof(header));
  header.msg_len = htonl(static_cast<uint32_t>(msg.size()));

  // 线程安全随机数
  {
    std::lock_guard<std::mutex> lock(rand_mutex);
    header.msg_id = htonl(static_cast<uint32_t>(rand_engine() % 10000));
  }

  // 发送消息头
  size_t total_sent = 0;
  const char *header_ptr = reinterpret_cast<const char *>(&header);
  size_t header_len = sizeof(MessageHeader);

  while (total_sent < header_len && !stop_flag_) {
    ssize_t n = send(fd, header_ptr + total_sent, header_len - total_sent,
                     MSG_NOSIGNAL);
    if (n > 0) {
      total_sent += n;
    } else if (n == 0) {
      LOG_ERROR("线程" + std::to_string(thread_id) + "发送消息头时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("线程" + std::to_string(thread_id) +
                  "发送消息头失败: " + std::string(strerror(errno)));
        return false;
      }
      // poll等待可写（延长超时到5秒）
      struct pollfd pfd = {fd, POLLOUT, 0};
      int ret = poll(&pfd, 1, 5000);
      if (ret <= 0) {
        LOG_DEBUG("线程" + std::to_string(thread_id) + "发送消息头超时/无事件");
        return false;
      }
    }
  }

  if (msg.empty())
    return true;

  // 发送消息体
  total_sent = 0;
  const char *msg_ptr = msg.data();
  size_t msg_len = msg.size();

  while (total_sent < msg_len && !stop_flag_) {
    ssize_t n =
        send(fd, msg_ptr + total_sent, msg_len - total_sent, MSG_NOSIGNAL);
    if (n > 0) {
      total_sent += n;
    } else if (n == 0) {
      LOG_ERROR("线程" + std::to_string(thread_id) + "发送消息体时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("线程" + std::to_string(thread_id) +
                  "发送消息体失败: " + std::string(strerror(errno)));
        return false;
      }
      // poll等待可写
      struct pollfd pfd = {fd, POLLOUT, 0};
      int ret = poll(&pfd, 1, 5000);
      if (ret <= 0) {
        LOG_DEBUG("线程" + std::to_string(thread_id) + "发送消息体超时/无事件");
        return false;
      }
    }
  }

  return total_sent == msg_len;
}

bool EchoClient::recv_message(int fd, int thread_id) {
  // poll等待可读（延长超时到5秒）
  struct pollfd pfd = {fd, POLLIN, 0};
  int ret = poll(&pfd, 1, 5000);
  if (ret < 0) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "poll失败: " + std::string(strerror(errno)));
    return false;
  } else if (ret == 0) {
    LOG_DEBUG("线程" + std::to_string(thread_id) + "接收超时");
    return false;
  }

  // 接收消息头
  MessageHeader header __attribute__((aligned(4)));
  memset(&header, 0, sizeof(header));
  size_t total_read = 0;
  char *header_ptr = reinterpret_cast<char *>(&header);
  size_t header_len = sizeof(MessageHeader);

  while (total_read < header_len && !stop_flag_) {
    ssize_t n = recv(fd, header_ptr + total_read, header_len - total_read, 0);
    if (n > 0) {
      total_read += n;
    } else if (n == 0) {
      LOG_ERROR("线程" + std::to_string(thread_id) + "接收消息头时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("线程" + std::to_string(thread_id) +
                  "接收消息头失败: " + std::string(strerror(errno)));
        return false;
      }
      // poll等待可读
      struct pollfd pfd = {fd, POLLIN, 0};
      int ret = poll(&pfd, 1, 2000);
      if (ret <= 0) {
        LOG_DEBUG("线程" + std::to_string(thread_id) + "接收消息头重试超时");
        return false;
      }
    }
  }

  if (total_read != header_len)
    return false;

  // 解析消息长度
  uint32_t msg_len = ntohl(header.msg_len);
  if (msg_len == 0 || msg_len > 1024 * 1024) {
    LOG_ERROR("线程" + std::to_string(thread_id) +
              "非法消息长度: " + std::to_string(msg_len));
    return false;
  }

  // 接收消息体
  std::vector<char> buf(msg_len);
  total_read = 0;

  while (total_read < msg_len && !stop_flag_) {
    ssize_t n = recv(fd, buf.data() + total_read, msg_len - total_read, 0);
    if (n > 0) {
      total_read += n;
    } else if (n == 0) {
      LOG_ERROR("线程" + std::to_string(thread_id) + "接收消息体时连接关闭");
      return false;
    } else {
      if (errno != EAGAIN && errno != EWOULDBLOCK) {
        LOG_ERROR("线程" + std::to_string(thread_id) +
                  "接收消息体失败: " + std::string(strerror(errno)));
        return false;
      }
      // poll等待可读
      struct pollfd pfd = {fd, POLLIN, 0};
      int ret = poll(&pfd, 1, 2000);
      if (ret <= 0) {
        LOG_DEBUG("线程" + std::to_string(thread_id) + "接收消息体重试超时");
        return false;
      }
    }
  }

  return total_read == msg_len;
}