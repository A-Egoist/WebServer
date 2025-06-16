#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>

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

std::string getContentType(const std::string&);

std::string readFile(const std::string&);
