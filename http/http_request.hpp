#pragma once

#include <string>
#include <unordered_map>
#include <sstream>

struct HttpRequest
{
    std::string method;
    std::string path;
    std::string version;
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