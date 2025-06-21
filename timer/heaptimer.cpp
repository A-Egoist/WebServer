#include "heaptimer.hpp"

void HeapTimer::addTimer(int client_fd, int timeout_ms) {
    int64_t expire = getTimeMs() + timeout_ms;
    heap_.push({client_fd, expire});
    client_fd_to_expire_[client_fd] = expire;
}

void HeapTimer::updateTimer(int client_fd, int timeout_ms) {
    addTimer(client_fd, timeout_ms);
}

void HeapTimer::removeTimer(int client_fd) {
    client_fd_to_expire_.erase(client_fd);
}

void HeapTimer::tick(const std::function<void(int)>& timeout_callback) {
    int64_t now = getTimeMs();

    while (!heap_.empty())
    {
        TimerNode node = heap_.top();
        // 如果已经被移除或存在更新的/更晚的版本，则跳过
        if (client_fd_to_expire_.count(node.client_fd) == 0 || client_fd_to_expire_[node.client_fd] != node.expire) {
            heap_.pop();
            continue;
        }
        if (node.expire > now) break;

        // 到期：执行回调, 关闭连接
        timeout_callback(node.client_fd);
        heap_.pop();
        client_fd_to_expire_.erase(node.client_fd);
    }
}

int64_t HeapTimer::getTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int HeapTimer::getNextTick() {
    tick([](int){});
    if (heap_.empty()) return -1;  // 无线阻塞
    int64_t now = getTimeMs();
    int64_t expire = heap_.top().expire;
    return expire > now ? (expire - now) : 0;  // 最多等待时间
}