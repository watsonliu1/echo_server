#include "echo_server.h"
#include <signal.h>
#include <iostream>

EchoServer* server_ptr = nullptr;

// 信号处理函数
void handle_signal(int signum) {
    if (server_ptr) {
        std::cout << "\nReceived signal " << signum << ", shutting down..." << std::endl;
        server_ptr->shutdown();
        exit(0);
    }
}

int main(int argc, char* argv[]) {
    // 注册信号处理
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    EchoServer server;
    server_ptr = &server;

    // 解析端口参数（可选）
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    // 初始化服务器
    if (!server.init(port)) {
        std::cerr << "Failed to initialize server" << std::endl;
        return 1;
    }

    std::cout << "Server started. Listening for connections..." << std::endl;
    std::cout << "Press Ctrl+C to stop server" << std::endl;

    // 运行服务器事件循环
    server.run();

    return 0;
}