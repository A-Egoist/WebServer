#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

template<typename T>
class BlockQueue {
public:
    BlockQueue(size_t max_size = 10000): max_size_(max_size) {}

    void push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_productor_.wait(lock, [this]() { return queue_.size() < max_size_; });
        queue_.push(item);
        cond_consumer_.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_consumer_.wait(lock, [this]() { return !queue_.empty(); });
        T item = queue_.front();
        queue_.pop();
        cond_productor_.notify_one();
        return item;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_productor_;
    std::condition_variable cond_consumer_;
    size_t max_size_;
};