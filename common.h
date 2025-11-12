#ifndef COMMON_H
#define COMMON_H

#include <cstdint>
#include <string>

// 32位有效魔数（0x1A2B3C4D = 439041101 < 2^32）
const uint32_t MAGIC_NUMBER = 0x1A2B3C4D;

const int DEFAULT_PORT = 15000;
const int BUFFER_SIZE = 4096;  // 大于最大消息长度

// 报文头结构（固定12字节）
struct MessageHeader {
    uint32_t magic;      // 魔数（网络字节序）
    uint32_t data_len;   // 数据长度（网络字节序）
    uint32_t msg_id;     // 消息ID（网络字节序）
};

struct ClientConfig {
    std::string server_ip = "127.0.0.1";
    int server_port = DEFAULT_PORT;
    int connections = 1;
    int messages_per_conn = 1;
    int message_size = 1024;
    bool pressure_test = false;
};

// 日志函数
inline void log_info(const std::string& msg) {
    printf("[INFO] %s\n", msg.c_str());
}

inline void log_error(const std::string& msg) {
    printf("[ERROR] %s\n", msg.c_str());
}

#endif // COMMON_H