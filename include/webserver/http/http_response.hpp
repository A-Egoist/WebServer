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