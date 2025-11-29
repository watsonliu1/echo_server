#ifndef ECHO_SERVER_H
#define ECHO_SERVER_H

#include <string>

class EchoServer {
private:
  int port;
  int epoll_fd;
  int listen_fd;

  void set_nonblocking(int fd);
  void handle_new_connection();
  void handle_client_data(int conn_fd);

public:
  EchoServer(int port = 15000);
  ~EchoServer();
  void init();
  void run();
};

#endif // ECHO_SERVER_H