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