#include "config.h"
#include "echo_server.h"
#include "logger.h"
#include <iostream>
#include <signal.h>

std::unique_ptr<EchoServer> g_server;

void signal_handler(int sig) {
  LOG_INFO("收到信号: " + std::to_string(sig) + "，服务器准备退出");
  if (g_server) {
    g_server->stop();
  }
  exit(0);
}

int main(int argc, char *argv[]) {
  // 加载配置文件
  std::string config_path = (argc > 1) ? argv[1] : "config.ini";
  if (!Config::get_instance().load(config_path)) {
    std::cerr << "加载配置文件失败" << std::endl;
    return -1;
  }

  // 初始化日志
  Logger::get_instance().set_log_level(
      Config::get_instance().get_log_level("log", "level", LogLevel::INFO));
  Logger::get_instance().set_log_file(
      Config::get_instance().get_string("log", "file", "echo_server.log"));

  // 注册信号处理
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);
  signal(SIGPIPE, SIG_IGN);

  try {
    uint16_t port = Config::get_instance().get_int("server", "port", 15000);
    size_t thread_pool_size =
        Config::get_instance().get_int("server", "thread_pool_size", 4);
    int monitor_interval =
        Config::get_instance().get_int("monitor", "interval", 1);

    g_server =
        std::make_unique<EchoServer>(port, thread_pool_size, monitor_interval);
    if (g_server->start() != RET_SUCCESS) {
      LOG_ERROR("服务器启动失败");
      return -1;
    }
  } catch (const std::exception &e) {
    LOG_ERROR("服务器异常: " + std::string(e.what()));
    return -1;
  }

  return 0;
}