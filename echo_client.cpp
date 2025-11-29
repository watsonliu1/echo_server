// 引入Echo客户端类定义头文件
#include "echo_client.h"
// 引入客户端性能监控类头文件（用于统计CPU/内存/消息量）
#include "client_performance_monitor.h"
// 引入IP地址转换函数库（inet_pton等）
#include <arpa/inet.h>
// 引入原子操作库（用于多线程安全的统计变量）
#include <atomic>
// 引入时间库（用于计时、超时控制）
#include <chrono>
// 引入字符串操作库（memset等）
#include <cstring>
// 引入错误码定义（errno相关）
#include <errno.h>
// 引入文件控制库（fcntl设置非阻塞）
#include <fcntl.h>
// 引入IPv4地址结构定义（sockaddr_in）
#include <netinet/in.h>
// 引入TCP协议选项定义（TCP_NODELAY）
#include <netinet/tcp.h>
// 引入字符串流库（用于线程ID转换）
#include <sstream>
// 引入异常类库（std::exception）
#include <stdexcept>
// 引入Socket编程库（socket/connect/send/recv等）
#include <sys/socket.h>
// 引入线程库（std::thread）
#include <thread>
// 引入系统调用库（close/usleep等）
#include <unistd.h>
// 引入向量容器（存储线程对象）
#include <vector>

// 带超时重试的send函数：解决非阻塞发送时的EAGAIN问题，确保数据完整发送
// fd：socket文件描述符；buf：待发送数据缓冲区；len：待发送数据长度
// thread_id：线程标识（用于日志区分）；max_retry：最大重试次数（默认1000次）
ssize_t send_with_retry(int fd, const void *buf, size_t len,
                        const std::string &thread_id, int max_retry = 1000) {
  size_t total_sent = 0; // 已成功发送的字节数
  int retry_count = 0;   // 当前重试次数

  // 循环发送直到全部数据发完或达到最大重试次数
  while (total_sent < len && retry_count < max_retry) {
    // 发送剩余未发送的数据
    ssize_t ret = send(fd, static_cast<const char *>(buf) + total_sent,
                       len - total_sent, 0);
    if (ret == -1) {
      // 非阻塞发送时缓冲区满（EAGAIN/EWOULDBLOCK），等待后重试
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        retry_count++;
        // 每200次重试打印一次日志，避免日志刷屏
        if (retry_count % 200 == 0) {
          log_info("[Thread " + thread_id + "] Send retry (" +
                   std::to_string(retry_count) + "/" +
                   std::to_string(max_retry) + ") - EAGAIN");
        }
        // 微秒级休眠后重试（避免CPU空转）
        std::this_thread::sleep_for(std::chrono::microseconds(50));
        continue;
      }
      // 其他错误（如连接断开），记录错误日志并返回失败
      log_error("[Thread " + thread_id +
                "] Send failed (errno: " + std::to_string(errno) + ")");
      return -1;
    }
    total_sent += ret; // 累加已发送字节数
    retry_count = 0;   // 发送成功后重置重试计数
  }

  // 达到最大重试次数仍未发送完成，记录错误
  if (retry_count >= max_retry) {
    log_error("[Thread " + thread_id + "] Send retry exceeded max limit (" +
              std::to_string(max_retry) + ")");
    return -1;
  }
  return total_sent; // 返回成功发送的总字节数
}

// 初始化EchoClient类的静态原子成员变量（多线程共享统计）
std::atomic<int> EchoClient::total_connections(0); // 总成功建立的连接数
std::atomic<int> EchoClient::total_sent(0);        // 总发送消息数
std::atomic<int> EchoClient::total_received(0);    // 总接收响应数
std::atomic<int> EchoClient::total_errors(0);      // 总错误数

// EchoClient构造函数：初始化客户端配置
// cfg：客户端配置结构体（包含连接数、消息大小、测试时长等）
EchoClient::EchoClient(const ClientConfig &cfg) : config(cfg) {}

// 获取当前线程的ID字符串（简化为后三位，便于日志展示）
std::string EchoClient::get_thread_id() {
  std::stringstream ss;
  ss << std::this_thread::get_id(); // 将线程ID转为字符串流
  std::string id_str = ss.str();
  // 截取线程ID的最后三位（若长度超过3），否则返回原字符串
  return id_str.size() > 3 ? id_str.substr(id_str.size() - 3) : id_str;
}

// 处理单个正常TCP连接的核心逻辑：创建连接、发送消息、接收响应、性能监控
void EchoClient::handle_normal_connection() {
  std::string thread_id = get_thread_id(); // 获取当前线程ID（用于日志区分）
  log_info("[Thread " + thread_id + "] Starting connection");

  int sockfd = -1; // Socket文件描述符（初始化为-1表示未创建）
  try {
    // 创建非阻塞TCP
    // Socket（AF_INET：IPv4，SOCK_STREAM：TCP，SOCK_NONBLOCK：非阻塞）
    sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd == -1) {
      log_error("[Thread " + thread_id + "] Socket creation failed (errno: " +
                std::to_string(errno) + ")");
      total_errors++; // 统计错误数
      return;
    }

    // 设置TCP_NODELAY选项：禁用Nagle算法，减少小数据包延迟（提升实时性）
    int opt = 1;
    setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // 初始化服务器地址结构
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr)); // 内存清零
    server_addr.sin_family = AF_INET;             // IPv4协议族
    // 端口号转换为网络字节序（htons：主机字节序转网络字节序）
    server_addr.sin_port = htons(config.server_port);
    // 将服务器IP字符串转为网络字节序的二进制地址
    if (inet_pton(AF_INET, config.server_ip.c_str(), &server_addr.sin_addr) <=
        0) {
      log_error("[Thread " + thread_id + "] Invalid server IP");
      close(sockfd); // 关闭Socket
      total_errors++;
      return;
    }

    // 非阻塞模式下调用connect（立即返回，不会阻塞等待连接建立）
    int ret = connect(sockfd, reinterpret_cast<struct sockaddr *>(&server_addr),
                      sizeof(server_addr));
    // 非阻塞connect返回-1且errno为EINPROGRESS表示连接正在建立，否则为连接失败
    if (ret == -1 && errno != EINPROGRESS) {
      log_error("[Thread " + thread_id +
                "] Connect failed (errno: " + std::to_string(errno) + ")");
      close(sockfd);
      total_errors++;
      return;
    }

    // 使用select等待连接建立完成（超时时间2秒）
    fd_set wfds; // 写事件集合（用于检测Socket可写，即连接建立完成）
    FD_ZERO(&wfds);        // 清空事件集合
    FD_SET(sockfd, &wfds); // 将Socket加入写事件集合
    timeval tv = {2, 0};   // 超时时间：2秒
    // select检测写事件，若返回<=0表示超时或失败
    if (select(sockfd + 1, nullptr, &wfds, nullptr, &tv) <= 0) {
      log_error("[Thread " + thread_id + "] Connect timeout");
      close(sockfd);
      total_errors++;
      return;
    }

    // 检查连接是否真正建立成功（通过SO_ERROR选项获取错误码）
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

    total_connections++; // 统计成功建立的连接数
    log_info("[Thread " + thread_id +
             "] Connection established (fd: " + std::to_string(sockfd) + ")");

    // 初始化发送数据：构造指定大小的字符串（内容为'a'）
    std::string send_data(config.message_size, 'a');
    // 创建性能监控实例（记录当前线程的CPU/内存/消息量）
    ClientPerformanceMonitor monitor;
    int msg_count = 0; // 当前连接已发送的消息计数

    // 记录测试开始时间（用于控制测试时长）
    auto test_start = std::chrono::high_resolution_clock::now();
    const std::chrono::seconds test_duration(
        config.test_duration_seconds);           // 测试总时长
    const std::chrono::seconds log_interval(10); // 性能日志打印间隔（10秒）
    auto last_log_time = test_start; // 上一次打印日志的时间

    // 消息发送循环：直到达到测试时长或出现错误
    while (true) {
      auto now = std::chrono::high_resolution_clock::now();

      // 检查是否达到测试时长：若当前时间-开始时间>=测试时长，退出循环
      if (now - test_start >= test_duration) {
        log_info("[Thread " + thread_id + "] Test duration reached (" +
                 std::to_string(config.test_duration_seconds) + "s)");
        break;
      }

      // 定期打印性能日志：每10秒打印一次当前线程的CPU/内存/消息量
      if (now - last_log_time >= log_interval) {
        monitor.log_current_stats(thread_id, msg_count);
        last_log_time = now;
      }

      // 构造消息头部（按网络字节序填充，确保服务器端正确解析）
      MessageHeader header;
      memset(&header, 0, sizeof(MessageHeader)); // 头部内存清零
      header.magic = htonl(MAGIC_NUMBER);        // 魔数转为网络字节序
      header.data_len = htonl(config.message_size); // 数据长度转为网络字节序
      header.msg_id = htonl(msg_count); // 消息ID转为网络字节序

      // 发送消息头部（使用带重试的send函数）
      log_info("[Thread " + thread_id + "] Sending Msg " +
               std::to_string(msg_count) + " header");
      if (send_with_retry(sockfd, &header, sizeof(MessageHeader), thread_id) !=
          sizeof(MessageHeader)) {
        log_error("[Thread " + thread_id + "] Msg " +
                  std::to_string(msg_count) + " header send failed");
        total_errors++;
        break;
      }

      // 发送消息体数据（使用带重试的send函数）
      if (send_with_retry(sockfd, send_data.c_str(), send_data.size(),
                          thread_id) != (ssize_t)send_data.size()) {
        log_error("[Thread " + thread_id + "] Msg " +
                  std::to_string(msg_count) + " data send failed");
        total_errors++;
        break;
      }

      total_sent++; // 统计总发送消息数
      msg_count++;  // 当前连接的消息计数+1

      // 非阻塞接收响应（MSG_DONTWAIT：非阻塞模式）
      char recv_buf[sizeof(MessageHeader) +
                    config.message_size]; // 接收缓冲区（头部+数据）
      ssize_t recv_len = recv(sockfd, recv_buf, sizeof(recv_buf), MSG_DONTWAIT);
      if (recv_len > 0) {
        total_received++; // 统计总接收响应数
        log_info("[Thread " + thread_id + "] Msg " +
                 std::to_string(msg_count - 1) + " received (" +
                 std::to_string(recv_len) + " bytes)");
      } else if (recv_len == 0) {
        // recv返回0表示服务器主动关闭连接
        log_error("[Thread " + thread_id + "] Server disconnected");
        total_errors++;
        break;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        // 非阻塞接收时的其他错误（非缓冲区空）
        log_error("[Thread " + thread_id +
                  "] Recv error (errno: " + std::to_string(errno) + ")");
        total_errors++;
        break;
      }

      // 微秒级延迟：模拟实际业务场景的间隔，避免发送过快
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    close(sockfd); // 关闭Socket连接
    log_info("[Thread " + thread_id + "] Connection closed");
  } catch (const std::exception &e) {
    // 捕获所有标准异常，记录错误日志
    log_error("[Thread " + thread_id + "] Exception: " + std::string(e.what()));
    total_errors++;
    if (sockfd != -1) // 若Socket已创建，关闭以释放资源
      close(sockfd);
  }
}

// 客户端主运行逻辑：创建多线程连接、分批启动、等待所有线程结束并打印统计
void EchoClient::run() {
  // 打印测试配置信息（便于日志追溯）
  log_info("Starting client with " + std::to_string(config.connections) +
           " connections");
  log_info("Test duration: " + std::to_string(config.test_duration_seconds) +
           " seconds");
  log_info("Message size: " + std::to_string(config.message_size) + " bytes");

  std::vector<std::thread> threads; // 存储线程对象的容器

  // 分批启动线程（每批10个）：避免瞬间创建大量线程导致系统资源耗尽
  int batch_size = 10;
  for (int i = 0; i < config.connections; ++i) {
    // 创建线程并绑定handle_normal_connection函数（每个线程处理一个连接）
    threads.emplace_back(&EchoClient::handle_normal_connection, this);
    // 每创建batch_size个线程，休眠50毫秒
    if ((i + 1) % batch_size == 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }

  // 等待所有线程执行完成（join：阻塞直到线程结束）
  for (auto &t : threads) {
    if (t.joinable())
      t.join();
  }

  print_stats(); // 打印最终统计信息
}

// 打印客户端测试的最终统计结果（总连接数、发送/接收数、错误数）
void EchoClient::print_stats() {
  log_info("===== Client Final Statistics =====");
  log_info("Total connections established: " +
           std::to_string(total_connections));
  log_info("Total messages sent: " + std::to_string(total_sent));
  log_info("Total messages received: " + std::to_string(total_received));
  log_info("Total errors: " + std::to_string(total_errors));
  log_info("====================================");
}