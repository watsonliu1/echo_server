#include "common.h"
#include "echo_server.h"
#include <iostream>

/**
 * 服务器主函数
 * 初始化并启动echo服务器
 */
int main() {
  try {
    // 创建服务器实例（监听默认端口15000）
    EchoServer server(DEFAULT_PORT);
    // 初始化服务器（创建socket、绑定、监听）
    server.init();
    // 启动主循环（处理epoll事件）
    server.run();
  } catch (const std::exception &e) {
    // 捕获并打印异常
    log_error("Server exception: " + std::string(e.what()));
    return 1;
  }
  return 0;
}