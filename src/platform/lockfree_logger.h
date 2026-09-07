#pragma once
#pragma warning(push)
#pragma warning(disable: 4324)

#include <atomic>
#include <fstream>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Lock-Free Ring Buffer for Log Events
// ============================================================

template <typename T, size_t Capacity>
class LockFreeRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t Mask = Capacity - 1;
    
public:
    LockFreeRingBuffer() = default;
    
    // Try to push (returns false if full)
    bool TryPush(const T& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t nextHead = (head + 1) & Mask;
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            return false; // Full
        }
        buffer_[head] = item;
        head_.store(nextHead, std::memory_order_release);
        return true;
    }
    
    // Push with overwrite (always succeeds, overwrites oldest if full)
    void Push(const T& item) noexcept {
        size_t head = head_.load(std::memory_order_relaxed);
        size_t nextHead = (head + 1) & Mask;
        if (nextHead == tail_.load(std::memory_order_acquire)) {
            // Full - advance tail (overwrite oldest)
            tail_.store((tail_.load(std::memory_order_relaxed) + 1) & Mask, std::memory_order_release);
        }
        buffer_[head] = item;
        head_.store((head + 1) & Mask, std::memory_order_release);
    }
    
    // Try to pop (returns false if empty)
    bool TryPop(T& out) noexcept {
        size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false; // Empty
        }
        out = buffer_[tail];
        tail_.store((tail + 1) & Mask, std::memory_order_release);
        return true;
    }
    
    // Pop all available items into vector
    size_t PopAll(std::vector<T>& out, size_t maxItems = SIZE_MAX) noexcept {
        size_t count = 0;
        T item;
        while (count < maxItems && TryPop(item)) {
            out.push_back(std::move(item));
            count++;
        }
        return count;
    }
    
    [[nodiscard]] bool Empty() const noexcept {
        return head_.load(std::memory_order_acquire) == tail_.load(std::memory_order_acquire);
    }
    
    [[nodiscard]] size_t Size() const noexcept {
        size_t head = head_.load(std::memory_order_acquire);
        size_t tail = tail_.load(std::memory_order_acquire);
        return (head - tail) & Mask;
    }
    
    [[nodiscard]] size_t GetCapacity() const noexcept { return Capacity; }
    
    void Clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
    T buffer_[Capacity];
};

// ============================================================
// Log Event Structure
// ============================================================

enum class LogSeverity : uint8_t {
    Trace = 0,
    Debug = 1,
    Info = 2,
    Warning = 3,
    Error = 4,
    Critical = 5
};

enum class LogSubsystem : uint8_t {
    Core = 0,
    Auth,
    DMA,
    VMM,
    LeechCore,
    Ftdi,
    Pnp,
    ProcInfo,
    Process,
    Runtime,
    UI,
    Updater,
    Radar,
    Input,
    Config,
    Adapter,
    Read,
    Watchdog,
    Game,
    Platform,
    InputDev,
    Update,
    App
};

struct LogEvent {
    uint64_t timestamp = 0;           // nanoseconds since epoch
    uint32_t threadId = 0;
    uint8_t severity = 0;
    uint8_t subsystem = 0;
    char message[256];
    uint16_t msgLen = 0;
    
    // Optional fields (packed)
    uint32_t errorSequence = 0;
    bool hasErrorId = false;
    char errorId[16];  // "OG-XXX-XXX"
    
    // Key-value fields (compact)
    struct Field {
        char key[32];
        char value[128];
        bool sensitive = false;
    };
    Field fields[4];
    uint8_t fieldCount = 0;
};

// ============================================================
// Lock-Free Logger
// ============================================================

class LockFreeLogger {
public:
    static LockFreeLogger& Instance() noexcept {
        static LockFreeLogger instance;
        return instance;
    }
    
    LockFreeLogger() = default;
    ~LockFreeLogger() { Shutdown(); }
    
    bool Initialize(const std::filesystem::path& logPath, size_t maxBytes = 5 * 1024 * 1024);
    void Shutdown();
    
    // Lock-free logging (never blocks)
    void Log(LogSeverity severity, uint8_t subsystem, std::string_view message) noexcept;
    void Log(LogSeverity severity, uint8_t subsystem, std::string_view message, 
             std::initializer_list<std::pair<std::string_view, std::string_view>> fields) noexcept;
    
    // Flush pending logs to file (called periodically)
    void Flush();
    
    // Get recent events for UI
    void GetRecentEvents(std::vector<std::string>& out, size_t maxCount = 64) const noexcept;
    
    // Statistics
    struct Stats {
        uint64_t totalLogged = 0;
        uint64_t dropped = 0;
        uint64_t fileBytes = 0;
        size_t queueSize = 0;
    };
    [[nodiscard]] Stats GetStats() const noexcept;
    
    // Set output file
    void SetLogFile(const std::filesystem::path& path, size_t maxBytes = 5 * 1024 * 1024);
    void SetConsoleOutput(bool enabled) noexcept { consoleOutput_.store(enabled); }
    
private:
    struct LogEntry {
        LogEvent event;
        char messageData[256];  // Variable length message
    };
    
    void WriterThread();
    void WriteToFile();
    void RotateIfNeeded();
    
    // Ring buffer for log entries
    LockFreeRingBuffer<LogEvent, 4096> eventQueue_;
    
    // File output
    std::filesystem::path logPath_;
    size_t maxFileBytes_ = 5 * 1024 * 1024;
    std::ofstream logFile_;
    size_t fileBytes_ = 0;
    std::mutex fileMutex_;  // Only for file operations
    
    // Writer thread
    std::thread writerThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> flushRequested_{false};
    
    // Console output
    std::atomic<bool> consoleOutput_{true};
    
    // Stats
    std::atomic<uint64_t> totalLogged_{0};
    std::atomic<uint64_t> dropped_{0};
    
    // Session info
    std::string sessionId_;
    std::chrono::steady_clock::time_point sessionStart_;
    
    void WriteHeader();
    void WriteEvent(const LogEvent& event);
    std::string FormatEvent(const LogEvent& event) const;
};

// Convenience macros
#define LF_LOG(sev, subsys, msg) \
    OmniGhost::Platform::LockFreeLogger::Instance().Log( \
        OmniGhost::Platform::LogSeverity::sev, \
        static_cast<uint8_t>(subsys), \
        msg)

#define LF_LOG_FMT(sev, subsys, fmt, ...) \
    do { \
        char _lf_buf[256]; \
        int _lf_len = snprintf(_lf_buf, sizeof(_lf_buf), fmt, __VA_ARGS__); \
        if (_lf_len > 0) \
            OmniGhost::Platform::LockFreeLogger::Instance().Log( \
                OmniGhost::Platform::LogSeverity::sev, \
                static_cast<uint8_t>(subsys), \
                std::string_view(_lf_buf, static_cast<size_t>(_lf_len))); \
    } while(0)

} // namespace OmniGhost::Platform
#pragma warning(pop)
