#ifndef ECHO_CLIENT_H
#define ECHO_CLIENT_H

#include "common.h"
#include <thread>
#include <atomic>

class EchoClient {
private:
    ClientConfig config;
    static std::atomic<int> total_connections;
    static std::atomic<int> total_sent;
    static std::atomic<int> total_received;
    static std::atomic<int> total_errors;

    std::string get_thread_id();
    void handle_normal_connection();  // 同步发送模式（关键）
    void handle_pressure_connection();

public:
    EchoClient(const ClientConfig& cfg);
    ~EchoClient() = default;
    void run();
    void print_stats();
};

#endif // ECHO_CLIENT_H