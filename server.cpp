#include "server.hpp"

constexpr int MAX_EVENTS = 1024;  // epoll 每次最多返回的事件数量
constexpr int READ_BUFFER = 4096;  // 每个 socket 读取数据时的缓冲区大小

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
    std::string raw_data = receiveHttpRequest(client_fd);
    HttpRequest request = parseHttpRequest(raw_data);

    processHttpRequest(client_fd, request);
    closeClient(client_fd);
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
                // 接收新连接, 持续接收, 直至没有新的连接到达
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
                    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);
                }
            } else {
                // 处理客户端数据
                handleConnection(fd);
            }
        }
    }

    close(listen_fd_);
    close(epoll_fd_);
}

void WebServer::closeClient(int client_fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    Logger::getInstance().log("INFO", "Client[" + std::to_string(client_fd) + "] closed");
}

void WebServer::processHttpRequest(int client_fd, HttpRequest& request) {
    // 根据客户端请求的 URL 路径，动态返回不同的页面内容
    std::string file_path = router(request.path);
    
    // POST
    if (request.method == "POST") {
        bool success = handlePOST(request, client_fd);
        if (success) {
            std::string redirect_response = "HTTP/1.1 302 Found\r\nLocation: /welcome\r\nContent-Length: 0\r\nConnection: close\r\n";
            send(client_fd, redirect_response.c_str(), redirect_response.size(), 0);  // 发送重定向响应
            return ;
        } else {
            // TODO, Incorrect username or password;
        }
    }

    // GET
    std::string status_line;
    std::string response_body;
    std::string content_type;
    std::ifstream file(file_path, std::ios::binary);
    
    if (file) {
        status_line = "HTTP/1.1 200 OK\r\n";
        response_body = readFile(file_path);
        content_type = getContentType(file_path);
    } else {
        status_line = "HTTP/1.1 404 Not Found\r\n";
        response_body = readFile("/home/amonologue/Projects/WebServer/resources/404.html");
        content_type = "text/html";
    }

    std::string response = 
        status_line + 
        "Content-Type: " + content_type + "\r\n" + 
        "Content-Length: " + std::to_string(response_body.size()) + "\r\n" + 
        "Connection: close\r\n\r\n" + 
        response_body;
    
    send(client_fd, response.c_str(), response.size(), 0);  // 发送响应
}

std::string WebServer::router(std::string& requestPath) {
    std::string resources_root_path = "/home/amonologue/Projects/WebServer/resources";
    std::string file_absolute_path;
    // 路由匹配
    if (requestPath == "/") {
        file_absolute_path = resources_root_path + "/index.html";
    } else if (requestPath == "/picture") {
        file_absolute_path = resources_root_path + "/picture.html";
    } else if (requestPath == "/video") {
        file_absolute_path = resources_root_path + "/video.html";
    } else if (requestPath == "/login") {
        file_absolute_path = resources_root_path + "/login.html";
    } else if (requestPath == "/register") {
        file_absolute_path = resources_root_path + "/register.html";
    } else if (requestPath == "/welcome") {
        file_absolute_path = resources_root_path + "/welcome.html";
    } else {
        file_absolute_path = resources_root_path + requestPath;
    }
    return file_absolute_path;
}