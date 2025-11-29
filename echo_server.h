// 头文件保护宏：防止该头文件被重复包含，避免编译时的重定义错误
#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

// 引入字符串库（用于日志输出、IP地址转换等字符串处理）
#include <string>

/**
 * @class EchoServer
 * @brief Echo服务器核心类，基于epoll IO多路复用实现高并发TCP服务器
 * @note
 * 采用非阻塞IO+epoll水平触发模式，支持多客户端连接管理、数据回射及性能监控
 */
class EchoServer {
private:
  /**
   * @brief 服务器监听端口号
   * @note 默认值为15000（与common.h中DEFAULT_PORT一致）
   */
  int port;

  /**
   * @brief epoll实例的文件描述符
   * @note 用于管理所有监听的文件描述符及事件，初始化后在析构函数中关闭
   */
  int epoll_fd;

  /**
   * @brief 服务器监听socket的文件描述符
   * @note 用于接收客户端新连接，初始化后在析构函数中关闭
   */
  int listen_fd;

  /**
   * @brief 私有方法：将指定文件描述符设置为非阻塞模式
   * @param fd 目标文件描述符（监听fd或客户端连接fd）
   * @note 通过fcntl修改文件描述符标志位，实现非阻塞IO操作
   */
  void set_nonblocking(int fd);

  /**
   * @brief 私有方法：处理新的客户端连接请求
   * @note 循环调用accept4接受所有待处理连接，为新连接设置非阻塞并注册到epoll
   */
  void handle_new_connection();

  /**
   * @brief 私有方法：处理客户端连接的数据读写
   * @param conn_fd 客户端连接的文件描述符
   * @note 读取客户端发送的数据并原样回射，处理连接关闭、读写错误等情况
   */
  void handle_client_data(int conn_fd);

public:
  /**
   * @brief 构造函数：初始化服务器监听端口及核心文件描述符
   * @param port 服务器监听端口，默认值为15000
   * @note 初始化port为传入值，epoll_fd和listen_fd初始化为-1（未创建状态）
   */
  EchoServer(int port = 15000);

  /**
   * @brief 析构函数：释放服务器资源
   * @note 关闭epoll实例和监听socket，避免文件描述符泄漏
   */
  ~EchoServer();

  /**
   * @brief 初始化服务器：创建监听socket、epoll实例，完成端口绑定与监听
   * @note
   * 包含socket创建、选项配置（SO_REUSEADDR/SO_REUSEPORT）、地址绑定、epoll注册等步骤
   */
  void init();

  /**
   * @brief 服务器主循环：启动epoll事件监听与处理
   * @note
   * 循环等待epoll事件，分发处理新连接、客户端数据、连接异常等事件，定期输出性能监控
   */
  void run();
};

// 头文件保护宏结束
#endif // ECHO_SERVER_H