#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include "http/http_request.hpp"
#include "sql/MySQLConnector.hpp"
#include "log/log.hpp"
#include "timer/heaptimer.hpp"
#include "pool/ThreadPool.hpp"

class WebServer {
public:
    explicit WebServer(int port);
    void run();
    void closeClient(int fd);

private:
    int port_;  // 端口号
    int listen_fd_;  // 
    int epoll_fd_;  // 
    MySQLConnector mysql;
    struct ClientConnection
    {
        std::string buffer;
        bool keepAlive;
        int useCount;
    };
    std::unordered_map<int, ClientConnection> clients;
    HeapTimer heap_timer_;
    ThreadPool thread_pool_;
    std::mutex clients_mutex_;

    void initSocket();
    void handleConnection(int client_fd);
    void setNonBlocking(int fd);
    bool handlePOST(HttpRequest& request, int client_fd);
    void processHttpRequest(int client_fd, HttpRequest& request);
    std::string router(std::string& requestPath);
    bool receiveHttpRequest(int client_fd, std::string& output);
};

std::string getContentType(const std::string& path);

std::string readFile(const std::string& file_path);

void parseFormURLEncoded(const std::string& body, std::unordered_map<std::string, std::string>& data);

std::string decodeURLComponent(const std::string& s);
