#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <sstream>

struct HttpRequest
{
    std::string method;  // [GET, POST, ...]
    std::string path;  // request path, ["/", "/index", "/picture", ...]
    std::string version;  // HTTP version, ["HTTP/1.1", "HTTP/1.0", ...]
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

enum class ParseState {
    REQUEST_LINE,
    HEADERS,
    BODY,
    FINISH
};

void parseRequestLine(const std::string&, HttpRequest&);

void parseHeaderLine(const std::string&, HttpRequest&);

HttpRequest parseHttpRequest(const std::string&);