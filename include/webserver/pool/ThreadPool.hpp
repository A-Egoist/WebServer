#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool {
public:
    ThreadPool(size_t threadCount = 4);
    ~ThreadPool();

    // 添加任务到线程池
    void enqueue(const std::function<void()>& task);

private:
    void worker();
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
};