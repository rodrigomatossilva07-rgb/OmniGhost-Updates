#include "lockfree_logger.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

namespace OmniGhost::Platform {

bool LockFreeLogger::Initialize(const std::filesystem::path& logPath, size_t maxBytes) {
    if (running_.load()) return true;
    
    logPath_ = logPath;
    maxFileBytes_ = maxBytes;
    
    // Create directory
    std::filesystem::create_directories(logPath_.parent_path());
    
    // Open file
    logFile_.open(logPath_, std::ios::out | std::ios::app | std::ios::binary);
    if (!logFile_.is_open()) return false;
    
    // Get current file size
    logFile_.seekp(0, std::ios::end);
    fileBytes_ = static_cast<size_t>(logFile_.tellp());
    
    // Generate session ID
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_s(&local, &time);
    std::ostringstream ss;
    ss << std::put_time(&local, "%Y%m%d-%H%M%S") << "-pid" << GetCurrentProcessId();
    sessionId_ = ss.str();
    sessionStart_ = std::chrono::steady_clock::now();
    
    // Start writer thread
    running_.store(true);
    writerThread_ = std::thread(&LockFreeLogger::WriterThread, this);
    
    // Write header
    LogEvent headerEvent{};
    headerEvent.severity = static_cast<uint8_t>(LogSeverity::Info);
    headerEvent.subsystem = static_cast<uint8_t>(LogSubsystem::Core);
    headerEvent.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    headerEvent.threadId = GetCurrentThreadId();
    const char* msg = "Session initialized";
    memcpy(headerEvent.message, msg, strlen(msg) + 1);
    headerEvent.msgLen = static_cast<uint16_t>(strlen(msg));
    headerEvent.subsystem = static_cast<uint8_t>(LogSubsystem::Core);
    headerEvent.severity = static_cast<uint8_t>(LogSeverity::Info);
    
    // Push header event
    eventQueue_.Push(headerEvent);
    
    return true;
}

void LockFreeLogger::Shutdown() {
    if (!running_.load()) return;
    
    // Log shutdown
    LogEvent shutdownEvent{};
    shutdownEvent.severity = static_cast<uint8_t>(LogSeverity::Info);
    shutdownEvent.subsystem = static_cast<uint8_t>(LogSubsystem::Core);
    shutdownEvent.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    shutdownEvent.threadId = GetCurrentThreadId();
    const char* msg = "Session shutdown";
    memcpy(shutdownEvent.message, msg, strlen(msg) + 1);
    shutdownEvent.msgLen = static_cast<uint16_t>(strlen(msg));
    shutdownEvent.severity = static_cast<uint8_t>(LogSeverity::Info);
    shutdownEvent.subsystem = static_cast<uint8_t>(LogSubsystem::Core);
    eventQueue_.Push(shutdownEvent);
    
    // Signal flush and wait
    flushRequested_.store(true);
    if (writerThread_.joinable()) {
        writerThread_.join();
    }
    
    running_.store(false);
    
    // Final flush
    Flush();
}

void LockFreeLogger::Log(LogSeverity severity, uint8_t subsystem, std::string_view message) noexcept {
    LogEvent event{};
    event.severity = static_cast<uint8_t>(severity);
    event.subsystem = subsystem;
    event.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    event.threadId = GetCurrentThreadId();
    
    size_t msgLen = std::min(message.size(), sizeof(event.message) - 1);
    memcpy(event.message, message.data(), msgLen);
    event.message[msgLen] = '\0';
    event.msgLen = static_cast<uint16_t>(msgLen);
    event.severity = static_cast<uint8_t>(severity);
    event.subsystem = subsystem;
    event.fieldCount = 0;
    
    if (!eventQueue_.TryPush(event)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    } else {
        totalLogged_.fetch_add(1, std::memory_order_relaxed);
    }
}

void LockFreeLogger::Log(LogSeverity severity, uint8_t subsystem, std::string_view message,
                         std::initializer_list<std::pair<std::string_view, std::string_view>> fields) noexcept {
    LogEvent event{};
    event.severity = static_cast<uint8_t>(severity);
    event.subsystem = subsystem;
    event.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    event.threadId = GetCurrentThreadId();
    
    size_t msgLen = std::min(message.size(), sizeof(event.message) - 1);
    memcpy(event.message, message.data(), msgLen);
    event.message[msgLen] = '\0';
    event.msgLen = static_cast<uint16_t>(msgLen);
    event.severity = static_cast<uint8_t>(severity);
    event.subsystem = subsystem;
    
    // Add fields (up to 4)
    event.fieldCount = 0;
    for (const auto& field : fields) {
        if (event.fieldCount >= 4) break;
        auto& f = event.fields[event.fieldCount];
        size_t keyLen = std::min(field.first.size(), sizeof(f.key) - 1);
        memcpy(f.key, field.first.data(), keyLen);
        f.key[keyLen] = '\0';
        size_t valLen = std::min(field.second.size(), sizeof(f.value) - 1);
        memcpy(f.value, field.second.data(), valLen);
        f.value[valLen] = '\0';
        f.sensitive = false;
        event.fieldCount++;
    }
    
    if (!eventQueue_.TryPush(event)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    } else {
        totalLogged_.fetch_add(1, std::memory_order_relaxed);
    }
}

void LockFreeLogger::Flush() {
    flushRequested_.store(true);
    // Wait for flush to complete (with timeout)
    auto start = std::chrono::steady_clock::now();
    while (flushRequested_.load() && 
           std::chrono::steady_clock::now() - start < std::chrono::seconds(2)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void LockFreeLogger::GetRecentEvents(std::vector<std::string>& out, size_t maxCount) const noexcept {
    // This would require a separate ring buffer for formatted strings
    // For now, return empty - can be implemented with a separate recent events buffer
}

LockFreeLogger::Stats LockFreeLogger::GetStats() const noexcept {
    Stats stats;
    stats.totalLogged = totalLogged_.load();
    stats.dropped = dropped_.load();
    stats.fileBytes = fileBytes_;
    stats.queueSize = eventQueue_.Size();
    return stats;
}

void LockFreeLogger::SetLogFile(const std::filesystem::path& path, size_t maxBytes) {
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (logFile_.is_open()) {
        logFile_.close();
    }
    logPath_ = path;
    maxFileBytes_ = maxBytes;
    logFile_.open(logPath_, std::ios::out | std::ios::app | std::ios::binary);
    if (logFile_.is_open()) {
        logFile_.seekp(0, std::ios::end);
        fileBytes_ = static_cast<size_t>(logFile_.tellp());
    }
}

void LockFreeLogger::WriterThread() {
    constexpr auto kFlushInterval = std::chrono::milliseconds(100);
    constexpr auto kRotateCheckInterval = std::chrono::seconds(30);
    
    auto lastFlush = std::chrono::steady_clock::now();
    auto lastRotateCheck = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        auto now = std::chrono::steady_clock::now();
        
        // Write events to file
        WriteToFile();
        
        // Periodic flush
        if (now - lastFlush >= kFlushInterval || flushRequested_.load()) {
            {
                std::lock_guard<std::mutex> lock(fileMutex_);
                if (logFile_.is_open()) {
                    logFile_.flush();
                }
            }
            flushRequested_.store(false);
            lastFlush = now;
        }
        
        // Periodic rotation check
        if (now - lastRotateCheck >= kRotateCheckInterval) {
            RotateIfNeeded();
            lastRotateCheck = now;
        }
        
        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void LockFreeLogger::WriteToFile() {
    std::vector<LogEvent> batch;
    batch.reserve(64);

    // Drain queue into batch
    eventQueue_.PopAll(batch, 64);
    if (batch.empty()) return;

    // Write batch to file
    {
        std::lock_guard<std::mutex> lock(fileMutex_);
        if (!logFile_.is_open()) return;

        for (const auto& ev : batch) {
            std::string line = FormatEvent(ev);
            logFile_.write(line.c_str(), static_cast<std::streamsize>(line.size()));
            fileBytes_ += line.size();
        }
        RotateIfNeeded();
    }
}

void LockFreeLogger::RotateIfNeeded() {
    // Caller must hold fileMutex_ (WriteToFile).
    if (!logFile_.is_open()) return;

    if (fileBytes_ < maxFileBytes_) return;

    logFile_.flush();
    logFile_.close();

    try {
        const auto backup = logPath_.parent_path() / (logPath_.stem().string() + "_old" + logPath_.extension().string());
        std::error_code ec;
        std::filesystem::rename(logPath_, backup, ec);
    } catch (...) {
        // Best-effort rotation.
    }

    logFile_.open(logPath_, std::ios::out | std::ios::app | std::ios::binary);
    fileBytes_ = 0;
}

void LockFreeLogger::WriteHeader() {
    // Header is written via Log() call in Initialize()
}

std::string LockFreeLogger::FormatEvent(const LogEvent& event) const {
    std::ostringstream oss;

    // event.timestamp is steady_clock nanos since session start — format as relative ms.
    const auto msTotal = event.timestamp / 1'000'000ULL;
    const auto secs = msTotal / 1000ULL;
    const auto ms = msTotal % 1000ULL;

    oss << '[' << secs << '.' << std::setfill('0') << std::setw(3) << ms << ']';

    static const char* severityNames[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "CRITICAL"};
    const int sev = static_cast<int>(event.severity);
    oss << '[' << severityNames[(sev >= 0 && sev < 6) ? sev : 2] << ']';

    static const char* subsystemNames[] = {
        "CORE", "AUTH", "DMA", "VMM", "LEECHCORE", "FTDI", "PNP", "PROCINFO",
        "PROCESS", "RUNTIME", "UI", "UPDATER", "RADAR", "INPUT", "CONFIG",
        "ADAPTER", "READ", "WATCHDOG", "GAME", "PLATFORM", "INPUTDEV", "UPDATE", "APP"
    };
    const int sub = static_cast<int>(event.subsystem);
    oss << '[' << subsystemNames[(sub >= 0 && sub < 23) ? sub : 0] << ']';

#ifdef _DEBUG
    oss << "[T" << event.threadId << ']';
#endif

    oss << ' ' << std::string_view(event.message, event.msgLen);

    if (event.hasErrorId) {
        oss << '[' << std::string_view(event.errorId) << ']';
    }

    oss << '\n';
    return oss.str();
}

} // namespace OmniGhost::Platform