#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include "webserver/sql/sql_connector.hpp"

class SqlPool {
public:
    ~SqlPool();

    // 获取单例实例
    static SqlPool* getInstance();

    // 初始化连接池
    void init(const std::string& host, const std::string& user, const std::string& passwd, const std::string& dbname, int port, int max_conn_size);

    // 从连接池中获取一个链接，使用智能指针管理
    std::shared_ptr<MySQLConnector> getConnection();

    // 将连接归还给连接池
    void releaseConnection(std::shared_ptr<MySQLConnector> conn);

    // 获取当前连接池中空闲连接的数量
    int getFreeConnCount();

private:
    std::string host_;
    std::string user_;
    std::string passwd_;
    std::string dbname_;
    int port_;
    int max_conn_size_;  // 最大连接数
    int free_conn_count_;  // 当前空闲连接数
    std::queue<MySQLConnector*> conn_queue_;  // 连接队列
    std::mutex queue_mutex_;  // 保护连接队列的互斥锁
    std::condition_variable cond_var_;  // 条件变量，用于线程等待和唤醒

    SqlPool() = default;

    // 禁用拷贝构造函数和拷贝赋值运算符
    SqlPool(const SqlPool&) = delete;
    SqlPool& operator=(const SqlPool&) = delete;
};