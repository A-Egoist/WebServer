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

#define LOG_INFO(msg) Logger::getInstance().log("INFO", msg)
#define LOG_DEBUG(msg) Logger::getInstance().log("DEBUG", msg)
#define LOG_WARNING(msg) Logger::getInstance().log("WARNING", msg)
#define LOG_ERROR(msg) Logger::getInstance().log("ERROR", msg)

class Logger {
public:
    static Logger& getInstance();
    void init(const std::string& filename, bool async = true, int max_queue_size = 10000);
    void log(const std::string& level, const std::string& message);
    void stop();

private:
    Logger();
    ~Logger();
    void writeLog();

    std::ofstream log_file_;  // 日志文件
    BlockingQueue<std::string>* queue_;  // 异步日志队列
    std::thread write_thread_;  // 工作线程
    std::atomic<bool> is_initialized_{false};  // 标志是否初始化，用于线程安全
    bool async_;  // 同步异步标志，true 表示异步，false 表示同步
    std::mutex log_mutex_;  // 日志锁
};