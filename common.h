#ifndef COMMON_H
#define COMMON_H

#include <chrono>
#include <cstdint>
#include <string>

// 日志级别
enum class LogLevel { DEBUG, INFO, ERROR };

// 消息结构（解决粘包问题）
struct MessageHeader {
  uint32_t msg_len; // 消息体长度（字节）
  uint32_t msg_id;  // 消息ID（可选）
} __attribute__((packed));

// 公共常量
constexpr size_t MAX_EVENTS = 1024;
constexpr size_t BUFFER_SIZE = 8192;

// 错误码
constexpr int RET_SUCCESS = 0;
constexpr int RET_ERROR = -1;

// 时间戳工具
inline uint64_t get_timestamp_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

#endif // COMMON_H