#include "server.hpp"

constexpr int MAX_EVENTS = 1024;  // epoll 每次最多返回的事件数量
constexpr int READ_BUFFER = 4096;  // 每个 socket 读取数据时的缓冲区大小
constexpr int MAX_TIMEOUT = 5000;  // 5 秒未活跃则关闭
constexpr int MAX_THREAD_COUNT = 10;  // 线程池最大容量

// 构造函数中只是初始化端口号和一些成员变量，listen_fd_ 和 epoll_fd_ 暂时设为无效值。
WebServer::WebServer(int port) : port_(port), listen_fd_(-1), epoll_fd_(-1), mysql(), thread_pool_(MAX_THREAD_COUNT) {}

// 设置文件描述符非阻塞
void WebServer::setNonBlocking(int fd) {
    // 在使用 epoll 的 边缘触发（EPOLLET） 模式时，必须配合非阻塞 IO，否则会有“事件丢失”的风险
    int flags = fcntl(fd, F_GETFL, 0);  // 使用 fcntl 给 socket 加上 O_NONBLOCK 标志
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

void WebServer::initSocket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);  // 创建 TCP socket
    if (listen_fd_ == -1) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    setNonBlocking(listen_fd_);  // 设置非阻塞

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;  // 监听所有 IP

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 避免 bind 报地址被占用
    if (bind(listen_fd_, (sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 开始监听连接
    if (listen(listen_fd_, SOMAXCONN) == -1) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    epoll_fd_ = epoll_create1(0);  // 创建 epoll 实例
    if (epoll_fd_ == -1) {
        perror("epoll_create failed");
        exit(EXIT_FAILURE);
    }

    epoll_event event{};
    event.data.fd = listen_fd_;
    event.events = EPOLLIN | EPOLLET;  // 可读 + 边缘触发
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);
}

void WebServer::closeClient(int client_fd) {
    heap_timer_.removeTimer(client_fd);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    // Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] is closed, which is used " + std::to_string(clients[client_fd].useCount) + " times.");
}

void WebServer::handleConnection(int client_fd) {
    HTTPConnection* conn_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        // HTTPConnection http_connection(client_fd);
        // auto [iter, success] = clients.try_emplace(client_fd, std::move(http_connection));
        auto [iter, success] = clients.try_emplace(client_fd, client_fd, &mysql);
        conn_ptr = &(iter->second);
        ++ conn_ptr->use_count;
    }
    HTTPConnection& conn = *conn_ptr;

    // 接收请求数据
    std::string raw_data;
    bool isConnection = conn.receiveRequest(raw_data);
    if (!isConnection) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                Logger::getInstance().log("ERROR", "Client[" + std::to_string(client_fd) + "] is closed due to network error or read error, and it is used " + std::to_string(conn.use_count) + " times.");
                closeClient(client_fd);
                clients.erase(client_fd);
            }
        }
        return;
    }

    // 处理请求
    conn.parseRequest(raw_data);
    conn.sendResponse();

    // 根据连接状态处理
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        if (conn.is_keep_alive) {
            heap_timer_.updateTimer(client_fd, MAX_TIMEOUT);
        } else {
            Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] is closed due to http request, and it is used " + std::to_string(conn.use_count) + " times.");
            closeClient(client_fd);
            clients.erase(client_fd);
        }
    }
}

void WebServer::run() {
    initSocket();  // 初始化服务器 socket + epoll
    std::cout << "Listening on port " << port_ << "...\n";
    Logger::getInstance().log("INFO", "Listening on port " + std::to_string(port_) + "...");

    epoll_event events[MAX_EVENTS];  // 每个 events[i] 都表示一个就绪的 socket 文件描述符（fd）及其事件类型

    // 持续监听
    while (true) {
        int timeout = heap_timer_.getNextTick();  // 每次循环动态调整等待时间

        // 获取请求队列长度
        // int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);  // 阻塞等待就绪事件
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, timeout);  // 阻塞等待就绪事件
        if (nfds == -1) {
            perror("epoll_wait failed");
            break;
        }
        
        // 遍历请求队列中的每一个 Connection
        for (int i = 0; i < nfds; ++ i) {
            int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                // 接收新连接, 持续接收, 直至没有新的连接到达
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(listen_fd_, (sockaddr*)&client_addr, &client_len);
                    if (client_fd < 0) break;

                    setNonBlocking(client_fd);  // 设置为非阻塞模式
                    heap_timer_.addTimer(client_fd, MAX_TIMEOUT);  // 给client_fd添加定时器
                    Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] in!");

                    epoll_event event{};
                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);
                }
            } else {
                // 处理客户端数据
                thread_pool_.enqueue([this, fd] {
                    this->handleConnection(fd);
                });
            }
        }
        std::vector<int> expired_fds;
        heap_timer_.tick(expired_fds);

        for (int fd: expired_fds) {
            auto it = clients.find(fd);
            if (it != clients.end()) {
                Logger::getInstance().log("INFO", "Client[" + std::to_string(fd) + "] is closed due to timeout, and it is used " + std::to_string(it->second.use_count) + " times.");
                closeClient(fd);
                clients.erase(fd);
            }
        }
    }

    close(listen_fd_);
    close(epoll_fd_);
}