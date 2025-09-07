#pragma once

#include <string>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iostream>
#include "webserver/utils/block_queue.hpp"

class Logger {
private:
    Logger();
    ~Logger();
    void writeLog();

    std::ofstream log_file_;
    BlockingQueue<std::string>* queue_;
    std::thread write_thread_;
    std::atomic<bool> is_initialized_{false};  // 标志是否初始化，用于线程安全
    bool async_;
    std::mutex log_mutex_;

public:
    static Logger& getInstance();
    void init(const std::string& filename, bool async = true, int max_queue_size = 10000);
    void log(const std::string& level, const std::string& message);
    void stop();
};