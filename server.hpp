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
#include "timer/timer_task.hpp"

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
    // TimerManager timer_mgr_;
    std::unordered_map<int, std::shared_ptr<TimerTask>> timers_;
    std::queue<int> close_queue_;
    std::mutex close_queue_mutex_;

    void initSocket();
    void handleConnection(int client_fd);
    void setNonBlocking(int fd);
    void handleGET(HttpRequest&, int);
    bool handlePOST(HttpRequest&, int);
    void processHttpRequest(int client_fd, HttpRequest& request);
    void addOrUpdateTimer(int client_fd, int timeout_ms);
    void processCloseQueue();
};

std::string getContentType(const std::string&);

std::string readFile(const std::string&);

std::string receiveHttpRequest(int);

bool tryParseHttpRequest(const std::string& raw, HttpRequest& request, size_t& request_len);

void parseFormURLEncoded(const std::string&, std::unordered_map<std::string, std::string>&);

std::string decodeURLComponent(const std::string&);
