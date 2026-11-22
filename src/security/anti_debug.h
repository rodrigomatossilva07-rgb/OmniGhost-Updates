#pragma once

// Anti-Debug / Anti-Dump Module
// Comprehensive debugger detection and anti-dumping protection

#ifndef OMNIGHOST_ANTI_DEBUG_H
#define OMNIGHOST_ANTI_DEBUG_H

#include "security_common.h"

#include <atomic>
#include <mutex>
#include <thread>

namespace omnighost::security {

// =============================================================================
// Debugger Detection Methods
// =============================================================================

enum class DebuggerDetectionMethod : uint32_t {
    None = 0,

    // Windows API
    IsDebuggerPresent = 1 << 0,
    CheckRemoteDebuggerPresent = 1 << 1,
    NtQueryInformationProcess_DebugPort = 1 << 2,
    NtQueryInformationProcess_DebugFlags = 1 << 3,
    NtQueryInformationProcess_DebugObjectHandle = 1 << 4,
    NtSetInformationThread_HideThread = 1 << 5,

    // PEB / TEB
    PEB_BeingDebugged = 1 << 6,
    PEB_NtGlobalFlag = 1 << 7,
    PEB_HeapFlags = 1 << 8,
    TEB_HideThread = 1 << 9,

    // Timing Checks
    RDTSC_Timing = 1 << 10,
    QueryPerformanceCounter_Timing = 1 << 11,
    GetTickCount_Timing = 1 << 12,
    TimeGetTime_Timing = 1 << 13,

    // Hardware Breakpoints
    HardwareBreakpoints_DR = 1 << 14,
    HardwareBreakpoints_Context = 1 << 15,

    // Software Breakpoints
    INT3_SCAN = 1 << 16,
    CC_SCAN = 1 << 17,
    TrapFlag_SingleStep = 1 << 17,

    // Exception Handling
    VectoredExceptionHandler = 1 << 18,
    UnhandledExceptionFilter = 1 << 19,
    Exception_ContinueExecution = 1 << 19,

    // Process/Thread
    ThreadHideFromDebugger = 1 << 20,
    ProcessDebugObjectHandle = 1 << 21,
    DebugObjectHandle = 1 << 22,

    // Memory
    GuardPages = 1 << 23,
    MemoryBreakpoints = 1 << 24,

    // Advanced
    OutputDebugString = 1 << 24,
    BlockInput = 1 << 25,
    NtCreateThreadEx = 1 << 25,
    NtYieldExecution = 1 << 26,

    // VM / Sandbox
    VMWare_Backdoor = 1 << 26,
    VirtualPC_Exception = 1 << 27,
    HyperV_CPUID = 1 << 27,

    // Custom
    Custom_Check = 1u << 31,
};

// =============================================================================
// Configuration
// =============================================================================

struct AntiDebugConfig {
    // Detection methods to enable (bitmask)
    uint32_t enabled_methods = static_cast<uint32_t>(DebuggerDetectionMethod::IsDebuggerPresent) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::CheckRemoteDebuggerPresent) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::NtQueryInformationProcess_DebugPort) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::NtQueryInformationProcess_DebugFlags) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::PEB_BeingDebugged) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::PEB_NtGlobalFlag) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::PEB_HeapFlags) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::RDTSC_Timing) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::HardwareBreakpoints_DR) |
                               static_cast<uint32_t>(DebuggerDetectionMethod::INT3_SCAN);

    // Actions on detection
    bool terminate_on_detection = true;
    bool log_detection = true;
    bool callback_on_detection = true;

    // Timing thresholds (cycles for RDTSC, ms for others)
    uint64_t rdtsc_threshold_cycles = 100000;
    uint32_t timing_threshold_ms = 100;

    // Scan intervals (ms)
    uint32_t scan_interval_ms = 5000;

    // Actions
    bool terminate_on_detection_immediate = true;
    bool corrupt_critical_data = false;  // Corrupt own data to crash debugger
    bool spawn_decoy_thread = true;       // Spawn decoy threads to confuse debugger
};

// =============================================================================
// Detection Result
// =============================================================================

struct DetectionResult {
    bool debugger_detected = false;
    DebuggerDetectionMethod method = DebuggerDetectionMethod::None;
    uint32_t details = 0;
    uint64_t timestamp = 0;
    const char* description = nullptr;
};

// =============================================================================
// Anti-Debug Class
// =============================================================================

class AntiDebug {
public:
    // Singleton
    static AntiDebug& instance() {
        static AntiDebug instance;
        return instance;
    }

    AntiDebug(const AntiDebug&) = delete;
    AntiDebug& operator=(const AntiDebug&) = delete;
    AntiDebug(AntiDebug&&) = delete;
    AntiDebug& operator=(AntiDebug&&) = delete;

    // Initialization
    OMNIGHOST_SECURE_FUNC bool initialize(const AntiDebugConfig& config = {});
    OMNIGHOST_SECURE_FUNC void shutdown();

    OMNIGHOST_SECURE_INLINE void set_config(const AntiDebugConfig& config) { config_ = config; }
    OMNIGHOST_SECURE_INLINE const AntiDebugConfig& get_config() const { return config_; }

    // Main check - runs all enabled detection methods
    OMNIGHOST_SECURE_FUNC bool check_debugger();

    // Individual checks
    OMNIGHOST_SECURE_FUNC bool check_is_debugger_present();
    OMNIGHOST_SECURE_FUNC bool check_remote_debugger_present();
    OMNIGHOST_SECURE_FUNC bool check_nt_query_debug_port();
    OMNIGHOST_SECURE_FUNC bool check_nt_query_debug_flags();
    OMNIGHOST_SECURE_FUNC bool check_peb_being_debugged();
    OMNIGHOST_SECURE_FUNC bool check_peb_nt_global_flag();
    OMNIGHOST_SECURE_FUNC bool check_peb_heap_flags();
    OMNIGHOST_SECURE_FUNC bool check_rdtsc_timing();
    OMNIGHOST_SECURE_FUNC bool check_hardware_breakpoints();
    OMNIGHOST_SECURE_FUNC bool check_int3_scan();
    OMNIGHOST_SECURE_FUNC bool check_software_breakpoints();
    OMNIGHOST_SECURE_FUNC bool check_vectored_exception_handler();
    OMNIGHOST_SECURE_FUNC bool check_unhandled_exception_filter();
    OMNIGHOST_SECURE_FUNC bool check_hardware_breakpoints_context();

    // Callbacks
    using DetectionCallback = void(*)(const DetectionResult&);
    OMNIGHOST_SECURE_INLINE void set_detection_callback(DetectionCallback cb) {
        detection_callback_ = cb;
    }

    // Status
    OMNIGHOST_SECURE_INLINE bool is_initialized() const { return initialized_; }
    OMNIGHOST_SECURE_INLINE uint32_t get_detection_count() const { return detection_count_; }
    OMNIGHOST_SECURE_INLINE uint64_t get_last_check_time() const { return last_check_time_; }

    // Manual check
    OMNIGHOST_SECURE_FUNC void force_check();

    // Thread-safe periodic check
    OMNIGHOST_SECURE_FUNC void start_periodic_check();
    OMNIGHOST_SECURE_FUNC void stop_periodic_check();

private:
    AntiDebug() = default;
    ~AntiDebug() = default;

    AntiDebugConfig config_;
    bool initialized_ = false;
    std::thread periodic_thread_;
    std::atomic<bool> periodic_running_{false};
    std::mutex mutex_;

    DetectionCallback detection_callback_ = nullptr;
    uint32_t detection_count_ = 0;
    uint64_t last_check_time_ = 0;

    // Internal
    OMNIGHOST_SECURE_FUNC bool run_all_checks();
    OMNIGHOST_SECURE_FUNC void report_detection(const DetectionResult& result);
    OMNIGHOST_SECURE_FUNC void periodic_worker();

    // Platform-specific
    #if OMNIGHOST_PLATFORM_WINDOWS
    OMNIGHOST_SECURE_FUNC bool check_nt_query_debug_object_handle();
    OMNIGHOST_SECURE_FUNC bool check_thread_hide_from_debugger();
    OMNIGHOST_SECURE_FUNC bool check_process_debug_object();
    OMNIGHOST_SECURE_FUNC bool check_output_debug_string();
    OMNIGHOST_SECURE_FUNC bool check_block_input();
    #endif
};

// =============================================================================
// Anti-Dump
// =============================================================================

class AntiDump {
public:
    static AntiDump& instance() {
        static AntiDump instance;
        return instance;
    }

    AntiDump(const AntiDump&) = delete;
    AntiDump& operator=(const AntiDump&) = delete;

    OMNIGHOST_SECURE_FUNC bool initialize();
    OMNIGHOST_SECURE_FUNC void shutdown();

    // Dump prevention
    OMNIGHOST_SECURE_FUNC bool protect_process_memory();
    OMNIGHOST_SECURE_FUNC bool protect_critical_sections();
    OMNIGHOST_SECURE_FUNC bool corrupt_on_dump_attempt();

    // Callbacks
    using DumpCallback = void(*)(uint32_t method);
    OMNIGHOST_SECURE_INLINE void set_dump_callback(DumpCallback cb) { dump_callback_ = cb; }

private:
    AntiDump() = default;
    ~AntiDump() = default;

    bool initialized_ = false;
    std::mutex mutex_;
    DumpCallback dump_callback_ = nullptr;
};

// =============================================================================
// Global Functions
// =============================================================================

OMNIGHOST_SECURE_FUNC bool check_debugger_present();
OMNIGHOST_SECURE_FUNC void trigger_debugger_detection(DebuggerDetectionMethod method);
OMNIGHOST_SECURE_FUNC void install_vectored_exception_handler();
OMNIGHOST_SECURE_FUNC void remove_vectored_exception_handler();

} // namespace omnighost::security

#endif // OMNIGHOST_ANTI_DEBUG_H
