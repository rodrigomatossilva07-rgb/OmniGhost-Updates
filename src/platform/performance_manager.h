#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Frame Time Budget & Adaptive Quality
// ============================================================

struct FrameBudget {
    // Target frame time in milliseconds
    float targetFrameMs = 16.667f;  // 60 FPS
    // Budget allocation (must sum to <= 1.0)
    float dmaReadBudget = 0.30f;      // 30% for DMA reads
    float gameLogicBudget = 0.25f;    // 25% for game logic (aim/ESP)
    float uiRenderBudget = 0.20f;     // 20% for UI rendering
    float overheadBudget = 0.25f;     // 25% overhead/safety margin
    
    // Adaptive quality thresholds
    float qualityHighThreshold = 0.70f;   // 70% budget used -> reduce quality
    float qualityCriticalThreshold = 0.90f; // 90% budget used -> aggressive reduction
    float qualityRecoveryThreshold = 0.50f; // 50% budget used -> restore quality
};

enum class QualityLevel : uint8_t {
    Ultra = 0,      // All features, max distance, max entities
    High = 1,       // Most features, reduced distance
    Medium = 2,     // Core features only, reduced distance/entities
    Low = 3,        // Minimal features, low distance
    Potato = 4      // Absolute minimum for playability
};

enum class AdaptiveQualityFeature : uint8_t {
    EspDistance,
    EspMaxEntities,
    EspSkeleton,
    EspTrails,
    EspRadar,
    AimMaxDistance,
    AimPrediction,
    AimSmoothing,
    RadarWeb,
    DigitalRain,
    WorldEsp,
    VehicleEsp,
    Count
};

struct QualitySettings {
    // ESP
    float espMaxDistance = 500.0f;
    uint32_t espMaxEntities = 128;
    bool espSkeleton = true;
    bool espTrails = true;
    bool espRadar = true;
    bool worldEsp = true;
    bool vehicleEsp = true;
    
    // Aim
    float aimMaxDistance = 300.0f;
    bool aimPrediction = true;
    float aimSmoothing = 1.0f;
    
    // Radar
    bool radarWeb = true;
    
    // Visual
    bool digitalRain = true;
    uint32_t digitalRainColumns = 80;
    
    // Apply quality level
    void ApplyLevel(QualityLevel level) {
        switch (level) {
            case QualityLevel::Ultra:
                espMaxDistance = 500.0f; espMaxEntities = 256;
                espSkeleton = true; espTrails = true; espRadar = true;
                worldEsp = true; vehicleEsp = true;
                aimMaxDistance = 500.0f; aimPrediction = true; aimSmoothing = 1.0f;
                radarWeb = true; digitalRain = true; digitalRainColumns = 120;
                break;
            case QualityLevel::High:
                espMaxDistance = 400.0f; espMaxEntities = 192;
                espSkeleton = true; espTrails = true; espRadar = true;
                worldEsp = true; vehicleEsp = true;
                aimMaxDistance = 400.0f; aimPrediction = true; aimSmoothing = 0.8f;
                radarWeb = true; digitalRain = true; digitalRainColumns = 80;
                break;
            case QualityLevel::Medium:
                espMaxDistance = 300.0f; espMaxEntities = 128;
                espSkeleton = true; espTrails = false; espRadar = true;
                worldEsp = true; vehicleEsp = false;
                aimMaxDistance = 300.0f; aimPrediction = true; aimSmoothing = 0.6f;
                radarWeb = false; digitalRain = true; digitalRainColumns = 60;
                break;
            case QualityLevel::Low:
                espMaxDistance = 200.0f; espMaxEntities = 64;
                espSkeleton = false; espTrails = false; espRadar = false;
                worldEsp = false; vehicleEsp = false;
                aimMaxDistance = 200.0f; aimPrediction = false; aimSmoothing = 0.4f;
                radarWeb = false; digitalRain = true; digitalRainColumns = 40;
                break;
            case QualityLevel::Potato:
                espMaxDistance = 150.0f; espMaxEntities = 32;
                espSkeleton = false; espTrails = false; espRadar = false;
                worldEsp = false; vehicleEsp = false;
                aimMaxDistance = 150.0f; aimPrediction = false; aimSmoothing = 0.2f;
                radarWeb = false; digitalRain = false; digitalRainColumns = 0;
                break;
        }
    }
};

// Per-frame timing measurements
struct FrameTiming {
    float totalFrameMs = 0.0f;
    float dmaReadMs = 0.0f;
    float gameLogicMs = 0.0f;
    float uiRenderMs = 0.0f;
    float overheadMs = 0.0f;
    
    uint64_t dmaReadsCount = 0;
    uint64_t entitiesProcessed = 0;
    uint64_t bonesProcessed = 0;
    
    QualityLevel currentQuality = QualityLevel::Ultra;
    bool budgetExceeded = false;
    float budgetUsage = 0.0f;
};

struct PerformanceStats {
    // Rolling averages (exponential moving average)
    float avgFrameMs = 16.667f;
    float avgDmaMs = 0.0f;
    float avgLogicMs = 0.0f;
    float avgUiMs = 0.0f;
    float avgFps = 60.0f;
    
    // Percentiles
    float p99FrameMs = 16.667f;
    float p99DmaMs = 0.0f;
    
    // Quality
    QualityLevel currentQuality = QualityLevel::Ultra;
    uint32_t qualityChanges = 0;
    
    // Counters
    uint64_t totalFrames = 0;
    uint64_t budgetExceededFrames = 0;
    uint64_t framesDropped = 0;
};

// ============================================================
// Thread Pool System
// ============================================================

enum class ThreadPoolType : uint8_t {
    DmaHigh,      // High priority - DMA reads, entity processing
    GameNormal,   // Normal priority - aim/ESP logic
    UiNormal,     // Normal priority - UI rendering
    LoggingLow,   // Low priority - logging, metrics
    Count
};

struct ThreadPoolConfig {
    size_t threadCount = 0;  // 0 = auto (hardware_concurrency - 1)
    int priority = 0;        // OS priority adjustment
    std::string name;
};

class ThreadPool {
public:
    ThreadPool() = default;
    ~ThreadPool() { Shutdown(); }
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = default;
    ThreadPool& operator=(ThreadPool&&) = default;
    
    bool Initialize(const ThreadPoolConfig& config);
    void Shutdown();
    
    template <typename F, typename... Args>
    auto Enqueue(F&& f, Args&&... args) -> std::future<decltype(std::declval<F>()(std::declval<Args>()...))> {
        using ReturnType = decltype(std::declval<F>()(std::declval<Args>()...));
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<ReturnType> future = task->get_future();
        {
            std::lock_guard lock(queueMutex_);
            if (stop_) throw std::runtime_error("ThreadPool stopped");
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return future;
    }
    
    void EnqueueDetached(std::function<void()> task) {
        {
            std::lock_guard lock(queueMutex_);
            if (stop_) return;
            tasks_.emplace(std::move(task));
        }
        condition_.notify_one();
    }
    
    [[nodiscard]] size_t QueueSize() const noexcept {
        std::lock_guard lock(queueMutex_);
        return tasks_.size();
    }
    
    [[nodiscard]] size_t ThreadCount() const noexcept { return threads_.size(); }
    [[nodiscard]] bool IsRunning() const noexcept { return !stop_; }
    
private:
    void WorkerLoop();
    
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    bool stop_ = false;
};

// ============================================================
// Object Pools for Hot Paths
// ============================================================

template <typename T, size_t PoolSize = 256>
class ObjectPool {
public:
    ObjectPool() {
        pool_.reserve(PoolSize);
        for (size_t i = 0; i < PoolSize; ++i) {
            pool_.emplace_back(std::make_unique<T>());
        }
    }
    
    // Non-copyable, non-movable due to mutex
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    ObjectPool(ObjectPool&&) = delete;
    ObjectPool& operator=(ObjectPool&&) = delete;
    
    template <typename... Args>
    T* Acquire(Args&&... args) {
        std::lock_guard lock(mutex_);
        if (pool_.empty()) {
            return new T(std::forward<Args>(args)...);
        }
        auto ptr = pool_.back().release();
        pool_.pop_back();
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }
    
    void Release(T* obj) {
        if (!obj) return;
        std::lock_guard lock(mutex_);
        if (pool_.size() < PoolSize) {
            obj->~T();
            pool_.emplace_back(obj);
        } else {
            delete obj;
        }
    }
    
    void Reset() {
        std::lock_guard lock(mutex_);
        pool_.clear();
        pool_.reserve(PoolSize);
        for (size_t i = 0; i < PoolSize; ++i) {
            pool_.emplace_back(std::make_unique<T>());
        }
    }
    
    [[nodiscard]] size_t Available() const noexcept {
        std::lock_guard lock(mutex_);
        return pool_.size();
    }
    
    [[nodiscard]] size_t Capacity() const noexcept { return PoolSize; }
    
private:
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<T>> pool_;
};

// ============================================================
// Math Lookup Tables
// ============================================================

class MathTables {
public:
    static MathTables& Instance() {
        static MathTables instance;
        return instance;
    }
    
    [[nodiscard]] float Sin(float radians) const noexcept {
        // Fast lookup with linear interpolation
        const float idx = (radians * kInvTwoPi + 1.0f) * 0.5f * kTableSize;
        const size_t i0 = static_cast<size_t>(idx) & kTableMask;
        const size_t i1 = (i0 + 1) & kTableMask;
        const float t = idx - static_cast<float>(i0);
        return sinTable_[i0] + t * (sinTable_[i1] - sinTable_[i0]);
    }
    
    [[nodiscard]] float Cos(float radians) const noexcept {
        return Sin(radians + 1.57079632679f); // +pi/2
    }
    
    [[nodiscard]] float SinCos(float radians, float& cosOut) const noexcept {
        cosOut = Cos(radians);
        return Sin(radians);
    }
    
    [[nodiscard]] float FastSqrt(float x) const noexcept {
        // Fast approximate sqrt using bit manipulation
        union { float f; uint32_t i; } u;
        u.f = x;
        u.i = (u.i >> 1) + 0x1FC00000;
        // One Newton-Raphson iteration
        return 0.5f * (u.f + x / u.f);
    }
    
    [[nodiscard]] float FastInvSqrt(float x) const noexcept {
        // Quake-style fast inverse sqrt
        union { float f; uint32_t i; } u;
        u.f = x;
        u.i = 0x5F3759DF - (u.i >> 1);
        float y = u.f;
        y = y * (1.5f - 0.5f * x * y * y); // Newton-Raphson
        return y;
    }
    
private:
    MathTables() {
        for (size_t i = 0; i <= kTableSize; ++i) {
            sinTable_[i] = std::sin(2.0f * 3.14159265359f * i / kTableSize);
        }
    }
    
    static constexpr size_t kTableSize = 4096;
    static constexpr size_t kTableMask = kTableSize - 1;
    static constexpr float kInvTwoPi = 0.15915494309189535f;
    std::array<float, kTableSize + 1> sinTable_;
};

// ============================================================
// Performance Manager - Main Entry Point
// ============================================================

class PerformanceManager {
public:
    PerformanceManager() = default;
    ~PerformanceManager() { Shutdown(); }
    
    PerformanceManager(const PerformanceManager&) = delete;
    PerformanceManager& operator=(const PerformanceManager&) = delete;
    
    bool Initialize();
    void Shutdown();
    
    // Frame budget
    void BeginFrame();
    void EndFrame();
    void MarkDmaReadStart();
    void MarkDmaReadEnd();
    void MarkGameLogicStart();
    void MarkGameLogicEnd();
    void MarkUiRenderStart();
    void MarkUiRenderEnd();
    
    // DMA read counter
    void IncrementDmaReads(uint64_t count = 1) noexcept;
    void IncrementEntitiesProcessed(uint64_t count = 1) noexcept;
    void IncrementBonesProcessed(uint64_t count = 1) noexcept;
    
    // Quality management
    void SetQualityLevel(QualityLevel level) noexcept;
    [[nodiscard]] QualityLevel GetQualityLevel() const noexcept;
    [[nodiscard]] const QualitySettings& GetQualitySettings() const noexcept;
    void SetAdaptiveQualityEnabled(bool enabled) noexcept;
    [[nodiscard]] bool IsAdaptiveQualityEnabled() const noexcept;
    
    // Thread pools
    ThreadPool& GetDmaPool() noexcept { return dmaPool_; }
    ThreadPool& GetGamePool() noexcept { return gamePool_; }
    ThreadPool& GetUiPool() noexcept { return uiPool_; }
    ThreadPool& GetLoggingPool() noexcept { return loggingPool_; }
    
    // Object pools
    template <typename T, size_t PoolSize>
    ObjectPool<T, PoolSize>& GetPool() {
        // Would need a registry for multiple pool types
        static ObjectPool<T, PoolSize> pool;
        return pool;
    }
    
    // Math tables
    [[nodiscard]] const MathTables& GetMathTables() const noexcept { return MathTables::Instance(); }
    
    // Stats
    [[nodiscard]] FrameTiming GetLastFrameTiming() const noexcept;
    [[nodiscard]] PerformanceStats GetStats() const noexcept;
    [[nodiscard]] bool IsBudgetExceeded() const noexcept;
    [[nodiscard]] float GetBudgetUsage() const noexcept;
    
    // Configuration
    void SetFrameBudget(const FrameBudget& budget) noexcept { frameBudget_ = budget; }
    [[nodiscard]] const FrameBudget& GetFrameBudget() const noexcept { return frameBudget_; }
    
    // Callbacks
    using QualityChangeCallback = std::function<void(QualityLevel oldLevel, QualityLevel newLevel)>;
    void SetQualityChangeCallback(QualityChangeCallback callback) { qualityChangeCallback_ = std::move(callback); }
    
    using BudgetExceededCallback = std::function<void(float usage)>;
    void SetBudgetExceededCallback(BudgetExceededCallback callback) { budgetExceededCallback_ = std::move(callback); }
    
private:
    void UpdateAdaptiveQuality();
    void UpdateStats();
    
    FrameBudget frameBudget_;
    QualitySettings qualitySettings_;
    bool adaptiveQualityEnabled_ = true;
    QualityLevel currentQuality_ = QualityLevel::Ultra;
    
    // Timing
    std::chrono::high_resolution_clock::time_point frameStart_;
    std::chrono::high_resolution_clock::time_point dmaStart_, dmaEnd_;
    std::chrono::high_resolution_clock::time_point logicStart_, logicEnd_;
    std::chrono::high_resolution_clock::time_point uiStart_, uiEnd_;
    
    // Frame data
    FrameTiming lastFrame_;
    PerformanceStats stats_;
    
    // Rolling history for percentiles
    std::array<float, 120> frameHistory_;  // ~2 seconds at 60fps
    std::array<float, 120> dmaHistory_;
    size_t historyIndex_ = 0;
    
    // Thread pools
    ThreadPool dmaPool_;
    ThreadPool gamePool_;
    ThreadPool uiPool_;
    ThreadPool loggingPool_;
    
    // Callbacks
    QualityChangeCallback qualityChangeCallback_;
    BudgetExceededCallback budgetExceededCallback_;
    
    mutable std::mutex statsMutex_;
};

// Global accessor
inline PerformanceManager& GetPerformanceManager() noexcept {
    static PerformanceManager instance;
    return instance;
}

// Convenience macros
#define PERF_DMA_SCOPE() \
    OmniGhost::Platform::GetPerformanceManager().MarkDmaReadStart(); \
    auto _perf_dma_scope = OmniGhost::Platform::MakeScopeExit([]{ \
        OmniGhost::Platform::GetPerformanceManager().MarkDmaReadEnd(); \
    })

#define PERF_LOGIC_SCOPE() \
    OmniGhost::Platform::GetPerformanceManager().MarkGameLogicStart(); \
    auto _perf_logic_scope = OmniGhost::Platform::MakeScopeExit([]{ \
        OmniGhost::Platform::GetPerformanceManager().MarkGameLogicEnd(); \
    })

#define PERF_UI_SCOPE() \
    OmniGhost::Platform::GetPerformanceManager().MarkUiRenderStart(); \
    auto _perf_ui_scope = OmniGhost::Platform::MakeScopeExit([]{ \
        OmniGhost::Platform::GetPerformanceManager().MarkUiRenderEnd(); \
    })

} // namespace OmniGhost::Platform