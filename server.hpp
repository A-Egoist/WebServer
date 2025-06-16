#pragma once
#include <string>

class WebServer {
public:
    explicit WebServer(int port);
    void run();

private:
    int port_;  // 端口号
    int listen_fd_;  // 
    int epoll_fd_;  // 

    void initSocket();
    void handleConnection(int client_fd);
    void setNonBlocking(int fd);
};