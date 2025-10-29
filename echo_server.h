#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H
/**
 * @file echo_server.h
 * @brief 回声服务器核心类定义，实现基于epoll的高并发TCP回声服务
 * @details 该类封装了服务器的初始化、事件循环、连接管理、数据处理等核心功能，
 *          采用epoll边缘触发（EPOLLET）+ 非阻塞I/O模型，支持大量并发客户端连接，
 *          核心功能是接收客户端消息并原样回射（Echo）
 */

#include "common.h"           // 引入公共配置（端口、缓冲区大小等）
#include <sys/epoll.h>        // epoll相关系统调用（epoll_create, epoll_ctl等）
#include <sys/socket.h>       // 套接字操作（socket, bind, listen等）
#include <netinet/in.h>       // 网络地址结构体（sockaddr_in等）
#include <unordered_map>      // 哈希表容器，用于管理客户端缓冲区
#include <mutex>              // 互斥锁，保证多线程安全访问共享资源
#include <string>             // 字符串处理（可选，用于日志等）

/**
 * @class EchoServer
 * @brief 回声服务器核心类，封装服务器的所有功能
 * @details 负责创建监听套接字、管理epoll事件、处理客户端连接与数据交互，
 *          采用面向对象设计，隐藏内部实现细节，提供简洁的初始化和运行接口
 */
class EchoServer
{
private:
    int epoll_fd;                          ///< epoll实例的文件描述符，用于事件管理
    int listen_fd;                         ///< 监听套接字的文件描述符，用于接收新连接
    std::unordered_map<int, char*> client_buffers;  ///< 客户端缓冲区映射表：键为客户端fd，值为缓冲区指针
    std::mutex buffer_mutex;               ///< 保护client_buffers的互斥锁，避免并发访问冲突

    /**
     * @brief 设置文件描述符为非阻塞模式
     * @param fd 待设置的文件描述符（监听套接字或客户端fd）
     * @return 成功返回true，失败返回false
     * @note 非阻塞模式是epoll边缘触发（EPOLLET）的必要条件，确保I/O操作不阻塞进程
     */
    bool set_non_blocking(int fd);

    /**
     * @brief 将文件描述符添加到epoll实例，注册关注的事件
     * @param fd 待添加的文件描述符
     * @param events 关注的事件类型（如EPOLLIN | EPOLLET：边缘触发的读事件）
     * @note 新连接的客户端fd和监听fd都需要通过此函数加入epoll监控
     */
    void add_to_epoll(int fd, uint32_t events);

    /**
     * @brief 将文件描述符从epoll实例中移除
     * @param fd 待移除的文件描述符（通常是断开连接的客户端fd）
     * @note 客户端断开后必须移除，避免epoll持续触发无效事件
     */
    void remove_from_epoll(int fd);

    /**
     * @brief 处理监听套接字上的新客户端连接请求
     * @details 当epoll检测到listen_fd有EPOLLIN事件时调用，执行accept、设置非阻塞、
     *          分配缓冲区、注册epoll事件等操作，完成新连接建立
     */
    void handle_new_connection();

    /**
     * @brief 处理客户端发送的数据（核心业务逻辑）
     * @param client_fd 客户端套接字的文件描述符
     * @details 当epoll检测到客户端fd有EPOLLIN事件时调用，读取数据并原样回射给客户端，
     *          使用client_buffers中对应的缓冲区暂存数据
     */
    void handle_client_data(int client_fd);

    /**
     * @brief 关闭客户端连接并清理资源
     * @param client_fd 客户端套接字的文件描述符
     * @details 包括从epoll移除fd、关闭套接字、释放缓冲区内存、从映射表删除记录
     */
    void close_client(int client_fd);

public:
    /**
     * @brief 构造函数，初始化成员变量
     * @note 将epoll_fd和listen_fd初始化为-1（无效值），避免与有效fd冲突
     */
    EchoServer() : epoll_fd(-1), listen_fd(-1) {}

    /**
     * @brief 析构函数，自动关闭服务器
     * @note 调用shutdown()确保资源释放，即使用户未显式调用也能避免资源泄漏
     */
    ~EchoServer() { shutdown(); }

    /**
     * @brief 初始化服务器（核心初始化接口）
     * @param port 服务器绑定的端口号，默认使用common.h中的DEFAULT_PORT
     * @return 初始化成功返回true，失败返回false
     * @details 执行创建监听套接字、设置端口复用、绑定地址、监听连接、
     *          创建epoll实例、将监听fd加入epoll等初始化操作
     */
    bool init(int port = DEFAULT_PORT);

    /**
     * @brief 启动服务器事件循环（核心运行接口）
     * @details 进入epoll_wait循环，持续等待并处理事件（新连接、客户端数据、错误等），
     *          直到服务器被中断（如收到SIGINT信号）
     */
    void run();

    /**
     * @brief 关闭服务器并释放所有资源
     * @details 包括关闭epoll实例、监听套接字、所有客户端连接，释放缓冲区内存，
     *          确保程序退出时无资源泄漏
     */
    void shutdown();
};

#endif  // ECHO_SERVER_H  防止头文件重复包含的宏定义结束