#include "log.hpp"

Logger::Logger() : running_(false), async_(true) {}

Logger::~Logger() {
    running_ = false;
    if (write_thread_.joinable()) write_thread_.join();
    flush();
    if (log_file_.is_open()) log_file_.close();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& filename, bool async) {
    async_ = async;
    log_file_.open(filename, std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "Cannot open log file: " << filename << "\n";
        exit(1);
    }

    running_ = true;
    if (async_) {
        write_thread_ = std::thread(&Logger::writeLog, this);
    }
}

/* level: ["INFO", "DEBUG", "WARNING", "ERROR"] */
void Logger::log(const std::string& level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&now_c), "%F %T") << "]\t" << "[" << level << "]\t" << message << "\n";

    if (async_) {
        queue_.push(ss.str());
    } else {
        log_file_ << ss.str();
    }
}

void Logger::flush() {
    log_file_.flush();
}

void Logger::writeLog() {
    while (running_) {
        std::string msg = queue_.pop();
        log_file_ << msg;
        flush();
    }
}