#pragma once
#include <string>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iostream>
#include "block_queue.hpp"

class Logger {
public:
    static Logger& getInstance();
    void init(const std::string& filename = "webserver.log", bool async = true);
    void log(const std::string& level, const std::string& message);
    void flush();

private:
    Logger();
    ~Logger();
    void writeLog();

    std::ofstream log_file_;
    BlockQueue<std::string> queue_;
    std::thread write_thread_;
    std::atomic<bool> running_;
    bool async_;
};