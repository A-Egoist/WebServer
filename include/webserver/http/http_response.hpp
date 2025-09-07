#pragma once

#include <string>
#include <unordered_map>
#include <sstream>

class HttpResponse {
public:
    // 构造函数
    HttpResponse() : status_code_(200), status_message_("OK") {}

    // 设置状态行
    void set_status(int code, const std::string& message) {
        status_code_ = code;
        status_message_ = message;
    }

    // 添加响应头
    void add_header(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    // 设置响应体
    void set_body(const std::string& body) {
        body_ = body;
    }

    // 将整个响应转换为字符串，供发送
    std::string toString() const {
        std::ostringstream response_stream;
        response_stream << "HTTP/1.1 " << status_code_ << " " << status_message_ << "\r\n";
        
        // 添加 Content-Length
        response_stream << "Content-Length: " << body_.size() << "\r\n";

        // 添加其他响应头
        for (const auto& header : headers_) {
            response_stream << header.first << ": " << header.second << "\r\n";
        }
        
        response_stream << "\r\n"; // 结束头
        response_stream << body_; // 添加响应体
        
        return response_stream.str();
    }

private:
    int status_code_;
    std::string status_message_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};