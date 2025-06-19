#include "server.hpp"

constexpr int MAX_EVENTS = 1024;  // epoll 每次最多返回的事件数量
constexpr int READ_BUFFER = 4096;  // 每个 socket 读取数据时的缓冲区大小
constexpr int KEEP_ALIVE_TIMEOUT = 60000;  // 5 秒

// 构造函数中只是初始化端口号和一些成员变量，listen_fd_ 和 epoll_fd_ 暂时设为无效值。
WebServer::WebServer(int port) : port_(port), listen_fd_(-1), epoll_fd_(-1), mysql() {}

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
    // event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;  // 可读 + 边缘触发
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);
}

std::string getContentType(const std::string& path) {
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
    return "application/octet-stream";
}

std::string readFile(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

std::string receiveHttpRequest(int client_fd) {
    char buffer[READ_BUFFER];
    memset(buffer, 0, sizeof(buffer));
    std::string raw_data;

    ssize_t n;
    while ((n = recv(client_fd, buffer, READ_BUFFER, 0)) > 0) {
        raw_data.append(buffer, n);

        // 查找 header 结束位置
        size_t header_end = raw_data.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            // 查找 Content-Length
            size_t content_len = 0;
            size_t pos = raw_data.find("Content-Length:");
            if (pos != std::string::npos) {
                size_t start = pos + strlen("Content-Length:");
                size_t end = raw_data.find("\r\n", start);
                std::string len_str = raw_data.substr(start, end - start);
                content_len = std::stoi(len_str);
            }

            // 当前是否已经接收完整报文
            size_t total_expected = header_end + 4 + content_len;
            if (raw_data.size() >= total_expected) {
                break;
            }
        }
    }

    return raw_data;
}

void WebServer::handleGET(HttpRequest& request, int client_fd) {

}

void parseFormURLEncoded(const std::string& body, std::unordered_map<std::string, std::string>& data) {
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

std::string decodeURLComponent(const std::string& s) {
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

bool WebServer::handlePOST(HttpRequest& request, int client_fd) {
    bool success = false;
    std::unordered_map<std::string, std::string> account;
    parseFormURLEncoded(request.body, account);
    if (request.path == "/register") {
        success = mysql.insertUser(account["username"], account["password"]);
    } else if (request.path == "/login") {
        success = mysql.verifyUser(account["username"], account["password"]);
    }
    return success;
}

void WebServer::handleConnection(int client_fd) {
    char buffer[READ_BUFFER];
    static std::unordered_map<int, std::string> read_buffers;

    while (true) {
        int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            else {
                closeClient(client_fd);
                return;
            }
        } else if (bytes_read == 0) {
            // 对方关闭连接
            closeClient(client_fd);
            return;
        }

        // 累积读取数据
        read_buffers[client_fd].append(buffer, bytes_read);
    }

    std::string& data = read_buffers[client_fd];

    while (true) {
        HttpRequest request;
        size_t consumed = 0;

        // 尝试解析完整请求；若不完整则 break 等待下一次数据到来
        if (!tryParseHttpRequest(data, request, consumed)) break;

        processHttpRequest(client_fd, request);
        data.erase(0, consumed);

        // 非持久连接，关闭
        if (request.headers.count("Connection") && request.headers["Connection"] == "close") {
            closeClient(client_fd);
            read_buffers.erase(client_fd);
            return;
        }

        // 持久连接：刷新定时器
        addOrUpdateTimer(client_fd, KEEP_ALIVE_TIMEOUT);
    }

    epoll_event event{};
    event.data.fd = client_fd;
    event.events = EPOLLIN | EPOLLET;
    // event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &event);
}


void WebServer::run() {
    initSocket();  // 初始化服务器 socket + epoll
    std::cout << "Listening on port " << port_ << "...\n";
    Logger::getInstance().log("INFO", "Listening on port " + std::to_string(port_) + "...");

    epoll_event events[MAX_EVENTS];  // 每个 events[i] 都表示一个就绪的 socket 文件描述符（fd）及其事件类型

    // 持续监听
    while (true) {
        // 获取请求队列长度
        int nfds = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);  // 阻塞等待就绪事件
        if (nfds == -1) {
            perror("epoll_wait failed");
            break;
        }

        // 遍历请求队列中的每一个 Connection
        for (int i = 0; i < nfds; ++ i) {
            int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(listen_fd_, (sockaddr*)&client_addr, &client_len);
                    if (client_fd < 0) break;

                    setNonBlocking(client_fd);
                    Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] in!");

                    epoll_event event{};
                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLET;
                    // event.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
                    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);

                    // 添加定时器（初始超时时间）
                    addOrUpdateTimer(client_fd, KEEP_ALIVE_TIMEOUT);
                }
            } else {
                handleConnection(fd);
            }
        }
        processCloseQueue();
    }

    close(listen_fd_);
    close(epoll_fd_);
}

void WebServer::processCloseQueue() {
    std::lock_guard<std::mutex> lock(close_queue_mutex_);
    while (!close_queue_.empty()) {
        int fd = close_queue_.front();
        close_queue_.pop();
        closeClient(fd);
    }
}

void WebServer::closeClient(int client_fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);

    if (timers_.count(client_fd)) {
        timers_[client_fd]->cancel();
        timers_.erase(client_fd);
    }
    Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] closed");
}

void WebServer::processHttpRequest(int client_fd, HttpRequest& request) {
    // 根据客户端请求的 URL 路径，动态返回不同的页面内容
    std::string resources_root_path = "/home/amonologue/Projects/WebServer/resources";
    std::string file_path;
    std::string response_body;
    std::string content_type;
    std::string status_line;

    // 路由匹配
    bool main_path = true;
    if (request.path == "/") {
        file_path = resources_root_path + "/index.html";
    } else if (request.path == "/picture") {
        file_path = resources_root_path + "/picture.html";
    } else if (request.path == "/video") {
        file_path = resources_root_path + "/video.html";
    } else if (request.path == "/login") {
        file_path = resources_root_path + "/login.html";
    } else if (request.path == "/register") {
        file_path = resources_root_path + "/register.html";
    } else if (request.path == "/welcome") {
        file_path = resources_root_path + "/welcome.html";
    } else {
        file_path = resources_root_path + request.path;
        main_path = false;
    }

    bool is_redirect = false;
    if (request.method == "POST") {
        bool success = handlePOST(request, client_fd);
        if (success) {
            is_redirect = true;  // 设置重定向 flag
        }
    }

    std::ifstream file(file_path, std::ios::binary);
    if (is_redirect) {
        status_line = "HTTP/1.1 302 Found\r\n";
        std::string redirect_location = "/welcome";  // 重定向到 welcome 界面

        std::string redirect_response = 
            status_line + 
            "Location: " + redirect_location + "\r\n" + 
            "Content-Length: 0\r\n" + 
            "Connection: keep-alive\r\n" + 
            "Keep-Alive: timeout=5\r\n\r\n";
        if (KEEP_ALIVE_TIMEOUT == 0) {
            redirect_response = 
                status_line + 
                "Location: " + redirect_location + "\r\n" + 
                "Content-Length: 0\r\n" + 
                "Connection: close\r\n\r\n";
        }
        send(client_fd, redirect_response.c_str(), redirect_response.size(), 0);  // 发送响应
        if (main_path) Logger::getInstance().log("INFO", "Response to Client[" + std::to_string(client_fd) + "]");
        return ;
    }

    if (file) {
        status_line = "HTTP/1.1 200 OK\r\n";
        response_body = readFile(file_path);
        content_type = getContentType(file_path);
    } else {
        status_line = "HTTP/1.1 404 Not Found\r\n";
        response_body = readFile(resources_root_path + "/404.html");
        content_type = "text/html";
    }

    std::string response = 
        status_line + 
        "Content-Type: " + content_type + "\r\n" + 
        "Content-Length: " + std::to_string(response_body.size()) + "\r\n" + 
        "Connection: keep-alive\r\n" + 
        "Keep-Alive: timeout=5\r\n\r\n" + 
        response_body;
    if (KEEP_ALIVE_TIMEOUT == 0) {
        response = 
            status_line + 
            "Content-Type: " + content_type + "\r\n" + 
            "Content-Length: " + std::to_string(response_body.size()) + "\r\n" + 
            "Connection: close\r\n\r\n" + 
            response_body;
    }
    
    send(client_fd, response.c_str(), response.size(), 0);  // 发送响应
    if (main_path) Logger::getInstance().log("INFO", "Response to Client[" + std::to_string(client_fd) + "]");
}

void WebServer::addOrUpdateTimer(int client_fd, int timeout_ms) {
    if (timers_.count(client_fd)) {
        timers_[client_fd]->reset(timeout_ms);
    } else {
        // auto timer = std::make_shared<TimerTask>(
        //     client_fd, timeout_ms, [client_fd]() {
        //         Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] timed out");
        //         // closeClient(client_fd);
        //         close(client_fd);
        //     });
        auto timer = std::make_shared<TimerTask>(
            client_fd, timeout_ms, [this, client_fd]() {
                Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] timed out");
                std::lock_guard<std::mutex> lock(this->close_queue_mutex_);
                this->close_queue_.push(client_fd);
            });

        timers_[client_fd] = timer;
        timer->start();
    }
}

bool tryParseHttpRequest(const std::string& raw, HttpRequest& request, size_t& request_len) {
    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return false;  // 说明 header 还没收完整

    std::istringstream stream(raw.substr(0, header_end + 2));  // 含最后一个空行
    std::string line;
    ParseState state = ParseState::REQUEST_LINE;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        switch (state) {
            case ParseState::REQUEST_LINE:
                parseRequestLine(line, request);
                state = ParseState::HEADERS;
                break;
            case ParseState::HEADERS:
                if (line.empty()) {
                    state = ParseState::BODY;
                } else {
                    parseHeaderLine(line, request);
                }
                break;
            default:
                break;
        }
    }

    size_t content_length = 0;
    if (request.headers.count("Content-Length")) {
        content_length = std::stoi(request.headers["Content-Length"]);
    }

    size_t total_len = header_end + 4 + content_length;
    if (raw.size() < total_len) return false;  // 还没收到完整 body

    if (content_length > 0)
        request.body = raw.substr(header_end + 4, content_length);

    request_len = total_len;
    return true;
}
