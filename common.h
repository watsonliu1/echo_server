#ifndef COMMON_H
#define COMMON_H

#include <atomic>
#include <cstdint>
#include <string>

const uint32_t MAGIC_NUMBER = 0x1A2B3C4D;
const int DEFAULT_PORT = 15000;
const int BUFFER_SIZE = 4096;

struct MessageHeader {
  uint32_t magic;
  uint32_t data_len;
  uint32_t msg_id;
};

struct ClientConfig {
  std::string server_ip = "127.0.0.1";
  int server_port = DEFAULT_PORT;
  int connections = 1;
  int messages_per_conn = 1;
  int message_size = 1024;
  int test_duration_seconds = 43200; // 默认12小时=43200秒
};

// 服务器全局统计
extern std::atomic<long long> g_total_processed_msgs;
extern std::atomic<int> g_active_connections;

inline void log_info(const std::string &msg) {
  printf("[INFO] %s\n", msg.c_str());
}

inline void log_error(const std::string &msg) {
  printf("[ERROR] %s\n", msg.c_str());
}

#endif // COMMON_H