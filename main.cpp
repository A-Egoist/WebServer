#include <iostream>
#include "server.hpp"

int main() {
    WebServer server(8080);
    server.run();
    return 0;
}