#include "echo_server.h"
#include "common.h"

int main() {
    try {
        EchoServer server(DEFAULT_PORT);
        server.start();
    } catch (const std::exception& e) {
        log_error("Server exception: " + std::string(e.what()));
        return 1;
    }
    return 0;
}