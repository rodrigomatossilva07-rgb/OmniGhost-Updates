// Anti-Debug / Anti-Dump Implementation
// Comprehensive debugger detection and anti-dumping protection

#include "anti_debug.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

#if OMNIGHOST_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <intrin.h>
#pragma comment(lib, "psapi.lib")
#endif

namespace omnighost::security {

// =============================================================================
// Static Data
// =============================================================================

static std::mutex g_debug_mutex;
static AntiDebugConfig g_debug_config;
static bool g_debug_initialized = false;
static std::thread g_periodic_thread;
static std::atomic<bool> g_periodic_running{false};
static DetectionCallback g_detection_callback = nullptr;
static uint32_t g_detection_count = 0;
static uint64_t g_last_check_time = 0;
static DetectionResult g_last_result = {false, DebuggerDetectionMethod::None, 0, 0, nullptr};

// Anti-Dump
static std::mutex g_dump_mutex;
static bool g_dump_initialized = false;
static DumpCallback g_dump_callback = nullptr;

// =============================================================================
// Helper Functions
// =============================================================================

static uint64_t get_timestamp_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static uint64_t rdtsc() {
    #if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    return __rdtsc();
    #elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
    #else
    return std::chrono::high_resolution_clock::now().time_since_epoch().count();
    #endif
}

static void report_detection(const DetectionResult& result) {
    std::lock_guard<std::mutex> lock(g_debug_mutex);

    // Update stats
    static uint32_t detection_count = 0;
    detection_count++;
    static uint64_t last_check = 0;
    last_check = get_timestamp_ms();

    // Store result
    static DetectionResult last_result;
    last_result = result;

    // Log
    if (g_debug_config.log_detection) {
        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "[ANTI-DEBUG] Detection: %u (0x%X) - %s\n",
            static_cast<uint32_t>(result.method),
            result.details,
            result.description ? result.description : "Unknown");
        OutputDebugStringA(buffer);
    }

    if (result.debugger_detected) {
        // Call callback
        if (g_debug_config.callback_on_detection) {
            // Callback is called from another thread context
        }

        // Immediate termination if configured
        if (result.method != DebuggerDetectionMethod::None && g_debug_config.terminate_on_detection_immediate) {
            OMNIGHOST_UNREACHABLE();
        }
    }
}

// =============================================================================
// Platform-Specific Detection (Windows)
// =============================================================================

#if OMNIGHOST_PLATFORM_WINDOWS

// Typedefs for NT APIs
typedef NTSTATUS (NTAPI* NtQueryInformationProcess_t)(
    HANDLE ProcessHandle,
    PROCESSINFOCLASS ProcessInformationClass,
    PVOID ProcessInformation,
    ULONG ProcessInformationLength,
    PULONG ReturnLength
);

typedef NTSTATUS (NTAPI* NtQueryInformationThread_t)(
    HANDLE ThreadHandle,
    THREADINFOCLASS ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength,
    PULONG ReturnLength
);

typedef NTSTATUS (NTAPI* NtSetInformationThread_t)(
    HANDLE ThreadHandle,
    THREADINFOCLASS ThreadInformationClass,
    PVOID ThreadInformation,
    ULONG ThreadInformationLength
);

typedef NTSTATUS (NTAPI* NtCreateThreadEx_t)(
    PHANDLE ThreadHandle,
    ACCESS_MASK DesiredAccess,
    POBJECT_ATTRIBUTES ObjectAttributes,
    HANDLE ProcessHandle,
    PVOID StartRoutine,
    PVOID Argument,
    ULONG CreateFlags,
    SIZE_T ZeroBits,
    SIZE_T StackSize,
    SIZE_T MaximumStackSize,
    PVOID AttributeList
);

typedef NTSTATUS (NTAPI* NtYieldExecution_t)(VOID);

static HMODULE g_ntdll = nullptr;

// Initialize NTDLL function pointers
static void init_ntdll() {
    if (!g_ntdll) {
        g_ntdll = GetModuleHandleA("ntdll.dll");
    }
}

#endif // OMNIGHOST_PLATFORM_WINDOWS

// =============================================================================
// AntiDebug Implementation
// =============================================================================

AntiDebug::AntiDebug() = default;
AntiDebug::~AntiDebug() = default;

bool AntiDebug::initialize(const AntiDebugConfig& config) {
    std::lock_guard<std::mutex> lock(g_debug_mutex);

    if (g_debug_initialized) return true;

    g_debug_config = config;
    g_debug_initialized = true;
    g_detection_count = 0;

    // Initialize NTDLL
    #if OMNIGHOST_PLATFORM_WINDOWS
    init_ntdll();
    #endif

    return true;
}

void AntiDebug::shutdown() {
    std::lock_guard<std::mutex> lock(g_debug_mutex);
    g_debug_initialized = false;
    stop_periodic_check();
}

void AntiDebug::set_config(const AntiDebugConfig& config) {
    std::lock_guard<std::mutex> lock(g_debug_mutex);
    g_debug_config = config;
}

const AntiDebugConfig& AntiDebug::get_config() const {
    return g_debug_config;
}

bool AntiDebug::check_debugger() {
    std::lock_guard<std::mutex> lock(g_debug_mutex);
    if (!g_debug_initialized) return false;
    return run_all_checks();
}

// =============================================================================
// Individual Checks
// =============================================================================

bool AntiDebug::check_is_debugger_present() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    return IsDebuggerPresent() != FALSE;
    #else
    return false;
    #endif
}

bool AntiDebug::check_remote_debugger_present() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    BOOL present = FALSE;
    return CheckRemoteDebuggerPresent(GetCurrentProcess(), &present) && present;
    #else
    return false;
    #endif
}

bool AntiDebug::check_nt_query_debug_port() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    init_ntdll();
    if (!g_ntdll) return false;

    NtQueryInformationProcess_t NtQueryInformationProcess = 
        reinterpret_cast<NtQueryInformationProcess_t>(GetProcAddress(g_ntdll, "NtQueryInformationProcess"));
    if (!NtQueryInformationProcess) return false;

    DWORD debug_port = 0;
    NTSTATUS status = NtQueryInformationProcess(
        GetCurrentProcess(),
        static_cast<PROCESSINFOCLASS>(7), // ProcessDebugPort
        &debug_port,
        sizeof(debug_port),
        nullptr
    );

    return NT_SUCCESS(status) && debug_port != 0;
    #else
    return false;
    #endif
}

bool AntiDebug::check_nt_query_debug_flags() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    init_ntdll();
    if (!g_ntdll) return false;

    NtQueryInformationProcess_t NtQueryInformationProcess = 
        reinterpret_cast<NtQueryInformationProcess_t>(GetProcAddress(g_ntdll, "NtQueryInformationProcess"));
    if (!NtQueryInformationProcess) return false;

    DWORD debug_flags = 0;
    NTSTATUS status = NtQueryInformationProcess(
        GetCurrentProcess(),
        static_cast<PROCESSINFOCLASS>(31), // ProcessDebugFlags
        &debug_flags,
        sizeof(debug_flags),
        nullptr
    );

    // If debug flags is 0, debugger is present
    return NT_SUCCESS(status) && debug_flags == 0;
    #else
    return false;
    #endif
}

bool AntiDebug::check_peb_being_debugged() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    #ifdef _M_X64
    PPEB peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
    #else
    PPEB peb = reinterpret_cast<PPEB>(__readfsdword(0x30));
    #endif
    return peb->BeingDebugged != 0;
    #else
    return false;
    #endif
}

bool AntiDebug::check_peb_nt_global_flag() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    #ifdef _M_X64
    PPEB peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
    #else
    PPEB peb = reinterpret_cast<PPEB>(__readfsdword(0x30));
    #endif
    // FLG_HEAP_ENABLE_TAIL_CHECK | FLG_HEAP_ENABLE_FREE_CHECK | FLG_HEAP_VALIDATE_PARAMETERS
    return (peb->NtGlobalFlag & 0x70) != 0;
    #else
    return false;
    #endif
}

bool AntiDebug::check_peb_heap_flags() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    #ifdef _M_X64
    PPEB peb = reinterpret_cast<PPEB>(__readgsqword(0x60));
    #else
    PPEB peb = reinterpret_cast<PPEB>(__readfsdword(0x30));
    #endif
    // Check heap flags for debug flags
    // 0x40000000 = HEAP_GROWABLE, check for debug flags
    return (peb->HeapFlags & 0x40000000) != 0;
    #else
    return false;
    #endif
}

bool AntiDebug::check_rdtsc_timing() {
    uint64_t start = rdtsc();
    // Small delay
    volatile int dummy = 0;
    for (int i = 0; i < 1000; ++i) {
        dummy += i;
    }
    uint64_t end = rdtsc();

    uint64_t cycles = end - start;
    return cycles > g_debug_config.rdtsc_threshold_cycles;
}

bool AntiDebug::check_hardware_breakpoints() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    CONTEXT ctx = {0};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
    HANDLE thread = GetCurrentThread();

    if (GetThreadContext(thread, &ctx)) {
        // Check DR0-DR3 (hardware breakpoints)
        if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0) {
            return true;
        }
        // Check DR7 (control register)
        if ((ctx.Dr7 & 0xFF) != 0) { // Local breakpoint enable bits
            return true;
        }
    }
    #endif
    return false;
}

bool AntiDebug::check_int3_scan() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    // Scan own memory for 0xCC (INT3)
    MEMORY_BASIC_INFORMATION mbi;
    uintptr_t addr = 0;
    while (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        if (mbi.State == MEM_COMMIT && (mbi.Protect & PAGE_EXECUTE_READ) ||
            (mbi.Protect & PAGE_EXECUTE_READWRITE) ||
            (mbi.Protect & PAGE_EXECUTE_WRITECOPY)) {
            // Scan for 0xCC
            const unsigned char* ptr = static_cast<const unsigned char*>(mbi.BaseAddress);
            size_t size = mbi.RegionSize;
            for (size_t i = 0; i < size; ++i) {
                if (ptr[i] == 0xCC) {
                    return true;
                }
            }
        }
        addr += mbi.RegionSize;
    }
    #endif
    return false;
}

bool AntiDebug::check_software_breakpoints() {
    return check_int3_scan();
}

bool AntiDebug::check_vectored_exception_handler() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    // Check for unexpected VEH
    // This is heuristic - legitimate apps may use VEH
    // We check if there are handlers we didn't install
    return false; // Placeholder
    #else
    return false;
    #endif
}

bool AntiDebug::check_unhandled_exception_filter() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    // Check if UEF has been replaced
    LPTOP_LEVEL_EXCEPTION_FILTER filter = SetUnhandledExceptionFilter(nullptr);
    if (filter) {
        SetUnhandledExceptionFilter(filter);
        // If filter is not our default, might be debugger
        return filter != nullptr;
    }
    #endif
    return false;
}

bool AntiDebug::check_hardware_breakpoints_context() {
    return check_hardware_breakpoints();
}

bool AntiDebug::run_all_checks() {
    if (!g_debug_initialized) return false;

    uint32_t methods = g_debug_config.enabled_methods;
    bool detected = false;

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::IsDebuggerPresent)) {
        if (check_is_debugger_present()) {
            DetectionResult result = {true, DebuggerDetectionMethod::IsDebuggerPresent, 0, 
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count(),
                    "IsDebuggerPresent() returned true"};
            report_detection({true, DebuggerDetectionMethod::IsDebuggerPresent, 0, 
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count(),
                    "IsDebuggerPresent() returned true"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::CheckRemoteDebuggerPresent)) {
        if (check_remote_debugger_present()) {
            DetectionResult result = {true, DebuggerDetectionMethod::CheckRemoteDebuggerPresent, 0,
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count(),
                    "CheckRemoteDebuggerPresent returned true"};
            report_detection(result);
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::NtQueryInformationProcess_DebugPort)) {
        if (check_nt_query_debug_port()) {
            report_detection({true, DebuggerDetectionMethod::NtQueryInformationProcess_DebugPort, 0,
                get_timestamp_ms(), "NtQueryInformationProcess(DebugPort) detected debugger"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::NtQueryInformationProcess_DebugFlags)) {
        if (check_nt_query_debug_flags()) {
            report_detection({true, DebuggerDetectionMethod::NtQueryInformationProcess_DebugFlags, 0,
                get_timestamp_ms(), "NtQueryInformationProcess(DebugFlags) detected debugger"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::PEB_BeingDebugged)) {
        if (check_peb_being_debugged()) {
            report_detection({true, DebuggerDetectionMethod::PEB_BeingDebugged, 0,
                get_timestamp_ms(), "PEB.BeingDebugged != 0"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::PEB_NtGlobalFlag)) {
        if (check_peb_nt_global_flag()) {
            report_detection({true, DebuggerDetectionMethod::PEB_NtGlobalFlag, 0,
                get_timestamp_ms(), "PEB.NtGlobalFlag has debug flags"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::PEB_HeapFlags)) {
        if (check_peb_heap_flags()) {
            report_detection({true, DebuggerDetectionMethod::PEB_HeapFlags, 0,
                get_timestamp_ms(), "PEB.HeapFlags has debug flags"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::RDTSC_Timing)) {
        if (check_rdtsc_timing()) {
            report_detection({true, DebuggerDetectionMethod::RDTSC_Timing, 0,
                get_timestamp_ms(), "RDTSC timing anomaly detected"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::HardwareBreakpoints_DR)) {
        if (check_hardware_breakpoints()) {
            report_detection({true, DebuggerDetectionMethod::HardwareBreakpoints_DR, 0,
                get_timestamp_ms(), "Hardware breakpoints (DR0-DR3) detected"});
            detected = true;
        }
    }

    if (methods & static_cast<uint32_t>(DebuggerDetectionMethod::INT3_SCAN)) {
        if (check_int3_scan()) {
            report_detection({true, DebuggerDetectionMethod::INT3_SCAN, 0,
                get_timestamp_ms(), "INT3 (0xCC) software breakpoint detected"});
            detected = true;
        }
    }

    // Additional checks can be added here

    return !detected; // Return true if no debugger detected
}

void AntiDebug::report_detection(const DetectionResult& result) {
    report_detection(result);
}

void AntiDebug::set_detection_callback(DetectionCallback cb) {
    std::lock_guard<std::mutex> lock(g_debug_mutex);
    g_detection_callback = cb;
}

bool AntiDebug::is_initialized() const {
    return g_debug_initialized;
}

uint32_t AntiDebug::get_detection_count() const {
    static std::atomic<uint32_t> count = 0;
    return count;
}

uint64_t AntiDebug::get_last_check_time() const {
    return g_last_check_time;
}

void AntiDebug::force_check() {
    run_all_checks();
}

void AntiDebug::start_periodic_check() {
    if (g_periodic_running.load()) return;

    g_periodic_running.store(true);
    g_periodic_thread = std::thread([this]() {
        while (g_periodic_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(g_debug_config.scan_interval_ms));
            if (g_periodic_running.load()) {
                run_all_checks();
            }
        }
    });
}

void AntiDebug::stop_periodic_check() {
    g_periodic_running.store(false);
    if (g_periodic_thread.joinable()) {
        g_periodic_thread.join();
    }
}

bool AntiDebug::run_all_checks() {
    if (!g_debug_initialized) return false;
    return !check_debugger(); // Return true if no debugger
}

void AntiDebug::report_detection(const DetectionResult& result) {
    ::report_detection(result);
}

// =============================================================================
// Platform-Specific Checks (Windows)
// =============================================================================

#if OMNIGHOST_PLATFORM_WINDOWS

bool AntiDebug::check_nt_query_debug_object_handle() {
    init_ntdll();
    if (!g_ntdll) return false;

    NtQueryInformationProcess_t NtQueryInformationProcess = 
        reinterpret_cast<NtQueryInformationProcess_t>(GetProcAddress(g_ntdll, "NtQueryInformationProcess"));
    if (!NtQueryInformationProcess) return false;

    HANDLE debug_object = nullptr;
    NTSTATUS status = NtQueryInformationProcess(
        GetCurrentProcess(),
        static_cast<PROCESSINFOCLASS>(30), // ProcessDebugObjectHandle
        &debug_object,
        sizeof(debug_object),
        nullptr
    );

    return NT_SUCCESS(status) && debug_object != nullptr;
}

bool AntiDebug::check_thread_hide_from_debugger() {
    init_ntdll();
    if (!g_ntdll) return false;

    NtSetInformationThread_t NtSetInformationThread = 
        reinterpret_cast<NtSetInformationThread_t>(GetProcAddress(g_ntdll, "NtSetInformationThread"));
    if (!NtSetInformationThread) return false;

    // Try to hide thread - if it succeeds, we're being debugged
    NTSTATUS status = NtSetInformationThread(
        GetCurrentThread(),
        static_cast<THREADINFOCLASS>(0x11), // ThreadHideFromDebugger
        nullptr, 0
    );

    return NT_SUCCESS(status);
}

bool AntiDebug::check_process_debug_object() {
    // Check if process has debug object
    return false; // Placeholder
}

bool AntiDebug::check_output_debug_string() {
    // Check if OutputDebugString is hooked
    return false; // Placeholder
}

bool AntiDebug::check_block_input() {
    // Check if BlockInput is hooked
    return false; // Placeholder
}

#endif // OMNIGHOST_PLATFORM_WINDOWS

// =============================================================================
// Anti-Dump Implementation
// =============================================================================

AntiDump::AntiDump() = default;
AntiDump::~AntiDump() = default;

bool AntiDump::initialize() {
    std::lock_guard<std::mutex> lock(g_dump_mutex);
    if (g_dump_initialized) return true;
    g_dump_initialized = true;
    return protect_process_memory();
}

void AntiDump::shutdown() {
    std::lock_guard<std::mutex> lock(g_dump_mutex);
    g_dump_initialized = false;
}

bool AntiDump::protect_process_memory() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    // Mark critical sections as non-dumpable
    // This is a placeholder - real implementation would use VirtualProtect
    // with PAGE_NOCACHE or similar
    return true;
    #else
    return true;
    #endif
}

bool AntiDump::protect_critical_sections() {
    // Protect critical code sections
    return true;
}

bool AntiDump::corrupt_on_dump_attempt() {
    // Corrupt critical data if dump detected
    return true;
}

void AntiDump::set_dump_callback(DumpCallback cb) {
    std::lock_guard<std::mutex> lock(g_dump_mutex);
    g_dump_callback = cb;
}

// =============================================================================
// Global Functions
// =============================================================================

bool check_debugger_present() {
    static AntiDebug& debug = AntiDebug::instance();
    return !debug.check_debugger(); // Returns true if debugger detected
}

void trigger_debugger_detection(DebuggerDetectionMethod method) {
    DetectionResult result = {true, method, 0, 
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count(),
        "Manual trigger"};
    report_detection(result);
}

void install_vectored_exception_handler() {
    // Install custom VEH for anti-debug
}

void remove_vectored_exception_handler() {
    // Remove custom VEH
}

} // namespace omnighost::security