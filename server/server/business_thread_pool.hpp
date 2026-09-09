#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <iostream>

namespace ev {

class BusinessThreadPool {
public:
    static BusinessThreadPool& instance() {
        static BusinessThreadPool pool;
        return pool;
    }

    void init(size_t thread_count = 24) {
        if (is_running_) return;
        is_running_ = true;
        workers_.reserve(thread_count);
        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this]() {
                worker_loop();
            });
        }
        std::cout << "[BusinessThreadPool] 成功初始化业务工作线程池 (" << thread_count << " 线程并发执行业务与DB任务)\n";
    }

    void shutdown() {
        if (!is_running_) return;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            is_running_ = false;
        }
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) {
                t.join();
            }
        }
        workers_.clear();
    }

    template <typename F>
    void submit(F&& f) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (!is_running_) return;
            tasks_.emplace(std::forward<F>(f));
        }
        cv_.notify_one();
    }

    ~BusinessThreadPool() {
        shutdown();
    }

private:
    BusinessThreadPool() = default;

    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() {
                    return !is_running_ || !tasks_.empty();
                });
                if (!is_running_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            if (task) {
                task();
            }
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> is_running_{false};
};

} // namespace ev
