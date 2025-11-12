#ifndef COMMON_H
#define COMMON_H

#include <cstdint>   // 用于固定宽度整数类型（如uint32_t）
#include <string>    // 用于字符串处理

// 32位有效魔数（0x1A2B3C4D = 439041101 < 2^32）
// 作用：用于校验客户端与服务器之间的通信合法性，防止非法连接
const uint32_t MAGIC_NUMBER = 0x1A2B3C4D;

// 默认服务端监听端口
const int DEFAULT_PORT = 15000;

// 缓冲区大小（设置为大于最大消息长度，避免数据截断）
const int BUFFER_SIZE = 4096;

// 报文头结构（固定12字节）
// 作用：定义通信协议的头部格式，用于解析消息边界和元信息
struct MessageHeader
{
    uint32_t magic;      // 魔数（网络字节序）：用于验证消息合法性
    uint32_t data_len;   // 数据长度（网络字节序）：标识后续数据部分的字节数
    uint32_t msg_id;     // 消息ID（网络字节序）：用于消息编号和匹配请求/响应
};

// 客户端配置结构体
// 作用：存储客户端的所有配置参数，通过命令行参数初始化
struct ClientConfig 
{
    std::string server_ip = "127.0.0.1";  // 服务器IP地址，默认本地回环
    int server_port = DEFAULT_PORT;       // 服务器端口，默认15000
    int connections = 1;                  // 连接数，默认1个连接
    int messages_per_conn = 1;            // 每个连接发送的消息数，默认1条
    int message_size = 1024;              // 每条消息的数据部分大小，默认1024字节
};

// 日志函数：输出信息级日志
// 参数：msg - 日志内容
inline void log_info(const std::string& msg)
{
    printf("[INFO] %s\n", msg.c_str());
}

// 日志函数：输出错误级日志
// 参数：msg - 日志内容
inline void log_error(const std::string& msg)
{
    printf("[ERROR] %s\n", msg.c_str());
}

#endif // COMMON_H