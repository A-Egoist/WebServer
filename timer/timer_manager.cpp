#include "timer_manager.hpp"

void TimerManager::addOrUpdateTimer(int fd, int timeout_ms, std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(mtx_);
    int version = ++ fd_versions_[fd];
    Timer timer{fd, version, std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms), cb};
    // Timer* timer = new Timer(fd, timeout_ms, cb);
    // fd_to_timer_[fd] = timer;
    timer_heap_.push(timer);
}

void TimerManager::handleExpired() {
    std::lock_guard<std::mutex> lock(mtx_);
    // auto now = Timer::Clock::now();
    auto now = std::chrono::steady_clock::now();
    while (!timer_heap_.empty()) {
        const Timer& timer = timer_heap_.top();
        if (timer.expire_time > now) break;
        if (timer.valid(fd_versions_)) {
            timer.callback();
            fd_versions_.erase(timer.fd);
        }
        timer_heap_.pop();

        // timer->runCallback();
        // timer_heap_.pop();
        // fd_to_timer_.erase(timer->fd);
        // delete timer;
    }
}