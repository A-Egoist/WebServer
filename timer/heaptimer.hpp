#pragma once

#include <chrono>
#include <unordered_map>
#include <queue>
#include <functional>

struct TimerNode {
    int client_fd;
    int64_t expire;
    bool operator<(const TimerNode& other) const {
        return expire > other.expire;  // 小根堆，越早过期优先级越高
    }
};

class HeapTimer {
public:
    // 添加新的定时器
    void addTimer(int client_fd, int timeout_ms);
    // 更新已有定时器
    void updateTimer(int client_fd, int timeout_ms);
    // 删除定时器
    void removeTimer(int client_fd);
    // 检查所有过期的连接，并调用回调关闭
    void tick(const std::function<void(int)>& timeout_callback);
    // 获取最近一次定时器事件剩余时间
    int getNextTick();
private:
    int64_t getTimeMs() const;

    std::priority_queue<TimerNode> heap_;  // 小根堆
    std::unordered_map<int, int64_t> client_fd_to_expire_;  // client_fd 到过期时间的映射
};
