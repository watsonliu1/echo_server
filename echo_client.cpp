#include "echo_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <chrono>
#include <cstring>

/**
 * @brief 客户端单连接处理函数，完成"创建连接→发送消息→接收回射→验证数据→清理资源"全流程
 * @details 每个客户端连接线程会调用此函数，负责与服务器建立TCP连接，按配置发送指定数量/大小的测试消息，
 *          接收服务器回射的数据并验证完整性，同时统计连接、消息收发及错误数量
 */
void EchoClient::handle_connection()
{
    // 1. 创建TCP套接字（客户端通信的核心句柄）
    //    AF_INET：IPv4地址族；SOCK_STREAM：面向连接的TCP协议；0：默认协议（TCP）
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)  // 套接字创建失败（返回-1，如系统资源不足）
    {
        log_error("socket creation failed");  // 输出错误日志
        total_errors++;                      // 累加错误计数（原子变量，线程安全）
        return;                               // 终止当前连接处理流程
    }

    // 2. 初始化服务器地址结构体（存储要连接的服务器IP和端口）
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));  // 结构体清零，避免随机值干扰
    server_addr.sin_family = AF_INET;              // IPv4地址族，与套接字一致
    // 将配置中的服务器端口转换为网络字节序（大端序），确保跨平台兼容性
    server_addr.sin_port = htons(config.server_port);
    
    // 3. 将服务器IP字符串（如"127.0.0.1"）转换为网络字节序的IPv4地址
    //    inet_pton：支持IPv4/IPv6，返回值>0表示转换成功；<=0表示无效IP或不支持的地址类型
    if (inet_pton(AF_INET, config.server_ip, &server_addr.sin_addr) <= 0)
    {
        log_error("invalid address/address not supported");  // 输出IP错误日志
        close(sockfd);                                       // 关闭已创建的套接字（避免资源泄漏）
        total_errors++;                                       // 累加错误计数
        return;
    }

    // 4. 与服务器建立TCP连接（三次握手）
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1)
    {
        log_error("connection failed");  // 输出连接失败日志（如服务器未启动、端口错误）
        close(sockfd);                   // 关闭套接字
        total_errors++;                  // 累加错误计数
        return;
    }

    // 5. 连接成功：更新统计信息并输出日志
    total_connections++;  // 原子变量，累加成功连接数
    log_info("Connected to server, sockfd: " + std::to_string(sockfd));  // 输出连接成功信息（含套接字句柄）

    // 6. 准备发送数据与接收缓冲区
    //    创建指定大小的测试数据（填充字符'a'，便于后续验证数据完整性）
    std::string send_data(config.message_size, 'a');

    //    分配接收缓冲区：大小+1是为了避免字符串处理时越界（可选，此处用于存储回射数据）
    char* recv_buffer = new char[config.message_size + 1];

    // 7. 循环发送并验证消息（按配置发送messages_per_conn条消息）
    for (int i = 0; i < config.messages_per_conn; i++)
    {
        // 7.1 发送数据到服务器
        //    send：返回实际发送的字节数；若失败返回-1
        //    第四个参数0：默认发送行为（阻塞发送，直到数据全部发送或出错）
        ssize_t bytes_sent = send(sockfd, send_data.c_str(), send_data.size(), 0);
        if (bytes_sent != send_data.size())  // 发送字节数与预期不符（发送失败或部分发送）
        {
            log_error("send failed, sockfd: " + std::to_string(sockfd) + 
                     ", sent: " + std::to_string(bytes_sent));  // 输出发送错误详情
            total_errors++;  // 累加错误计数
            break;           // 终止消息发送循环（当前连接后续消息不再发送）
        }
        total_sent++;  // 发送成功：累加总发送消息数（原子变量）

        // 7.2 接收服务器回射的数据
        //    recv：返回实际接收的字节数；<=0表示接收失败或连接关闭
        ssize_t bytes_read = recv(sockfd, recv_buffer, send_data.size(), 0);
        if (bytes_read <= 0)
        {
            log_error("recv failed, sockfd: " + std::to_string(sockfd) + 
                     ", read: " + std::to_string(bytes_read));  // 输出接收错误详情
            total_errors++;  // 累加错误计数
            break;           // 终止循环
        }

        // 7.3 验证回射数据的完整性（核心逻辑：确保接收数据与发送数据完全一致）
        //    检查1：接收字节数是否与发送字节数相同
        //    检查2：接收数据内容是否与发送数据内容相同（memcmp比较二进制数据）
        if (bytes_read != send_data.size() || 
            memcmp(recv_buffer, send_data.c_str(), bytes_read) != 0)
        {
            log_error("data mismatch, sockfd: " + std::to_string(sockfd));  // 输出数据不匹配错误
            total_errors++;  // 累加错误计数
            break;           // 终止循环
        }

        total_received++;  // 验证成功：累加总接收消息数（原子变量）
    }

    // 8. 清理当前连接的资源（避免内存泄漏和文件描述符泄漏）
    delete[] recv_buffer;  // 释放接收缓冲区（动态分配的char数组，需用delete[]）
    close(sockfd);         // 关闭客户端套接字（释放文件描述符）
    log_info("Disconnected, sockfd: " + std::to_string(sockfd));  // 输出断开连接日志
}

/**
 * @brief 线程安全客户端日志输出函数（信息级）
 * @details 用于输出客户端运行过程中的常规信息日志（如连接成功、消息发送完成等），
 *          保证多线程环境下日志输出的原子性，避免不同线程的日志内容混杂
 * @param msg 要输出的日志信息字符串
 */
void EchoClient::log_info(const std::string& msg)
{
    // 1. 使用互斥锁保护标准输出流（cout），确保日志输出的线程安全
    //    std::lock_guard在构造时加锁，析构时自动解锁，避免手动管理锁的遗漏
    std::lock_guard<std::mutex> lock(cout_mutex);
    
    // 2. 输出带"INFO"标识的日志信息，清晰区分日志级别
    std::cout << "[INFO] " << msg << std::endl;
}

/**
 * @brief 客户端日志输出函数（错误级）
 * @details 用于输出客户端运行过程中的错误信息（如连接失败、消息验证出错等），
 *          同样保证多线程环境下的输出完整性，使用标准错误流（cerr）单独输出
 * @param msg 要输出的错误信息字符串
 */
void EchoClient::log_error(const std::string& msg)
{
    // 1. 同样使用互斥锁保护输出流，避免多线程错误日志混杂
    std::lock_guard<std::mutex> lock(cout_mutex);
    
    // 2. 输出带"ERROR"标识的错误信息，使用cerr（标准错误流）
    //    cerr默认无缓冲，错误信息会立即输出，适合紧急错误提示
    std::cerr << "[ERROR] " << msg << std::endl;
}

/**
 * @brief 客户端核心运行函数，负责启动多连接测试、管理线程生命周期并输出最终统计结果
 * @details 按配置创建指定数量的连接线程，控制连接创建速率避免瞬时压力过大，
 *          等待所有线程执行完成后，计算并输出测试的关键指标（连接数、消息量、吞吐量等）
 */
void EchoClient::run()
{
    // 1. 记录测试开始时间（用于后续计算总耗时）
    //    std::chrono::high_resolution_clock：高精度时钟，适合计算短时间间隔
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // 2. 创建线程容器，用于管理所有连接线程（方便后续等待线程完成）
    std::vector<std::thread> threads;

    // 3. 输出测试启动信息，明确测试配置（便于用户确认参数是否正确）
    log_info("Starting " + std::to_string(config.connection_count) + " connections...");
    log_info("Each connection will send " + std::to_string(config.messages_per_conn) + 
            " messages of size " + std::to_string(config.message_size) + " bytes");

    // 4. 循环创建指定数量的连接线程（核心步骤：启动多连接测试）
    for (int i = 0; i < config.connection_count; i++)
    {
        // 向线程容器添加新线程，绑定到handle_connection成员函数（this指向当前EchoClient实例）
        // emplace_back：直接在容器中构造线程，比push_back更高效（避免临时对象拷贝）
        threads.emplace_back(&EchoClient::handle_connection, this);
        
        // 控制连接创建速率（关键优化：避免瞬间创建过多线程/连接导致系统资源耗尽）
        // 每创建100个连接，休眠10毫秒，给系统留出资源分配时间（可根据服务器承载能力调整）
        if (i % 100 == 0 && i > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    // 5. 等待所有连接线程执行完成（阻塞直到所有线程结束）
    //    遍历线程容器，对每个可join的线程调用join()，确保主线程等待所有测试任务完成
    for (auto& t : threads)
    {
        if (t.joinable())  // 检查线程是否可join（避免重复join导致错误）
        {
            t.join();  // 阻塞主线程，直到当前线程执行完毕
        }
    }

    // 6. 计算测试总耗时（从开始到所有线程结束的时间差）
    auto end_time = std::chrono::high_resolution_clock::now();
    // 将时间差转换为秒数（duration_cast用于不同时间单位的转换）
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(
        end_time - start_time
    ).count();

    // 7. 输出测试统计结果（核心指标，便于评估服务器性能和测试稳定性）
    log_info("\nTest completed!");
    log_info("Total connections attempted: " + std::to_string(config.connection_count));  // 尝试创建的总连接数
    log_info("Successful connections: " + std::to_string(total_connections));          // 成功建立的连接数
    log_info("Total messages sent: " + std::to_string(total_sent));                    // 总发送消息数
    log_info("Total messages received: " + std::to_string(total_received));            // 总接收（回射）消息数
    log_info("Total errors: " + std::to_string(total_errors));                        // 总错误数（连接/发送/接收/验证失败）
    log_info("Time elapsed: " + std::to_string(duration) + " seconds");                // 测试总耗时

    // 8. 计算并输出吞吐量（仅当耗时>0时，避免除以零错误）
    //    吞吐量=总接收消息数/总耗时，反映服务器每秒处理的消息量
    if (duration > 0)
    {
        log_info("Throughput: " + std::to_string(total_received / duration) + " messages/sec");
    }
}