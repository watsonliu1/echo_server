#include "echo_client.h"
#include <sys/socket.h>       // 用于socket相关系统调用
#include <netinet/in.h>       // 用于网络地址结构
#include <arpa/inet.h>        // 用于IP地址转换
#include <unistd.h>           // 用于close等系统调用
#include <fcntl.h>            // 用于fcntl（设置非阻塞）
#include <errno.h>            // 用于错误码
#include <chrono>             // 用于时间操作（超时控制）
#include <memory>             // 用于智能指针
#include <cstring>            // 用于内存操作
#include <sstream>            // 用于字符串流（线程ID处理）
#include <vector>             // 用于存储线程对象

// 初始化静态原子变量（统计计数器）
std::atomic<int> EchoClient::total_connections(0);
std::atomic<int> EchoClient::total_sent(0);
std::atomic<int> EchoClient::total_received(0);
std::atomic<int> EchoClient::total_errors(0);

// 构造函数：初始化配置
EchoClient::EchoClient(const ClientConfig& cfg) : config(cfg) {}

// 获取当前线程ID的后3位（用于日志区分线程）
std::string EchoClient::get_thread_id() {
    std::stringstream ss;
    ss << std::this_thread::get_id();  // 将线程ID写入字符串流
    std::string id_str = ss.str();
    // 返回最后3个字符（简化线程标识）
    return id_str.substr(id_str.size() - 3);
}

// 处理正常连接：建立连接，发送消息并验证回射
void EchoClient::handle_normal_connection() {
    std::string thread_id = get_thread_id();  // 获取线程标识
    log_info("[Thread " + thread_id + "] Starting normal connection");

    int sockfd = -1;  // 客户端套接字
    try {
        // 创建套接字（IPv4，TCP）
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) {  // 创建失败
            log_error("[Thread " + thread_id + "] Socket creation failed (errno: " + std::to_string(errno) + ")");
            total_errors++;  // 统计错误
            return;
        }
        log_info("[Thread " + thread_id + "] Created socket (fd: " + std::to_string(sockfd) + ")");

        // 设置套接字为非阻塞模式（避免recv阻塞线程）
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        // 初始化服务器地址结构
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;  // IPv4
        server_addr.sin_port = htons(config.server_port);  // 端口（主机转网络字节序）
        // 转换服务器IP字符串为网络字节序
        if (inet_pton(AF_INET, config.server_ip.c_str(), &server_addr.sin_addr) <= 0) {
            log_error("[Thread " + thread_id + "] Invalid server IP");
            close(sockfd);
            total_errors++;
            return;
        }

        // 连接服务器（非阻塞模式，可能返回EINPROGRESS表示正在连接）
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1 && errno != EINPROGRESS) {
            log_error("[Thread " + thread_id + "] Connect failed (errno: " + std::to_string(errno) + ")");
            close(sockfd);
            total_errors++;
            return;
        }
        total_connections++;  // 统计连接成功数
        log_info("[Thread " + thread_id + "] Connection established (fd: " + std::to_string(sockfd) + ")");

        // 准备发送的数据（指定大小，填充'a'）
        std::string send_data(config.message_size, 'a');
        // 接收缓冲区（智能指针管理）
        std::unique_ptr<char[]> recv_buffer(new char[config.message_size]);

        // 同步发送：每条消息必须收到回射后才发送下一条
        for (int i = 0; i < config.messages_per_conn; ++i) {
            // 构造报文头
            MessageHeader header;
            memset(&header, 0, sizeof(MessageHeader));
            header.magic = htonl(MAGIC_NUMBER);       // 魔数（主机转网络字节序）
            header.data_len = htonl(config.message_size);  // 数据长度
            header.msg_id = htonl(i);                 // 消息ID

            // 校验魔数转换是否正确（避免逻辑错误）
            uint32_t check_magic = ntohl(header.magic);
            if (check_magic != MAGIC_NUMBER) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " magic invalid");
                total_errors++;
                goto msg_timeout_exit;  // 跳转到清理部分
            }
            log_info("[Thread " + thread_id + "] Sending msg_id=" + std::to_string(i) + 
                     " (magic: 0x" + std::to_string(MAGIC_NUMBER) + ")");

            // 发送报文头
            ssize_t send_len = send(sockfd, &header, sizeof(MessageHeader), 0);
            if (send_len != sizeof(MessageHeader)) {  // 发送失败
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " header send failed");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 发送数据部分
            send_len = send(sockfd, send_data.c_str(), send_data.size(), 0);
            if (send_len != (ssize_t)send_data.size()) {  // 发送失败
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data send failed");
                total_errors++;
                goto msg_timeout_exit;
            }
            total_sent++;  // 统计发送成功数

            // 等待服务器回射消息
            MessageHeader recv_header;  // 接收的报文头
            ssize_t bytes_read = -1;    // 读取字节数
            int retry = 0;              // 重试次数
            const int max_retry = 5;    // 最大重试次数（5次）
            const int retry_delay_ms = 1000;  // 每次重试延迟（1秒）
            auto msg_start = std::chrono::high_resolution_clock::now();  // 开始等待时间
            const std::chrono::seconds msg_timeout(5);  // 总超时时间（5秒）

            // 读取回射的报文头（非阻塞+超时控制）
            while (retry < max_retry) {
                // 检查是否超时
                auto now = std::chrono::high_resolution_clock::now();
                if (now - msg_start > msg_timeout) {
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " header recv timeout (5s)");
                    total_errors++;
                    goto msg_timeout_exit;
                }

                // 读取报文头
                bytes_read = recv(sockfd, &recv_header, sizeof(MessageHeader), 0);
                if (bytes_read > 0) {
                    break;  // 读取成功，退出循环
                } else if (bytes_read == 0) {  // 服务器断开连接
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " server disconnected");
                    total_errors++;
                    goto msg_timeout_exit;
                } else {  // 读取失败
                    // 仅处理非阻塞导致的暂时无数据（EAGAIN/EWOULDBLOCK）
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " recv error (errno: " + std::to_string(errno) + ")");
                        total_errors++;
                        goto msg_timeout_exit;
                    }
                }

                // 重试前延迟并记录
                log_info("[Thread " + thread_id + "] Msg " + std::to_string(i) + " recv retry " + std::to_string(retry+1) + "/" + std::to_string(max_retry));
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                retry++;
            }

            // 检查报文头读取结果（重试耗尽或长度错误）
            if (bytes_read == -1 || bytes_read != sizeof(MessageHeader)) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " header recv failed (read: " + std::to_string(bytes_read) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 校验回射的魔数
            uint32_t recv_magic = ntohl(recv_header.magic);
            if (recv_magic != MAGIC_NUMBER) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " invalid recv magic (0x" + std::to_string(recv_magic) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 校验消息ID（确保回射的是当前消息）
            uint32_t recv_msg_id = ntohl(recv_header.msg_id);
            if (recv_msg_id != (uint32_t)i) {
                log_error("[Thread " + thread_id + "] Msg ID mismatch (expected: " + std::to_string(i) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 校验数据长度（确保回射的数据大小正确）
            uint32_t recv_data_len = ntohl(recv_header.data_len);
            if (recv_data_len != (uint32_t)config.message_size) {
                log_error("[Thread " + thread_id + "] Data len mismatch (expected: " + std::to_string(config.message_size) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 读取回射的数据部分（非阻塞+超时控制）
            bytes_read = -1;
            retry = 0;
            while (retry < max_retry) {
                // 检查超时
                auto now = std::chrono::high_resolution_clock::now();
                if (now - msg_start > msg_timeout) {
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data recv timeout (5s)");
                    total_errors++;
                    goto msg_timeout_exit;
                }

                // 读取数据
                bytes_read = recv(sockfd, recv_buffer.get(), recv_data_len, 0);
                if (bytes_read > 0) {
                    break;  // 读取成功
                } else if (bytes_read == 0) {  // 服务器断开
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " server disconnected");
                    total_errors++;
                    goto msg_timeout_exit;
                } else {  // 读取失败
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data recv error (errno: " + std::to_string(errno) + ")");
                        total_errors++;
                        goto msg_timeout_exit;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                retry++;
            }

            // 检查数据读取结果
            if (bytes_read == -1 || bytes_read != (ssize_t)recv_data_len) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data recv failed (read: " + std::to_string(bytes_read) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 校验数据内容（确保回射的数据与发送的一致）
            if (memcmp(recv_buffer.get(), send_data.c_str(), bytes_read) != 0) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data mismatch");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 成功接收并验证回射
            total_received++;
            log_info("[Thread " + thread_id + "] Received msg_id=" + std::to_string(i));

            // 发送下一条消息前短暂延迟（避免服务器压力过大）
            if (i < config.messages_per_conn - 1) {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        }

        // 所有消息处理完毕，关闭连接
        close(sockfd);
        log_info("[Thread " + thread_id + "] Connection closed normally");
        return;

    // 异常处理（若有异常抛出）
    } catch (const std::exception& e) {
        log_error("[Thread " + thread_id + "] Exception: " + std::string(e.what()));
        total_errors++;
    }

    // 清理标签（发生错误时跳转至此）
msg_timeout_exit:
    if (sockfd != -1) {
        close(sockfd);  // 关闭套接字
    }
    log_info("[Thread " + thread_id + "] Connection closed due to error");
}

// 运行客户端：根据配置创建线程处理连接
void EchoClient::run() {
    log_info("Starting client with config:");
    log_info("  Server IP: " + config.server_ip);
    log_info("  Server port: " + std::to_string(config.server_port));
    log_info("  Connections: " + std::to_string(config.connections));
    log_info("  Messages per connection: " + std::to_string(config.messages_per_conn));
    log_info("  Message size: " + std::to_string(config.message_size) + " bytes");

    // 存储线程对象的容器
    std::vector<std::thread> threads;
    threads.reserve(config.connections);  // 预分配空间

    // 根据配置的连接数创建线程
    for (int i = 0; i < config.connections; ++i) 
    {
        // 正常模式：每个线程处理一个连接
        threads.emplace_back(&EchoClient::handle_normal_connection, this);
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        if (t.joinable()) {
            t.join();
        }
    }

    // 打印统计信息
    print_stats();
}

// 打印统计信息
void EchoClient::print_stats() {
    log_info("\n===== Client Statistics =====");
    log_info("Total connections: " + std::to_string(total_connections));
    log_info("Total messages sent: " + std::to_string(total_sent));
    log_info("Total messages received (verified): " + std::to_string(total_received));
    log_info("Total errors: " + std::to_string(total_errors));
    log_info("=============================");
}