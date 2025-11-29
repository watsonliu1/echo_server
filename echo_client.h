#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H

#include "common.h"
#include <atomic>
#include <string>
#include <thread>
#include <vector>

class EchoClient {
private:
  ClientConfig config;
  std::string get_thread_id();
  void handle_normal_connection();

public:
  static std::atomic<int> total_connections;
  static std::atomic<int> total_sent;
  static std::atomic<int> total_received;
  static std::atomic<int> total_errors;

  EchoClient(const ClientConfig &cfg);
  void run();
  void print_stats();
};

#endif // ECHO_CLIENT_H