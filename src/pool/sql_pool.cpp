#include "webserver/pool/sql_pool.hpp"

// 获取单例实例
SqlPool* SqlPool::getInstance() {
    static SqlPool instance;
    return &instance;
}

// 初始化连接池
void SqlPool::init(const std::string& host, const std::string& user, const std::string& passwd, const std::string& dbname, int port, int max_conn_size) {
    if (max_conn_size <= 0) {
        LOG_ERROR("Connection pool size must be greater than 0.");
        return;
    }

    host_ = host;
    user_ = user;
    passwd_ = passwd;
    dbname_ = dbname;
    port_ = port;
    max_conn_size_ = max_conn_size;
    
    // 预先创建制定数量的数据库连接
    // TODO: 应该考虑到创建连接池失败的情况
    for (int i = 0; i < max_conn_size_; ++ i) {
        MySQLConnector* conn = new MySQLConnector(host_, user_, passwd_, dbname_, port_);
        conn_queue_.push(conn);
        ++ free_conn_count_;
        LOG_INFO("MySQL connection " + std::to_string(i + 1) + " initialized and added to pool.");
    }

    LOG_INFO("MySQL connection pool initialized successfully. Size: " + std::to_string(max_conn_size_));
}

// 从连接池中获取一个链接，使用智能指针管理
std::shared_ptr<MySQLConnector> SqlPool::getConnection() {
    std::unique_lock<std::mutex> lock(queue_mutex_);

    // 如果没有空闲连接，等待到有连接为止
    if (conn_queue_.empty()) {
        LOG_WARNING("Connection pool is empty, waiting for a free connection...");
        cond_var_.wait(lock, [this](){ return !conn_queue_.empty(); });
    }

    // 从队列中取出元素
    MySQLConnector* raw_conn = conn_queue_.front();
    conn_queue_.pop();
    -- free_conn_count_;

    std::shared_ptr<MySQLConnector> shared_conn(raw_conn, [this](MySQLConnector* p_conn) {
        // 自定义删除器：将连接归还给连接池
        std::unique_lock<std::mutex> lock(queue_mutex_);
        conn_queue_.push(p_conn);
        ++ free_conn_count_;
        cond_var_.notify_one();  // 唤醒一个等待连接的线程
    });

    return shared_conn;
}

// 将连接归还给连接池
void SqlPool::releaseConnection(std::shared_ptr<MySQLConnector> conn) {
    if (!conn) return;

    // 自定义删除器已实现归还逻辑，此方法可省略，或者作为接口封装
    // 它的作用主要是向外部暴露一个归还接口，但实际操作由智能指针完成
    LOG_INFO("Connection has been returned to the pool.");
}

// 获取当前连接池中空闲连接的数量
int SqlPool::getFreeConnCount() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return free_conn_count_;
}

SqlPool::~SqlPool() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!conn_queue_.empty()) {
        MySQLConnector* conn = conn_queue_.front();
        conn_queue_.pop();
        delete conn;
    }
    LOG_INFO("MySQL connection pool has been destroyed.");
}