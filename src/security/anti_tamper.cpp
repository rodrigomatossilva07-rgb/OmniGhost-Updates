// Anti-Tamper Implementation
// Runtime integrity verification of code sections and critical data

#include "anti_tamper.h"
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>
#include <mutex>

#if OMNIGHOST_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <intrin.h>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "version.lib")
#endif

namespace omnighost::security {

// =============================================================================
// Static Data
// =============================================================================

static std::mutex g_tamper_mutex;
static std::vector<ProtectedRegion> g_regions;
static AntiTamperConfig g_config;
static IntegrityViolationCallback g_violation_callback = nullptr;
static bool g_initialized = false;
static std::vector<uint8_t> g_original_text_section;
static std::vector<uint8_t> g_original_imports;
static std::vector<std::string> g_original_modules;
static uint64_t g_last_check_time = 0;
static uint32_t g_violation_count = 0;
static IntegrityReport g_last_report = {IntegrityStatus::Valid, 0, 0, 0, nullptr, 0};

// =============================================================================
// CRC32 Implementation
// =============================================================================

uint32_t CRC32::compute(const void* data, size_t length, uint32_t crc) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = ~crc;

    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (static_cast<uint32_t>(-(crc & 1)) & POLYNOMIAL);
        }
    }

    return ~crc;
}

// =============================================================================
// Platform-Specific Helpers
// =============================================================================

#if OMNIGHOST_PLATFORM_WINDOWS

static bool get_module_bounds(const char* module_name, uintptr_t& base, size_t& size) {
    HMODULE module = GetModuleHandleA(module_name);
    if (!module) return false;

    MODULEINFO info;
    if (!GetModuleInformation(GetCurrentProcess(), module, &info, sizeof(info))) {
        return false;
    }

    base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
    size = info.SizeOfImage;
    return true;
}

static bool get_section_bounds(const char* section_name, uintptr_t& base, size_t& size) {
    HMODULE module = GetModuleHandle(nullptr);
    if (!module) return false;

    PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uintptr_t>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        char name[9] = {0};
        memcpy(name, section->Name, 8);
        if (strcmp(name, section_name) == 0) {
            base = reinterpret_cast<uintptr_t>(module) + section->VirtualAddress;
            size = section->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

static bool get_import_table(uintptr_t& base, size_t& size) {
    HMODULE module = GetModuleHandle(nullptr);
    if (!module) return false;

    PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(module);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    PIMAGE_NT_HEADERS nt = reinterpret_cast<PIMAGE_NT_HEADERS>(reinterpret_cast<uintptr_t>(module) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    PIMAGE_DATA_DIRECTORY import_dir = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_dir->VirtualAddress == 0 || import_dir->Size == 0) return false;

    base = reinterpret_cast<uintptr_t>(module) + import_dir->VirtualAddress;
    size = import_dir->Size;
    return true;
}

static std::vector<std::string> get_loaded_modules() {
    std::vector<std::string> modules;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE) return modules;

    MODULEENTRY32 me = { sizeof(MODULEENTRY32) };
    if (Module32First(snapshot, &me)) {
        do {
            modules.emplace_back(me.szModule);
        } while (Module32Next(snapshot, &me));
    }
    CloseHandle(snapshot);
    return modules;
}

static uintptr_t get_text_section_base() {
    uintptr_t base = 0, size = 0;
    get_section_bounds(".text", base, size);
    return base;
}

static size_t get_text_section_size() {
    uintptr_t base = 0, size = 0;
    get_section_bounds(".text", base, size);
    return size;
}

#endif // OMNIGHOST_PLATFORM_WINDOWS

// =============================================================================
// Integrity Report
// =============================================================================

static void report_violation(const IntegrityReport& report) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    g_violation_count++;
    g_last_report = report;

    if (g_config.log_violation) {
        // Log to debug output
        char buffer[512];
        snprintf(buffer, sizeof(buffer),
            "[ANTI-TAMPER] Violation: %s at 0x%p (expected: 0x%08X, actual: 0x%08X)\n",
            report.violation_type ? report.violation_type : "Unknown",
            reinterpret_cast<void*>(report.violation_address),
            static_cast<uint32_t>(report.expected_crc),
            static_cast<uint32_t>(report.actual_crc));
        OutputDebugStringA(buffer);
    }

    if (g_config.callback_on_violation && g_violation_callback) {
        g_violation_callback(report);
    }

    if (g_config.terminate_on_violation) {
        OMNIGHOST_UNREACHABLE();
    }
}

// =============================================================================
// Region CRC Computation
// =============================================================================

static uint32_t compute_region_crc(const ProtectedRegion& region) {
    return CRC32::compute(reinterpret_cast<const void*>(region.start_address), region.size);
}

static bool verify_region_crc(const ProtectedRegion& region) {
    uint32_t actual_crc = compute_region_crc(region);
    if (actual_crc != region.expected_crc) {
        IntegrityReport report = {
            IntegrityStatus::Modified,
            region.expected_crc,
            actual_crc,
            region.start_address,
            "Region CRC mismatch",
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())
        };
        report_violation(report);
        return false;
    }
    return true;
}

// =============================================================================
// AntiTamper Class Implementation
// =============================================================================

AntiTamper::AntiTamper() = default;
AntiTamper::~AntiTamper() = default;

bool AntiTamper::initialize(const AntiTamperConfig& config) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);

    if (g_initialized) return true;

    g_config = config;
    g_initialized = true;
    g_violation_count = 0;
    g_last_check_time = 0;

    // Capture original text section
    #if OMNIGHOST_PLATFORM_WINDOWS
    uintptr_t text_base = get_text_section_base();
    size_t text_size = get_text_section_size();
    if (text_base && text_size > 0) {
        g_original_text_section.resize(text_size);
        memcpy(g_original_text_section.data(), reinterpret_cast<void*>(text_base), text_size);
    }

    // Capture imports
    uintptr_t import_base = 0, import_size = 0;
    if (get_import_table(import_base, import_size)) {
        g_original_imports.resize(import_size);
        memcpy(g_original_imports.data(), reinterpret_cast<void*>(import_base), import_size);
    }

    // Capture loaded modules
    g_original_modules = get_loaded_modules();
    #endif

    // Scan and register default regions
    scan_memory_regions();

    return true;
}

void AntiTamper::shutdown() {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    g_initialized = false;
    g_regions.clear();
    g_original_text_section.clear();
    g_original_imports.clear();
    g_original_modules.clear();
}

void AntiTamper::set_config(const AntiTamperConfig& config) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    g_config = config;
}

const AntiTamperConfig& AntiTamper::get_config() const {
    return g_config;
}

bool AntiTamper::add_protected_region(const ProtectedRegion& region) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);

    // Compute initial CRC
    ProtectedRegion new_region = region;
    new_region.expected_crc = compute_region_crc(region);

    // Check for duplicate
    auto it = std::find_if(g_regions.begin(), g_regions.end(),
        [&region](const ProtectedRegion& r) { return strcmp(r.name, region.name) == 0; });

    if (it != g_regions.end()) {
        *it = new_region;
    } else {
        g_regions.push_back(new_region);
    }
    return true;
}

bool AntiTamper::remove_protected_region(const char* name) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    auto it = std::find_if(g_regions.begin(), g_regions.end(),
        [name](const ProtectedRegion& r) { return strcmp(r.name, name) == 0; });
    if (it != g_regions.end()) {
        g_regions.erase(it);
        return true;
    }
    return false;
}

bool AntiTamper::update_region_crc(const char* name) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    auto it = std::find_if(g_regions.begin(), g_regions.end(),
        [name](const ProtectedRegion& r) { return strcmp(r.name, name) == 0; });
    if (it != g_regions.end()) {
        it->expected_crc = compute_region_crc(*it);
        return true;
    }
    return false;
}

bool AntiTamper::verify_all_regions() {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);

    bool all_valid = true;
    for (const auto& region : g_regions) {
        if (!verify_region_crc(region)) {
            all_valid = false;
            if (g_config.terminate_on_violation) return false;
        }
    }
    return all_valid;
}

bool AntiTamper::verify_region(const char* name) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    auto it = std::find_if(g_regions.begin(), g_regions.end(),
        [name](const ProtectedRegion& r) { return strcmp(r.name, name) == 0; });
    if (it != g_regions.end()) {
        return verify_region_crc(*it);
    }
    return false;
}

bool AntiTamper::verify_region(const ProtectedRegion& region) {
    return verify_region_crc(region);
}

bool AntiTamper::full_integrity_check() {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);

    bool all_valid = true;

    // Verify all registered regions
    all_valid &= verify_all_regions();

    // Verify text section
    #if OMNIGHOST_PLATFORM_WINDOWS
    if (g_config.protect_text_section) {
        uintptr_t text_base = get_text_section_base();
        size_t text_size = get_text_section_size();
        if (text_base && text_size > 0 && text_size == g_original_text_section.size()) {
            uint32_t current_crc = CRC32::compute(reinterpret_cast<void*>(text_base), text_size);
            uint32_t original_crc = CRC32::compute(g_original_text_section.data(), g_original_text_section.size());
            if (current_crc != original_crc) {
                IntegrityReport report = {
                    IntegrityStatus::Modified,
                    original_crc,
                    current_crc,
                    text_base,
                    "Text section modified",
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count())
                };
                report_violation(report);
                all_valid = false;
            }
        }
    }

    // Verify imports
    if (g_config.verify_imports) {
        uintptr_t import_base = 0, import_size = 0;
        if (get_import_table(import_base, import_size) && import_size == g_original_imports.size()) {
            uint32_t current_crc = CRC32::compute(reinterpret_cast<void*>(import_base), import_size);
            uint32_t original_crc = CRC32::compute(g_original_imports.data(), g_original_imports.size());
            if (current_crc != original_crc) {
                IntegrityReport report = {
                    IntegrityStatus::Modified,
                    original_crc,
                    current_crc,
                    import_base,
                    "Import table modified",
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count())
                };
                report_violation(report);
                all_valid = false;
            }
        }
    }

    // Verify loaded modules
    if (g_config.check_module_list) {
        auto current_modules = get_loaded_modules();
        if (current_modules.size() != g_original_modules.size()) {
            IntegrityReport report = {
                IntegrityStatus::Modified,
                static_cast<uint64_t>(g_original_modules.size()),
                static_cast<uint64_t>(current_modules.size()),
                0,
                "Module list changed",
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count())
            };
            report_violation(report);
            all_valid = false;
        } else {
            for (size_t i = 0; i < g_original_modules.size(); ++i) {
                if (g_original_modules[i] != current_modules[i]) {
                    IntegrityReport report = {
                        IntegrityStatus::Modified,
                        0, 0, 0,
                        "Module list order changed",
                        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count())
                    };
                    report_violation(report);
                    all_valid = false;
                    break;
                }
            }
        }
    }

    // Verify heap integrity
    #if OMNIGHOST_PLATFORM_WINDOWS
    if (g_config.protect_heap) {
        if (!HeapValidate(GetProcessHeap(), 0, nullptr)) {
            IntegrityReport report = {
                IntegrityStatus::MemoryCorrupted,
                0, 0, 0,
                "Heap corruption detected",
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count())
            };
            report_violation(report);
            all_valid = false;
        }
    }
    #endif
    #endif

    return true;
}

void AntiTamper::set_violation_callback(IntegrityViolationCallback cb) {
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    g_violation_callback = cb;
}

bool AntiTamper::is_initialized() const {
    return g_initialized;
}

size_t AntiTamper::get_region_count() const {
    return g_regions.size();
}

uint64_t AntiTamper::get_last_check_time() const {
    return g_last_check_time;
}

uint32_t AntiTamper::get_violation_count() const {
    return g_violation_count;
}

void AntiTamper::force_check() {
    full_integrity_check();
}

bool AntiTamper::scan_memory_regions() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    // Register default critical regions
    ProtectedRegion text_region = {".text", 0, 0, 0, ProtectedRegion::FLAG_EXECUTABLE | ProtectedRegion::FLAG_READABLE | ProtectedRegion::FLAG_CRITICAL};
    if (get_section_bounds(".text", text_region.start_address, text_region.size)) {
        text_region.expected_crc = compute_region_crc(text_region);
        g_regions.push_back(text_region);
    }

    ProtectedRegion rdata_region = {".rdata", 0, 0, 0, ProtectedRegion::FLAG_READABLE | ProtectedRegion::FLAG_CRITICAL};
    if (get_section_bounds(".rdata", rdata_region.start_address, rdata_region.size)) {
        rdata_region.expected_crc = compute_region_crc(rdata_region);
        g_regions.push_back(rdata_region);
    }

    // Import table
    uintptr_t import_base = 0, import_size = 0;
    if (get_import_table(import_base, import_size)) {
        ProtectedRegion import_region = {".idata", import_base, import_size, 0, ProtectedRegion::FLAG_READABLE | ProtectedRegion::FLAG_CRITICAL};
        import_region.expected_crc = compute_region_crc(import_region);
        g_regions.push_back(import_region);
    }
    #endif
    return true;
}

bool AntiTamper::verify_text_section() {
    return true; // Handled in full_integrity_check
}

bool AntiTamper::verify_imports() {
    return true; // Handled in full_integrity_check
}

bool AntiTamper::verify_modules() {
    return true; // Handled in full_integrity_check
}

bool AntiTamper::verify_heap_integrity() {
    #if OMNIGHOST_PLATFORM_WINDOWS
    return HeapValidate(GetProcessHeap(), 0, nullptr);
    #else
    return true;
    #endif
}

void AntiTamper::report_violation(const IntegrityReport& report) {
    ::report_violation(report);
}

// =============================================================================
// Global Functions
// =============================================================================

bool quick_integrity_check() {
    static AntiTamper& tamper = AntiTamper::instance();
    return tamper.verify_all_regions();
}

bool full_integrity_check() {
    static AntiTamper& tamper = AntiTamper::instance();
    return tamper.full_integrity_check();
}

bool register_protected_region(const ProtectedRegion& region) {
    static AntiTamper& tamper = AntiTamper::instance();
    return tamper.add_protected_region(region);
}

bool verify_region(const char* name) {
    static AntiTamper& tamper = AntiTamper::instance();
    return tamper.verify_region(name);
}

const IntegrityReport* get_last_report() {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(g_tamper_mutex);
    return &g_last_report;
}

} // namespace omnighost::security