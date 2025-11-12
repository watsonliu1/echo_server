#include "echo_client.h"  // 包含EchoClient类定义
#include "common.h"       // 包含公共常量、配置结构体和日志函数
#include <getopt.h>       // 用于解析命令行参数
#include <iostream>       // 用于标准输入输出

// 解析命令行参数到客户端配置
// 参数：
//   argc - 命令行参数数量
//   argv - 命令行参数数组
//   config - 输出参数，存储解析后的配置
void parse_args(int argc, char* argv[], ClientConfig& config) 
{
    int opt;  // 存储getopt的返回值（解析到的选项）
    // 使用getopt循环解析短选项（格式：-选项 参数）
    while ((opt = getopt(argc, argv, "c:m:s:i:p")) != -1) {
        switch (opt) {
            case 'c':  // 连接数（-c）
                config.connections = std::stoi(optarg);  // optarg为选项后的参数值
                break;
            case 'm':  // 每个连接的消息数（-m）
                config.messages_per_conn = std::stoi(optarg);
                break;
            case 's':  // 消息大小（-s）
                config.message_size = std::stoi(optarg);
                break;
            case 'i':  // 服务器IP（-i）
                config.server_ip = optarg;
                break;
            case 'p':  // 服务器端口（-p）
                config.server_port = std::stoi(optarg);
                break;
            default:  // 未知选项
                // 输出用法提示并退出
                std::cerr << "Usage: " << argv[0] << " [-c connections] [-m messages/conn] [-s msg_size] [-i ip] [-p port] [-t]\n";
                exit(1);
        }
    }
}

// 客户端程序入口函数
int main(int argc, char* argv[])
{
    ClientConfig config;  // 客户端配置对象（使用默认值初始化）
    parse_args(argc, argv, config);  // 解析命令行参数更新配置

    try {
        // 初始化EchoClient对象，传入配置
        EchoClient client(config);
        // 运行客户端（建立连接并发送消息）
        client.run();
    } catch (const std::exception& e) {
        // 捕获并输出客户端运行过程中的异常
        log_error("Client exception: " + std::string(e.what()));
        return 1;  // 异常退出
    }
    return 0;  // 正常退出
}