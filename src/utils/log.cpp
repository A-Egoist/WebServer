#include "webserver/utils/log.hpp"

Logger::Logger() : queue_(nullptr), async_(true) {}

Logger::~Logger() {
    stop();
}

Logger& Logger::getInstance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& filename, bool async, int max_queue_size) {
    std::cout << "Logger is initializing\n";
    bool expected = false;
    if (!is_initialized_.compare_exchange_strong(expected, true)) {
        return;  // 已经初始化，直接返回
    }

    async_ = async;
    // 打开文件，使用 std::ios::ate 模式，定位到文件末尾
    // 这样每次写入时都不需要重新打开文件
    log_file_.open(filename, std::ios::app);
    if (!log_file_.is_open()) {
        std::cerr << "Cannot open log file: " << filename << "\n";
        exit(1);
    }

    if (async_) {
        queue_ = new BlockingQueue<std::string>(max_queue_size);
        write_thread_ = std::thread(&Logger::writeLog, this);
    }

    std::cout << "Logger initialized\n";
}

/* level: ["INFO", "DEBUG", "WARNING", "ERROR"] */
void Logger::log(const std::string& level, const std::string& message) {
    if (!is_initialized_.load()) {
        return;  // 如果没有初始化直接返回
    }

    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&now_c), "%F %T") << "]\t" << "[" << level << "]\t" << message << "\n";
    std::string msg = ss.str();

    if (async_) {
        // 异步模式下，将消息推入队列
        queue_->push(msg);
    } else {
        // 同步模式下，直接加锁写入
        std::lock_guard<std::mutex> lock(log_mutex_);
        log_file_ << msg;
        log_file_.flush();
    }
}

void Logger::stop() {
    bool expected = true;
    if (!is_initialized_.compare_exchange_strong(expected, false)) {
        // 未初始化或者已经在关闭过程中
        return;
    }

    if (async_ && queue_ != nullptr) {
        // 1. 关闭队列，组织新的日志写入，并唤醒所有消费者
        queue_->close();

        // 2. 等待日志写入线程结束
        if (write_thread_.joinable()) {
            write_thread_.join();
        }

        // 3. 释放队列内存
        delete queue_;
        queue_ = nullptr;
    }

    if (log_file_.is_open()) {
        log_file_.close();
    }
}

void Logger::writeLog() {
    std::string msg;

    // 只要成功从队列中取出消息就写入文件
    while (queue_->pop(msg)) {
        log_file_ << msg;
        log_file_.flush();  // 可能存在瓶颈，后期优化
    }
}