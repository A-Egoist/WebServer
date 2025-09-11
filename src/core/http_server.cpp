#include "webserver/core/http_server.hpp"
#include "webserver/utils/log.hpp"
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

constexpr int MAX_TIMEOUT = 1000;  // 自动断开连接的时间设置为 1000ms
constexpr int MAX_WORK_THREAD = 128;

HttpServer::HttpServer(int port)
    : epoll_server_(port),
      thread_pool_(MAX_WORK_THREAD), // 假设线程池大小为10
      heap_timer_() {
    
    LOG_INFO("HttpServer constructor started.");

    auto sql_pool = SqlPool::getInstance();
    sql_pool->init("tcp://127.0.0.1", "root", "Lx@259416", "WebServer_DB", 3306, 10);
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
        // epoll_server_.addFd(client_fd, EPOLLIN | EPOLLET);  // 可读、边缘触发、一次性触发
        epoll_server_.addFd(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT);  // 可读、边缘触发、一次性触发

        Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] connected!");
    }
}

void HttpServer::handleRead(int client_fd) {
    // 1. 主线程中加锁，安全地访问共享 map
    std::unique_ptr<HTTPConnection> conn_ptr;
    {
        std::lock_guard<std::mutex> lock(connection_mutex_);
        auto it = http_connections_.find(client_fd);
        if (it == http_connections_.end()) {
            // 链接不存在，可能已经被另一个线程处理并关闭了
            epoll_server_.removeFd(client_fd);
            return ;
        }
        conn_ptr = std::move(it->second);
        http_connections_.erase(it);
    }

    // 2. 主线程进行 I/O 操作：从 socket 读取所有数据
    std::string raw_data;
    bool is_connection_alive = conn_ptr->receiveRequest(raw_data);
    if (!is_connection_alive) {
        // I/O 错误或连接关闭
        handleClose(client_fd);
        return ;
    }

    auto connection_ptr = conn_ptr.release();

    // 3. 将耗时的业务逻辑提交给线程池
    thread_pool_.enqueue([this, client_fd, raw_data, connection_ptr]() mutable {
        auto conn_ptr = std::make_unique<HTTPConnection>(*connection_ptr);
        connection_ptr = nullptr;
        // a. 在工作线程中解析请求和构建响应
        conn_ptr->parseRequest(raw_data);
        conn_ptr->buildResponse();

        // b. 将所有权归还给主线程的 map
        {
            std::lock_guard<std::mutex> lock(connection_mutex_);
            this->http_connections_[client_fd] = std::move(conn_ptr);
        }

        // c. 通知主线程该连接现在可以写入了
        this->epoll_server_.updateFd(client_fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
    });
}

void HttpServer::handleWrite(int client_fd) {
    // 1. 在主线程中加锁，安全地访问共享的 map
    std::lock_guard<std::mutex> lock(connection_mutex_);
    auto it = http_connections_.find(client_fd);
    if (it == http_connections_.end()) {
        epoll_server_.removeFd(client_fd);
        return;
    }

    HTTPConnection& conn = *(it->second);  // 取别名

    // 2. 在主线程中进行 I/O 操作：循环发送数据
    if (conn.sendResponse()) {
        if (conn.is_keep_alive) {
            // 长连接，重新监听可读事件
            epoll_server_.updateFd(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
        } else {
            // 短连接，处理关闭
            handleClose(client_fd);
        }
    } else {
        // 未发送完毕，继续监听可写事件
        epoll_server_.updateFd(client_fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
    }
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
        if (close(client_fd) == -1) {
            LOG_ERROR("Client[" + std::to_string(client_fd) + "] has closed.");
        }
        // close(client_fd);

        // 从 map 中移除，unique_ptr 会自动释放内存
        http_connections_.erase(it);
    }
}