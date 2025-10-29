#include "echo_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <cstring>

void EchoClient::handle_connection()
{
    // 创建套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
    {
        log_error("socket creation failed");
        total_errors++;
        return;
    }

    // 设置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config.server_port);
    
    // 转换IP地址
    if (inet_pton(AF_INET, config.server_ip, &server_addr.sin_addr) <= 0)
    {
        log_error("invalid address/address not supported");
        close(sockfd);
        total_errors++;
        return;
    }

    // 连接服务器
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        log_error("connection failed");
        close(sockfd);
        total_errors++;
        return;
    }

    total_connections++;
    log_info("Connected to server, sockfd: " + std::to_string(sockfd));

    // 准备发送的数据
    std::string send_data(config.message_size, 'a');  // 填充'a'作为测试数据
    char* recv_buffer = new char[config.message_size + 1];

    // 发送并验证消息
    for (int i = 0; i < config.messages_per_conn; i++)
    {
        // 发送数据
        ssize_t bytes_sent = send(sockfd, send_data.c_str(), send_data.size(), 0);
        if (bytes_sent != send_data.size())
        {
            log_error("send failed, sockfd: " + std::to_string(sockfd) + 
                     ", sent: " + std::to_string(bytes_sent));
            total_errors++;
            break;
        }
        total_sent++;

        // 接收回射数据
        ssize_t bytes_read = recv(sockfd, recv_buffer, send_data.size(), 0);
        if (bytes_read <= 0)
        {
            log_error("recv failed, sockfd: " + std::to_string(sockfd) + 
                     ", read: " + std::to_string(bytes_read));
            total_errors++;
            break;
        }

        // 验证回射数据是否正确
        if (bytes_read != send_data.size() || 
            memcmp(recv_buffer, send_data.c_str(), bytes_read) != 0)
        {
            log_error("data mismatch, sockfd: " + std::to_string(sockfd));
            total_errors++;
            break;
        }

        total_received++;
    }

    // 清理资源
    delete[] recv_buffer;
    close(sockfd);
    log_info("Disconnected, sockfd: " + std::to_string(sockfd));
}

void EchoClient::log_info(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "[INFO] " << msg << std::endl;
}

void EchoClient::log_error(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cerr << "[ERROR] " << msg << std::endl;
}

void EchoClient::run()
{
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> threads;

    log_info("Starting " + std::to_string(config.connection_count) + " connections...");
    log_info("Each connection will send " + std::to_string(config.messages_per_conn) + 
            " messages of size " + std::to_string(config.message_size) + " bytes");

    // 创建指定数量的连接线程
    for (int i = 0; i < config.connection_count; i++)
    {
        threads.emplace_back(&EchoClient::handle_connection, this);
        
        // 控制连接创建速度，避免瞬间创建过多连接导致失败
        if (i % 100 == 0 && i > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // 等待所有线程完成
    for (auto& t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    // 计算并输出统计信息
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time
    ).count();

    log_info("\nTest completed!");
    log_info("Total connections attempted: " + std::to_string(config.connection_count));
    log_info("Successful connections: " + std::to_string(total_connections));
    log_info("Total messages sent: " + std::to_string(total_sent));
    log_info("Total messages received: " + std::to_string(total_received));
    log_info("Total errors: " + std::to_string(total_errors));
    log_info("Time elapsed: " + std::to_string(duration) + " seconds");
    
    if (duration > 0)
    {
        log_info("Throughput: " + std::to_string(total_received / duration) + " messages/sec");
    }
}