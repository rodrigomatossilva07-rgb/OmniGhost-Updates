#pragma once

#include "unified_esp.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <chrono>
#include <vector>
#include <memory>

namespace OmniGhost::Gameplay::UnifiedESP {

// Job payload passed to optional thread
struct OptionalJob {
    std::vector<UnifiedPlayer> players;
    FeatureFlags features;
    LODConfig lod;
    AcquisitionBudget budget;
    uint64_t session_generation = 0;
    uint64_t world_sample_ms = 0;
    CameraSnapshot camera;
    float local_pos[3] = {};
};

// Result from optional thread
struct OptionalResult {
    std::vector<UnifiedPlayer> enriched_players;
    uint64_t session_generation = 0;
    float bones_ms = 0.f;
    float visibility_ms = 0.f;
    float weapon_ms = 0.f;
    float names_ms = 0.f;
    float dropped_ms = 0.f;
    float budget_left_ms = 0.f;
    uint32_t bones_count = 0;
    uint32_t visibility_count = 0;
    uint32_t weapons_count = 0;
    uint32_t names_count = 0;
    std::vector<UnifiedPlayer> dropped_items;
    bool dropped_items_valid = false;
};

// Optional thread interface - game implements the work function
class OptionalThread {
public:
    OptionalThread() = default;
    ~OptionalThread() { Stop(); }

    // Start the optional worker thread
    void Start() {
        stop_.store(false, std::memory_order_release);
        thread_ = std::thread(&OptionalThread::WorkerLoop, this);
    }

    // Stop the worker thread
    void Stop() {
        if (thread_.joinable()) {
            stop_.store(true, std::memory_order_release);
            cv_.notify_all();
            thread_.join();
        }
    }

    // Queue a job for the optional thread
    // Returns false if worker is busy/pending (job dropped)
    bool QueueJob(OptionalJob&& job) {
        std::lock_guard lock(mutex_);
        if (busy_.load(std::memory_order_acquire) || pending_.load(std::memory_order_acquire))
            return false;
        
        job_ = std::move(job);
        pending_.store(true, std::memory_order_release);
        cv_.notify_one();
        return true;
    }

    // Try to get completed result (non-blocking)
    // Returns false if no result available
    bool TryGetResult(OptionalResult& out) {
        std::lock_guard lock(mutex_);
        if (!result_ready_.load(std::memory_order_acquire))
            return false;
        out = std::move(result_);
        result_ready_.store(false, std::memory_order_release);
        return true;
    }

    // Check if worker is currently busy
    bool IsBusy() const { return busy_.load(std::memory_order_acquire); }

    // Check if a job is pending
    bool HasPendingJob() const { return pending_.load(std::memory_order_acquire); }

protected:
    // Game-specific work function - MUST be implemented by derived class
    virtual void DoWork(OptionalJob&& job, OptionalResult& result) = 0;

private:
    // Member variables
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> pending_{false};
    std::atomic<bool> result_ready_{false};
    OptionalJob job_;
    OptionalResult result_;

    void WorkerLoop() {
        while (!stop_.load(std::memory_order_acquire)) {
            OptionalJob job;
            {
                std::unique_lock lock(mutex_);
                cv_.wait_for(lock, std::chrono::milliseconds(50), [this] {
                    return pending_.load(std::memory_order_acquire) || stop_.load(std::memory_order_acquire);
                });
                if (stop_.load(std::memory_order_acquire))
                    break;
                if (!pending_.load(std::memory_order_acquire))
                    continue;
                job = std::move(job_);
                pending_.store(false, std::memory_order_release);
            }
            
            busy_.store(true, std::memory_order_release);
            OptionalResult result{};
            result.session_generation = job.session_generation;
            
            try {
                DoWork(std::move(job), result);
            } catch (...) {
                // Never crash the worker thread
            }
            
            {
                std::lock_guard lock(mutex_);
                result_ = std::move(result);
                result_ready_.store(true, std::memory_order_release);
            }
            busy_.store(false, std::memory_order_release);
        }
    }

    // Member variables
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> busy_{false};
    std::atomic<bool> pending_{false};
    std::atomic<bool> result_ready_{false};
    OptionalJob job_;
    OptionalResult result_;
};

// Helper for budget tracking within optional work
class PhaseBudget {
public:
    explicit PhaseBudget(float budget_ms) : budget_ms_(budget_ms), start_(std::chrono::steady_clock::now()) {}
    float Remaining() const {
        auto elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start_).count();
        return std::max(0.f, budget_ms_ - elapsed);
    }
    bool HasTime() const { return Remaining() > 0.1f; }
    void Reset(float budget_ms) { budget_ms_ = budget_ms; start_ = std::chrono::steady_clock::now(); }
private:
    float budget_ms_;
    std::chrono::steady_clock::time_point start_;
};

// Camera priority helper
inline bool ShouldYieldToCamera(std::atomic<uint64_t>& camera_deadline, std::atomic<bool>& camera_pending) {
    if (!camera_pending.load(std::memory_order_acquire)) return false;
    uint64_t deadline = camera_deadline.load(std::memory_order_acquire);
    uint64_t now = GetTickCount64();
    return deadline != 0 && now + 2u >= deadline;
}

// DMA scatter handle wrapper (RAII)
class ScatterHandle {
public:
    ScatterHandle() = default;
    ~ScatterHandle() { Reset(); }
    ScatterHandle(const ScatterHandle&) = delete;
    ScatterHandle& operator=(const ScatterHandle&) = delete;
    
    template<typename CreateFn>
    void Ensure(void* mem_handle, CreateFn create_fn) {
        if (!handle_ && mem_handle) handle_ = create_fn();
    }
    template<typename CloseFn>
    void Reset(CloseFn close_fn) {
        if (handle_) { close_fn(handle_); handle_ = nullptr; }
    }
    explicit operator bool() const { return handle_ != nullptr; }
    auto get() const { return handle_; }
    
    template<typename AddReq>
    void AddRequests(AddReq&& add) {
        if (handle_) add(handle_);
    }
    template<typename ExecFn>
    bool Execute(ExecFn exec_fn) {
        if (!handle_) return false;
        return exec_fn(handle_);
    }
    
private:
    void* handle_ = nullptr;
};

} // namespace OmniGhost::Gameplay::UnifiedESP