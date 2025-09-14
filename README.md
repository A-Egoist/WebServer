# WebServer

## 项目说明

本项目是基于 TinyWebServer 实现的一个 C++ WebServer 项目，目的是用一个更实际的项目来提升自己的 Coding 能力。

![image-20250914184731070](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202509141851957.png)



## TODO

### 基本要求

- [x] 连接数据库，存储用户账号和密码
    - [ ] 应该保证 username 不能重复，让 username 成为 key

- [x] 完成注册和登录功能，登录成功的用户应该返回 `welcome.html` 界面
- [x] 完成日志功能
- [x] 完成连接池

- [x] 完成工作线程池

- [x] 完成定时器，关闭非活动连接
    - [x] 目前对 HTTP 请求的响应都带有 `"Connection: close\r\n\r\n"`，这样的频繁的连接、断开连接、再连接的过程非常耗费资源，下一步应该添加定时器以关闭长时间没有使用的连接。
    - [x] 已经成功添加定时器，但是在压力测试后之后发现，性能大幅度降低，现在需要分析性能降低的原因并解决这个问题。

- [x] `webbench-1.5` 服务器压力测试
- [ ] 大文件传输优化
- [ ] 解决极少数情况放生的崩溃问题。首先需要想办法复现问题，再想办法解决问题
- [ ] 部署到腾讯云

### 额外功能

- [ ] 实现聊天服务器的功能，参考[GitHub](https://github.com/archibate/co_http)，考虑在现有WebServer的基础上增加聊天室的功能
- [ ] 添加二维码超分辨功能
- [ ] 实现音乐播放器的功能



## 具体实现

类设计按照 public -> protected -> private 的**接口优先，实现后置**的方式。



### BlockingQueue 类

使用生产者-消费者模式实现阻塞队列，支持优雅退出，这是 Log 类和线程池的基础。

使用两个条件变量，将生产者和消费者的等待分开：

*   生产者只关心队列是否已满。当队列满了，它就在 `cond_productor_`上等待；
*   消费者只关心队列是否为空。当队列为空，它就在 `cond_consumer_` 上等待；

这样，当一个消费者取走一个任务后，队列不再是满的，所以它应该唤醒生产者。

同样，当一个生产者放入一个任务后，队列不再是空的，所以它应该唤醒消费者。

这样分离确保了需要唤醒消费者的时候不会虚假唤醒生产者。

`block_queue.hpp`

```cpp
#pragma once
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockingQueue {
public:
    BlockingQueue(size_t max_size = 10000): max_size_(max_size) {}

    // 优雅的关闭队列
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_closed_.store(true);
        }
        // 唤醒所有的生产者和消费者，让它们检查 is_closed_ 状态并退出
        cond_productor_.notify_all();
        cond_consumer_.notify_all();
    }

    void push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_productor_.wait(lock, [this]() { return queue_.size() < max_size_ || is_closed_.load(); });
        // 判断是什么原因唤醒
        if (is_closed_.load()) {
            // 如果是应为队列关闭唤醒，则不再写入
            return;
        }
        queue_.push(value);
        cond_consumer_.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_consumer_.wait(lock, [this]() { return !queue_.empty() || is_closed_.load(); });
        // 判断什么原因唤醒
        if (is_closed_.load()) {
            // 如果是因为队列关闭唤醒，则不再弹出任务
            return false;
        }
        value = queue_.front();
        queue_.pop();
        cond_productor_.notify_one();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_productor_;  // 生产者条件变量
    std::condition_variable cond_consumer_;  // 消费者条件变量
    size_t max_size_;
    std::atomic<bool> is_closed_{false};  // 控制队列是否在没有任务的时候阻塞，当为 true 时，pop 操作不再等待，而是立即返回，用于优雅退出。
};
```

:bulb:



### Logger 类

使用单例模式(懒汉模式)实现 Logger 类，支持异步和同步操作：

*   同步模式(synchronous)：调用 `log()` 函数加锁写入内容。`log_mutex_` 是同步模式下的互斥锁。
*   异步模式(asynchronous)：将消息推入队列，用一个子线程专门负责从队列中读取消息并写入日志文件。

将 `BlockingQueue` 队列设计在堆上，便于在 init 中动态创建，只用于异步模式。

引入原子标记位 `is_initialized_` 用于标志 Logger 类是否实例化，保证线程安全。

使用步骤：

1.   先通过 `Logger::getInstance().init("logs/running.log", true)` 初始化，其中第一个参数表示日志文件路径，第二个参数表示是否异步模式；
2.   然后通过 `Logger::getInstance().log("INFO", "Server started")` 写入内容，如果是同步模式，会直接在文件中写入，如果是异步模式则会先写入到阻塞队列中再通过工作线程写入到文件中；
3.   在 `main()` 函数退出前通过调用 `Logger::getInstance().stop()` 保证数据的完整性和优雅退出。

此外，使用宏定义实现 Logger 类的四种快速调用方式：

*   `LOG_INFO(msg)`
*   `LOG_DEBUG(msg)`
*   `LOG_WARNING(msg)`
*   `LOG_ERROR(msg)`

`log.hpp`

```cpp
#pragma once

#include <string>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iostream>
#include "webserver/utils/block_queue.hpp"

#define LOG_INFO(msg) Logger::getInstance().log("INFO", msg)
#define LOG_DEBUG(msg) Logger::getInstance().log("DEBUG", msg)
#define LOG_WARNING(msg) Logger::getInstance().log("WARNING", msg)
#define LOG_ERROR(msg) Logger::getInstance().log("ERROR", msg)

class Logger {
public:
    static Logger& getInstance();
    void init(const std::string& filename, bool async = true, int max_queue_size = 10000);
    void log(const std::string& level, const std::string& message);
    void stop();

private:
    Logger();
    ~Logger();
    void writeLog();

    std::ofstream log_file_;  // 日志文件
    BlockingQueue<std::string>* queue_;  // 异步日志队列
    std::thread write_thread_;  // 工作线程
    std::atomic<bool> is_initialized_{false};  // 标志是否初始化，用于线程安全
    bool async_;  // 同步异步标志，true 表示异步，false 表示同步
    std::mutex log_mutex_;  // 日志锁
};
```

:bulb:使用宏定义简化使用，使用初始化列表初始化原子类型的bool变量，使用线程安全的阻塞队列来临时存储异步日志，然后通过工作线程写入到文件中。



`log.cpp`

```cpp
#include "webserver/utils/log.hpp"

Logger::Logger() : queue_(nullptr), async_(true) {}

Logger::~Logger() {
    stop();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& filename, bool async, int max_queue_size) {
    std::cout << "Logger is initializing\n";
    bool expected = false;
    if (!is_initialized_.compare_exchange_strong(expected, true)) {
        return;  // 已经初始化，直接返回
    }

    async_ = async;
    // 打开文件，使用 std::ios::ate 模式，定位到文件末尾
    // 这样每次写入时都不需要重新打开文件
    // log_file_.open(filename, std::ios::app);
    log_file_.open(filename, std::ios::ate);
    if (!log_file_.is_open()) {
        std::cerr << "Cannot open log file: " << filename << "\n";
        exit(1);
    }

    if (async_) {
        queue_ = new BlockingQueue<std::string>(max_queue_size);
        write_thread_ = std::thread(&Logger::writeLog, this);
    }

    std::cout << "Logger initialized\n";
}

/* level: ["INFO", "DEBUG", "WARNING", "ERROR"] */
void Logger::log(const std::string& level, const std::string& message) {
    if (!is_initialized_.load()) {
        return;  // 如果没有初始化直接返回
    }

    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&now_c), "%F %T") << "]\t" << "[" << level << "]\t" << message << "\n";
    std::string msg = ss.str();

    if (async_) {
        // 异步模式下，将消息推入队列
        queue_->push(msg);
    } else {
        // 同步模式下，直接加锁写入
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_file_ << msg;
        log_file_.flush();
    }
}

void Logger::stop() {
    bool expected = true;
    if (!is_initialized_.compare_exchange_strong(expected, false)) {
        // 未初始化或者已经在关闭过程中
        return;
    }

    if (async_ && queue_ != nullptr) {
        // 1. 关闭队列，组织新的日志写入，并唤醒所有消费者
        queue_->close();

        // 2. 等待日志写入线程结束
        if (write_thread_.joinable()) {
            write_thread_.join();
        }

        // 3. 释放队列内存
        delete queue_;
        queue_ = nullptr;
    }

    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void Logger::writeLog() {
    std::string msg;

    // 只要成功从队列中取出消息就写入文件
    while (queue_->pop(msg)) {
        log_file_ << msg;
        log_file_.flush();  // 可能存在瓶颈，后期优化
    }
}
```

:bulb:文件读写(`ofstream`, `ifstream`, `fstream`)的基本操作，工作线程创建，时间戳获取，`stringstream`，互斥锁的使用



### EpollServer 类

`epoll_server.hpp`

```cpp
#pragma once

#include <sys/epoll.h>
#include <functional>
#include <unordered_map>
#include "webserver/timer/heaptimer.hpp"

// 回调函数类型定义
// 当有新连接或数据可读时，EpollServer会调用这些函数
using EventCallback = std::function<void(int)>;

class EpollServer {
public:
    EpollServer(int port);
    ~EpollServer();

    void run();

    // 注册定时器
    void setTimer(HeapTimer* timer);

    // 注册回调函数
    void setConnectionCallback(EventCallback callback);
    void setReadCallback(EventCallback callback);
    void setWriteCallback(EventCallback callback);
    void setCloseCallback(EventCallback callback);

    // 供外部调用的 Epoll 相关接口
    void addFd(int fd, uint32_t events);
    void updateFd(int fd, uint32_t events);
    void removeFd(int fd);

private:
    int port_;
    int listen_fd_;
    int epoll_fd_;  // epoll 实例
    HeapTimer* timer_;  // 定时器

    void initSocket();

    EventCallback connection_callback_;
    EventCallback read_callback_;
    EventCallback write_callback_;
    EventCallback close_callback_;
};
```

:bulb:使用 `using`​ 来为类型创建别名，回调函数模式，Reactor模式(基于事件的、非阻塞的 I/O 模型)，注册回调函数，epoll的使用和各种模式



`epoll_server.cpp`

```cpp
#include "webserver/core/epoll_server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "webserver/utils/log.hpp"

constexpr int MAX_EVENTS = 1024;

EpollServer::EpollServer(int port) : port_(port), listen_fd_(-1), epoll_fd_(-1) {
    initSocket();
}

EpollServer::~EpollServer() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
    }
}

void EpollServer::initSocket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd_ == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd_, SOMAXCONN) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        perror("epoll_create failed");
        exit(EXIT_FAILURE);
    }

    addFd(listen_fd_, EPOLLIN | EPOLLET);
    Logger::getInstance().log("INFO", "EpollServer initialized. Listening on port " + std::to_string(port_));
}

void EpollServer::addFd(int fd, uint32_t events) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = events;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        perror("epoll_ctl_add failed");
        Logger::getInstance().log("ERROR", "Failed to add fd " + std::to_string(fd) + " to epoll.");
    }
}

void EpollServer::updateFd(int fd, uint32_t events) {
    epoll_event event{};
    event.data.fd = fd;
    event.events = events;
    // event.events = events | EPOLLET | EPOLLONESHOT; // 保持边缘触发和 EPOLLONESHOT
    // event.events = events | EPOLLET; // 保持边缘触发
    
    // 使用 EPOLL_CTL_MOD 操作来修改文件描述符的事件
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == -1) {
        // 错误处理，可以打印日志
    }
}

void EpollServer::removeFd(int fd) {
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
        perror("epoll_ctl_del failed");
        Logger::getInstance().log("ERROR", "Failed to remove fd " + std::to_string(fd) + " from epoll.");
    }
}

// 设置定时器
void EpollServer::setTimer(HeapTimer* timer) {
    timer_ = timer;
}

void EpollServer::setConnectionCallback(EventCallback callback) {
    connection_callback_ = std::move(callback);
}

void EpollServer::setReadCallback(EventCallback callback) {
    read_callback_ = std::move(callback);
}

void EpollServer::setWriteCallback(EventCallback callback) {
    write_callback_ = std::move(callback);
}

void EpollServer::setCloseCallback(EventCallback callback) {
    close_callback_ = std::move(callback);
}

void EpollServer::run() {
    epoll_event events[MAX_EVENTS];
    while (true) {

        int timeout_ms = (timer_ != nullptr) ? timer_->getNextTick() : -1;
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout_ms);  // 有一个 timeout 参数，决定了最长阻塞的时间
        if (nfds == -1) {
            perror("epoll_wait failed");
            Logger::getInstance().log("ERROR", "epoll_wait failed, shutting down server.");
            break;
        }

        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            uint32_t event_type = events[i].events;

            if (fd == listen_fd_) {
                // 处理新连接
                if (connection_callback_) {
                    connection_callback_(listen_fd_);
                }
            } else {
                // 处理错误和断开事件
                if (event_type & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                    if (close_callback_) {
                        LOG_INFO("Client[" + std::to_string(fd) + "] is closed due to EPOLLERR | EPOLLRDHUP | EPOLLHUP.");
                        close_callback_(fd);
                    }
                }

                // 处理可读事件
                if (event_type & EPOLLIN) {
                    if (read_callback_) {
                        read_callback_(fd);
                    }
                }

                // 处理可写事件
                if (event_type & EPOLLOUT) {
                    if (write_callback_) {
                        write_callback_(fd);
                    }
                }
            }
        }

        // 连接超时关闭
        if (timer_ != nullptr) {
            std::vector<int> expired_fds;
            timer_->tick(expired_fds);
            for (int fd : expired_fds) {
                // 调用回调函数来关闭连接
                if (close_callback_) {
                    LOG_INFO("Client[" + std::to_string(fd) + "] is closed due to timeout.");
                    close_callback_(fd);
                }
            }
        }
    }
}
```

:bulb:封装，epoll事件状态和事件处理



### HttpServer 类

`http_server.hpp`

```cpp
#pragma once

#include "webserver/core/epoll_server.hpp"
#include "webserver/http/http_connection.hpp"
#include "webserver/sql/sql_connector.hpp"
#include "webserver/pool/thread_pool.hpp"
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
    HeapTimer heap_timer_;  // 计时器
    std::unordered_map<int, std::unique_ptr<HttpConnection>> http_connections_;
    std::mutex connection_mutex_;  // 保护 http_connections_;
};
```

:bulb:使用哈希表来映射 `client_fd` 和其对应的 Http 连接



`http_server.cpp`

```cpp
#include "webserver/core/http_server.hpp"
#include "webserver/utils/log.hpp"
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <iostream>

constexpr int MAX_TIMEOUT = 5000;  // 自动断开连接的时间设置为 1000ms
constexpr int MAX_WORK_THREAD = 128;

HttpServer::HttpServer(int port)
    : epoll_server_(port),
      thread_pool_(MAX_WORK_THREAD), // 假设线程池大小为10
      heap_timer_() {
    
    LOG_INFO("HttpServer constructor started.");

    auto sql_pool = SqlPool::getInstance();
    sql_pool->init("tcp://127.0.0.1", "root", "Lx@259416", "WebServer_DB", 3306, 10);
    LOG_INFO("SqlPool instance obtained.");

    // 传递定时器
    epoll_server_.setTimer(&heap_timer_);

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
        http_connections_[client_fd] = std::make_unique<HttpConnection>(client_fd);

        // 添加定时器和事件到 EpollServer
        heap_timer_.addTimer(client_fd, MAX_TIMEOUT);
        // epoll_server_.addFd(client_fd, EPOLLIN | EPOLLET);  // 可读、边缘触发、一次性触发
        epoll_server_.addFd(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT);  // 可读、边缘触发、一次性触发

        Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] connected!");
    }
}

void HttpServer::handleRead(int client_fd) {
    // 1. 主线程中加锁，安全地访问共享 map
    std::unique_ptr<HttpConnection> conn_ptr;
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
        LOG_INFO("Client[" + std::to_string(client_fd) + "] is closed due to I/O error or connection closed.");
        handleClose(client_fd);
        return ;
    }

    auto connection_ptr = conn_ptr.release();

    // 3. 将耗时的业务逻辑提交给线程池
    thread_pool_.enqueue([this, client_fd, raw_data, connection_ptr]() mutable {
        auto conn_ptr = std::make_unique<HttpConnection>(*connection_ptr);
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
        this->heap_timer_.updateTimer(client_fd, MAX_TIMEOUT);
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

    HttpConnection& conn = *(it->second);  // 取别名

    // 2. 在主线程中进行 I/O 操作：循环发送数据
    if (conn.sendResponse()) {
        if (conn.is_keep_alive) {
            // 长连接，重新监听可读事件
            epoll_server_.updateFd(client_fd, EPOLLIN | EPOLLET | EPOLLONESHOT);
            heap_timer_.updateTimer(client_fd, MAX_TIMEOUT);
        } else {
            // 短连接，处理关闭
            LOG_INFO("Client[" + std::to_string(client_fd) + "] is closed due to http request.");
            handleClose(client_fd);
        }
    } else {
        // 未发送完毕，继续监听可写事件
        epoll_server_.updateFd(client_fd, EPOLLOUT | EPOLLET | EPOLLONESHOT);
        heap_timer_.updateTimer(client_fd, MAX_TIMEOUT);
    }
}

void HttpServer::handleClose(int client_fd) {
    // 线程安全地移除连接
    auto it = http_connections_.find(client_fd);
    if (it != http_connections_.end()) {
        // LOG_INFO("Client[" + std::to_string(client_fd) + "] is closed due to http request.");
        
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
```

:bulb:回调函数的实现与注册，主线程和工作线程的业务逻辑(I/O密集型操作，CPU密集型操作)，匿名函数的使用和变量捕获



### MySQLConnector类

实现一个 MySQLConnector 类，表示一个数据库的连接实例。

`sql_connector.hpp`

```cpp
#pragma once
#include <string>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>
#include <cppconn/prepared_statement.h>
#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <iostream>
#include "webserver/utils/log.hpp"

class MySQLConnector {
public:
    MySQLConnector(const std::string&, const std::string&, const std::string&, const std::string&, unsigned int);
    ~MySQLConnector();

    bool insertUser(const std::string&, const std::string&);
    bool verifyUser(const std::string&, const std::string&);
    
private:
    sql::Driver* driver_;  // 保存 driver 实例
    sql::Connection* conn_;  // 使用 Connector//C++ 的 Connection 对象
};
```

:bulb:



`sql_connector.cpp`

```cpp
#include "webserver/sql/sql_connector.hpp"

MySQLConnector::MySQLConnector(const std::string& host, const std::string& user, const std::string& passwd, const std::string& dbname, unsigned int port) {
    try
    {
        driver_ = get_driver_instance();
        conn_ = driver_->connect(host + ":" + std::to_string(port), user, passwd);
        conn_->setSchema(dbname);
        // LOG_INFO("MySQL connection successful!");
    }
    catch(sql::SQLException& e)
    {
        std::cerr << "MySQL Connector /C++ error:" << "\n";
        std::cerr << "Error code: " << e.getErrorCode() << "\n";
        std::cerr << "SQLState" << e.getSQLState() << "\n";
        std::cerr << "Message" << e.what() << std::endl;
    }
}

MySQLConnector::~MySQLConnector() {
    if (conn_) {
        delete conn_;
        conn_ = nullptr;
        // 不删除 driver_，因为它是单例对象
    }
}

bool MySQLConnector::insertUser(const std::string& username, const std::string& password) {
    try {
        sql::PreparedStatement* pstmt = conn_->prepareStatement("INSERT INTO user (username, password) VALUES (?, ?)");
        pstmt->setString(1, username);
        pstmt->setString(2, password);
        pstmt->executeUpdate();
        delete pstmt;
        return true;
    }
    catch(sql::SQLException& e) {
        std::cerr << "Insert failed: " << e.what() << std::endl;
        return false;
    }
}

bool MySQLConnector::verifyUser(const std::string& username, const std::string& password) {
    try {
        sql::PreparedStatement* pstmt = conn_->prepareStatement("SELECT password FROM user WHERE username = ?");
        pstmt->setString(1, username);
        sql::ResultSet* res = pstmt->executeQuery();
        bool isValid = false;
        if (res->next()) {
            std::string storedPassword = res->getString("password");
            isValid = (password == storedPassword);
        }

        delete res;
        delete pstmt;

        return isValid;
    }
    catch(sql::SQLException& e) {
        std::cerr << "Verification failed: " << e.what() << std::endl;
        return false;
    }
}

```

:bulb:



### SqlPool 类

`sql_pool.hpp`

```cpp
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
```

:bulb:单例模式，智能指针管理



`sql_pool.cpp`

```cpp
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
    LOG_INFO("Allocate a sql connection.");

    std::shared_ptr<MySQLConnector> shared_conn(raw_conn, [this](MySQLConnector* p_conn) {
        // 自定义删除器：将连接归还给连接池
        std::unique_lock<std::mutex> lock(queue_mutex_);
        conn_queue_.push(p_conn);
        ++ free_conn_count_;
        cond_var_.notify_one();  // 唤醒一个等待连接的线程
        LOG_INFO("Release a sql connection to the pool.");
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
```

:bulb:如何初始化一个连接池，自定义删除器



### HeapTimer 类

实现定时器，以在 HTTP 连接超时之后自动断开连接。

`heaptimer.hpp`

```cpp
#pragma once

#include <chrono>
#include <unordered_map>
#include <queue>
#include <functional>
#include <vector>
#include <mutex>

struct TimerNode {
    int client_fd;
    int64_t expire;
    bool operator<(const TimerNode& other) const {
        return expire > other.expire;  // 小根堆，越早过期优先级越高
    }
};

class HeapTimer {
public:
    // 添加新的定时器
    void addTimer(int client_fd, int timeout_ms);
    // 更新已有定时器
    void updateTimer(int client_fd, int timeout_ms);
    // 删除定时器
    void removeTimer(int client_fd);
    // 检查所有过期的连接，并调用回调关闭
    void tick(std::vector<int>& expired_fds);
    // 获取最近一次定时器事件剩余时间
    int getNextTick();

private:
    int64_t getTimeMs() const;
    std::mutex mutex_;

    std::priority_queue<TimerNode> heap_;  // 小根堆
    std::unordered_map<int, int64_t> client_fd_to_expire_;  // client_fd 到最新过期时间的映射
};

```

:bulb:小根堆，`client_fd` 的到期时间映射



`heaptimer.cpp`

```cpp
#include "webserver/timer/heaptimer.hpp"

void HeapTimer::addTimer(int client_fd, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t expire = getTimeMs() + timeout_ms;
    heap_.push({client_fd, expire});
    client_fd_to_expire_[client_fd] = expire;
}

void HeapTimer::updateTimer(int client_fd, int timeout_ms) {
    addTimer(client_fd, timeout_ms);
}

void HeapTimer::removeTimer(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_fd_to_expire_.erase(client_fd);
}

void HeapTimer::tick(std::vector<int>& expired_fds) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t now = getTimeMs();

    while (!heap_.empty())
    {
        TimerNode node = heap_.top();
        // 如果已经被移除或存在更新的/更晚的版本，则跳过
        auto it = client_fd_to_expire_.find(node.client_fd);
        if (it == client_fd_to_expire_.end() || it->second != node.expire) {
            heap_.pop();
            continue;
        }

        // 如果堆顶元素没有过期，则其他所有节点也没有过期，退出循环
        if (node.expire > now) break;

        expired_fds.push_back(node.client_fd);
        heap_.pop();
        client_fd_to_expire_.erase(node.client_fd);
    }
}

int64_t HeapTimer::getTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int HeapTimer::getNextTick() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (heap_.empty()) return -1;  // 无限阻塞

    // 清理堆顶的所有过期或无效节点
    while (!heap_.empty()) {
        TimerNode node = heap_.top();
        auto it = client_fd_to_expire_.find(node.client_fd);
        if (it == client_fd_to_expire_.end() || it->second != node.expire) {
            heap_.pop();
            continue;
        }
        break;
    }

    if (heap_.empty()) return -1;  // 无限阻塞

    int64_t now = getTimeMs();
    int64_t expire = heap_.top().expire;

    // 返回下一个过期事件的剩余时间
    return expire > now ? (expire - now) : 0;
}
```

:bulb:



### ThreadPool 类

`thread_pool.hpp`

```cpp
#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include "webserver/utils/log.hpp"

class ThreadPool {
public:
    ThreadPool(size_t threadCount = 4);
    ~ThreadPool();

    // 添加任务到线程池
    void enqueue(const std::function<void()>& task);

private:
    void worker();
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
};
```

:bulb:通过一个原子变量 `stop_` 来优雅地退出



`thread_pool.cpp`

```cpp
#include "webserver/pool/thread_pool.hpp"

ThreadPool::ThreadPool(size_t threadCount) : stop_(false) {
    for (size_t i = 0; i < threadCount; ++ i) {
        workers_.emplace_back([this]() {
            this->worker();
        });
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true);
    cv_.notify_all();
    for (std::thread& worker: workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(const std::function<void()>& task) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        tasks.emplace(task);
    }
    cv_.notify_one();
}

void ThreadPool::worker() {
    while (!stop_.load()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stop_.load() || !tasks.empty();
            });

            if (stop_.load() && tasks.empty()) return;

            task = std::move(tasks.front());
            tasks.pop();
        }
        
        task();  // 执行任务
    }
}
```

:bulb:



### HttpConnection 类

`http_request.hpp`

```cpp
#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>

class HttpRequest
{
public:
    enum class ParseState {
        REQUEST_LINE,
        HEADERS,
        BODY,
        FINISH
    };

    void parseRequestLine(const std::string& line) {
        std::istringstream stream(line);
        stream >> method_ >> path_ >> version_;
    }

    void parseHeaderLine(const std::string& line) {
        size_t pos = line.find(":");
        if (pos != std::string::npos) {
            // Content-Type: text/html
            // Content-Length: 46
            // Connection: close
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            while (!value.empty() && value.front() == ' ')
                value.erase(value.begin());
            headers_[key] = value;
        }
    }

    void parseRequest(const std::string& raw) {
        std::istringstream stream(raw);
        std::string line;
        ParseState state = ParseState::REQUEST_LINE;
        std::string body;
        bool hasBody = false;

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            switch (state) {
                case ParseState::REQUEST_LINE:
                    parseRequestLine(line);
                    state = ParseState::HEADERS;
                    break;
                case ParseState::HEADERS:
                    if (line.empty()) {
                        if (headers_.count("Content-Length")) {
                            hasBody = true;
                            state = ParseState::BODY;
                        } else {
                            state = ParseState::FINISH;
                        }
                    } else {
                        parseHeaderLine(line);
                    }
                    break;
                case ParseState::BODY:
                    body_ += line;
                    state = ParseState::FINISH;
                    break;
                case ParseState::FINISH:
                    break;
            }

            if (state == ParseState::FINISH) break;
        }
    }

public:
    std::string method_;  // [GET, POST, ...]
    std::string path_;  // request path, ["/", "/index", "/picture", ...]
    std::string version_;  // HTTP version, ["HTTP/1.1", "HTTP/1.0", ...]
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};
```

:bulb:



`http_response.hpp`

```cpp
#pragma once

#include <string>
#include <unordered_map>
#include <sstream>

class HttpResponse {
public:
    // 构造函数
    HttpResponse() {}

    // 设置状态行
    void set_status_line(int code, const std::string& message, const bool keep_alive) {
        status_code_ = code;
        status_line_ = message;
        if (keep_alive) connection_status_ = "keep-alive";
        else connection_status_ = "close";
        status_line_ += connection_status_ + "\r\n";
    }

    // 添加响应头
    void add_header(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    // 设置响应体
    void set_body(const std::string& body) {
        body_ = body;
    }

    void set_content_type(const std::string& content_type) {
        content_type_ = content_type;
    }

    std::string get_status_line() {
        std::ostringstream status_line_stream;
        status_line_stream << status_line_ << "\r\n";
        return status_line_stream.str();
    }

    // 将整个响应转换为字符串，供发送
    std::string toString() const {
        std::ostringstream response_stream;

        response_stream << status_line_;
        response_stream << "Content-Type: " << content_type_ << "\r\n";
        response_stream << "Content-Length: " << body_.size() << "\r\n";
        response_stream << "Connection: " << connection_status_ << "\r\n";
        response_stream << "\r\n";
        response_stream << body_;
        
        return response_stream.str();
    }

private:
    int status_code_;
    std::string status_line_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::string content_type_;
    std::string connection_status_;  // "keep-alive", "close"
};
```

:bulb:



`http_connection.hpp`

```cpp
#pragma once

#include <string>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include "webserver/http/http_request.hpp"
#include "webserver/http/http_response.hpp"
#include "webserver/pool/sql_pool.hpp"

class HttpConnection {
public:
    int use_count = 0;
    bool is_keep_alive;

    explicit HttpConnection(int client_fd);

    bool receiveRequest(std::string& raw_data);
    void parseRequest(const std::string& raw_data);
    void buildResponse();
    bool sendResponse();
    bool sendFile();
    // void sendResponse();

private:
    const int READ_BUFFER_ = 4096;
    int client_fd_;
    std::string resources_root_path_;
    std::string buffer_;
    size_t write_buffer_index_ = 0; // 新增：已发送字节数
    HttpRequest request_;
    HttpResponse response_;
    bool is_connection_;

    std::string router();
    bool handleGET();
    bool handlePOST();
    std::string decodeURLComponent(const std::string& s);
    std::string getContentType(const std::string& path);
    std::string readFile(const std::string& file_path);
    void parseFormURLEncoded(const std::string& body, std::unordered_map<std::string, std::string>& data);
};
```

:bulb:



`http_connection.cpp`

```cpp
#include "webserver/http/http_connection.hpp"

HttpConnection::HttpConnection(int client_fd) : client_fd_(client_fd), is_connection_(true), resources_root_path_("/home/amonologue/Projects/WebServer/resources") {}

bool HttpConnection::receiveRequest(std::string& raw_data) {
    char buffer[READ_BUFFER_];

    ssize_t n;
    while ((n = recv(client_fd_, buffer, READ_BUFFER_, 0)) > 0) {
        buffer_.append(buffer, n);

        // 查找 header 结束位置
        size_t header_end = buffer_.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            // 查找 Content-Length
            size_t content_len = 0;
            size_t pos = buffer_.find("Content-Length:");
            if (pos != std::string::npos) {
                size_t start = pos + strlen("Content-Length:");
                size_t end = buffer_.find("\r\n", start);
                std::string len_str = buffer_.substr(start, end - start);
                content_len = std::stoi(len_str);
            }

            // 当前是否已经接收完整报文
            size_t total_expected = header_end + 4 + content_len;  // len('/r/n/r/n') = 4
            if (buffer_.size() >= total_expected) {
                raw_data = buffer_.substr(0, total_expected);
                buffer_.erase(0, total_expected);  // erase the proposed request
                return true;
            }
        }
    }

    // The client closed the link
    if (n == 0) return false;

    // EAGAIN
    return false;
}

void HttpConnection::parseRequest(const std::string& raw_data) {
    request_.parseRequest(raw_data);

    is_keep_alive = (request_.headers_["Connection"] == "keep-alive");
}

void HttpConnection::buildResponse() {
    bool isSuccess = false;
    if (request_.method_ == "POST") {
        isSuccess = handlePOST();
    } else { // GET
        isSuccess = handleGET();
    }

    buffer_ = response_.toString();
}

bool HttpConnection::sendResponse() {
    size_t remaining = buffer_.size() - write_buffer_index_;
    while (remaining > 0) {
        ssize_t bytes_sent = send(client_fd_, buffer_.c_str() + write_buffer_index_, remaining, 0);
        if (bytes_sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 内核缓冲区已满，返回 false，表示未完成
                return false;
            } else {
                // 发生致命错误
                return false; // 或抛出异常
            }
        }
        write_buffer_index_ += bytes_sent;
        remaining -= bytes_sent;
    }
    // 所有数据已发送
    write_buffer_index_ = 0; // 重置
    buffer_.clear(); // 清空缓冲区
    // 所有数据发送完毕
    return true;
}

bool HttpConnection::sendFile() {
    // TODO
    return false;
}

std::string HttpConnection::router() {
    std::string file_absolute_path;
    // 路由匹配
    if (request_.path_ == "/") {
        file_absolute_path = resources_root_path_ + "/index.html";
    } else if (request_.path_ == "/picture") {
        file_absolute_path = resources_root_path_ + "/picture.html";
    } else if (request_.path_ == "/video") {
        file_absolute_path = resources_root_path_ + "/video.html";
    } else if (request_.path_ == "/login") {
        file_absolute_path = resources_root_path_ + "/login.html";
    } else if (request_.path_ == "/register") {
        file_absolute_path = resources_root_path_ + "/register.html";
    } else if (request_.path_ == "/welcome") {
        file_absolute_path = resources_root_path_ + "/welcome.html";
    } else {
        file_absolute_path = resources_root_path_ + request_.path_;
    }
    return file_absolute_path;
}

bool HttpConnection::handleGET() {
    bool isSuccess = false;

    std::string file_path = router();
    std::ifstream file(file_path, std::ios::binary);
    if (file) {
        isSuccess = true;
        response_.set_status_line(200, "HTTP/1.1 200 OK\r\n", is_keep_alive);
        response_.set_body(readFile(file_path));
        response_.set_content_type(getContentType(file_path));
    } else {
        isSuccess = false;
        response_.set_status_line(404, "HTTP/1.1 404 Not Found\r\n", is_keep_alive);
        response_.set_body(readFile("/home/amonologue/Projects/WebServer/resources/404.html"));
        response_.set_content_type("text/html");
    }

    return isSuccess;
}

bool HttpConnection::handlePOST() {
    bool isSuccess = false;

    std::unordered_map<std::string, std::string> account;
    parseFormURLEncoded(request_.body_, account);
    auto sql_pool_ = SqlPool::getInstance();
    auto mysql = sql_pool_->getConnection();
    if (request_.path_ == "/register") {
        isSuccess = mysql->insertUser(account["username"], account["password"]);
        if (isSuccess) LOG_INFO("register succeed.");
    } else if (request_.path_ == "/login") {
        isSuccess = mysql->verifyUser(account["username"], account["password"]);
        if (isSuccess) LOG_INFO("login succeed.");
    }

    if (isSuccess) {
        response_.set_status_line(302, "HTTP/1.1 302 Found\r\nLocation: /welcome\r\nContent-Length: 0\r\nConnection: ", is_keep_alive);
        send(client_fd_, response_.get_status_line().c_str(), response_.get_status_line().size(), 0);  // 发送重定向响应
    } else {
        response_.set_status_line(404, "HTTP/1.1 404 Not Found\r\n", is_keep_alive);
        response_.set_body(readFile("/home/amonologue/Projects/WebServer/resources/404.html"));
        response_.set_content_type("text/html");
    }

    return isSuccess;
}

std::string HttpConnection::decodeURLComponent(const std::string& s) {
    std::string result;
    char ch;
    int i, ii;
    for (i = 0; i < s.length(); ++ i) {
        if (int(s[i]) == 37) {
            sscanf(s.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            result += ch;
            i += 2;
        } else if (s[i] == '+') {
            result += ' ';
        } else {
            result += s[i];
        }
    }
    return result;
}

std::string HttpConnection::getContentType(const std::string& path) {
    if (path.ends_with(".html") || path.ends_with(".htm"))
        return "text/html";
    if (path.ends_with(".css"))
        return "text/css";
    if (path.ends_with(".js"))
        return "application/javascript";
    if (path.ends_with(".png"))
        return "image/png";
    if (path.ends_with(".jpg") || path.ends_with(".jpeg"))
        return "image/jpeg";
    if (path.ends_with(".txt"))
        return "text/plain";
    if (path.ends_with(".mp4"))
        return "video/mp4";
    return "application/octet-stream";
}

std::string HttpConnection::readFile(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

void HttpConnection::parseFormURLEncoded(const std::string& body, std::unordered_map<std::string, std::string>& data) {
    std::istringstream stream(body);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        size_t eq_pos = pair.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = decodeURLComponent(pair.substr(0, eq_pos));
            std::string value = decodeURLComponent(pair.substr(eq_pos + 1));
            data[key] = value;
        }
    }
}
```

:bulb:



## 运行和测试

编译并运行 WebServer

```bash
mkdir build
cd build
cmake ..
make
./webserver
```

服务器压力测试

```bash
cd webbench-1.5
make
./webbench -c 1000 -t 5 http://127.0.0.1:8080/
```



## 发布版本

### Version 1.0

-   [x] 重构代码，整理仓库结构
    -   [x] 重构 EpollServer
    -   [x] 重构 HttpServer
    -   [x] 重构日志类
    -   [x] 重构线程池
    -   [x] 重构数据库连接池
    -   [x] 重构定时器
-   [x] 解决连接长时间不自动释放的问题

![image-20250914185112889](C:\Users\Amonologue\AppData\Roaming\Typora\typora-user-images\image-20250914185112889.png)



### Version 0.7

![image-20250626150831128](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506261508970.png)

TODO:

-   [ ] 重新整理仓库结构，方便拓展
    目标仓库结构为：

    ```bash
    webserver/
    ├── apps/                   # 具体应用实现
    │   ├── blog/               # 博客应用
    │   ├── chat/               # 聊天室应用
    │   ├── game/               # 在线消消乐游戏
    │   └── ...                 # 其他应用
    ├── src/
    │   ├── core/               # 服务器核心组件
    │   │   ├── epoll/          # epoll 封装
    │   │   ├── threadpool/     # 线程池实现
    │   │   ├── timer/          # 定时器管理
    │   │   ├── connection/     # 连接管理
    │   │   └── server.cpp      # 服务器主循环
    │   ├── http/               # HTTP 协议处理
    │   │   ├── request.cpp     # 请求解析
    │   │   ├── response.cpp    # 响应生成
    │   │   ├── router.cpp      # 路由系统
    │   │   └── middleware/     # 中间件系统
    │   ├── database/           # 数据库抽象层
    │   │   ├── mysql/          # MySQL 实现
    │   │   ├── sqlite/         # SQLite 实现（可选）
    │   │   └── db_interface.h  # 统一数据库接口
    │   ├── utils/              # 工具类
    │   │   ├── logger/         # 日志系统
    │   │   ├── config/         # 配置解析
    │   │   ├── buffer/         # 缓冲区管理
    │   │   └── crypto/         # 加密工具
    │   └── api/                # 应用开发接口
    │       ├── application.h   # 应用接口基类
    │       └── plugin.h        # 插件接口
    ├── include/                # 公共头文件
    │   └── webserver/          # 项目头文件命名空间
    ├── third_party/            # 第三方依赖
    ├── tests/                  # 测试套件
    │   ├── unit/               # 单元测试
    │   ├── integration/        # 集成测试
    │   └── benchmark/          # 性能测试
    ├── scripts/                # 实用脚本
    │   ├── deploy/             # 部署脚本
    │   └── build/              # 构建脚本
    ├── config/                 # 配置文件
    │   ├── server.conf         # 服务器配置
    │   └── apps/               # 应用配置
    ├── docs/                   # 项目文档
    ├── resources/              # 静态资源
    │   ├── static/             # 静态文件（CSS/JS/图片）
    │   └── templates/          # HTML 模板
    ├── CMakeLists.txt          # 主构建配置
    ├── Dockerfile              # 容器化支持
    └── .github/                # CI/CD 配置
    ```

-   [ ] 完善此项目，为之后添加博客、在线聊天室、在线消消乐等功能打好基础；

-   [ ] 添加项目笔记；



### Version 0.6

[Link]()

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   持久连接，将 Connection 设置为 keep-alive，节约 TCP 连接的开销
*   添加 Timer 控制连接时间
*   拓展为多线程 Reactor 模型，添加线程池
*   模块化，将 WebServer 类中对 HTTP 的具体处理放到 `http/` 下
*   `webbench-1.5` 服务器压力测试，完成时间：2025-6-24 11:58:57
    ![image-20250624115946580](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506241159019.png)



### Version 0.5

[Link](https://github.com/A-Egoist/WebServer/tree/b854639c0a7b440c91564d307fa45448d07247a0)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   持久连接，将 Connection 设置为 keep-alive，节约 TCP 连接的开销
*   添加 Timer 控制连接时间
*   拓展为多线程 Reactor 模型
*   `webbench-1.5` 服务器压力测试，完成时间：2025-6-23 20:14:17
    ![image-20250623201442703](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506232014356.png)

TODO：

-   [ ] 在添加多线程之后，发现 QPS 有一些下降，需要分析原因



### Version 0.4

[Link](https://github.com/A-Egoist/WebServer/tree/8750b027ba9acd0f8eb961842a216ff6814ecc82)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   持久连接，将 Connection 设置为 keep-alive，节约 TCP 连接的开销
*   添加 Timer 控制连接时间
*   `webbench-1.5` 服务器压力测试，完成时间：2025-6-21 19:33:44
    ![image-20250621193406828](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506211934008.png)

测试是否 keep-alive：

```bash
curl -v --http1.1 http://127.0.0.1:8080/ http://127.0.0.1:8080/picture http://127.0.0.1:8080/login http://127.0.0.1:8080/welcome > http11NUL
```

```bash
curl -v --http1.0 http://127.0.0.1:8080/ http://127.0.0.1:8080/picture http://127.0.0.1:8080/login http://127.0.0.1:8080/welcome > http10NUL
```

```bash
curl -v --header "Connection: close" http://127.0.0.1:8080

curl -v --header "Connection: keep-alive" http://127.0.0.1:8080
```



### Version 0.3

[Link](https://github.com/A-Egoist/WebServer/tree/5a25bb874cd17e16a1e446188b69a10c97e098b7)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；
*   加入异步日志系统，有助于调试BUG，定位错误，数据分析；
*   完成一次 `webbench-1.5` 服务器压力测试，完成时间：2025-6-18 21:20:55
    ![image-20250618212041657](https://amonologue-image-bed.oss-cn-chengdu.aliyuncs.com/2025/202506182120838.png)

### Version 0.2

[Link](https://github.com/A-Egoist/WebServer/tree/11830bcd8217816af960a1f5e7cb15d7dda5bdcc)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；
*   连接 MySQL 数据库，支持用户的注册和登录；

### Version 0.1

[Link](https://github.com/A-Egoist/WebServer/tree/4dbadc7e63c5665b921c8cce4055bf8d54db595f)

已完成 WebServer 的基础功能，包括：

*   处理 GET 和 POST 请求，返回对应的静态内容；

