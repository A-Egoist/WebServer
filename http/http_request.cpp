#include "http_request.hpp"

void parseRequestLine(const std::string& line, HttpRequest& request) {
    std::istringstream stream(line);
    stream >> request.method >> request.path >> request.version;
}

void parseHeaderLine(const std::string& line, HttpRequest& request) {
    size_t pos = line.find(":");
    if (pos != std::string::npos) {
        // Content-Type: text/html
        // Content-Length: 46
        // Connection: close
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        while (!value.empty() && value.front() == ' ')
            value.erase(value.begin());
        request.headers[key] = value;
    }
}

HttpRequest parseHttpRequest(const std::string& raw) {
    std::istringstream stream(raw);
    std::string line;
    ParseState state = ParseState::REQUEST_LINE;
    HttpRequest request;
    std::string body;
    bool hasBody = false;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        switch (state) {
            case ParseState::REQUEST_LINE:
                parseRequestLine(line, request);
                state = ParseState::HEADERS;
                break;
            case ParseState::HEADERS:
                if (line.empty()) {
                    if (request.headers.count("Content-Length")) {
                        hasBody = true;
                        state = ParseState::BODY;
                    } else {
                        state = ParseState::FINISH;
                    }
                } else {
                    parseHeaderLine(line, request);
                }
                break;
            case ParseState::BODY:
                // 简单处理：直接将剩余所有内容视为 body（适用于小请求）
                std::getline(stream, body, '\0');
                request.body = body;
                state = ParseState::FINISH;
                break;
            case ParseState::FINISH:
                break;
        }

        if (state == ParseState::FINISH) break;
    }
    return request;
}