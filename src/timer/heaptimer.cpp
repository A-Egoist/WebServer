#include "webserver/timer/heaptimer.hpp"

void HeapTimer::addTimer(int client_fd, int timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t expire = getTimeMs() + timeout_ms;
    heap_.push({client_fd, expire});
    client_fd_to_expire_[client_fd] = expire;
}

void HeapTimer::updateTimer(int client_fd, int timeout_ms) {
    addTimer(client_fd, timeout_ms);
}

void HeapTimer::removeTimer(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_fd_to_expire_.erase(client_fd);
}

void HeapTimer::tick(std::vector<int>& expired_fds) {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t now = getTimeMs();

    while (!heap_.empty())
    {
        TimerNode node = heap_.top();
        // 如果已经被移除或存在更新的/更晚的版本，则跳过
        auto it = client_fd_to_expire_.find(node.client_fd);
        if (it == client_fd_to_expire_.end() || it->second != node.expire) {
            heap_.pop();
            continue;
        }

        // 如果堆顶元素没有过期，则其他所有节点也没有过期，退出循环
        if (node.expire > now) break;

        expired_fds.push_back(node.client_fd);
        heap_.pop();
        client_fd_to_expire_.erase(node.client_fd);
    }
}

int64_t HeapTimer::getTimeMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

int HeapTimer::getNextTick() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (heap_.empty()) return -1;  // 无限阻塞

    // 清理堆顶的所有过期或无效节点
    while (!heap_.empty()) {
        TimerNode node = heap_.top();
        auto it = client_fd_to_expire_.find(node.client_fd);
        if (it == client_fd_to_expire_.end() || it->second != node.expire) {
            heap_.pop();
            continue;
        }
        break;
    }

    if (heap_.empty()) return -1;  // 无限阻塞

    int64_t now = getTimeMs();
    int64_t expire = heap_.top().expire;

    // 返回下一个过期事件的剩余时间
    return expire > now ? (expire - now) : 0;
}