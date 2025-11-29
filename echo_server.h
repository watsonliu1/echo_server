#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

#include "common.h"
#include "performance_monitor.h"
#include "thread_pool.h"
#include <memory>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

struct ConnContext {
  int fd = -1;
  std::vector<char> recv_buf;
  std::vector<char> send_buf;
  bool is_closed = false;
};

class EchoServer {
public:
  EchoServer(uint16_t port, size_t thread_pool_size, int monitor_interval);
  ~EchoServer();

  int start();
  void stop();

private:
  int init_listen_fd();
  int set_nonblock(int fd);
  void handle_events();
  void handle_accept();
  void handle_read(int fd);
  void handle_write(int fd);
  void parse_message(ConnContext *ctx);
  void process_echo(const std::string &msg, int fd);

  int add_epoll_event(int fd, uint32_t events);
  int mod_epoll_event(int fd, uint32_t events);
  int del_epoll_event(int fd);
  void close_conn(int fd);

private:
  uint16_t port_;
  int listen_fd_ = -1;
  int epoll_fd_ = -1;
  std::unique_ptr<ThreadPool> thread_pool_;
  std::unique_ptr<PerformanceMonitor> monitor_;
  std::unordered_map<int, ConnContext> conn_map_;
  std::mutex conn_map_mutex_;
  bool stop_flag_ = false;
};

#endif // ECHO_SERVER_H