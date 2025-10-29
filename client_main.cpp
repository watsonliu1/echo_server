#include "echo_client.h"   // 引入EchoClient类定义，包含客户端核心逻辑
#include <iostream>        // 标准输入输出（用于打印帮助信息、日志）
#include <cstdlib>         // 包含exit()函数（用于解析参数出错时退出程序）
#include <unistd.h>        // 包含getopt()函数（用于解析命令行参数）

/**
 * @brief 打印客户端命令行参数的帮助信息
 * @details 当用户输入 `-h` 选项或参数格式错误时，输出可配置的选项列表及默认值，
 *          指导用户正确使用客户端程序
 * @param program_name 程序名称（通常为argv[0]，即执行文件的路径/名称）
 */
void print_help(const char* program_name)
{
    std::cout << "Usage: " << program_name << " [options]\n"  // 基础用法格式
              << "Options:\n"                                 // 选项列表标题
              << "  -i <ip>       Server IP address (default: 127.0.0.1)\n"  // 服务器IP选项及默认值
              << "  -p <port>     Server port (default: 15000)\n"          // 服务器端口选项及默认值（与common.h的DEFAULT_PORT对应）
              << "  -c <count>    Number of connections (default: 100)\n"   // 并发连接数选项及默认值
              << "  -m <num>      Messages per connection (default: 10)\n"  // 每个连接发送的消息数选项及默认值
              << "  -s <size>     Message size in bytes (default: 1024)\n"  // 每条消息大小（字节）选项及默认值
              << "  -h            Show this help message\n";                // 帮助信息选项说明
}

/**
 * @brief 解析客户端的命令行参数，生成ClientConfig配置结构体
 * @details 使用getopt()函数解析命令行中的选项（-i/-p/-c等），将用户输入的参数赋值到ClientConfig，
 *          未输入的参数保持结构体的默认值（在common.h中定义），支持参数错误时自动打印帮助信息
 * @param argc 命令行参数数量（main函数的argc）
 * @param argv 命令行参数数组（main函数的argv）
 * @return 填充好的ClientConfig结构体，包含服务器IP、端口、连接数等配置
 */
ClientConfig parse_args(int argc, char* argv[])
{
    ClientConfig config;  // 创建配置结构体，默认值由ClientConfig的成员初始化（如server_ip默认127.0.0.1）

    int opt;  // 存储getopt()解析出的选项字符（如'i'/'p'/'h'）
    // getopt()循环解析选项：argc/argv为参数列表，"i:p:c:m:s:h"为支持的选项（带:表示需接参数）
    while ((opt = getopt(argc, argv, "i:p:c:m:s:h")) != -1) {
        switch (opt) {
            case 'i':  // 解析服务器IP选项（-i）
                config.server_ip = optarg;  // optarg是getopt()自动保存的选项参数（如192.168.1.100）
                break;
            case 'p':  // 解析服务器端口选项（-p）
                // atoi()将字符串参数转为整数，赋值给server_port（默认值被覆盖）
                config.server_port = std::atoi(optarg);
                break;
            case 'c':  // 解析并发连接数选项（-c）
                config.connection_count = std::atoi(optarg);
                break;
            case 'm':  // 解析每个连接的消息数选项（-m）
                config.messages_per_conn = std::atoi(optarg);
                break;
            case 's':  // 解析每条消息大小选项（-s）
                config.message_size = std::atoi(optarg);
                break;
            case 'h':  // 解析帮助选项（-h）
                print_help(argv[0]);  // 打印帮助信息
                exit(0);              // 正常退出程序（无需继续执行）
            default:   // 解析到不支持的选项（getopt()返回'?'）
                print_help(argv[0]);  // 打印帮助信息，指导用户正确输入
                exit(1);              // 异常退出（返回非0值，标识参数错误）
        }
    }

    return config;  // 返回解析后的配置结构体
}

/**
 * @brief 客户端程序入口函数
 * @details 负责串联参数解析、客户端初始化和测试运行的全流程：先解析命令行参数生成配置，
 *          再创建EchoClient实例并传入配置，最后启动测试（run()函数）
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return 0表示程序正常退出，非0表示异常（实际由parse_args()中的exit()处理异常）
 */
int main(int argc, char* argv[])
{
    // 1. 解析命令行参数，生成客户端配置（用户输入优先，无输入则用默认值）
    ClientConfig config = parse_args(argc, argv);

    // 2. 创建EchoClient实例，将解析后的配置传入（构造函数初始化客户端配置）
    EchoClient client(config);

    // 3. 启动客户端测试：创建多线程并发连接、发送消息、验证回射并输出统计结果
    client.run();

    return 0;  // 程序正常结束（run()执行完所有测试后返回）
}