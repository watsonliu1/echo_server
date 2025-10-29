#ifndef COMMON_H
#define COMMON_H

#include <cstdint>

// 公共配置
const int DEFAULT_PORT = 15000;
const int BUFFER_SIZE = 1024 * 16;
const int MAX_EVENTS = 1024;
const int MAX_CONNECTIONS = 100000;

// 客户端配置结构体
struct ClientConfig
{
    const char* server_ip = "127.0.0.1";
    int server_port = DEFAULT_PORT;
    int connection_count = 100;
    int messages_per_conn = 10;
    int message_size = 1024;
};

#endif