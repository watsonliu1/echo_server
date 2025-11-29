#include "config.h"
#include "echo_client.h"
#include "logger.h"
#include <iostream>

int main(int argc, char *argv[]) {
  std::string config_path = (argc > 1) ? argv[1] : "config.ini";
  if (!Config::get_instance().load(config_path)) {
    std::cerr << "加载配置文件失败" << std::endl;
    return -1;
  }

  Logger::get_instance().set_log_level(
      Config::get_instance().get_log_level("log", "level", LogLevel::INFO));
  Logger::get_instance().set_log_file("echo_client.log");

  try {
    std::string server_ip =
        Config::get_instance().get_string("client", "server_ip", "127.0.0.1");
    uint16_t server_port =
        Config::get_instance().get_int("client", "server_port", 15000);
    int concurrent = Config::get_instance().get_int("client", "concurrent", 10);
    size_t msg_size =
        Config::get_instance().get_int("client", "message_size", 1024);
    int test_duration =
        Config::get_instance().get_int("client", "test_duration", 10);

    EchoClient client(server_ip, server_port, concurrent, msg_size,
                      test_duration);
    client.start();
  } catch (const std::exception &e) {
    LOG_ERROR("客户端异常: " + std::string(e.what()));
    return -1;
  }

  return 0;
}