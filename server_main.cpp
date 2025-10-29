#include "echo_server.h"
#include <signal.h>   // 信号处理相关函数（signal、SIGINT等）
#include <iostream>   // 标准输入输出（cout、cerr）

// 全局指针，用于在信号处理函数中访问服务器实例
// 信号处理函数无法直接接收类成员指针，因此用全局变量中转
EchoServer* server_ptr = nullptr;

/**
 * @brief 信号处理函数，响应服务器中断信号
 * @details 当服务器收到SIGINT（Ctrl+C）或SIGTERM（终止信号）时被调用，
 *          负责触发服务器的优雅关闭流程，避免强制终止导致的资源泄漏
 * @param signum 接收到的信号编号（如SIGINT为2，SIGTERM为15）
 */
void handle_signal(int signum) {
    // 检查服务器实例是否有效
    if (server_ptr) {
        std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
        server_ptr->shutdown();  // 调用服务器的关闭方法，释放所有资源
        exit(0);                 // 正常退出程序
    }
}

/**
 * @brief 服务器程序入口函数
 * @details 负责初始化服务器、注册信号处理、启动事件循环，是程序的起点
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组（argv[1]可选为端口号）
 * @return 0表示正常退出，非0表示异常退出
 */
int main(int argc, char* argv[]) {
    // 1. 注册信号处理函数，捕获中断信号
    //    SIGINT：用户按下Ctrl+C时产生的中断信号
    //    SIGTERM：系统发送的终止信号（如kill命令）
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    // 2. 创建EchoServer实例，并将全局指针指向该实例
    EchoServer server;
    server_ptr = &server;  // 使信号处理函数能访问到该实例

    // 3. 解析命令行参数中的端口号（可选）
    int port = DEFAULT_PORT;  // 默认使用common.h中定义的端口
    if (argc > 1) {           // 若用户传入了端口参数（如./echo_server 8080）
        port = std::atoi(argv[1]);  // 将字符串参数转换为整数端口号
        // 注：实际应用中应增加端口合法性检查（如1-65535范围）
    }

    // 4. 初始化服务器（创建套接字、绑定端口、初始化epoll等）
    if (!server.init(port)) {  // 初始化失败
        std::cerr << "Failed to initialize server" << std::endl;  // 输出错误信息
        return 1;  // 异常退出（返回非0值）
    }

    // 5. 输出启动信息，提示用户服务器已就绪
    std::cout << "Server started. Listening for connections..." << std::endl;
    std::cout << "Press Ctrl+C to stop server" << std::endl;

    // 6. 启动服务器事件循环（进入无限循环，处理客户端连接和数据）
    server.run();  // 该函数在服务器正常运行时不会返回，直到被信号中断

    // 正常情况下不会执行到此处（run()循环被信号中断后直接退出程序）
    return 0;
}