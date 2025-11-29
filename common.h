// 头文件保护宏：防止该头文件被重复包含（避免编译时的重定义错误）
#ifndef COMMON_H
#define COMMON_H

// 引入C++原子操作库（用于多线程安全的全局统计变量）
#include <atomic>
// 引入固定宽度整数类型（确保跨平台的类型大小一致性）
#include <cstdint>
// 引入字符串库（用于IP地址、日志信息等字符串处理）
#include <string>

/**
 * @brief 消息魔数（Magic Number）
 * @note
 * 用于网络传输中校验消息合法性，客户端发送的消息头部需包含此值，服务器验证后才处理消息，防止非法数据包
 */
const uint32_t MAGIC_NUMBER = 0x1A2B3C4D;

/**
 * @brief 服务器默认监听端口
 * @note 客户端默认连接此端口，与服务器初始化的监听端口保持一致（15000）
 */
const int DEFAULT_PORT = 15000;

/**
 * @brief 网络传输缓冲区大小
 * @note 定义单次读写的最大数据长度（4KB），适配消息头部+数据的传输需求
 */
const int BUFFER_SIZE = 4096;

/**
 * @struct MessageHeader
 * @brief 网络消息的头部结构（用于客户端与服务器的消息格式统一）
 * @note 作为消息的前缀，先传输头部再传输数据，确保双方能正确解析消息边界和内容
 */
struct MessageHeader {
  uint32_t magic; // 消息魔数（需等于MAGIC_NUMBER，用于合法性校验）
  uint32_t data_len; // 消息体数据的长度（单位：字节）
  uint32_t msg_id; // 消息唯一ID（用于标识消息序号，便于追踪和去重）
};

/**
 * @struct ClientConfig
 * @brief 客户端配置结构体（存储命令行参数解析后的配置项，提供默认值）
 * @note 包含客户端连接服务器的所有必要参数，默认值适配12小时长时测试场景
 */
struct ClientConfig {
  std::string server_ip = "127.0.0.1"; // 服务器IP地址，默认本地回环地址
  int server_port = DEFAULT_PORT; // 服务器端口，默认15000
  int connections = 1;            // 并发连接数，默认1个连接
  int messages_per_conn = 1;      // 每个连接发送的消息数，默认1条
  int message_size = 1024;        // 单条消息的字节大小，默认1KB
  int test_duration_seconds =
      43200; // 测试持续时长（秒），默认12小时（43200秒）
};

/**
 * @brief 服务器全局统计变量（原子类型保证多线程安全）
 */
// 服务器已处理的总消息数（所有连接累计）
extern std::atomic<long long> g_total_processed_msgs;
// 服务器当前活跃的连接数（实时统计并发连接）
extern std::atomic<int> g_active_connections;

/**
 * @brief 信息级日志打印函数（内联函数减少调用开销）
 * @param msg 日志内容字符串
 * @note 统一日志格式，输出[INFO]前缀，便于日志分类查看
 */
inline void log_info(const std::string &msg) {
  printf("[INFO] %s\n", msg.c_str());
}

/**
 * @brief 错误级日志打印函数（内联函数减少调用开销）
 * @param msg 错误内容字符串
 * @note 统一错误日志格式，输出[ERROR]前缀，便于快速定位问题
 */
inline void log_error(const std::string &msg) {
  printf("[ERROR] %s\n", msg.c_str());
}

#endif // COMMON_H