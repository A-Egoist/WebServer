#pragma once
#include <functional>
#include <chrono>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>

class TimerTask {
public:
    TimerTask(int fd, int timeout_ms, std::function<void()> cb);
    ~TimerTask();

    void start();      // 启动定时器线程
    void cancel();     // 取消定时器
    void reset(int timeout_ms);  // 重置定时器

private:
    void run();  // 定时器线程逻辑

    int fd_;
    int timeout_ms_;
    std::function<void()> callback_;

    std::thread thread_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> cancelled_;
    bool reset_flag_;
};
