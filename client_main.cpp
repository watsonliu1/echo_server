#include "common.h"
#include "echo_client.h"
#include <getopt.h>
#include <iostream>

/**
 * @brief 解析命令行参数，填充客户端配置结构体
 * @param argc 命令行参数个数（main函数传入）
 * @param argv 命令行参数数组（main函数传入）
 * @param config 输出参数：解析后的客户端配置信息
 * @note 支持的命令行选项：
 *       -c：并发连接数（默认值需在ClientConfig中定义）
 *       -m：每个连接发送的消息数（默认值需在ClientConfig中定义）
 *       -s：单条消息的字节大小（默认值需在ClientConfig中定义）
 *       -i：服务器IP地址
 *       -p：服务器端口号
 *       -t：测试持续时长（秒）（可选，用于控制测试运行时间）
 */
void parse_args(int argc, char *argv[], ClientConfig &config) {
  int opt; // 存储getopt解析出的选项字符

  // 循环解析命令行选项：getopt返回-1时表示解析完成
  // 选项格式说明："c:m:s:i:p:t:" 中带":"的选项表示需要跟随参数
  while ((opt = getopt(argc, argv, "c:m:s:i:p:t:")) != -1) {
    switch (opt) {
    case 'c':                                 // 解析并发连接数参数
      config.connections = std::stoi(optarg); // 将字符串参数转为整数
      break;

    case 'm': // 解析每个连接的消息发送数量参数
      config.messages_per_conn = std::stoi(optarg);
      break;

    case 's': // 解析单条消息的字节大小参数
      config.message_size = std::stoi(optarg);
      break;

    case 'i':                    // 解析服务器IP地址参数
      config.server_ip = optarg; // 直接赋值字符串（optarg为选项后的参数指针）
      break;

    case 'p': // 解析服务器端口号参数
      config.server_port = std::stoi(optarg);
      break;

    case 't': // 新增：解析测试持续时长（秒）参数
      config.test_duration_seconds = std::stoi(optarg);
      break;

    default: // 解析到未知选项或参数缺失时，输出用法并退出
      std::cerr << "Usage: " << argv[0] // 打印程序名称
                << " [-c connections] [-m messages/conn] [-s msg_size] [-i ip] "
                   "[-p port] [-t test_duration_seconds]\n"; // 打印支持的选项
      exit(1); // 异常退出（退出码1表示参数错误）
    }
  }
}

/**
 * @brief 客户端程序入口函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return int 程序退出码：0表示正常退出，1表示异常退出
 */
int main(int argc, char *argv[]) {
  ClientConfig config; // 初始化客户端配置结构体（需保证结构体有默认值）

  // 第一步：解析命令行参数，填充配置
  parse_args(argc, argv, config);

  try {
    // 第二步：创建Echo客户端实例，传入配置初始化
    EchoClient client(config);

    // 第三步：启动客户端测试（核心逻辑：创建连接、发送消息、接收响应等）
    client.run();
  } catch (
      const std::exception &e) { // 捕获所有标准异常（如参数错误、网络异常等）
    log_error("Client exception: " +
              std::string(e.what())); // 记录异常日志（log_error来自common.h）
    return 1;                         // 异常退出
  }

  return 0; // 正常退出
}