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
#include "http/HTTPConnection.hpp"
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
    std::unordered_map<int, HTTPConnection> clients;
    HeapTimer heap_timer_;
    ThreadPool thread_pool_;
    std::mutex clients_mutex_;

    void initSocket();
    void handleConnection(int client_fd);
    void setNonBlocking(int fd);
};