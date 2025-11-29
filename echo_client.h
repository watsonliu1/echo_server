#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H

#include "common.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ClientStats {
  std::atomic<uint64_t> sent = 0;
  std::atomic<uint64_t> received = 0;
  std::atomic<uint64_t> errors = 0;
};

class EchoClient {
public:
  EchoClient(const std::string &server_ip, uint16_t server_port, int concurrent,
             size_t msg_size, int test_duration);
  ~EchoClient();

  void start();
  void stop();

private:
  void worker_thread(int thread_id);
  int connect_to_server(int thread_id); // 添加 thread_id 参数
  int set_nonblock(int fd);
  bool send_message(int fd, const std::string &msg);
  bool recv_message(int fd);

private:
  std::string server_ip_;
  uint16_t server_port_;
  int concurrent_;
  size_t msg_size_;
  int test_duration_;

  std::vector<std::thread> worker_threads_;
  ClientStats stats_;
  std::atomic<bool> stop_flag_ = false;
  std::chrono::time_point<std::chrono::system_clock> start_time_;
};

#endif // ECHO_CLIENT_H