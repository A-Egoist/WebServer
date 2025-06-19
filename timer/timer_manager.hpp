#pragma once
#include <queue>
#include <unordered_map>
#include <mutex>
#include <chrono>
#include <functional>
// #include "timer.hpp"


struct Timer {
    int fd;
    int version;
    std::chrono::steady_clock::time_point expire_time;
    std::function<void()> callback;

    bool valid(const std::unordered_map<int, int>& versions) const {
        return versions.at(fd) == version;
    }

    bool operator>(const Timer& other) const {
        return expire_time > other.expire_time;
    }
};

class TimerManager {
public:
    void addOrUpdateTimer(int fd, int timeout_ms, std::function<void()> cb);
    void handleExpired();

private:
    // struct TimerCmp {
    //     bool operator()(const Timer* a, const Timer* b) {
    //         return a->getExpireTime() > b->getExpireTime();
    //     }
    // };

    // std::priority_queue<Timer*, std::vector<Timer*>, TimerCmp> timer_heap_;
    // std::unordered_map<int, Timer*> fd_to_timer_;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<>> timer_heap_;
    std::unordered_map<int, int> fd_versions_;
    std::mutex mtx_;
};