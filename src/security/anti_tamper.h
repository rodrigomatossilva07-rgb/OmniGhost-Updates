#pragma once

// Anti-Tamper / Integrity Check Module
// Provides runtime integrity verification of code sections and critical data

#ifndef OMNIGHOST_ANTI_TAMPER_H
#define OMNIGHOST_ANTI_TAMPER_H

#include "security_common.h"

namespace omnighost::security {

// =============================================================================
// Configuration
// =============================================================================

struct AntiTamperConfig {
    // Code sections to protect
    bool protect_text_section = true;
    bool protect_rodata_section = true;
    bool protect_data_section = false;
    bool protect_heap = true;
    bool protect_stack = false;

    // Check intervals (milliseconds)
    uint32_t integrity_check_interval_ms = 5000;
    uint32_t fast_check_interval_ms = 1000;

    // Actions on violation
    bool terminate_on_violation = true;
    bool log_violation = true;
    bool callback_on_violation = true;

    // Advanced
    bool verify_imports = true;
    bool verify_exports = false;
    bool verify_relocations = false;
    bool check_module_list = true;

    // CRC32 polynomial (standard IEEE 802.3)
    static constexpr uint32_t CRC32_POLY = 0xEDB88320;
};

// =============================================================================
// CRC32 Implementation
// =============================================================================

class CRC32 {
public:
    static constexpr uint32_t POLYNOMIAL = 0xEDB88320;

    OMNIGHOST_FORCE_INLINE static uint32_t compute(const void* data, size_t length, uint32_t initialCrc = 0xFFFFFFFF) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint32_t crc = ~initialCrc;

        for (size_t i = 0; i < length; ++i) {
            crc ^= bytes[i];
            for (int j = 0; j < 8; ++j) {
                const uint32_t mask = 0u - (crc & 1u);
                crc = (crc >> 1) ^ (mask & POLYNOMIAL);
            }
        }

        return ~crc;
    }

    OMNIGHOST_FORCE_INLINE static uint32_t compute_incremental(const void* data, size_t length, uint32_t crc) {
        return compute(data, length, crc);
    }
};

// =============================================================================
// Protected Region
// =============================================================================

struct ProtectedRegion {
    const char* name;
    uintptr_t start_address;
    size_t size;
    uint32_t expected_crc;
    uint32_t flags;

    enum Flags : uint32_t {
        FLAG_NONE = 0,
        FLAG_EXECUTABLE = 1 << 0,
        FLAG_READABLE = 1 << 1,
        FLAG_WRITABLE = 1 << 2,
        FLAG_CRITICAL = 1 << 3,      // Critical section - immediate termination on modification
        FLAG_SELF_MODIFYING = 1 << 4, // Known to self-modify (JIT, etc.)
    };
};

// =============================================================================
// Anti-Tamper Class
// =============================================================================

class AntiTamper {
public:
    // Singleton access
    static AntiTamper& instance() {
        static AntiTamper instance;
        return instance;
    }

    // Non-copyable, non-movable
    AntiTamper(const AntiTamper&) = delete;
    AntiTamper& operator=(const AntiTamper&) = delete;
    AntiTamper(AntiTamper&&) = delete;
    AntiTamper& operator=(AntiTamper&&) = delete;

    // Initialization
    OMNIGHOST_SECURE_FUNC bool initialize(const AntiTamperConfig& config = {});
    OMNIGHOST_SECURE_FUNC void shutdown();

    // Configuration
    OMNIGHOST_SECURE_INLINE void set_config(const AntiTamperConfig& config) { config_ = config; }
    OMNIGHOST_SECURE_INLINE const AntiTamperConfig& get_config() const { return config_; }

    // Region management
    OMNIGHOST_SECURE_FUNC bool add_protected_region(const ProtectedRegion& region);
    OMNIGHOST_SECURE_FUNC bool remove_protected_region(const char* name);
    OMNIGHOST_SECURE_FUNC bool update_region_crc(const char* name);

    // Integrity checks
    OMNIGHOST_SECURE_FUNC bool verify_all_regions();
    OMNIGHOST_SECURE_FUNC bool verify_region(const char* name);
    OMNIGHOST_SECURE_FUNC bool verify_region(const ProtectedRegion& region);

    // Full integrity check (includes imports, modules, etc.)
    OMNIGHOST_SECURE_FUNC bool full_integrity_check();

    // Callbacks
    using ViolationCallback = void(*)(const IntegrityReport&);
    OMNIGHOST_SECURE_INLINE void set_violation_callback(IntegrityViolationCallback cb) {
        violation_callback_ = cb;
    }

    // Status
    OMNIGHOST_SECURE_INLINE bool is_initialized() const { return initialized_; }
    OMNIGHOST_SECURE_INLINE size_t get_region_count() const { return regions_.size(); }
    OMNIGHOST_SECURE_INLINE uint64_t get_last_check_time() const { return last_check_time_; }
    OMNIGHOST_SECURE_INLINE uint32_t get_violation_count() const { return violation_count_; }

    // Manual trigger
    OMNIGHOST_SECURE_FUNC void force_check();

private:
    AntiTamper() = default;
    ~AntiTamper() = default;

    // Configuration
    AntiTamperConfig config_;

    // State
    bool initialized_ = false;
    std::vector<ProtectedRegion> regions_;
    IntegrityViolationCallback violation_callback_ = nullptr;
    uint64_t last_check_time_ = 0;
    uint32_t violation_count_ = 0;

    // Known good state
    std::vector<uint8_t> original_text_section_;
    std::vector<uint8_t> original_imports_;
    std::vector<std::string> original_modules_;

    // Internal methods
    OMNIGHOST_SECURE_FUNC bool scan_memory_regions();
    OMNIGHOST_SECURE_FUNC bool verify_text_section();
    OMNIGHOST_SECURE_FUNC bool verify_imports();
    OMNIGHOST_SECURE_FUNC bool verify_modules();
    OMNIGHOST_SECURE_FUNC bool verify_heap_integrity();
    OMNIGHOST_SECURE_FUNC void report_violation(const IntegrityReport& report);

    // Platform-specific
    OMNIGHOST_SECURE_FUNC bool get_module_bounds(const char* module_name, uintptr_t& base, size_t& size);
    OMNIGHOST_SECURE_FUNC bool get_section_bounds(const char* section_name, uintptr_t& base, size_t size);
};

// =============================================================================
// Global Functions
// =============================================================================

// Quick integrity check (for periodic calls)
OMNIGHOST_SECURE_FUNC bool quick_integrity_check();

// Full integrity check (comprehensive)
OMNIGHOST_SECURE_FUNC bool full_integrity_check();

// Register protected region (called from module initialization)
OMNIGHOST_SECURE_FUNC bool register_protected_region(const ProtectedRegion& region);

// Verify specific region
OMNIGHOST_SECURE_FUNC bool verify_region(const char* name);

// Get integrity report
OMNIGHOST_SECURE_FUNC const IntegrityReport* get_last_report();

} // namespace omnighost::security

#endif // OMNIGHOST_ANTI_TAMPER_H
