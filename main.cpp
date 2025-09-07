#include <iostream>
#include "webserver/core/server.hpp"
#include "webserver/utils/log.hpp"

int main() {
    Logger::getInstance().init("running.log", true);
    std::cout << "Server started" << std::endl;
    Logger::getInstance().log("INFO", "Server started");

    WebServer server(8080);
    server.run();

    return 0;
}