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
    std::istringstream stream(raw_data);
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
                parseRequestLine(line, request_);
                state = ParseState::HEADERS;
                break;
            case ParseState::HEADERS:
                if (line.empty()) {
                    if (request_.headers.count("Content-Length")) {
                        hasBody = true;
                        state = ParseState::BODY;
                    } else {
                        state = ParseState::FINISH;
                    }
                } else {
                    parseHeaderLine(line, request_);
                }
                break;
            case ParseState::BODY:
                request_.body += line;
                state = ParseState::FINISH;
                break;
            case ParseState::FINISH:
                break;
        }

        if (state == ParseState::FINISH) break;
    }

    is_keep_alive = (request_.headers["Connection"] == "keep-alive");
}

void HttpConnection::buildResponse() {
    if (request_.method == "POST") {
        bool success = handlePOST();
        if (success) {
            response_.set_status_line(302, "HTTP/1.1 302 Found\r\nLocation: /welcome\r\nContent-Length: 0\r\nConnection: ", is_keep_alive);
            send(client_fd_, response_.get_status_line().c_str(), response_.get_status_line().size(), 0);  // 发送重定向响应
        } else {
            response_.set_status_line(404, "HTTP/1.1 404 Not Found\r\n", is_keep_alive);
            response_.set_body(readFile("/home/amonologue/Projects/WebServer/resources/404.html"));
            response_.set_content_type("text/html");
        }
    } else { // GET
        std::string file_path = router();
        std::ifstream file(file_path, std::ios::binary);
        if (file) {
            response_.set_status_line(200, "HTTP/1.1 200 OK\r\n", is_keep_alive);
            response_.set_body(readFile(file_path));
            response_.set_content_type(getContentType(file_path));
        } else {
            response_.set_status_line(404, "HTTP/1.1 404 Not Found\r\n", is_keep_alive);
            response_.set_body(readFile("/home/amonologue/Projects/WebServer/resources/404.html"));
            response_.set_content_type("text/html");
        }
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
    return false;
}

std::string HttpConnection::router() {
    std::string file_absolute_path;
    // 路由匹配
    if (request_.path == "/") {
        file_absolute_path = resources_root_path_ + "/index.html";
    } else if (request_.path == "/picture") {
        file_absolute_path = resources_root_path_ + "/picture.html";
    } else if (request_.path == "/video") {
        file_absolute_path = resources_root_path_ + "/video.html";
    } else if (request_.path == "/login") {
        file_absolute_path = resources_root_path_ + "/login.html";
    } else if (request_.path == "/register") {
        file_absolute_path = resources_root_path_ + "/register.html";
    } else if (request_.path == "/welcome") {
        file_absolute_path = resources_root_path_ + "/welcome.html";
    } else {
        file_absolute_path = resources_root_path_ + request_.path;
    }
    return file_absolute_path;
}

void HttpConnection::handleGET() {

}

bool HttpConnection::handlePOST() {
    bool success = false;
    std::unordered_map<std::string, std::string> account;
    parseFormURLEncoded(request_.body, account);
    auto sql_pool_ = SqlPool::getInstance();
    auto mysql = sql_pool_->getConnection();
    if (request_.path == "/register") {
        success = mysql->insertUser(account["username"], account["password"]);
        if (success) LOG_INFO("register succeed.");
    } else if (request_.path == "/login") {
        success = mysql->verifyUser(account["username"], account["password"]);
        if (success) LOG_INFO("login succeed.");
    }
    return success;
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