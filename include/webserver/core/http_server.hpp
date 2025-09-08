#pragma once

#include "webserver/core/epoll_server.hpp"
#include "webserver/http/http_connection.hpp"
#include "webserver/sql/MySQLConnector.hpp"
#include "webserver/pool/ThreadPool.hpp"
#include "webserver/pool/sql_pool.hpp"
#include "webserver/timer/heaptimer.hpp"
#include <unordered_map>
#include <memory>

class HttpServer {
public:
    HttpServer(int port);
    ~HttpServer();

    void run();

private:
    void handleNewConnection(int listen_fd);
    void handleRead(int client_fd);
    void handleWrite(int client_fd);
    void handleClose(int client_fd);

    EpollServer epoll_server_;  // epoll server
    ThreadPool thread_pool_;  // 线程池
    // MySQLConnector mysql_connector_;  // sql 连接
    SqlPool* sql_pool_;  // 数据库连接池
    HeapTimer heap_timer_;  // 计时器
    std::unordered_map<int, std::unique_ptr<HTTPConnection>> http_connections_;
};