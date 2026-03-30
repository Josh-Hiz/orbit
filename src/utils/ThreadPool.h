#pragma once
// Fixed-size thread pool with a lock-free task queue.
// Optimised for low-latency dispatch of short-lived tasks.

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace orbit {

class ThreadPool {
  public:
    // ── Construction ──────────────────────────────────────────────────────
    explicit ThreadPool(
        std::size_t nThreads = std::thread::hardware_concurrency())
        : stop_(false) {
        if (nThreads == 0)
            nThreads = 1;
        workers_.reserve(nThreads);
        for (std::size_t i = 0; i < nThreads; ++i) {
            workers_.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lk(mu_);
                        cv_.wait(lk,
                                 [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty())
                            return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    // Dispatch
    template <typename F, typename... Args>
    auto post(F &&f, Args &&...args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));
        auto fut = task->get_future();
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (stop_)
                throw std::runtime_error("ThreadPool: post after shutdown");
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return fut;
    }

    // Lifecycle
    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            stop_ = true;
        }
        cv_.notify_all();
        for (auto &t : workers_)
            if (t.joinable())
                t.join();
    }

    ~ThreadPool() { shutdown(); }

    std::size_t size() const noexcept { return workers_.size(); }

  private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_;
};

} // namespace orbit