#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>
#include <vector>

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

    // void parseRequest(const std::string& raw) {
    //     std::istringstream stream(raw);
    //     std::string line;
    //     ParseState state = ParseState::REQUEST_LINE;
    //     std::string body;
    //     bool hasBody = false;

    //     while (std::getline(stream, line)) {
    //         if (!line.empty() && line.back() == '\r') {
    //             line.pop_back();
    //         }

    //         switch (state) {
    //             case ParseState::REQUEST_LINE:
    //                 parseRequestLine(line);
    //                 state = ParseState::HEADERS;
    //                 break;
    //             case ParseState::HEADERS:
    //                 if (line.empty()) {
    //                     if (headers_.count("Content-Length")) {
    //                         hasBody = true;
    //                         state = ParseState::BODY;
    //                     } else {
    //                         state = ParseState::FINISH;
    //                     }
    //                 } else {
    //                     parseHeaderLine(line);
    //                 }
    //                 break;
    //             case ParseState::BODY:
    //                 body_ += line;
    //                 state = ParseState::FINISH;
    //                 break;
    //             case ParseState::FINISH:
    //                 break;
    //         }

    //         if (state == ParseState::FINISH) break;
    //     }
    // }

    void parseRequest(const std::string& raw) {
        std::string current_line;
        size_t pos = 0;
        
        // 1. Parse Request Line
        pos = raw.find("\r\n");
        if (pos == std::string::npos) return;
        parseRequestLine(raw.substr(0, pos));
        pos += 2; // Move past the \r\n

        // 2. Parse Headers
        size_t header_end_pos = raw.find("\r\n\r\n", pos);
        if (header_end_pos == std::string::npos) return;

        std::istringstream header_stream(raw.substr(pos, header_end_pos - pos));
        while (std::getline(header_stream, current_line, '\n')) {
            if (!current_line.empty() && current_line.back() == '\r') {
                current_line.pop_back();
            }
            if (!current_line.empty()) {
                parseHeaderLine(current_line);
            }
        }
        
        // 3. Parse Body
        size_t body_start_pos = header_end_pos + 4;
        body_ = raw.substr(body_start_pos);

        // Now, your body_ string contains the raw, uncorrupted binary data.
        // Your extractFileData() function can then correctly parse the multipart data from here.
    }

    std::vector<unsigned char> extractFileData(const std::string& boundary) const {
        std::string boundary_start = "--" + boundary;
        std::string boundary_end = "--" + boundary + "--";
        
        // 找到第一个文件部分的开始
        size_t part_start = body_.find(boundary_start);
        if (part_start == std::string::npos) {
            return {};
        }
        part_start += boundary_start.length();
        
        // 找到下一个部分的开始，或者结束边界
        size_t part_end = body_.find(boundary_end, part_start);
        if (part_end == std::string::npos) {
            part_end = body_.find(boundary_start, part_start);
        }
        if (part_end == std::string::npos) {
            return {};
        }

        // 提取文件部分的头部和数据
        std::string part_content = body_.substr(part_start, part_end - part_start);

        // 找到双换行符 (\r\n\r\n) 来分隔头部和数据
        size_t header_end = part_content.find("\r\n\r\n");
        if (header_end == std::string::npos) {
            return {};
        }

        // 提取纯粹的二进制数据
        size_t data_start = header_end + 4;
        std::string file_data_str = part_content.substr(data_start);
        
        // 返回一个包含二进制数据的vector
        std::vector<unsigned char> file_data(file_data_str.begin(), file_data_str.end());
        return file_data;
    }

    std::string getHeader(const std::string& key) {
        if (headers_.count(key)) {
            return headers_[key];
        }
        else return "";
    }

public:
    std::string method_;  // [GET, POST, ...]
    std::string path_;  // request path, ["/", "/index", "/picture", ...]
    std::string version_;  // HTTP version, ["HTTP/1.1", "HTTP/1.0", ...]
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};