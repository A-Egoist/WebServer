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
#include "webserver/http/http_request.hpp"
#include "webserver/http/HTTPConnection.hpp"
#include "webserver/sql/MySQLConnector.hpp"
#include "webserver/utils/log.hpp"
#include "webserver/timer/heaptimer.hpp"
#include "webserver/pool/ThreadPool.hpp"

class WebServer {
private:
    int port_;  // 端口号
    int listen_fd_;  // 
    int epoll_fd_;  // 
    MySQLConnector mysql_;
    std::unordered_map<int, HTTPConnection> clients;
    HeapTimer heap_timer_;
    ThreadPool thread_pool_;
    std::mutex clients_mutex_;

    void initSocket();
    void handleConnection(int client_fd);
    void setNonBlocking(int fd);

public:
    explicit WebServer(int port);
    void run();
    void closeClient(int fd);
};