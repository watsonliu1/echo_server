#include "echo_server.h"
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
#include <iostream>
#include <cstring>

/**
 * @brief 设置文件描述符为非阻塞模式
 * @details 非阻塞模式是epoll边缘触发（EPOLLET）的必要条件，确保I/O操作不会阻塞进程，
 *          配合epoll实现高效的事件驱动模型
 * @param fd 待设置的文件描述符（可以是监听套接字或客户端连接套接字）
 * @return 成功返回true，失败返回false
 */
bool EchoServer::set_non_blocking(int fd)
{
    // 1. 获取当前文件描述符的状态标志（F_GETFL：获取文件状态标志）
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        // 获取标志失败（可能因fd无效或权限问题），返回false
        return false;
    }

    // 2. 在原有标志基础上添加非阻塞标志（O_NONBLOCK），并设置回文件描述符
    //    fcntl返回0表示成功，-1表示失败，因此用"!= -1"判断操作是否成功
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

/**
 * @brief 将文件描述符添加到epoll实例，并注册关注的事件
 * @details 用于将监听套接字或客户端连接套接字纳入epoll的事件监控范围，
 *          是epoll事件驱动模型的核心步骤
 * @param fd 待添加的文件描述符
 * @param events 关注的事件类型（如EPOLLIN：读事件，EPOLLET：边缘触发）
 */
void EchoServer::add_to_epoll(int fd, uint32_t events)
{
    // 1. 定义epoll事件结构体，描述待监控的事件及关联数据
    struct epoll_event ev;
    ev.events = events;       // 设置关注的事件（如EPOLLIN | EPOLLET）
    ev.data.fd = fd;          // 绑定文件描述符，事件触发时可通过此获取fd

    // 2. 调用epoll_ctl添加事件到epoll实例
    //    EPOLL_CTL_ADD：操作类型为"添加事件"
    //    若失败，通过perror打印错误信息（如fd已被监控、epoll_fd无效等）
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
        perror("epoll_ctl add failed");  // 自动输出错误原因（如"File exists"表示fd已添加）
    }
}

/**
 * @brief 将文件描述符从epoll实例中移除
 * @details 当客户端断开连接或不再需要监控某个文件描述符时调用，避免epoll继续处理无效事件
 * @param fd 待移除的文件描述符
 */
void EchoServer::remove_from_epoll(int fd) {
    // 调用epoll_ctl移除事件
    // EPOLL_CTL_DEL：操作类型为"删除事件"
    // 第四个参数可为nullptr（删除时无需指定事件结构体）
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl del failed");  // 输出错误原因（如"Invalid argument"表示fd未被监控）
    }
}

/**
 * @brief 处理监听套接字上的新客户端连接请求
 * @details 当epoll检测到监听套接字（listen_fd）有读事件（EPOLLIN）时调用，
 *          完成accept接收连接、设置非阻塞、分配缓冲区、注册epoll事件等核心步骤，
 *          是服务器建立客户端连接的关键入口
 */
void EchoServer::handle_new_connection()
{
    // 1. 定义客户端地址结构体与地址长度变量
    // sockaddr_in：专门用于存储IPv4客户端的IP地址和端口号
    struct sockaddr_in client_addr;
    // 必须初始化地址长度，accept会通过此参数返回实际的地址结构体长度
    socklen_t client_len = sizeof(client_addr);

    // 2. 接收新连接（核心操作）
    // accept：从监听套接字的连接队列中取出一个待处理的连接，创建新的客户端套接字
    // 参数1：监听套接字（listen_fd）
    // 参数2：输出参数，存储客户端的IP和端口信息（强制转换为通用sockaddr类型）
    // 参数3：输入输出参数，传入地址结构体长度，传出实际使用的长度
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    
    // 3. 检查accept是否成功
    if (client_fd == -1)
    {
        // 失败时打印错误原因（如连接队列空、系统资源不足等）
        perror("accept failed");
        return;  // 终止当前函数，不再处理后续步骤
    }

    // 4. 将新创建的客户端套接字设置为非阻塞模式
    // 非阻塞是epoll边缘触发（EPOLLET）的必要条件，避免I/O操作阻塞进程
    if (!set_non_blocking(client_fd)) {
        // 设置失败时打印错误，关闭无效的客户端套接字（避免文件描述符泄漏）
        perror("set non-blocking failed");
        close(client_fd);
        return;  // 终止函数，不再继续
    }

    // 5. 为新客户端分配独立的接收缓冲区
    // 每个客户端用独立缓冲区存储待处理数据，避免多客户端数据混淆
    // BUFFER_SIZE：预定义的缓冲区大小（如16KB），在common.h等头文件中声明
    char* buffer = new char[BUFFER_SIZE];
    {
        // 加互斥锁保护客户端缓冲区映射表（client_buffers）
        // 避免多线程/多事件并发操作时导致的容器访问冲突
        std::lock_guard<std::mutex> lock(buffer_mutex);
        // 将客户端套接字（client_fd）与缓冲区绑定，存入映射表
        client_buffers[client_fd] = buffer;
    }

    // 6. 将客户端套接字添加到epoll，监听读事件（边缘触发模式）
    // EPOLLIN：表示关注“客户端有数据发送过来”的事件
    // EPOLLET：边缘触发模式，仅在数据状态变化时触发一次事件（高效，适合高并发）
    add_to_epoll(client_fd, EPOLLIN | EPOLLET);

    // 7. 打印新连接日志（便于调试和监控）
    // inet_ntoa：将网络字节序的IPv4地址（client_addr.sin_addr）转换为字符串格式（如192.168.1.100）
    // client_fd：新客户端的唯一标识，用于后续数据交互和资源管理
    std::cout << "New connection from " << inet_ntoa(client_addr.sin_addr) 
              << ", fd: " << client_fd << std::endl;
}

/**
 * @brief 处理客户端发送的数据（核心业务逻辑实现）
 * @details 当epoll检测到客户端套接字（client_fd）有可读事件（EPOLLIN）时调用，
 *          负责核心流程：读取客户端数据 → 原样回射数据 → 处理异常情况（连接关闭/读写出错）
 *          基于非阻塞I/O和边缘触发（EPOLLET）模式，确保高效处理数据
 * @param client_fd 客户端套接字的文件描述符（唯一标识一个客户端连接）
 */
void EchoServer::handle_client_data(int client_fd)
{
    // 1. 获取该客户端对应的缓冲区（通过映射表查找）
    char* buffer;  // 指向客户端专属缓冲区的指针
    {
        // 加锁保护client_buffers，防止查找/访问时缓冲区被并发删除
        std::lock_guard<std::mutex> lock(buffer_mutex);
        
        // 查找客户端fd对应的缓冲区迭代器
        auto it = client_buffers.find(client_fd);
        if (it == client_buffers.end()) {
            // 未找到缓冲区（可能客户端已断开并清理），直接返回
            return;
        }
        buffer = it->second;  // 取出缓冲区指针（it->first是client_fd，it->second是缓冲区）
    }

    ssize_t bytes_read;  // 记录每次read的字节数（ssize_t支持负数值表示错误）
    // 2. 循环读取数据（非阻塞模式下需一次性读完所有可用数据）
    //    边缘触发（EPOLLET）模式下，epoll仅触发一次可读事件，需循环读至无数据
    while (true) {
        // 从客户端fd读取数据到缓冲区，最多读BUFFER_SIZE字节
        bytes_read = read(client_fd, buffer, BUFFER_SIZE);
        
        if (bytes_read > 0) {
            // 3. 读取成功：将数据原样回射给客户端
            ssize_t bytes_written = write(client_fd, buffer, bytes_read);
            
            // 检查写操作是否失败（排除非阻塞模式的正常"暂时无法写入"情况）
            if (bytes_written == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("write failed");  // 输出错误原因（如连接已断开）
                close_client(client_fd); // 关闭客户端连接并清理资源
                return;                  // 终止处理流程
            }
            // 注：若bytes_written < bytes_read（部分写入），需考虑缓存剩余数据（此处简化处理）
        } 
        else if (bytes_read == 0) {
            // 4. 读取到0字节：客户端主动关闭连接（发送FIN包）
            std::cout << "Client " << client_fd << " disconnected" << std::endl;
            close_client(client_fd);  // 关闭连接并清理资源
            return;                   // 终止处理流程
        } 
        else {
            // 5. 读取失败（bytes_read == -1）
            //    非阻塞模式下，EAGAIN/EWOULDBLOCK表示"当前无数据可读"，是正常情况
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                // 其他错误（如连接重置、被关闭等），需关闭连接
                perror("read failed");
                close_client(client_fd);
            }
            break;  // 跳出循环（无论是正常无数据还是错误，都停止读取）
        }
    }
}

/**
 * @brief 关闭客户端连接并彻底清理相关资源
 * @details 当客户端断开连接、读写发生错误或服务器主动关闭连接时调用，
 *          负责完整的资源释放流程：从epoll移除事件监控→关闭套接字→释放缓冲区内存→删除映射表记录，
 *          避免资源泄漏和无效事件触发
 * @param client_fd 待关闭的客户端套接字文件描述符
 */
void EchoServer::close_client(int client_fd) {
      // 1. 从epoll实例中移除该客户端的事件监控
    //    避免客户端套接字已关闭后，epoll仍触发无效事件（如EPOLLHUP）
    remove_from_epoll(client_fd);

    // 2. 关闭客户端套接字
    //    释放系统级的文件描述符资源，避免文件描述符耗尽
    close(client_fd);
    
    // 3. 释放客户端专属缓冲区并从映射表中删除记录
    //    加锁保护client_buffers，防止并发操作导致的内存访问冲突
    std::lock_guard<std::mutex> lock(buffer_mutex);
    // 查找该客户端在映射表中的记录
    auto it = client_buffers.find(client_fd);
    if (it != client_buffers.end()) {
        // 释放动态分配的缓冲区内存（避免内存泄漏）
        delete[] it->second;  // it->second是缓冲区指针（char*）
        // 从映射表中删除该客户端的记录（避免后续操作访问无效fd）
        client_buffers.erase(it);
    }

    // 注：执行到此处，与该客户端相关的所有资源（epoll监控、套接字、缓冲区）均已释放

}

/**
 * @brief 服务器初始化函数，完成启动前的所有准备工作
 * @details 负责服务器启动阶段的核心初始化流程，包括创建监听套接字、设置网络参数、
 *          绑定端口、开始监听、初始化epoll等，是服务器能够进入运行状态的前提
 * @param port 服务器要绑定的端口号，默认使用common.h中定义的DEFAULT_PORT
 * @return 初始化成功返回true，任何一步失败返回false并清理已分配资源
 */
bool EchoServer::init(int port)
{
    // 1. 创建监听套接字（TCP套接字）
    //    AF_INET：使用IPv4地址族
    //    SOCK_STREAM：使用面向连接的TCP协议
    //    0：默认协议（TCP）
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)  // socket创建失败（返回-1）
    {
        perror("socket creation failed");  // 打印错误原因（如系统资源不足）
        return false;
    }

    // 2. 设置端口复用（关键配置）
    //    解决"Address already in use"问题：服务器重启时，快速复用已关闭的端口
    int opt = 1;  // 启用选项（非0值表示启用）
    //    SOL_SOCKET：设置套接字级别的选项
    //    SO_REUSEADDR：允许地址/端口复用
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");  // 打印错误（如权限不足）
        close(listen_fd);             // 清理已创建的监听套接字
        return false;
    }

    // 3. 绑定套接字到指定端口和所有网络接口
    struct sockaddr_in server_addr;  // 存储服务器地址信息的结构体
    memset(&server_addr, 0, sizeof(server_addr));  // 初始化结构体（清零）
    server_addr.sin_family = AF_INET;              // IPv4地址族
    server_addr.sin_addr.s_addr = INADDR_ANY;      // 绑定到所有可用网络接口（0.0.0.0）
    server_addr.sin_port = htons(port);            // 端口号转换为网络字节序（大端序）

    // 绑定操作：将套接字与地址结构关联
    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind failed");       // 打印错误（如端口被占用、无权限使用低端口）
        close(listen_fd);            // 清理资源
        return false;
    }

    // 4. 开始监听连接请求
    //    SOMAXCONN：由系统决定的最大连接队列长度（通常为128或更多）
    //    监听后，套接字进入被动模式，可接收客户端连接
    if (listen(listen_fd, SOMAXCONN) == -1) {
        perror("listen failed");     // 打印错误（如套接字未绑定）
        close(listen_fd);            // 清理资源
        return false;
    }

    // 5. 将监听套接字设置为非阻塞模式
    //    配合epoll边缘触发（EPOLLET）使用，确保accept操作不会阻塞进程
    if (!set_non_blocking(listen_fd)) {
        perror("set non-blocking failed");  // 打印错误
        close(listen_fd);                   // 清理资源
        return false;
    }

    // 6. 创建epoll实例（事件多路复用器）
    //    参数0：使用默认配置
    //    epoll_fd用于后续的事件注册、等待和处理
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create failed");  // 打印错误（如系统资源不足）
        close(listen_fd);               // 清理资源
        return false;
    }

    // 7. 将监听套接字添加到epoll，注册读事件（边缘触发模式）
    //    EPOLLIN：关注"有新连接请求"事件
    //    EPOLLET：边缘触发模式，提高事件处理效率
    add_to_epoll(listen_fd, EPOLLIN | EPOLLET);

    // 初始化成功，打印启动信息
    std::cout << "Echo server initialized on port " << port << std::endl;
    return true;
}

/**
 * @brief 服务器主事件循环，处理所有I/O事件并驱动服务运行
 * @details 这是服务器的核心运行函数，通过epoll_wait持续等待并处理各类事件（新连接、客户端数据、错误等），
 *          是整个服务器的"心脏"，一旦进入该循环，服务器将持续提供服务直到被中断
 */
void EchoServer::run() {
    // 1. 检查服务器是否已正确初始化
    //    若epoll实例或监听套接字未创建（值为-1），则无法运行，直接返回
    if (epoll_fd == -1 || listen_fd == -1) {
        std::cerr << "Server not initialized" << std::endl;  // 输出错误提示
        return;
    }

    // 2. 定义事件数组，用于存储epoll_wait返回的就绪事件
    //    MAX_EVENTS：一次最多处理的事件数量（在common.h中定义）
    struct epoll_event events[MAX_EVENTS];
    
    // 3. 进入无限事件循环（服务器的核心运行逻辑）
    while (true) {
        // 3.1 等待事件就绪（阻塞调用，直到有事件发生或被信号中断）
        //    参数1：epoll实例的文件描述符
        //    参数2：输出参数，用于存放就绪的事件
        //    参数3：最多接收的事件数量（不超过MAX_EVENTS）
        //    参数4：超时时间（-1表示永久阻塞，直到有事件发生）
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        
        // 检查epoll_wait是否失败（如被信号中断可能返回-1）
        if (num_events == -1) {
            perror("epoll_wait failed");  // 打印错误原因（如"Interrupted system call"）
            break;  // 退出事件循环，服务器停止运行
        }

        // 3.2 遍历所有就绪事件，分类型处理
        for (int i = 0; i < num_events; ++i) {
            // 事件对应的文件描述符（通过events[i].data.fd获取）
            // 3.2.1 若事件来自监听套接字：表示有新客户端连接请求
            if (events[i].data.fd == listen_fd) {
                handle_new_connection();  // 调用新连接处理函数
            }
            // 3.2.2 若事件是客户端的读事件（EPOLLIN）：表示客户端发送了数据
            else if (events[i].events & EPOLLIN) {
                handle_client_data(events[i].data.fd);  // 调用客户端数据处理函数
            }
            // 3.2.3 若事件是错误（EPOLLERR）或连接挂断（EPOLLHUP）：客户端连接异常
            else if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                std::cerr << "Client " << events[i].data.fd << " error/hangup" << std::endl;
                close_client(events[i].data.fd);  // 关闭客户端连接并清理资源
            }
        }
    }
}

/**
 * @brief 关闭服务器并释放所有资源（优雅终止服务器）
 * @details 负责服务器退出时的资源清理工作，包括关闭epoll实例、监听套接字、
 *          所有客户端连接，以及释放客户端缓冲区内存，确保程序退出时无资源泄漏
 *          通常在服务器收到中断信号（如SIGINT）或析构函数中调用
 */
void EchoServer::shutdown() {
    // 1. 关闭epoll实例并重置文件描述符
    if (epoll_fd != -1) {  // 检查epoll实例是否已创建
        close(epoll_fd);   // 关闭epoll文件描述符，释放epoll相关资源
        epoll_fd = -1;     // 重置为无效值，避免重复关闭
    }

    // 2. 关闭监听套接字并重置文件描述符
    if (listen_fd != -1) {  // 检查监听套接字是否已创建
        close(listen_fd);   // 关闭监听套接字，停止接收新连接
        listen_fd = -1;     // 重置为无效值，避免重复关闭
    }
    
    // 3. 清理所有客户端连接及对应的缓冲区
    //    加锁保护client_buffers，防止并发访问冲突
    std::lock_guard<std::mutex> lock(buffer_mutex);
    
    // 遍历所有客户端连接，逐个关闭并释放缓冲区
    for (auto& pair : client_buffers) {
        close(pair.first);       // 关闭客户端套接字（pair.first是client_fd）
        delete[] pair.second;    // 释放客户端专属缓冲区（pair.second是char*）
    }
    
    // 清空客户端缓冲区映射表，彻底释放所有记录
    client_buffers.clear();

    // 注：执行完成后，服务器所有资源（epoll、监听套接字、客户端连接、内存）均已释放
}