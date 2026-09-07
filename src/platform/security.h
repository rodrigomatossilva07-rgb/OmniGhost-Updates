#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <thread>
#include <deque>
#include <condition_variable>
#include <mutex>

namespace OmniGhost::Security {

// ============================================================
// Compile-time String Obfuscation
// ============================================================

template <size_t N>
struct ObfuscatedString {
    constexpr ObfuscatedString(const char (&str)[N]) noexcept : size(N - 1) {
        for (size_t i = 0; i < size; ++i) {
            data[i] = str[i] ^ key[i % keySize];
        }
    }

    [[nodiscard]] std::string Decrypt() const noexcept {
        std::string result;
        result.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            result.push_back(data[i] ^ key[i % keySize]);
        }
        return result;
    }

    [[nodiscard]] std::string_view DecryptView() const noexcept {
        // Returns decrypted string (temporary, use immediately)
        static thread_local std::string buffer;
        buffer.clear();
        buffer.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            buffer.push_back(data[i] ^ key[i % keySize]);
        }
        return buffer;
    }

    size_t size;
    char data[256];
    static constexpr char key[32] = {0x5A, 0xA5, 0x3C, 0xC3, 0x69, 0x96, 0x12, 0x21,
                                      0x4B, 0xB4, 0x7E, 0xE7, 0x8D, 0xD8, 0xF1, 0x1F,
                                      0x2A, 0xA2, 0x5F, 0xF5, 0x3C, 0xC3, 0x0F, 0xF0,
                                      0x6B, 0xB6, 0x4D, 0xD4, 0x1E, 0xE1, 0x98, 0x89};
    static constexpr size_t keySize = 32;
};

#define OBSTR(str) []{ constexpr OmniGhost::Security::ObfuscatedString<sizeof(str)> obs(str); return obs.DecryptView(); }()

// ============================================================
// Anti-Debug / Anti-Dump
// ============================================================

class AntiDebug {
public:
    static void Initialize() noexcept {
        if (IsDebuggerPresent()) {
            TerminateProcess(GetCurrentProcess(), 0xDEAD);
        }
    }

    static bool IsDebuggerAttached() noexcept {
        return IsDebuggerPresent() != FALSE;
    }

    static void CheckRemoteDebugger() noexcept {
        BOOL isDebugged = FALSE;
        CheckRemoteDebuggerPresent(GetCurrentProcess(), &isDebugged);
        if (isDebugged) {
            TerminateProcess(GetCurrentProcess(), 0xDEAD);
        }
    }

    static void CheckNtGlobalFlag() noexcept {
        // Check PEB->NtGlobalFlag for debug heap
        // This is a simplified check
        if (IsDebuggerAttached()) {
            TerminateProcess(GetCurrentProcess(), 0xDEAD);
        }
    }

    static void CheckTiming() noexcept {
        // Timing-based anti-debug
        auto start = std::chrono::high_resolution_clock::now();
        volatile int dummy = 0;
        for (int i = 0; i < 10000; ++i) dummy += i;
        auto end = std::chrono::high_resolution_clock::now();
        
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        if (elapsed > 10000) { // More than 10ms for simple loop = debugger
            TerminateProcess(GetCurrentProcess(), 0xDEAD);
        }
    }

    static void RunAllChecks() noexcept {
        Initialize();
        CheckRemoteDebugger();
        CheckNtGlobalFlag();
        CheckTiming();
    }

    static void InstallPeriodicCheck(std::chrono::milliseconds interval = std::chrono::seconds(30)) {
        std::thread([interval]() {
            while (true) {
                std::this_thread::sleep_for(interval);
                RunAllChecks();
            }
        }).detach();
    }
};

// ============================================================
// Anti-Dump Protection
// ============================================================

class AntiDump {
public:
    static void ProtectMemory() noexcept {
        // Mark critical sections as PAGE_NOACCESS when not in use
        // This is a placeholder for more sophisticated protection
    }

    static void ScrambleMemory() noexcept {
        // Scramble sensitive data in memory when not in use
    }

    static void InstallPageGuard(void* address, size_t size) noexcept {
        DWORD oldProtect = 0;
        VirtualProtect(address, size, PAGE_GUARD | PAGE_READWRITE, &oldProtect);
        (void)oldProtect;
    }

    static void RemovePageGuard(void* address, size_t size) noexcept {
        DWORD oldProtect = 0;
        VirtualProtect(address, size, PAGE_READWRITE, &oldProtect);
        (void)oldProtect;
    }
};

// ============================================================
// Integrity Verification
// ============================================================

class IntegrityVerifier {
public:
    struct HashResult {
        std::array<uint8_t, 32> hash;
        bool valid = false;
    };

    static HashResult CalculatePEHash() noexcept {
        HashResult result;
        
        HMODULE hModule = GetModuleHandleW(nullptr);
        if (!hModule) return result;

        PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(hModule);
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) return result;

        PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(
            reinterpret_cast<uint8_t*>(hModule) + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) return result;

        // Hash the PE headers and sections (excluding checksum)
        // This is a simplified implementation
        result.valid = true;
        return result;
    }

    static bool VerifyPEIntegrity() noexcept {
        auto hash = CalculatePEHash();
        return hash.valid;
    }

    static HashResult CalculateResourceHash(std::string_view resourceName) noexcept {
        HashResult result;
        
        HMODULE hModule = GetModuleHandleW(nullptr);
        HRSRC hResource = FindResourceW(hModule, 
            std::wstring(resourceName.begin(), resourceName.end()).c_str(), 
            RT_RCDATA);
        
        if (!hResource) return result;
        
        HGLOBAL hGlobal = LoadResource(GetModuleHandleW(nullptr), hResource);
        if (!hGlobal) return result;
        
        void* pData = LockResource(hGlobal);
        DWORD size = SizeofResource(GetModuleHandleW(nullptr), hResource);
        
        if (!pData || size == 0) return result;
        
        // Calculate SHA-256
        // Simplified - would use BCrypt in production
        result.valid = true;
        return result;
    }

    static bool VerifyResources() noexcept {
        // Verify all embedded resources
        return true; // Simplified
    }

    static void StartPeriodicVerification(std::chrono::seconds interval = std::chrono::minutes(5)) {
        std::thread([interval]() {
            while (true) {
                std::this_thread::sleep_for(interval);
                if (!VerifyPEIntegrity() || !VerifyResources()) {
                    TerminateProcess(GetCurrentProcess(), 0xBADF00D);
                }
            }
        }).detach();
    }
};

// ============================================================
// String/Offset Obfuscation
// ============================================================

template <size_t N>
class ObfuscatedOffset {
public:
    constexpr ObfuscatedOffset(uint64_t value, uint64_t key) noexcept 
        : encrypted(value ^ key) {}
    
    [[nodiscard]] uint64_t Decrypt(uint64_t key) const noexcept {
        return encrypted ^ key;
    }
    
private:
    uint64_t encrypted;
};

#define OBFUSCATE_OFFSET(value, key) OmniGhost::Security::ObfuscatedOffset<sizeof(uint64_t)>(value, key)

// Compile-time string encryption
template <size_t N>
struct EncryptedString {
    constexpr EncryptedString(const char (&str)[N]) noexcept : size(N - 1) {
        for (size_t i = 0; i < size; ++i) {
            data[i] = str[i] ^ key[i % keySize];
        }
    }
    
    [[nodiscard]] std::string Decrypt() const noexcept {
        std::string result;
        result.reserve(size);
        for (size_t i = 0; i < size; ++i) {
            result.push_back(data[i] ^ key[i % keySize]);
        }
        return result;
    }
    
    size_t size;
    char data[N];
    static constexpr char key[16] = {0xAA, 0x55, 0x33, 0xCC, 0x66, 0x99, 0x12, 0x34,
                                     0x5A, 0xA5, 0x3C, 0xC3, 0x7E, 0xE7, 0x1F, 0xF1};
    static constexpr size_t keySize = 16;
};

#define ENCRYPT_STR(str) []{ constexpr OmniGhost::Security::EncryptedString<sizeof(str)> es(str); return es.Decrypt(); }()

// ============================================================
// Hardware Heartbeat
// ============================================================

class HardwareHeartbeat {
public:
    struct Config {
        std::chrono::milliseconds interval = std::chrono::seconds(10);
        std::chrono::milliseconds timeout = std::chrono::seconds(30);
        std::function<void()> onTimeout;
        std::function<bool()> isConnected;
    };

    explicit HardwareHeartbeat(Config config) : config_(std::move(config)) {}
    
    void Start() noexcept {
        running_.store(true);
        thread_ = std::thread(&HardwareHeartbeat::Run, this);
    }
    
    void Stop() noexcept {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
    }
    
    void Feed() noexcept {
        lastFeed_.store(std::chrono::steady_clock::now(), std::memory_order_release);
    }
    
    [[nodiscard]] bool IsHealthy() const noexcept {
        auto now = std::chrono::steady_clock::now();
        auto last = lastFeed_.load(std::memory_order_acquire);
        return (now - last) < config_.timeout;
    }
    
    ~HardwareHeartbeat() {
        Stop();
    }
    
private:
    void Run() noexcept {
        while (running_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(config_.interval);
            
            if (!config_.isConnected()) {
                continue;
            }
            
            auto now = std::chrono::steady_clock::now();
            auto last = lastFeed_.load(std::memory_order_acquire);
            
            if (now - last > config_.timeout) {
                if (config_.onTimeout) {
                    config_.onTimeout();
                }
                break;
            }
        }
    }
    
    Config config_;
    std::atomic<bool> running_{false};
    std::atomic<std::chrono::steady_clock::time_point> lastFeed_{std::chrono::steady_clock::now()};
    std::thread thread_;
};

// ============================================================
// Security Audit Logging
// ============================================================

class SecurityAuditLog {
public:
    enum class EventType {
        LoginAttempt,
        LoginSuccess,
        LoginFailure,
        HardwareConnected,
        HardwareDisconnected,
        HardwareTimeout,
        LicenseCheck,
        LicenseFailure,
        IntegrityFailure,
        DebugAttempt,
        DumpAttempt,
        ConfigTampering,
        OffsetTampering,
        NetworkAnomaly
    };
    
    struct Event {
        EventType type;
        std::chrono::system_clock::time_point timestamp;
        std::string details;
        uint32_t severity; // 1=info, 2=warning, 3=error, 4=critical
    };
    
    static void Log(EventType type, std::string_view details, uint32_t severity = 1) noexcept {
        Event event{
            type,
            std::chrono::system_clock::now(),
            std::string(details),
            severity
        };
        
        // Write to secure log (encrypted)
        WriteEvent(event);
    }
    
    static void LogLoginAttempt(std::string_view username, bool success) noexcept {
        Log(success ? EventType::LoginSuccess : EventType::LoginFailure,
            std::string("User: ") + std::string(username), success ? 1 : 3);
    }
    
    static void LogHardwareEvent(EventType type, std::string_view device, bool connected) noexcept {
        std::string details = std::string(device) + " " + (connected ? "connected" : "disconnected");
        Log(type, details, connected ? 1 : 3);
    }
    
    static void LogIntegrityFailure(std::string_view component, std::string_view detail) noexcept {
        Log(EventType::IntegrityFailure, 
            std::string("Component: ") + std::string(component) + " - " + std::string(detail), 4);
    }
    
    static void LogAntiDebugTrigger(std::string_view method) noexcept {
        Log(EventType::DebugAttempt, 
            std::string("Method: ") + std::string(method), 4);
    }

private:
    static void WriteEvent(const Event& event) noexcept {
        (void)event;
        // Write to encrypted log file
        // Implementation would use lockfree_logger
    }
};

// ============================================================
// Network Rate Limiting & Retry
// ============================================================

class NetworkRateLimiter {
public:
    struct Config {
        size_t maxRequestsPerWindow = 10;
        std::chrono::seconds windowSize = std::chrono::seconds(60);
        std::chrono::milliseconds baseBackoff = std::chrono::seconds(1);
        size_t maxRetries = 3;
        double backoffMultiplier = 2.0;
        std::chrono::seconds maxBackoff = std::chrono::minutes(5);
    };
    
    explicit NetworkRateLimiter(Config config = {}) : config_(config) {}
    
    template <typename Func>
    auto ExecuteWithRetry(Func&& func) -> std::invoke_result_t<Func> {
        std::chrono::milliseconds backoff = config_.baseBackoff;
        
        for (size_t attempt = 0; attempt <= config_.maxRetries; ++attempt) {
            // Check rate limit
            WaitForSlot();
            
            auto result = func();
            if (result) {
                RecordSuccess();
                return result;
            }
            
            // Check if we should retry
            if (attempt < config_.maxRetries && IsRetryable(result.error())) {
                std::this_thread::sleep_for(backoff);
                backoff = std::min(
                    std::chrono::milliseconds(static_cast<long long>(backoff.count() * config_.backoffMultiplier)),
                    config_.maxBackoff
                );
                continue;
            }
            
            return result;
        }
        
        return Err<std::invoke_result_t<Func>>("Max retries exceeded");
    }
    
private:
    void WaitForSlot() {
        // Simple token bucket implementation
        std::unique_lock lock(mutex_);
        auto now = std::chrono::steady_clock::now();
        
        // Remove expired entries
        while (!requests_.empty() && 
               now - requests_.front() > config_.windowSize) {
            requests_.pop_front();
        }
        
        if (requests_.size() >= config_.maxRequestsPerWindow) {
            auto waitTime = config_.windowSize - (now - requests_.front());
            cv_.wait_for(lock, waitTime);
            return WaitForSlot(); // Recheck
        }
        
        requests_.push_back(now);
    }
    
    void RecordSuccess() {
        std::lock_guard lock(mutex_);
        // Could track success rate for adaptive limiting
    }
    
    bool IsRetryable(std::string_view error) {
        // Determine if error is retryable
        static constexpr std::string_view retryable[] = {
            "timeout", "connection", "network", "temporary", 
            "503", "504", "429", "ECONNREFUSED", "ETIMEDOUT"
        };
        
        for (const auto& r : retryable) {
            if (error.find(r) != std::string::npos) return true;
        }
        return false;
    }
    
    Config config_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::chrono::steady_clock::time_point> requests_;
};

// ============================================================
// FPGA Disconnect Detection & Recovery
// ============================================================

class FpgaMonitor {
public:
    struct Config {
        std::chrono::milliseconds checkInterval = std::chrono::seconds(5);
        size_t maxConsecutiveFailures = 3;
        std::function<void()> onDisconnect;
        std::function<bool()> tryReconnect;
        std::function<void()> onReconnect;
    };
    
    explicit FpgaMonitor(Config config) : config_(std::move(config)) {}
    
    void Start() noexcept {
        running_.store(true);
        thread_ = std::thread(&FpgaMonitor::MonitorLoop, this);
    }
    
    void Stop() noexcept {
        running_.store(false);
        if (thread_.joinable()) thread_.join();
    }
    
    void ReportSuccess() noexcept {
        consecutiveFailures_.store(0);
        lastSuccess_ = std::chrono::steady_clock::now();
    }
    
    void ReportFailure() noexcept {
        size_t failures = ++consecutiveFailures_;
        if (failures >= config_.maxConsecutiveFailures) {
            if (config_.onDisconnect) config_.onDisconnect();
            
            // Attempt reconnection
            if (config_.tryReconnect && config_.tryReconnect()) {
                consecutiveFailures_.store(0);
                if (config_.onReconnect) config_.onReconnect();
            }
        }
    }
    
    [[nodiscard]] bool IsConnected() const noexcept {
        return consecutiveFailures_.load() < config_.maxConsecutiveFailures;
    }
    
    ~FpgaMonitor() { Stop(); }
    
private:
    void MonitorLoop() noexcept {
        while (running_.load()) {
            std::this_thread::sleep_for(config_.checkInterval);
            // The actual health check is done by the DMA layer calling ReportSuccess/Failure
        }
    }
    
    Config config_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::atomic<size_t> consecutiveFailures_{0};
    std::chrono::steady_clock::time_point lastSuccess_;
};

} // namespace OmniGhost::Security