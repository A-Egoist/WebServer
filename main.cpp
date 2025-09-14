#include <iostream>
#include "webserver/utils/log.hpp"
#include "webserver/core/http_server.hpp"

int main() {
    Logger::getInstance().init("logs/running.log", true);
    std::cout << "Server started" << std::endl;
    LOG_INFO("Server started");

    HttpServer server(8080);
    server.run();

    return 0;
}