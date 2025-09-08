#include "webserver/core/http_server.hpp"
#include "webserver/utils/log.hpp"
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

constexpr int MAX_TIMEOUT = 1000;  // 自动断开连接的时间设置为 1000ms

HttpServer::HttpServer(int port)
    : epoll_server_(port),
      thread_pool_(10), // 假设线程池大小为10
      heap_timer_() {
    
    LOG_INFO("HttpServer constructor started.");

    sql_pool_ = SqlPool::getInstance();
    sql_pool_->init("tcp://127.0.0.1", "root", "Lx@259416", "WebServer_DB", 3306, 10);
    LOG_INFO("SqlPool instance obtained.");

    // 注册所有回调函数
    epoll_server_.setConnectionCallback(std::bind(&HttpServer::handleNewConnection, this, std::placeholders::_1));
    epoll_server_.setReadCallback(std::bind(&HttpServer::handleRead, this, std::placeholders::_1));
    epoll_server_.setWriteCallback(std::bind(&HttpServer::handleWrite, this, std::placeholders::_1)); // 暂不实现，读为主
    epoll_server_.setCloseCallback(std::bind(&HttpServer::handleClose, this, std::placeholders::_1));
    LOG_INFO("Callbacks set.");
    LOG_INFO("HttpServer constructor finished.");
}

HttpServer::~HttpServer() {
    // 析构函数可以为空，因为成员变量会自动析构
    LOG_INFO("HttpServer destroyed.");
}

void HttpServer::run() {
    // 启动 EpollServer 的事件循环
    epoll_server_.run();
}

void HttpServer::handleNewConnection(int listen_fd) {
    // 持续接收新连接，直至没有新的连接到达（边缘触发）
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 所有新连接都已处理
                break;
            } else {
                perror("accept failed");
                Logger::getInstance().log("ERROR", "Accept failed.");
                break;
            }
        }

        // 创建新的 HTTPConnection 对象并管理
        http_connections_[client_fd] = std::make_unique<HTTPConnection>(client_fd);

        // 添加定时器和事件到 EpollServer
        heap_timer_.addTimer(client_fd, MAX_TIMEOUT);
        epoll_server_.addFd(client_fd, EPOLLIN | EPOLLET);  // 可读、边缘触发、一次性触发
        // epoll_server_.addFd(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT);  // 可读、边缘触发、一次性触发

        Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] connected!");
    }
}

void HttpServer::handleRead(int client_fd) {
    // 提交任务到线程池
    thread_pool_.enqueue([this, client_fd] {
        auto it = http_connections_.find(client_fd);
        if (it == http_connections_.end()) {
            // 连接不存在，直接返回
            return;
        }

        HTTPConnection& conn = *(it->second);
        
        // 接收数据
        std::string raw_data;
        bool is_connection_alive = conn.receiveRequest(raw_data);
        if (!is_connection_alive) {
            // 连接已关闭或出错，直接处理关闭
            handleClose(client_fd);
            return;
        }

        // 处理请求并发送响应
        conn.parseRequest(raw_data);
        conn.sendResponse();

        // 根据连接状态更新或关闭
        if (conn.is_keep_alive) {
            heap_timer_.updateTimer(client_fd, MAX_TIMEOUT);
        } else {
            handleClose(client_fd);
        }
    });
}

// 暂不实现，因为非阻塞写更复杂
void HttpServer::handleWrite(int client_fd) {
    // 从 map 中获取连接
    // std::lock_guard<std::mutex> lock(connection_mutex_);
    // auto it = http_connections_.find(client_fd);
    // if (it == http_connections_.end()) {
    //     return;
    // }
    // HTTPConnection& conn = *(it->second);
    
    // // 调用 sendResponse 循环发送数据
    // if (conn.sendResponse()) {
    //     // 所有数据已发送，清除 EPOLLOUT 事件
    //     epoll_server_.updateFd(client_fd, EPOLLIN | EPOLLRDHUP);
    // }
}

void HttpServer::handleClose(int client_fd) {
    // 线程安全地移除连接
    auto it = http_connections_.find(client_fd);
    if (it != http_connections_.end()) {
        Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] is closed due to timeout or http request.");
        
        // 从 epoll 和定时器中移除
        epoll_server_.removeFd(client_fd);
        heap_timer_.removeTimer(client_fd);
        
        // 关闭 socket
        close(client_fd);

        // 从 map 中移除，unique_ptr 会自动释放内存
        http_connections_.erase(it);
    }
}