#pragma once

#include "idma_thread_pool.h"
#include "platform/expected.h"
#include <future>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace OmniGhost::Platform {






class DmaThreadPool final : public IDmaThreadPool {
public:
    using Task = std::function<void()>;

    struct Stats {
        std::atomic<size_t> tasksSubmitted{0};
        std::atomic<size_t> tasksCompleted{0};
        std::atomic<size_t> tasksFailed{0};
        std::chrono::steady_clock::time_point startTime;
    };

    explicit DmaThreadPool(size_t threadCount = std::thread::hardware_concurrency());
    ~DmaThreadPool() override;


    DmaThreadPool(const DmaThreadPool&) = delete;
    DmaThreadPool& operator=(const DmaThreadPool&) = delete;
    DmaThreadPool(DmaThreadPool&&) noexcept = default;
    DmaThreadPool& operator=(DmaThreadPool&&) noexcept = default;


    template <typename F, typename... Args>
    auto Submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        using PackagedTask = std::packaged_task<ReturnType()>;

        auto task = std::make_shared<PackagedTask>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        auto future = task->get_future();

        {
            std::unique_lock lock(queueMutex_);
            if (stopped_.load(std::memory_order_acquire)) {
                throw std::runtime_error("Thread pool is stopped");
            }
            tasks_.emplace([task]() { (*task)(); });
            tasksSubmitted_.fetch_add(1, std::memory_order_relaxed);
        }

        condition_.notify_one();
        return future;
    }


    std::future<void> SubmitVoid(std::function<void()> task) override {
        return Submit(std::move(task));
    }


    template <typename T>
    std::future<Expected<T>> SubmitDmaRead(std::function<Expected<T>()> readOp) {
        return Submit([readOp = std::move(readOp)]() mutable -> Expected<T> {
            try {
                return readOp();
            } catch (const std::exception& ex) {
                return Failure<T>(ErrorCode::DmaError, ex.what());
            }
        });
    }


    std::future<Expected<void>> SubmitDmaWrite(std::function<Expected<void>()> writeOp) override {
        return Submit([writeOp = std::move(writeOp)]() mutable -> Expected<void> {
            try {
                return writeOp();
            } catch (const std::exception& ex) {
                return Failure<void>(ErrorCode::DmaError, ex.what());
            }
        });
    }


    DmaThreadPoolStats GetStats() const noexcept override {
        DmaThreadPoolStats result = stats_;
        result.tasksSubmitted = tasksSubmitted_.load(std::memory_order_relaxed);
        result.tasksCompleted = tasksCompleted_.load(std::memory_order_relaxed);
        result.tasksFailed = tasksFailed_.load(std::memory_order_relaxed);
        return result;
    }


    size_t QueueSize() const noexcept override {
        std::unique_lock lock(queueMutex_);
        return tasks_.size();
    }


    size_t ThreadCount() const noexcept override {
        return workers_.size();
    }


    bool IsStopped() const noexcept override {
        return stopped_.load(std::memory_order_acquire);
    }


    void WaitForAll() override {
        std::unique_lock lock(queueMutex_);
        condition_.wait(lock, [this] {
            return tasks_.empty() && activeTasks_ == 0;
        });
    }


    void Shutdown() override {
        {
            std::unique_lock lock(queueMutex_);
            stopped_.store(true, std::memory_order_release);
        }
        condition_.notify_all();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::atomic<bool> stopped_{false};
    std::atomic<size_t> activeTasks_{0};
    std::atomic<size_t> tasksSubmitted_{0};
    std::atomic<size_t> tasksCompleted_{0};
    std::atomic<size_t> tasksFailed_{0};
    DmaThreadPoolStats stats_;

    void WorkerThread() {
        while (true) {
            Task task;
            {
                std::unique_lock lock(queueMutex_);
                condition_.wait(lock, [this] {
                    return stopped_.load(std::memory_order_acquire) || !tasks_.empty();
                });

                if (stopped_.load(std::memory_order_acquire) && tasks_.empty()) {
                    return;
                }

                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop();
                    activeTasks_.fetch_add(1, std::memory_order_relaxed);
                }
            }

            if (task) {
                try {
                    task();
                    tasksCompleted_.fetch_add(1, std::memory_order_relaxed);
                } catch (...) {
                    tasksFailed_.fetch_add(1, std::memory_order_relaxed);
                }
                activeTasks_.fetch_sub(1, std::memory_order_relaxed);

                condition_.notify_all();
            }
        }
    }
};

}
