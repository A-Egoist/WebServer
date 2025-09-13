#pragma once

#include <string>
#include <cstring>
#include <fstream>
#include <netinet/in.h>
#include "webserver/http/http_request.hpp"
#include "webserver/http/http_response.hpp"
// #include "webserver/sql/MySQLConnector.hpp"
#include "webserver/pool/sql_pool.hpp"

class HttpConnection {
public:
    int use_count = 0;
    bool is_keep_alive;

    explicit HttpConnection(int client_fd);

    bool receiveRequest(std::string& raw_data);
    void parseRequest(const std::string& raw_data);
    void buildResponse();
    bool sendResponse();
    bool sendFile();
    // void sendResponse();

private:
    const int READ_BUFFER_ = 4096;
    int client_fd_;
    std::string resources_root_path_;
    std::string buffer_;
    size_t write_buffer_index_ = 0; // 新增：已发送字节数
    HttpRequest request_;
    HttpResponse response_;
    bool is_connection_;
    // std::shared_ptr<MySQLConnector> mysql_;
    // SqlPool* sql_pool_;
    // MySQLConnector* mysql_;

    std::string router();
    void handleGET();
    bool handlePOST();
    std::string decodeURLComponent(const std::string& s);
    std::string getContentType(const std::string& path);
    std::string readFile(const std::string& file_path);
    void parseFormURLEncoded(const std::string& body, std::unordered_map<std::string, std::string>& data);
};