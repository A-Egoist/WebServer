#pragma once
#include <queue>
#include <atomic>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockingQueue {
public:
    BlockingQueue(size_t max_size = 10000): max_size_(max_size) {}

    // 优雅的关闭队列
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            is_closed_.store(true);
        }
        // 唤醒所有的生产者和消费者，让它们检查 is_closed_ 状态并退出
        cond_productor_.notify_all();
        cond_consumer_.notify_all();
    }

    void push(const T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_productor_.wait(lock, [this]() { return queue_.size() < max_size_ || is_closed_.load(); });
        // 判断是什么原因唤醒
        if (is_closed_.load()) {
            // 如果是应为队列关闭唤醒，则不再写入
            return;
        }
        queue_.push(value);
        cond_consumer_.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_consumer_.wait(lock, [this]() { return !queue_.empty() || is_closed_.load(); });
        // 判断什么原因唤醒
        if (is_closed_.load()) {
            // 如果是因为队列关闭唤醒，则不再弹出任务
            return false;
        }
        value = queue_.front();
        queue_.pop();
        cond_productor_.notify_one();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_productor_;  // 生产者条件变量
    std::condition_variable cond_consumer_;  // 消费者条件变量
    size_t max_size_;
    std::atomic<bool> is_closed_{false};  // 控制队列是否在没有任务的时候阻塞，当为 true 时，pop 操作不再等待，而是立即返回，用于优雅退出。
};