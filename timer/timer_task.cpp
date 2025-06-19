#include "timer_task.hpp"

TimerTask::TimerTask(int fd, int timeout_ms, std::function<void()> cb)
    : fd_(fd), timeout_ms_(timeout_ms), callback_(std::move(cb)),
      cancelled_(false), reset_flag_(false) {}

TimerTask::~TimerTask() {
    cancel();
    if (thread_.joinable()) thread_.join();
}

void TimerTask::start() {
    thread_ = std::thread(&TimerTask::run, this);
}

void TimerTask::cancel() {
    {
        std::lock_guard<std::mutex> lock(mtx_);
        cancelled_ = true;
        cv_.notify_all();
    }
}

void TimerTask::reset(int timeout_ms) {
    std::lock_guard<std::mutex> lock(mtx_);
    timeout_ms_ = timeout_ms;
    reset_flag_ = true;
    cv_.notify_all();
}

void TimerTask::run() {
    std::unique_lock<std::mutex> lock(mtx_);
    while (!cancelled_) {
        if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms_)) == std::cv_status::timeout) {
            if (!cancelled_) {
                callback_();
            }
            break;
        } else if (reset_flag_) {
            reset_flag_ = false;
            continue;  // 重新等待新的超时
        }
    }
}
