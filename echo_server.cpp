#include "echo_server.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <thread>
#include <chrono>
#include <cstring>

EchoServer::EchoServer(int port) 
    : server_fd(-1), epoll_fd(-1), port(port), running(false) {}

EchoServer::~EchoServer() {
    running = false;
    if (server_fd != -1) close(server_fd);
    if (epoll_fd != -1) close(epoll_fd);
    // 清理缓冲区和互斥锁
    std::lock_guard<std::mutex> lock(buffer_mutex);
    client_buffers.clear();
    std::lock_guard<std::mutex> fd_lock(fd_mutex_map_mutex);
    fd_mutexes.clear();
}

int EchoServer::set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void EchoServer::close_client(int client_fd) {
    log_info("Closed connection for fd: " + std::to_string(client_fd));
    close(client_fd);
    // 清理缓冲区和互斥锁
    std::lock_guard<std::mutex> lock(buffer_mutex);
    client_buffers.erase(client_fd);
    std::lock_guard<std::mutex> fd_lock(fd_mutex_map_mutex);
    fd_mutexes.erase(client_fd);
}

// 获取FD对应的互斥锁（不存在则创建）
std::mutex& EchoServer::get_fd_mutex(int client_fd) {
    std::lock_guard<std::mutex> lock(fd_mutex_map_mutex);
    return fd_mutexes[client_fd];
}

void EchoServer::handle_client_data(int client_fd) {
    std::lock_guard<std::mutex> fd_lock(get_fd_mutex(client_fd));

    std::unique_ptr<char[]> buffer;
    {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        auto it = client_buffers.find(client_fd);
        if (it == client_buffers.end()) {
            log_error("Client buffer not found (fd: " + std::to_string(client_fd) + ")");
            close_client(client_fd);
            return;
        }
        buffer = std::move(it->second);
        client_buffers.erase(it);
    }

    memset(buffer.get(), 0, BUFFER_SIZE);

    // 关键修改：循环读取，直到读满12字节报文头（绝不中途放弃）
    MessageHeader header;
    size_t header_read = 0;
    const size_t header_total = sizeof(MessageHeader);
    auto header_start = std::chrono::high_resolution_clock::now();
    const std::chrono::seconds header_timeout(3); // 报文头读取超时3秒

    while (header_read < header_total) {
        // 计算剩余未读字节
        size_t remaining = header_total - header_read;
        ssize_t bytes_read = read(client_fd, (char*)&header + header_read, remaining);

        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 数据未到，短暂延迟后重试
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                // 检查超时
                auto now = std::chrono::high_resolution_clock::now();
                if (now - header_start > header_timeout) {
                    log_error("Header read timeout (fd: " + std::to_string(client_fd) + ")");
                    close_client(client_fd);
                    return;
                }
                continue;
            } else {
                log_error("Read header failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        } else if (bytes_read == 0) {
            log_info("Client disconnected (fd: " + std::to_string(client_fd) + ") while reading header");
            close_client(client_fd);
            return;
        }

        header_read += bytes_read;
        log_info("FD " + std::to_string(client_fd) + " read header chunk: " + std::to_string(bytes_read) + " bytes (total: " + std::to_string(header_read) + "/" + std::to_string(header_total) + ")");
    }

    // 校验魔数
    uint32_t magic_host = ntohl(header.magic);
    log_info("FD " + std::to_string(client_fd) + " received magic: " +
             "net=0x" + std::to_string(header.magic) + ", " +
             "host=0x" + std::to_string(magic_host) + ", " +
             "expected=0x" + std::to_string(MAGIC_NUMBER));
    if (magic_host != MAGIC_NUMBER) {
        log_error("Invalid magic number (fd: " + std::to_string(client_fd) + ")");
        close_client(client_fd);
        return;
    }

    // 校验数据长度和消息ID
    uint32_t data_len = ntohl(header.data_len);
    uint32_t msg_id = ntohl(header.msg_id);
    if (data_len <= 0 || data_len > BUFFER_SIZE) {
        log_error("Invalid data length (" + std::to_string(data_len) + ") (fd: " + std::to_string(client_fd) + ")");
        close_client(client_fd);
        return;
    }

    // 读取完整数据
    size_t data_read = 0;
    auto data_start = std::chrono::high_resolution_clock::now();
    const std::chrono::seconds data_timeout(5);
    while (data_read < data_len) {
        size_t remaining = data_len - data_read;
        ssize_t bytes_read = read(client_fd, buffer.get() + data_read, remaining);

        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::microseconds(200));
                auto now = std::chrono::high_resolution_clock::now();
                if (now - data_start > data_timeout) {
                    log_error("Data read timeout (fd: " + std::to_string(client_fd) + ")");
                    close_client(client_fd);
                    return;
                }
                continue;
            } else {
                log_error("Read data failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        } else if (bytes_read == 0) {
            log_info("Client disconnected (fd: " + std::to_string(client_fd) + ") while reading data");
            close_client(client_fd);
            return;
        }

        data_read += bytes_read;
    }

    // 回射数据（延长延迟，确保客户端接收完整）
    size_t total_written = 0;
    while (total_written < header_total) {
        ssize_t bytes_written = write(client_fd, &header, header_total - total_written);
        if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
                continue;
            } else {
                log_error("Write header failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        }
        total_written += bytes_written;
    }

    total_written = 0;
    while (total_written < data_len) {
        ssize_t bytes_written = write(client_fd, buffer.get() + total_written, data_len - total_written);
        if (bytes_written == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                std::this_thread::sleep_for(std::chrono::microseconds(1000));
                continue;
            } else {
                log_error("Write data failed (fd: " + std::to_string(client_fd) + ")");
                close_client(client_fd);
                return;
            }
        }
        total_written += bytes_written;
    }

    // 终极清理：读空所有残留数据（包括粘包数据）
    char dummy[4096];
    while (true) {
        ssize_t leftover = read(client_fd, dummy, sizeof(dummy));
        if (leftover <= 0) break;
        log_info("FD " + std::to_string(client_fd) + " cleared leftover: " + std::to_string(leftover) + " bytes");
    }

    // 处理完后延迟500微秒，避免快速注册事件导致冲突
    std::this_thread::sleep_for(std::chrono::microseconds(500));

    // 归还缓冲区并重新注册事件
    std::lock_guard<std::mutex> lock(buffer_mutex);
    client_buffers[client_fd] = std::move(buffer);
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    event.data.fd = client_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, client_fd, &event);

    log_info("Processed msg_id: " + std::to_string(msg_id) + ", fd: " + std::to_string(client_fd));
}

void EchoServer::start() {
    // 创建监听套接字
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        log_error("Socket creation failed (errno: " + std::to_string(errno) + ")");
        return;
    }

    // 端口复用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        log_error("Setsockopt failed (errno: " + std::to_string(errno) + ")");
        close(server_fd);
        return;
    }

    // 绑定端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        log_error("Bind failed (errno: " + std::to_string(errno) + ")");
        close(server_fd);
        return;
    }

    // 监听
    if (listen(server_fd, 1024) == -1) {
        log_error("Listen failed (errno: " + std::to_string(errno) + ")");
        close(server_fd);
        return;
    }

    // 非阻塞
    if (set_nonblocking(server_fd) == -1) {
        log_error("Set nonblocking failed for server_fd");
        close(server_fd);
        return;
    }

    // 创建epoll
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        log_error("Epoll create failed");
        close(server_fd);
        return;
    }

    // 注册监听套接字
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        log_error("Epoll add server_fd failed");
        close(epoll_fd);
        close(server_fd);
        return;
    }

    log_info("Server initialized on port " + std::to_string(port));
    log_info("Server started, waiting for connections...");
    running = true;

    // 事件循环
    const int MAX_EVENTS = 1024;
    struct epoll_event events[MAX_EVENTS];
    while (running) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            if (errno == EINTR) continue;
            log_error("Epoll wait failed");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == server_fd) {
                // 新连接
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
                    log_error("Accept failed");
                    continue;
                }

                // 非阻塞
                if (set_nonblocking(client_fd) == -1) {
                    log_error("Set nonblocking failed for client_fd");
                    close(client_fd);
                    continue;
                }

                // 分配缓冲区
                std::lock_guard<std::mutex> lock(buffer_mutex);
                client_buffers[client_fd] = std::make_unique<char[]>(BUFFER_SIZE);

                // 注册客户端事件
                struct epoll_event client_event;
                client_event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                client_event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) == -1) {
                    log_error("Epoll add client_fd failed");
                    close_client(client_fd);
                    continue;
                }

                log_info("New connection from " + std::string(inet_ntoa(client_addr.sin_addr)) + 
                         ":" + std::to_string(ntohs(client_addr.sin_port)) + " (fd: " + std::to_string(client_fd) + ")");
            } else {
                // 处理客户端数据（已通过FD互斥锁保证串行）
                handle_client_data(events[i].data.fd);
            }
        }
    }
}