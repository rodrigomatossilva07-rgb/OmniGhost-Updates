#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <array>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>

#include "memory_span.h"
#include "file_integrity.h"

namespace OmniGhost::Security {

// =============================================================================
// Anti-Tamper & Integrity Checks
// =============================================================================

// Memory protection guard using VirtualProtect
class MemoryGuard {
public:
    MemoryGuard() = default;
    MemoryGuard(void* address, std::size_t size, DWORD protect = PAGE_READONLY);
    ~MemoryGuard();

    // Non-copyable, movable
    MemoryGuard(const MemoryGuard&) = delete;
    MemoryGuard& operator=(const MemoryGuard&) = delete;
    MemoryGuard(MemoryGuard&& other) noexcept;
    MemoryGuard& operator=(MemoryGuard&& other) noexcept;

    // Apply protection
    [[nodiscard]] bool Protect(void* address, std::size_t size, DWORD protect = PAGE_READONLY);
    // Remove protection (restore original)
    void Unprotect();

    // Check if memory is still protected
    [[nodiscard]] bool IsProtected() const noexcept { return protected_ && address_ != nullptr; }

    // Get protected region info
    [[nodiscard]] void* Address() const noexcept { return address_; }
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }
    [[nodiscard]] DWORD OriginalProtection() const noexcept { return originalProtect_; }

private:
    void* address_ = nullptr;
    std::size_t size_ = 0;
    DWORD originalProtect_ = 0;
    bool protected_ = false;
};

// Guard multiple critical regions
class MemoryGuardRegistry {
public:
    static MemoryGuardRegistry& Instance() noexcept {
        static MemoryGuardRegistry instance;
        return instance;
    }

    // Register a critical memory region for protection
    // Returns guard ID for later reference
    [[nodiscard]] std::size_t RegisterRegion(void* address, std::size_t size,
                                             DWORD protect = PAGE_READONLY,
                                             std::string_view name = "");

    // Unregister and unprotect region
    bool UnregisterRegion(std::size_t guardId);

    // Verify all protected regions are intact
    [[nodiscard]] bool VerifyAll() const;

    // Verify specific region
    [[nodiscard]] bool VerifyRegion(std::size_t guardId) const;

    // Get all registered regions
    struct RegionInfo {
        std::size_t id;
        void* address;
        std::size_t size;
        DWORD protection;
        std::string name;
        bool valid;
    };
    [[nodiscard]] std::vector<RegionInfo> GetAllRegions() const;

    // Enable/disable protection for all regions
    void SetProtectionEnabled(bool enabled);

private:
    MemoryGuardRegistry() = default;
    std::mutex mutex_;
    std::vector<MemoryGuard> guards_;
    std::atomic<bool> protectionEnabled_{true};
};

// =============================================================================
// Timing Checks (RDTSC) for Debugger Detection
// =============================================================================

class TimingCheck {
public:
    // Check for debugger using RDTSC timing
    // Returns true if timing anomaly detected (possible debugger)
    [[nodiscard]] static bool CheckRDTSC(uint64_t thresholdCycles = 10000);

    // Check for debugger using QueryPerformanceCounter
    [[nodiscard]] static bool CheckQPC(uint64_t thresholdMicroseconds = 1000);

    // Check for debugger using NtQueryInformationProcess
    [[nodiscard]] static bool CheckDebuggerPresent();

    // Check for hardware breakpoints
    [[nodiscard]] static bool CheckHardwareBreakpoints();

    // Combined check - runs all timing checks
    [[nodiscard]] static bool RunAllChecks(uint64_t rdtscThreshold = 10000,
                                           uint64_t qpcThreshold = 1000);

    // Get timing statistics for analysis
    struct TimingStats {
        uint64_t rdtscDelta = 0;
        uint64_t qpcDelta = 0;
        bool debuggerDetected = false;
        std::string details;
    };
    [[nodiscard]] static TimingStats GetTimingStats();
};

// =============================================================================
// Module Integrity Verification
// =============================================================================

struct ModuleIntegrityInfo {
    std::string moduleName;
    std::uintptr_t baseAddress = 0;
    std::size_t size = 0;
    std::string expectedSHA256;
    std::string actualSHA256;
    uint32_t crc32 = 0;
    bool verified = false;
    std::string error;
};

class ModuleIntegrity {
public:
    // Add a module to verify
    void AddModule(std::string_view moduleName,
                   std::uintptr_t baseAddress,
                   std::size_t size,
                   std::string_view expectedSHA256 = "");

    // Add module by name (will find base address automatically)
    void AddModuleByName(std::string_view moduleName, std::string_view expectedSHA256 = "");

    // Verify all registered modules
    [[nodiscard]] bool VerifyAll(std::vector<ModuleIntegrityInfo>& results);

    // Verify single module
    [[nodiscard]] bool VerifyModule(std::string_view moduleName, ModuleIntegrityInfo& result);

    // Calculate SHA256 of memory region
    [[nodiscard]] static bool CalculateSHA256(std::uintptr_t address, std::size_t size, std::string& hash);

    // Calculate CRC32 of memory region
    [[nodiscard]] static uint32_t CalculateCRC32(std::uintptr_t address, std::size_t size);

    // Get all registered modules
    [[nodiscard]] const std::vector<ModuleIntegrityInfo>& GetRegisteredModules() const noexcept;

    // Clear registered modules
    void Clear();

private:
    std::mutex mutex_;
    std::vector<ModuleIntegrityInfo> modules_;
};

// =============================================================================
// Startup Integrity Verification
// =============================================================================

// Run all integrity checks at startup
// Returns true if all checks pass
[[nodiscard]] bool RunStartupIntegrityChecks();

// Run periodic integrity checks (call from main loop)
// Returns true if all checks pass
[[nodiscard]] bool RunPeriodicIntegrityChecks();

// Install memory guards on critical regions
void InstallCriticalMemoryGuards();

// Run timing checks
[[nodiscard]] bool RunTimingChecks();

// =============================================================================
// Helper: Get current module base address
// =============================================================================
[[nodiscard]] std::uintptr_t GetCurrentModuleBase();

// =============================================================================
// Helper: Get module info by name
// =============================================================================
struct ModuleInfo {
    std::uintptr_t baseAddress = 0;
    std::size_t size = 0;
    std::string name;
    std::string path;
};
[[nodiscard]] std::optional<ModuleInfo> GetModuleInfo(std::string_view moduleName);

} // namespace OmniGhost::Security