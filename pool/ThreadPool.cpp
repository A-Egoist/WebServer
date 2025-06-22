#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t threadCount) : stop_(false) {
    for (size_t i = 0; i < threadCount; ++ i) {
        workers_.emplace_back([this]() {
            this->worker();
        });
    }
}

ThreadPool::~ThreadPool() {
    stop_.store(true);
    cv_.notify_all();
    for (std::thread& worker: workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(const std::function<void()>& task) {
    {
        std::unique_lock<std::mutex> lock(mutex_);
        tasks.emplace(task);
    }
    cv_.notify_one();
}

void ThreadPool::worker() {
    while (!stop_.load()) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() {
                return stop_.load() || !tasks.empty();
            });

            if (stop_.load() && tasks.empty()) return;

            task = std::move(tasks.front());
            tasks.pop();
        }

        task();  // 执行任务
    }
}