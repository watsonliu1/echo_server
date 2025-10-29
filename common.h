#ifndef COMMON_H
#define COMMON_H
/**
 * @file common.h
 * @brief 公共配置头文件，定义服务器和客户端共用的常量、结构体及头文件依赖
 * @details 该文件集中管理跨模块的公共配置，避免服务器与客户端代码中出现重复定义，
 *          确保双方使用一致的基础参数（如端口、缓冲区大小），减少兼容性问题
 */

#include <cstdint>  // 引入标准整数类型定义（如int32_t等，增强类型安全性）


// ------------------------------ 公共常量配置 ------------------------------
/**
 * @brief 默认服务器端口号
 * @note 若启动时未指定端口，服务器将绑定此端口，客户端默认连接此端口
 */
const int DEFAULT_PORT = 15000;

/**
 * @brief 数据缓冲区大小（字节）
 * @details 用于服务器为每个客户端分配的接收缓冲区，以及客户端发送/接收消息的缓冲区
 *          16KB（1024*16）的大小适合大多数场景，可根据实际需求调整
 */
const int BUFFER_SIZE = 1024 * 16;

/**
 * @brief epoll一次最多处理的事件数量
 * @details 服务器调用epoll_wait时，用于存储就绪事件的数组大小上限，
 *          1024表示一次最多处理1024个事件，可根据并发量调整
 */
const int MAX_EVENTS = 1024;

/**
 * @brief 服务器支持的最大并发连接数限制
 * @details 用于服务器内部做连接数控制，防止过多连接耗尽系统资源（如文件描述符、内存）
 *          此处设为100000，实际上限还受操作系统ulimit等配置影响
 */
const int MAX_CONNECTIONS = 100000;


// ------------------------------ 客户端配置结构体 ------------------------------
/**
 * @brief 客户端运行参数配置结构体
 * @details 存储客户端连接服务器所需的所有配置信息，支持通过命令行参数动态修改
 */
struct ClientConfig
{
    const char* server_ip = "127.0.0.1";  ///< 服务器IP地址，默认本地回环地址
    int server_port = DEFAULT_PORT;       ///< 服务器端口号，默认与DEFAULT_PORT保持一致
    int connection_count = 100;           // 客户端要创建的并发连接数，默认100个
    int messages_per_conn = 10;           // 每个连接发送的消息数量，默认每个连接发10条
    int message_size = 1024;              // 每条消息的大小（字节），默认1KB
};

#endif  // COMMON_H  防止头文件被重复包含的宏定义结束