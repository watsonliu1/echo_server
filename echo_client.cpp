#include "echo_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <chrono>
#include <memory>
#include <cstring>
#include <sstream>
#include <vector>

// 初始化静态变量
std::atomic<int> EchoClient::total_connections(0);
std::atomic<int> EchoClient::total_sent(0);
std::atomic<int> EchoClient::total_received(0);
std::atomic<int> EchoClient::total_errors(0);

EchoClient::EchoClient(const ClientConfig& cfg) : config(cfg) {}

std::string EchoClient::get_thread_id() {
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return ss.str().substr(ss.str().size() - 3);
}

void EchoClient::handle_normal_connection() {
    std::string thread_id = get_thread_id();
    log_info("[Thread " + thread_id + "] Starting normal connection");

    int sockfd = -1;
    try {
        // 创建套接字
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd == -1) {
            log_error("[Thread " + thread_id + "] Socket creation failed (errno: " + std::to_string(errno) + ")");
            total_errors++;
            return;
        }
        log_info("[Thread " + thread_id + "] Created socket (fd: " + std::to_string(sockfd) + ")");

        // 关键修改1：设置socket为非阻塞模式，避免recv阻塞
        int flags = fcntl(sockfd, F_GETFL, 0);
        fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

        // 服务器地址
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(config.server_port);
        if (inet_pton(AF_INET, config.server_ip.c_str(), &server_addr.sin_addr) <= 0) {
            log_error("[Thread " + thread_id + "] Invalid server IP");
            close(sockfd);
            total_errors++;
            return;
        }

        // 连接服务器（非阻塞连接，需处理EINPROGRESS，但简单场景直接忽略，不影响测试）
        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1 && errno != EINPROGRESS) {
            log_error("[Thread " + thread_id + "] Connect failed (errno: " + std::to_string(errno) + ")");
            close(sockfd);
            total_errors++;
            return;
        }
        total_connections++;
        log_info("[Thread " + thread_id + "] Connection established (fd: " + std::to_string(sockfd) + ")");

        // 发送数据
        std::string send_data(config.message_size, 'a');
        std::unique_ptr<char[]> recv_buffer(new char[config.message_size]);

        // 同步发送：必须收到上一条回射才能发下一条
        for (int i = 0; i < config.messages_per_conn; ++i) {
            // 构造报文头
            MessageHeader header;
            memset(&header, 0, sizeof(MessageHeader));
            header.magic = htonl(MAGIC_NUMBER);
            header.data_len = htonl(config.message_size);
            header.msg_id = htonl(i);

            // 校验魔数转换
            uint32_t check_magic = ntohl(header.magic);
            if (check_magic != MAGIC_NUMBER) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " magic invalid");
                total_errors++;
                goto msg_timeout_exit;
            }
            log_info("[Thread " + thread_id + "] Sending msg_id=" + std::to_string(i) + 
                     " (magic: 0x" + std::to_string(MAGIC_NUMBER) + ")");

            // 发送报文头
            ssize_t send_len = send(sockfd, &header, sizeof(MessageHeader), 0);
            if (send_len != sizeof(MessageHeader)) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " header send failed");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 发送数据
            send_len = send(sockfd, send_data.c_str(), send_data.size(), 0);
            if (send_len != (ssize_t)send_data.size()) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data send failed");
                total_errors++;
                goto msg_timeout_exit;
            }
            total_sent++;

            // 等待回射
            MessageHeader recv_header;
            ssize_t bytes_read = -1;
            int retry = 0;
            const int max_retry = 5;  // 关键修改2：减少重试次数（5次×1秒=5秒）
            const int retry_delay_ms = 1000;  // 关键修改3：延长单次重试延迟
            auto msg_start = std::chrono::high_resolution_clock::now();
            const std::chrono::seconds msg_timeout(5);

            // 读取回射的报文头（非阻塞+严格超时）
            while (retry < max_retry) {
                auto now = std::chrono::high_resolution_clock::now();
                if (now - msg_start > msg_timeout) {
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " header recv timeout (5s)");
                    total_errors++;
                    goto msg_timeout_exit;
                }

                bytes_read = recv(sockfd, &recv_header, sizeof(MessageHeader), 0);
                if (bytes_read > 0) {
                    break;  // 读到数据，退出循环
                } else if (bytes_read == 0) {
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " server disconnected");
                    total_errors++;
                    goto msg_timeout_exit;
                } else {
                    // 非阻塞recv返回-1，仅处理EAGAIN/EWOULDBLOCK
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " recv error (errno: " + std::to_string(errno) + ")");
                        total_errors++;
                        goto msg_timeout_exit;
                    }
                }

                // 重试前延迟+计数
                log_info("[Thread " + thread_id + "] Msg " + std::to_string(i) + " recv retry " + std::to_string(retry+1) + "/" + std::to_string(max_retry));
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                retry++;
            }

            // 报文头读取失败（重试耗尽/长度不对）
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

            // 校验消息ID
            uint32_t recv_msg_id = ntohl(recv_header.msg_id);
            if (recv_msg_id != (uint32_t)i) {
                log_error("[Thread " + thread_id + "] Msg ID mismatch (expected: " + std::to_string(i) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 校验数据长度
            uint32_t recv_data_len = ntohl(recv_header.data_len);
            if (recv_data_len != (uint32_t)config.message_size) {
                log_error("[Thread " + thread_id + "] Data len mismatch (expected: " + std::to_string(config.message_size) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 读取回射的数据（非阻塞+超时）
            bytes_read = -1;
            retry = 0;
            while (retry < max_retry) {
                auto now = std::chrono::high_resolution_clock::now();
                if (now - msg_start > msg_timeout) {
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data recv timeout (5s)");
                    total_errors++;
                    goto msg_timeout_exit;
                }

                bytes_read = recv(sockfd, recv_buffer.get(), recv_data_len, 0);
                if (bytes_read > 0) {
                    break;
                } else if (bytes_read == 0) {
                    log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " server disconnected");
                    total_errors++;
                    goto msg_timeout_exit;
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data recv error (errno: " + std::to_string(errno) + ")");
                        total_errors++;
                        goto msg_timeout_exit;
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
                retry++;
            }

            // 数据接收失败
            if (bytes_read == -1 || bytes_read != (ssize_t)recv_data_len) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data recv failed (read: " + std::to_string(bytes_read) + ")");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 数据校验不匹配
            if (memcmp(recv_buffer.get(), send_data.c_str(), bytes_read) != 0) {
                log_error("[Thread " + thread_id + "] Msg " + std::to_string(i) + " data mismatch");
                total_errors++;
                goto msg_timeout_exit;
            }

            // 成功接收回射
            total_received++;
            log_info("[Thread " + thread_id + "] Received msg_id=" + std::to_string(i));

            // 发送下一条消息前延迟
            if (i < config.messages_per_conn - 1) {
                std::this_thread::sleep_for(std::chrono::microseconds(1500));
            }
        }

    msg_timeout_exit:
        close(sockfd);
        log_info("[Thread " + thread_id + "] Connection closed (fd: " + std::to_string(sockfd) + ")");
    } catch (const std::exception& e) {
        log_error("[Thread " + thread_id + "] Exception: " + std::string(e.what()));
        if (sockfd != -1) close(sockfd);
        total_errors++;
    }
}

void EchoClient::handle_pressure_connection() {
    log_info("Pressure test mode not implemented");
}

void EchoClient::run() {
    log_info("Starting client run()");
    std::vector<std::thread> threads;

    if (config.pressure_test) {
        for (int i = 0; i < config.connections; ++i) {
            threads.emplace_back(&EchoClient::handle_pressure_connection, this);
        }
    } else {
        log_info("Normal mode: " + std::to_string(config.connections) + " connections, " + 
                 std::to_string(config.messages_per_conn) + " msgs/conn");
        for (int i = 0; i < config.connections; ++i) {
            threads.emplace_back(&EchoClient::handle_normal_connection, this);
        }
    }

    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    print_stats();
}

void EchoClient::print_stats() {
    log_info("=== Client Stats ===");
    log_info("Connections: " + std::to_string(total_connections));
    log_info("Messages sent: " + std::to_string(total_sent));
    log_info("Messages received: " + std::to_string(total_received));
    log_info("Errors: " + std::to_string(total_errors));
}