#pragma once

#include <string>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include "http_request.hpp"
#include "../sql/MySQLConnector.hpp"

class HTTPConnection {
public:
    int use_count = 0;
    bool is_keep_alive;

    explicit HTTPConnection(int client_fd, MySQLConnector* mysql);

    bool receiveRequest(std::string& raw_data);
    void parseRequest(const std::string& raw_data);
    void sendResponse();

private:
    const int READ_BUFFER_ = 4096;
    int client_fd_;
    std::string resources_root_path_;
    std::string buffer_;
    HttpRequest request_;
    std::string response_;
    bool is_connection_;
    MySQLConnector* mysql_;

    std::string router();
    void handleGET();
    bool handlePOST();
    std::string decodeURLComponent(const std::string& s);
    std::string getContentType(const std::string& path);
    std::string readFile(const std::string& file_path);
    void parseFormURLEncoded(const std::string& body, std::unordered_map<std::string, std::string>& data);
};