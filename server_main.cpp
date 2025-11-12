#include "echo_server.h"  // 包含EchoServer类定义
#include "common.h"       // 包含公共常量和日志函数

// 服务器程序入口函数
int main() {
    try {
        // 初始化EchoServer对象，指定监听端口为默认端口（15000）
        EchoServer server(DEFAULT_PORT);
        // 启动服务器（进入事件循环，开始监听和处理连接）
        server.start();
    } catch (const std::exception& e) {
        // 捕获并输出服务器运行过程中的异常
        log_error("Server exception: " + std::string(e.what()));
        return 1;  // 异常退出，返回非0状态码
    }
    return 0;  // 正常退出
}