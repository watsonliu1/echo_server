#include "echo_client.h"
#include "client_performance_monitor.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

// 带超时的send重试函数
ssize_t send_with_retry(int fd, const void *buf, size_t len,
                        const std::string &thread_id, int max_retry = 1000) {
  size_t total_sent = 0;
  int retry_count = 0;

  while (total_sent < len && retry_count < max_retry) {
    ssize_t ret = send(fd, static_cast<const char *>(buf) + total_sent,
                       len - total_sent, 0);
    if (ret == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        retry_count++;
        if (retry_count % 200 == 0) {
          log_info("[Thread " + thread_id + "] Send retry (" +
                   std::to_string(retry_count) + "/" +
                   std::to_string(max_retry) + ") - EAGAIN");
        }
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        continue;
      }
      log_error("[Thread " + thread_id +
                "] Send failed (errno: " + std::to_string(errno) + ")");
      return -1;
    }
    total_sent += ret;
    retry_count = 0;
  }

  if (retry_count >= max_retry) {
    log_error("[Thread " + thread_id + "] Send retry exceeded max limit (" +
              std::to_string(max_retry) + ")");
    return -1;
  }
  return total_sent;
}

// 初始化静态成员变量
std::atomic<int> EchoClient::total_connections(0);
std::atomic<int> EchoClient::total_sent(0);
std::atomic<int> EchoClient::total_received(0);
std::atomic<int> EchoClient::total_errors(0);

EchoClient::EchoClient(const ClientConfig &cfg) : config(cfg) {}

std::string EchoClient::get_thread_id() {
  std::stringstream ss;
  ss << std::this_thread::get_id();
  std::string id_str = ss.str();
  return id_str.size() > 3 ? id_str.substr(id_str.size() - 3) : id_str;
}

void EchoClient::handle_normal_connection() {
  std::string thread_id = get_thread_id();
  log_info("[Thread " + thread_id + "] Starting connection");

  int sockfd = -1;
  try {
    sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd == -1) {
      log_error("[Thread " + thread_id + "] Socket creation failed (errno: " +
                std::to_string(errno) + ")");
      total_errors++;
      return;
    }

    // 设置TCP_NODELAY
    int opt = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config.server_port);
    if (inet_pton(AF_INET, config.server_ip.c_str(), &server_addr.sin_addr) <=
        0) {
      log_error("[Thread " + thread_id + "] Invalid server IP");
      close(sockfd);
      total_errors++;
      return;
    }

    // 非阻塞connect
    int ret = connect(sockfd, reinterpret_cast<struct sockaddr *>(&server_addr),
                      sizeof(server_addr));
    if (ret == -1 && errno != EINPROGRESS) {
      log_error("[Thread " + thread_id +
                "] Connect failed (errno: " + std::to_string(errno) + ")");
      close(sockfd);
      total_errors++;
      return;
    }

    // 等待连接完成（2秒超时）
    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(sockfd, &wfds);
    timeval tv = {2, 0};
    if (select(sockfd + 1, nullptr, &wfds, nullptr, &tv) <= 0) {
      log_error("[Thread " + thread_id + "] Connect timeout");
      close(sockfd);
      total_errors++;
      return;
    }

    // 检查连接是否成功
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (error != 0) {
      log_error("[Thread " + thread_id +
                "] Connect failed (error: " + std::to_string(error) + ")");
      close(sockfd);
      total_errors++;
      return;
    }

    total_connections++;
    log_info("[Thread " + thread_id +
             "] Connection established (fd: " + std::to_string(sockfd) + ")");

    std::string send_data(config.message_size, 'a');
    PerformanceMonitor monitor;
    int msg_count = 0;

    // 使用秒级测试时长
    auto test_start = std::chrono::high_resolution_clock::now();
    const std::chrono::seconds test_duration(config.test_duration_seconds);
    const std::chrono::seconds log_interval(10);
    auto last_log_time = test_start;

    while (true) {
      auto now = std::chrono::high_resolution_clock::now();

      // 检查测试时长（秒级）
      if (now - test_start >= test_duration) {
        log_info("[Thread " + thread_id + "] Test duration reached (" +
                 std::to_string(config.test_duration_seconds) + "s)");
        break;
      }

      // 定期打印性能日志
      if (now - last_log_time >= log_interval) {
        monitor.log_current_stats(thread_id, msg_count);
        last_log_time = now;
      }

      // 构造报文头
      MessageHeader header;
      memset(&header, 0, sizeof(MessageHeader));
      header.magic = htonl(MAGIC_NUMBER);
      header.data_len = htonl(config.message_size);
      header.msg_id = htonl(msg_count);

      // 发送报文头
      log_info("[Thread " + thread_id + "] Sending Msg " +
               std::to_string(msg_count) + " header");
      if (send_with_retry(sockfd, &header, sizeof(MessageHeader), thread_id) !=
          sizeof(MessageHeader)) {
        log_error("[Thread " + thread_id + "] Msg " +
                  std::to_string(msg_count) + " header send failed");
        total_errors++;
        break;
      }

      // 发送数据
      if (send_with_retry(sockfd, send_data.c_str(), send_data.size(),
                          thread_id) != (ssize_t)send_data.size()) {
        log_error("[Thread " + thread_id + "] Msg " +
                  std::to_string(msg_count) + " data send failed");
        total_errors++;
        break;
      }

      total_sent++;
      msg_count++;

      // 非阻塞接收
      char recv_buf[sizeof(MessageHeader) + config.message_size];
      ssize_t recv_len = recv(sockfd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);
      if (recv_len > 0) {
        total_received++;
        log_info("[Thread " + thread_id + "] Msg " +
                 std::to_string(msg_count - 1) + " received (" +
                 std::to_string(recv_len) + " bytes)");
      } else if (recv_len == 0) {
        log_error("[Thread " + thread_id + "] Server disconnected");
        total_errors++;
        break;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        log_error("[Thread " + thread_id +
                  "] Recv error (errno: " + std::to_string(errno) + ")");
        total_errors++;
        break;
      }

      // 轻微延迟
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    close(sockfd);
    log_info("[Thread " + thread_id + "] Connection closed");
  } catch (const std::exception &e) {
    log_error("[Thread " + thread_id + "] Exception: " + std::string(e.what()));
    total_errors++;
    if (sockfd != -1)
      close(sockfd);
  }
}

void EchoClient::run() {
  log_info("Starting client with " + std::to_string(config.connections) +
           " connections");
  log_info("Test duration: " + std::to_string(config.test_duration_seconds) +
           " seconds");
  log_info("Message size: " + std::to_string(config.message_size) + " bytes");

  std::vector<std::thread> threads;

  // 分批启动线程
  int batch_size = 10;
  for (int i = 0; i < config.connections; ++i) {
    threads.emplace_back(&EchoClient::handle_normal_connection, this);
    if ((i + 1) % batch_size == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  for (auto &t : threads) {
    if (t.joinable())
      t.join();
  }

  print_stats();
}

void EchoClient::print_stats() {
  log_info("===== Client Final Statistics =====");
  log_info("Total connections established: " +
           std::to_string(total_connections));
  log_info("Total messages sent: " + std::to_string(total_sent));
  log_info("Total messages received: " + std::to_string(total_received));
  log_info("Total errors: " + std::to_string(total_errors));
  log_info("====================================");
}