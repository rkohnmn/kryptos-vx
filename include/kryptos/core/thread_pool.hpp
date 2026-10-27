#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// KRYPTOS-VX — Multithreaded Work Queue & Cache-Line Aligned Atomic Counters
// Compliant with SASMP v1.3.0 / performance v3.0.0
// ═══════════════════════════════════════════════════════════════════════════════

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace kryptos::core {

// ─────────────────────────────────────────────────────────────────────────────
// Cache-Line Aligned Counter (64-byte aligned to eliminate false sharing across
// hardware threads during high-frequency directory and token updates)
// ─────────────────────────────────────────────────────────────────────────────
struct alignas(64) CacheAlignedCounter {
    std::atomic<uint64_t> count{0};
    char padding[64 - sizeof(std::atomic<uint64_t>)]{};

    void add(uint64_t val = 1) noexcept {
        count.fetch_add(val, std::memory_order_relaxed);
    }

    [[nodiscard]] uint64_t load() const noexcept {
        return count.load(std::memory_order_relaxed);
    }

    void reset() noexcept {
        count.store(0, std::memory_order_relaxed);
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Production ThreadPool with std::future Task Enqueueing & RAII Shutdown
// ─────────────────────────────────────────────────────────────────────────────
class ThreadPool {
    std::vector<std::thread> workers_{};
    std::queue<std::function<void()>> tasks_{};
    mutable std::mutex mutex_{};
    std::condition_variable cv_{};
    std::atomic<bool> stop_{false};

public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency()) {
        if (threads == 0) threads = 4;
        workers_.reserve(threads);
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(mutex_);
                        cv_.wait(lock, [this] {
                            return stop_.load(std::memory_order_acquire) || !tasks_.empty();
                        });
                        if (stop_.load(std::memory_order_acquire) && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {
        using return_type = std::invoke_result_t<F, Args...>;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stop_.load(std::memory_order_acquire)) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        cv_.notify_one();
        return res;
    }

    [[nodiscard]] size_t worker_count() const noexcept {
        return workers_.size();
    }

    ~ThreadPool() noexcept {
        stop_.store(true, std::memory_order_release);
        cv_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};

} // namespace kryptos::core
