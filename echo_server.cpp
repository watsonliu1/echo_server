#include "echo_server.h"
#include <sys/socket.h>       // 用于socket相关系统调用
#include <netinet/in.h>       // 用于网络地址结构（sockaddr_in）
#include <arpa/inet.h>        // 用于IP地址转换（inet_pton等）
#include <unistd.h>           // 用于close等系统调用
#include <fcntl.h>            // 用于fcntl（设置非阻塞）
#include <errno.h>            // 用于错误码（errno）
#include <thread>             // 用于线程相关操作
#include <chrono>             // 用于时间相关操作（超时控制）
#include <cstring>            // 用于内存操作（memset等）

// 构造函数：初始化服务器状态
// 参数：port - 监听端口
EchoServer::EchoServer(int port) 
    : server_fd(-1), epoll_fd(-1), port(port), running(false) {}

// 析构函数：停止服务器并释放资源
EchoServer::~EchoServer() {
    running = false;  // 停止事件循环
    // 关闭监听套接字和epoll句柄（若已初始化）
    if (server_fd != -1) close(server_fd);
    if (epoll_fd != -1) close(epoll_fd);
    // 清理客户端缓冲区（加锁保证线程安全）
    std::lock_guard<std::mutex> lock(buffer_mutex);
    client_buffers.clear();
    // 清理客户端fd对应的互斥锁（加锁保证线程安全）
    std::lock_guard<std::mutex> fd_lock(fd_mutex_map_mutex);
    fd_mutexes.clear();
}

// 设置文件描述符为非阻塞模式
// 作用：避免IO操作（read/write）阻塞进程，提高并发处理能力
// 参数：fd - 目标文件描述符
// 返回：0成功，-1失败
int EchoServer::set_nonblocking(int fd) {
    // 获取当前文件描述符的标志
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;  // 获取失败
    // 设置非阻塞标志（O_NONBLOCK）
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 关闭客户端连接并清理资源
// 作用：释放客户端相关的文件描述符、缓冲区和互斥锁
// 参数：client_fd - 客户端文件描述符
void EchoServer::close_client(int client_fd) {
    log_info("Closed connection for fd: " + std::to_string(client_fd));
    close(client_fd);  // 关闭客户端套接字
    // 清理缓冲区（加锁保证线程安全）
    std::lock_guard<std::mutex> lock(buffer_mutex);
    client_buffers.erase(client_fd);
    // 清理该fd对应的互斥锁（加锁保证线程安全）
    std::lock_guard<std::mutex> fd_lock(fd_mutex_map_mutex);
    fd_mutexes.erase(client_fd);
}

// 获取客户端fd对应的互斥锁（不存在则创建）
// 作用：确保同一客户端的IO操作串行化，避免多线程同时读写导致数据混乱
// 参数：client_fd - 客户端文件描述符
// 返回：该fd对应的互斥锁引用
std::mutex& EchoServer::get_fd_mutex(int client_fd) {
    std::lock_guard<std::mutex> lock(fd_mutex_map_mutex);  // 保护fd_mutexes的访问
    // 若不存在则默认构造一个新的mutex，返回引用
    return fd_mutexes[client_fd];
}

// 处理客户端数据：读取完整消息、校验、回射
// 参数：client_fd - 客户端文件描述符
void EchoServer::handle_client_data(int client_fd) {
    // 加锁：确保同一客户端的操作串行执行（避免数据竞争）
    std::lock_guard<std::mutex> fd_lock(get_fd_mutex(client_fd));

    // 获取该客户端的缓冲区（智能指针管理，自动释放）
    std::unique_ptr<char[]> buffer;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);  // 保护client_buffers
        auto it = client_buffers.find(client_fd);
        if (it == client_buffers.end()) {  // 缓冲区不存在（异常情况）
            log_error("Client buffer not found (fd: " + std::to_string(client_fd) + ")");
            close_client(client_fd);
            return;
        }
        buffer = std::move(it->second);  // 转移缓冲区所有权
        client_buffers.erase(it);        // 临时移除，处理完后归还
    }

    // 清空缓冲区（避免残留数据影响）
    memset(buffer.get(), 0, BUFFER_SIZE);

    // 读取完整报文头（固定12字节），循环读取直到读满（防止数据分片）
    MessageHeader header;
    size_t header_read = 0;  // 已读取的报文头字节数
    const size_t header_total = sizeof(MessageHeader);  // 报文头总字节数（12）
    auto header_start = std::chrono::high_resolution_clock::now();  // 开始读取时间
    const std::chrono::seconds header_timeout(3);  // 报文头读取超时时间（3秒）

    while (header_read < header_total) {
        size_t remaining = header_total - header_read;  // 剩余未读字节数
        // 读取剩余部分到header结构体中（从当前已读位置开始）
        ssize_t bytes_read = read(client_fd, (char*)&header + header_read, remaining);

        if (bytes_read == -1) {  // 读取失败
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 非阻塞模式下暂时无数据
                std::this_thread::sleep_for(std::chrono::microseconds(200));  // 短暂等待后重试
                // 检查是否超时
                auto now = std::chrono::high_resolution_clock::now();
                if (now - header_start > header_timeout) {
                    log_error("Header read timeout (fd: " + std::to_string(client_fd) + ")");
                    close_client(client_fd);
                    return;
                }
                continue;  // 继续重试
            } else {  // 其他错误（如连接断开）
                log_error("Read header failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        } else if (bytes_read == 0) {  // 客户端主动断开连接
            log_info("Client disconnected (fd: " + std::to_string(client_fd) + ") while reading header");
            close_client(client_fd);
            return;
        }

        // 累加已读字节数并日志记录
        header_read += bytes_read;
        log_info("FD " + std::to_string(client_fd) + " read header chunk: " + 
                 std::to_string(bytes_read) + " bytes (total: " + 
                 std::to_string(header_read) + "/" + std::to_string(header_total) + ")");
    }

    // 校验魔数（网络字节序转主机字节序后对比）
    uint32_t magic_host = ntohl(header.magic);  // 网络字节序（大端）转主机字节序
    log_info("FD " + std::to_string(client_fd) + " received magic: " +
             "net=0x" + std::to_string(header.magic) + ", " +  // 网络字节序原始值
             "host=0x" + std::to_string(magic_host) + ", " +  // 转换后的值
             "expected=0x" + std::to_string(MAGIC_NUMBER));   // 预期魔数
    if (magic_host != MAGIC_NUMBER) {  // 魔数不匹配（非法消息）
        log_error("Invalid magic number (fd: " + std::to_string(client_fd) + ")");
        close_client(client_fd);
        return;
    }

    // 解析数据长度和消息ID（网络字节序转主机字节序）
    uint32_t data_len = ntohl(header.data_len);  // 数据部分长度
    uint32_t msg_id = ntohl(header.msg_id);      // 消息ID
    // 校验数据长度合法性（必须为正数且不超过缓冲区大小）
    if (data_len <= 0 || data_len > BUFFER_SIZE) {
        log_error("Invalid data length (" + std::to_string(data_len) + ") (fd: " + std::to_string(client_fd) + ")");
        close_client(client_fd);
        return;
    }

    // 读取完整数据部分（根据报文头的data_len）
    size_t data_read = 0;  // 已读取的数据字节数
    auto data_start = std::chrono::high_resolution_clock::now();  // 开始读取时间
    const std::chrono::seconds data_timeout(5);  // 数据读取超时时间（5秒）
    while (data_read < data_len) {
        size_t remaining = data_len - data_read;  // 剩余未读字节数
        // 读取剩余数据到缓冲区（从当前已读位置开始）
        ssize_t bytes_read = read(client_fd, buffer.get() + data_read, remaining);

        if (bytes_read == -1) {  // 读取失败
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 暂时无数据
                std::this_thread::sleep_for(std::chrono::microseconds(200));  // 等待后重试
                // 检查超时
                auto now = std::chrono::high_resolution_clock::now();
                if (now - data_start > data_timeout) {
                    log_error("Data read timeout (fd: " + std::to_string(client_fd) + ")");
                    close_client(client_fd);
                    return;
                }
                continue;
            } else {  // 其他错误
                log_error("Read data failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        } else if (bytes_read == 0) {  // 客户端断开连接
            log_info("Client disconnected (fd: " + std::to_string(client_fd) + ") while reading data");
            close_client(client_fd);
            return;
        }

        data_read += bytes_read;  // 累加已读字节数
    }

    // 回射数据：将接收到的报文头和数据原样返回给客户端
    // 先回射报文头
    size_t total_written = 0;  // 已写入的字节数
    while (total_written < header_total) {
        // 写入剩余的报文头部分
        ssize_t bytes_written = write(client_fd, &header, header_total - total_written);
        if (bytes_written == -1) {  // 写入失败
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 缓冲区满，暂时无法写入
                std::this_thread::sleep_for(std::chrono::microseconds(1000));  // 等待后重试
                continue;
            } else {  // 其他错误
                log_error("Write header failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        }
        total_written += bytes_written;  // 累加已写入字节数
    }

    // 再回射数据部分
    total_written = 0;
    while (total_written < data_len) {
        // 写入剩余的数据部分
        ssize_t bytes_written = write(client_fd, buffer.get() + total_written, data_len - total_written);
        if (bytes_written == -1) {  // 写入失败
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // 缓冲区满
                std::this_thread::sleep_for(std::chrono::microseconds(1000));  // 等待后重试
                continue;
            } else {  // 其他错误
                log_error("Write data failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        }
        total_written += bytes_written;  // 累加已写入字节数
    }

    // 清理残留数据（处理粘包：读取所有未处理的剩余数据，避免影响下一条消息）
    char dummy[4096];  // 临时缓冲区
    while (true) {
        ssize_t leftover = read(client_fd, dummy, sizeof(dummy));  // 读取残留数据
        if (leftover <= 0) break;  // 无残留数据或读取失败（正常退出）
        log_info("FD " + std::to_string(client_fd) + " cleared leftover: " + std::to_string(leftover) + " bytes");
    }

    // 短暂延迟：避免快速重新注册事件导致的冲突
    std::this_thread::sleep_for(std::chrono::microseconds(500));

    // 归还缓冲区并重新注册epoll事件（继续监听该客户端的新数据）
    std::lock_guard<std::mutex> lock(buffer_mutex);
    client_buffers[client_fd] = std::move(buffer);  // 归还缓冲区所有权
    struct epoll_event event;
    // 事件类型：可读（EPOLLIN）、边缘触发（EPOLLET）、一次性触发（EPOLLONESHOT）
    // 注：EPOLLONESHOT确保事件只被一个线程处理，处理完需重新注册
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    event.data.fd = client_fd;  // 关联客户端fd
    // 修改epoll事件（重新启用监听）
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event);

    log_info("Processed msg_id: " + std::to_string(msg_id) + ", fd: " + std::to_string(client_fd));
}

// 启动服务器：初始化并进入事件循环
void EchoServer::start()
{
    // 创建监听套接字（IPv4，TCP）
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {  // 创建失败
        log_error("Socket creation failed (errno: " + std::to_string(errno) + ")");
        return;
    }

    // 设置端口复用（避免服务器重启时端口被占用的TIME_WAIT状态阻塞）
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        log_error("Setsockopt failed (errno: " + std::to_string(errno) + ")");
        close(server_fd);  // 清理资源
        return;
    }

    // 绑定套接字到指定端口
    struct sockaddr_in server_addr;  // 服务器地址结构
    memset(&server_addr, 0, sizeof(server_addr));  // 清零
    server_addr.sin_family = AF_INET;  // IPv4
    server_addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有网络接口
    server_addr.sin_port = htons(port);  // 端口（主机字节序转网络字节序）
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        log_error("Bind failed (errno: " + std::to_string(errno) + ")");
        close(server_fd);
        return;
    }

    // 开始监听（最大等待队列长度1024）
    if (listen(server_fd, 1024) == -1) {
        log_error("Listen failed (errno: " + std::to_string(errno) + ")");
        close(server_fd);
        return;
    }

    // 设置监听套接字为非阻塞模式
    if (set_nonblocking(server_fd) == -1) {
        log_error("Set nonblocking failed for server_fd");
        close(server_fd);
        return;
    }

    // 创建epoll实例（参数0表示不使用标志）
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        log_error("Epoll create failed");
        close(server_fd);
        return;
    }

    // 注册监听套接字到epoll（监听可读事件，边缘触发）
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;  // 可读事件，边缘触发
    event.data.fd = server_fd;         // 关联监听套接字
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {  // 添加事件
        log_error("Epoll add server_fd failed");
        close(epoll_fd);
        close(server_fd);
        return;
    }

    log_info("Server initialized on port " + std::to_string(port));
    log_info("Server started, waiting for connections...");
    running = true;  // 标记服务器运行中

    // 事件循环：持续处理epoll事件
    const int MAX_EVENTS = 1024;  // 一次最多处理的事件数
    struct epoll_event events[MAX_EVENTS];  // 存储就绪事件的数组

    while (running) {
        // 等待事件就绪（超时-1表示无限等待）
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events == -1) {  // 等待失败
            if (errno == EINTR) continue;  // 被信号中断，继续循环
            log_error("Epoll wait failed");
            break;  // 其他错误，退出循环
        }

        // 处理每个就绪事件
        for (int i = 0; i < num_events; ++i) {
            int fd = events[i].data.fd;  // 事件关联的文件描述符

            // 监听套接字就绪：有新连接到来
            if (fd == server_fd) {
                while (true) {  // 循环接受所有新连接（边缘触发需一次性处理完）
                    struct sockaddr_in client_addr;  // 客户端地址结构
                    socklen_t client_len = sizeof(client_addr);
                    // 接受新连接（非阻塞模式）
                    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    if (client_fd == -1) {  // 接受失败
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;  // 没有更多连接，退出循环
                        } else {
                            log_error("Accept failed (errno: " + std::to_string(errno) + ")");
                            break;
                        }
                    }

                    // 输出客户端连接信息
                    log_info("New connection from " + std::string(inet_ntoa(client_addr.sin_addr)) + 
                             ":" + std::to_string(ntohs(client_addr.sin_port)) + 
                             " (fd: " + std::to_string(client_fd) + ")");

                    // 设置客户端套接字为非阻塞模式
                    if (set_nonblocking(client_fd) == -1) {
                        log_error("Set nonblocking failed for client_fd: " + std::to_string(client_fd));
                        close(client_fd);
                        continue;
                    }

                    // 为客户端分配缓冲区（4096字节）
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    client_buffers[client_fd] = std::make_unique<char[]>(BUFFER_SIZE);

                    // 注册客户端套接字到epoll（可读事件，边缘触发，一次性触发）
                    struct epoll_event client_event;
                    client_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    client_event.data.fd = client_fd;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
                        log_error("Epoll add client_fd failed (fd: " + std::to_string(client_fd) + ")");
                        close_client(client_fd);  // 清理失败的客户端
                    }
                }
            } else {  // 客户端套接字就绪：有数据可读
                // 启动线程处理客户端数据（避免阻塞事件循环）
                std::thread(&EchoServer::handle_client_data, this, fd).detach();
            }
        }
    }
}