#include "echo_client.h"
#include <iostream>
#include <cstdlib>
#include <unistd.h>

// 显示帮助信息
void print_help(const char* program_name)
{
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -i <ip>       Server IP address (default: 127.0.0.1)\n"
              << "  -p <port>     Server port (default: 15000)\n"
              << "  -c <count>    Number of connections (default: 100)\n"
              << "  -m <num>      Messages per connection (default: 10)\n"
              << "  -s <size>     Message size in bytes (default: 1024)\n"
              << "  -h            Show this help message\n";
}

// 解析命令行参数
ClientConfig parse_args(int argc, char* argv[])
{
    ClientConfig config;

    int opt;
    while ((opt = getopt(argc, argv, "i:p:c:m:s:h")) != -1) {
        switch (opt) {
            case 'i':
                config.server_ip = optarg;
                break;
            case 'p':
                config.server_port = std::atoi(optarg);
                break;
            case 'c':
                config.connection_count = std::atoi(optarg);
                break;
            case 'm':
                config.messages_per_conn = std::atoi(optarg);
                break;
            case 's':
                config.message_size = std::atoi(optarg);
                break;
            case 'h':
                print_help(argv[0]);
                exit(0);
            default:
                print_help(argv[0]);
                exit(1);
        }
    }

    return config;
}

int main(int argc, char* argv[])
{
    // 解析命令行参数
    ClientConfig config = parse_args(argc, argv);

    // 运行客户端测试
    EchoClient client(config);
    client.run();

    return 0;
}