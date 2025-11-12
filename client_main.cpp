#include "echo_client.h"
#include "common.h"
#include <getopt.h>
#include <iostream>

void parse_args(int argc, char* argv[], ClientConfig& config) {
    int opt;
    while ((opt = getopt(argc, argv, "c:m:s:i:p:t")) != -1) {
        switch (opt) {
            case 'c': config.connections = std::stoi(optarg); break;
            case 'm': config.messages_per_conn = std::stoi(optarg); break;
            case 's': config.message_size = std::stoi(optarg); break;
            case 'i': config.server_ip = optarg; break;
            case 'p': config.server_port = std::stoi(optarg); break;
            case 't': config.pressure_test = true; break;
            default:
                std::cerr << "Usage: " << argv[0] << " [-c connections] [-m messages/conn] [-s msg_size] [-i ip] [-p port] [-t]\n";
                exit(1);
        }
    }
}

int main(int argc, char* argv[]) {
    ClientConfig config;
    parse_args(argc, argv, config);

    try {
        EchoClient client(config);
        client.run();
    } catch (const std::exception& e) {
        log_error("Client exception: " + std::string(e.what()));
        return 1;
    }
    return 0;
}