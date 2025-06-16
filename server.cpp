#include "server.hpp"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include "http/http_request.hpp"

constexpr int MAX_EVENTS = 1024;  // epoll 每次最多返回的事件数量
constexpr int READ_BUFFER = 4096;  // 每个 socket 读取数据时的缓冲区大小

// 构造函数中只是初始化端口号和一些成员变量，listen_fd_ 和 epoll_fd_ 暂时设为无效值。
WebServer::WebServer(int port) : port_(port), listen_fd_(-1), epoll_fd_(-1) {}

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

void WebServer::handleConnection(int client_fd) {
    char buffer[READ_BUFFER];
    memset(buffer, 0, sizeof(buffer));

    int bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);  // 从客户端读取数据
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    // 解析收到的request并输出
    std::string raw(buffer, bytes_read);
    HttpRequest request = parseHttpRequest(raw);

    std::cout << "Method: " << request.method << "\n";
    std::cout << "Path: " << request.path << "\n";
    std::cout << "Version: " << request.version << "\n";
    // for (auto& [key, value]: request.headers) {
    //     std::cout << "Header: " << key << ": " << value << "\n";
    // }
    // if (!request.body.empty()) {
    //     std::cout << "Body: " << request.body << "\n";
    // }

    // 根据客户端请求的 URL 路径，动态返回不同的页面内容
    std::string resources_root_path = "/home/amonologue/Projects/WebServer/resources";
    std::string file_path;
    std::string response_body;
    std::string content_type;
    std::string status_line;

    // 路由匹配
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
    } else {
        file_path = resources_root_path + request.path;
    }

    std::ifstream file(file_path, std::ios::binary);
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
        "Connection: close\r\n\r\n" + 
        response_body;
    
    send(client_fd, response.c_str(), response.size(), 0);  // 发送响应
    close(client_fd);  // 因为 HTTP/1.1 默认是持久连接，但这里我们主动设置 Connection: close，并立即 close(client_fd)，因此是非 keep-alive 处理
}

void WebServer::run() {
    initSocket();  // 初始化服务器 socket + epoll
    std::cout << "Listening on port " << port_ << "...\n";

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
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                // accept() 可能一次不止一个连接到达，使用 while(true) 反复处理
                while (true) {
                    sockaddr_in client_addr{};
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(listen_fd_, (sockaddr*)&client_addr, &client_len);
                    if (client_fd < 0) break;

                    setNonBlocking(client_fd);

                    epoll_event event{};
                    event.data.fd = client_fd;
                    event.events = EPOLLIN | EPOLLET;
                    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);
                }
            } else {
                handleConnection(fd);
            }
        }
    }

    close(listen_fd_);
    close(epoll_fd_);
}