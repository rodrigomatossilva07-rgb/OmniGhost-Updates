#include "anti_tamper.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <intrin.h>
#include <bcrypt.h>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <chrono>
#include <filesystem>

#pragma comment(lib, "bcrypt.lib")

namespace OmniGhost::Security {

// =============================================================================
// MemoryGuard Implementation
// =============================================================================

MemoryGuard::MemoryGuard(void* address, std::size_t size, DWORD protect)
    : address_(address), size_(size), originalProtect_(0), protected_(false) {
    if (address && size > 0) {
        Protect(address, size, protect);
    }
}

MemoryGuard::~MemoryGuard() {
    Unprotect();
}

MemoryGuard::MemoryGuard(MemoryGuard&& other) noexcept
    : address_(other.address_), size_(other.size_),
      originalProtect_(other.originalProtect_), protected_(other.protected_) {
    other.address_ = nullptr;
    other.size_ = 0;
    other.originalProtect_ = 0;
    other.protected_ = false;
}

MemoryGuard& MemoryGuard::operator=(MemoryGuard&& other) noexcept {
    if (this != &other) {
        Unprotect();
        address_ = other.address_;
        size_ = other.size_;
        originalProtect_ = other.originalProtect_;
        protected_ = other.protected_;
        other.address_ = nullptr;
        other.size_ = 0;
        other.originalProtect_ = 0;
        other.protected_ = false;
    }
    return *this;
}

bool MemoryGuard::Protect(void* address, std::size_t size, DWORD protect) {
    if (protected_) {
        Unprotect();
    }

    DWORD oldProtect = 0;
    if (!VirtualProtect(address, size, protect, &oldProtect)) {
        return false;
    }

    address_ = address;
    size_ = size;
    originalProtect_ = oldProtect;
    protected_ = true;
    return true;
}

void MemoryGuard::Unprotect() {
    if (protected_ && address_ && size_ > 0 && originalProtect_ != 0) {
        DWORD dummy = 0;
        VirtualProtect(address_, size_, originalProtect_, &dummy);
        protected_ = false;
    }
}

// =============================================================================
// MemoryGuardRegistry Implementation
// =============================================================================

std::size_t MemoryGuardRegistry::RegisterRegion(void* address, std::size_t size,
                                                DWORD protect, std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);

    MemoryGuard guard;
    if (!guard.Protect(address, size, protect)) {
        return static_cast<std::size_t>(-1);
    }

    const std::size_t id = guards_.size();
    guards_.push_back(std::move(guard));
    return id;
}

bool MemoryGuardRegistry::UnregisterRegion(std::size_t guardId) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (guardId >= guards_.size()) {
        return false;
    }

    guards_[guardId].Unprotect();
    // Mark as invalid by setting address to null
    guards_[guardId] = MemoryGuard();
    return true;
}

bool MemoryGuardRegistry::VerifyAll() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!protectionEnabled_.load()) {
        return true;
    }

    for (const auto& guard : guards_) {
        if (guard.IsProtected()) {
            // Verify protection is still in place
            MEMORY_BASIC_INFORMATION mbi{};
            if (VirtualQuery(guard.Address(), &mbi, sizeof(mbi)) == sizeof(mbi)) {
                if ((mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_READONLY)) == 0) {
                    // Protection may have been changed
                    return false;
                }
            }
        }
    }
    return true;
}

bool MemoryGuardRegistry::VerifyRegion(std::size_t guardId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (guardId >= guards_.size()) {
        return false;
    }

    const auto& guard = guards_[guardId];
    if (!guard.IsProtected()) {
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(guard.Address(), &mbi, sizeof(mbi)) != sizeof(mbi)) {
        return false;
    }

    return (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_READONLY)) != 0;
}

std::vector<MemoryGuardRegistry::RegionInfo> MemoryGuardRegistry::GetAllRegions() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<RegionInfo> result;
    result.reserve(guards_.size());

    for (std::size_t i = 0; i < guards_.size(); ++i) {
        const auto& guard = guards_[i];
        RegionInfo info;
        info.id = i;
        info.address = guard.Address();
        info.size = guard.Size();
        info.protection = guard.OriginalProtection();
        info.valid = guard.IsProtected();
        result.push_back(info);
    }

    return result;
}

void MemoryGuardRegistry::SetProtectionEnabled(bool enabled) {
    protectionEnabled_.store(enabled);
}

// =============================================================================
// TimingCheck Implementation
// =============================================================================

bool TimingCheck::CheckRDTSC(uint64_t thresholdCycles) {
    // Serialize with CPUID
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);

    uint64_t start = __rdtsc();

    // Small workload
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }
    __cpuid(cpuInfo, 0);  // Serialize

    uint64_t end = __rdtsc();
    uint64_t delta = end - start;

    return delta > thresholdCycles;
}

bool TimingCheck::CheckQPC(uint64_t thresholdMicroseconds) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    LARGE_INTEGER start, end;
    QueryPerformanceCounter(&start);

    // Small workload
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) {
        sum += i;
    }

    QueryPerformanceCounter(&end);

    uint64_t delta = static_cast<uint64_t>((end.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart);
    return delta > thresholdMicroseconds;
}

bool TimingCheck::CheckDebuggerPresent() {
    // Check IsDebuggerPresent
    if (IsDebuggerPresent()) {
        return true;
    }

    // Check NtGlobalFlag (PEB)
    // This is more advanced - check for debug flags in PEB
    return false;
}

bool TimingCheck::CheckHardwareBreakpoints() {
    // Check for hardware breakpoints via GetThreadContext
    // This requires thread context access
    CONTEXT ctx = {};
    ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;

    HANDLE thread = GetCurrentThread();
    if (GetThreadContext(thread, &ctx)) {
        // DR0-DR3 contain hardware breakpoint addresses
        // DR7 contains control register
        if (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0) {
            if ((ctx.Dr7 & 0xFF) != 0) {  // Any breakpoint enabled
                return true;
            }
        }
    }
    return false;
}

bool TimingCheck::RunAllChecks(uint64_t rdtscThreshold, uint64_t qpcThreshold) {
    if (CheckRDTSC(rdtscThreshold)) return true;
    if (CheckQPC(qpcThreshold)) return true;
    if (CheckDebuggerPresent()) return true;
    if (CheckHardwareBreakpoints()) return true;
    return false;
}

TimingCheck::TimingStats TimingCheck::GetTimingStats() {
    TimingStats stats;

    // RDTSC
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);
    uint64_t start = __rdtsc();
    volatile int sum = 0;
    for (int i = 0; i < 1000; ++i) sum += i;
    __cpuid(cpuInfo, 0);
    uint64_t end = __rdtsc();
    stats.rdtscDelta = end - start;

    // QPC
    LARGE_INTEGER freq, qpcStart, qpcEnd;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&qpcStart);
    sum = 0;
    for (int i = 0; i < 1000; ++i) sum += i;
    QueryPerformanceCounter(&qpcEnd);
    stats.qpcDelta = static_cast<uint64_t>((qpcEnd.QuadPart - qpcStart.QuadPart) * 1000000 / freq.QuadPart);

    stats.debuggerDetected = RunAllChecks();
    stats.details = "RDTSC: " + std::to_string(stats.rdtscDelta) + " cycles, QPC: " +
                    std::to_string(stats.qpcDelta) + " us";

    return stats;
}

// =============================================================================
// ModuleIntegrity Implementation
// =============================================================================

void ModuleIntegrity::AddModule(std::string_view moduleName,
                                std::uintptr_t baseAddress,
                                std::size_t size,
                                std::string_view expectedSHA256) {
    std::lock_guard<std::mutex> lock(mutex_);

    ModuleIntegrityInfo info;
    info.moduleName = moduleName;
    info.baseAddress = baseAddress;
    info.size = size;
    info.expectedSHA256 = expectedSHA256;
    modules_.push_back(info);
}

void ModuleIntegrity::AddModuleByName(std::string_view moduleName, std::string_view expectedSHA256) {
    auto modInfo = GetModuleInfo(moduleName);
    if (modInfo) {
        AddModule(moduleName, modInfo->baseAddress, modInfo->size, expectedSHA256);
    }
}

bool ModuleIntegrity::VerifyAll(std::vector<ModuleIntegrityInfo>& results) {
    std::lock_guard<std::mutex> lock(mutex_);

    results.clear();
    results.reserve(modules_.size());

    bool allVerified = true;
    for (auto& module : modules_) {
        ModuleIntegrityInfo result;
        if (VerifyModule(module.moduleName, result)) {
            result.verified = true;
        } else {
            result.verified = false;
            allVerified = false;
        }
        results.push_back(result);
    }

    return allVerified;
}

bool ModuleIntegrity::VerifyModule(std::string_view moduleName, ModuleIntegrityInfo& result) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = std::find_if(modules_.begin(), modules_.end(),
                           [&](const auto& m) { return m.moduleName == moduleName; });

    if (it == modules_.end()) {
        result.error = "Module not registered";
        return false;
    }

    const auto& module = *it;
    result.moduleName = module.moduleName;
    result.baseAddress = module.baseAddress;
    result.size = module.size;
    result.expectedSHA256 = module.expectedSHA256;

    // Calculate actual SHA256
    if (!CalculateSHA256(module.baseAddress, module.size, result.actualSHA256)) {
        result.error = "Failed to calculate SHA256";
        return false;
    }

    // Calculate CRC32
    result.crc32 = CalculateCRC32(module.baseAddress, module.size);

    // Compare with expected if provided
    if (!module.expectedSHA256.empty()) {
        if (!Platform::ConstantTimeEquals(module.expectedSHA256, result.actualSHA256)) {
            result.error = "SHA256 mismatch";
            return false;
        }
    }

    result.verified = true;
    return true;
}

bool ModuleIntegrity::CalculateSHA256(std::uintptr_t address, std::size_t size, std::string& hash) {
    if (address == 0 || size == 0) {
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;

    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        return false;
    }

    auto cleanup = [&]() {
        if (hashHandle) BCryptDestroyHash(hashHandle);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    };

    DWORD objectLength = 0, resultLength = 0, hashLength = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) < 0 ||
        objectLength == 0 || hashLength == 0) {
        cleanup();
        return false;
    }

    std::vector<unsigned char> object(objectLength);
    std::vector<unsigned char> digest(hashLength);

    if (BCryptCreateHash(algorithm, &hashHandle, object.data(), objectLength, nullptr, 0, 0) < 0) {
        cleanup();
        return false;
    }

    // Hash the memory region in chunks
    const std::size_t chunkSize = 64 * 1024;
    std::uintptr_t current = address;
    std::size_t remaining = size;

    while (remaining > 0) {
        const std::size_t toHash = (std::min)(chunkSize, remaining);
        if (BCryptHashData(hashHandle,
                reinterpret_cast<PUCHAR>(current), static_cast<ULONG>(toHash), 0) < 0) {
            cleanup();
            return false;
        }
        current += toHash;
        remaining -= toHash;
    }

    if (BCryptFinishHash(hashHandle, digest.data(), static_cast<ULONG>(hashLength), 0) < 0) {
        cleanup();
        return false;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        out << std::setw(2) << static_cast<int>(byte);
    }
    hash = out.str();

    cleanup();
    return true;
}

uint32_t ModuleIntegrity::CalculateCRC32(std::uintptr_t address, std::size_t size) {
    // Simple CRC32 implementation (IEEE 802.3 polynomial)
    static const uint32_t crcTable[256] = {
        0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
        0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
        0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
        0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
        0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
        0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
        0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06112, 0x25B4339F, 0x52B72307, 0xCBBE92BD, 0xBCB7822B,
        0x224D5354, 0x554A63C2, 0xCC431278, 0xBC4422EE, 0x2C8E1F9B, 0x5B8D2F03, 0xC2847EB9, 0xB5874E2F,
        0x56D8D758, 0x21DBA7CE, 0xB8D2F674, 0xCFD5C6E2, 0x5497D574, 0x2394E5E6, 0xBADF745C, 0xCDDC44CA,
        0x611E37BC, 0x1619072A, 0x8F105690, 0xF8172602, 0x649C4D5D, 0x139B7DCB, 0x8A922C75, 0xF1951CE3,
        0x6B727E4E, 0x1C754ED6, 0x857CFD4C, 0xF27B8DD4, 0x6D3ED10B, 0x1A39E19D, 0x83307027, 0xF43700B1,
        0x68D20F2A, 0x1FD53FB2, 0x86DCAE08, 0xFBDF9E9E, 0x654B6B07, 0x124C5B9F, 0x8B45E825, 0xFC42D8B3,
        0x6F0B79DE, 0x180C4948, 0x810518F2, 0xF6022864, 0x69613BDF, 0x1E660B4B, 0x876FBAF1, 0xF06ACA67,
        0x4E891A98, 0x398E2A0E, 0xA0857BB4, 0xD7824B22, 0x41CE3473, 0x36C904E5, 0xAFC1955F, 0xD8C6A5C9,
        0x49073C5C, 0x3E040CEE, 0xA70F5F54, 0xD0086FC6, 0x4E6D9D0D, 0x396AE99B, 0xA0633821, 0xD76008B7,
        0x400491E0, 0x3707A176, 0xAE0EF0CC, 0xD90DC05A, 0x436B52AF, 0x346C6239, 0xAD653383, 0xDA620315,
        0x4C05829E, 0x3B02B20A, 0xA20B23B0, 0xD50C1322, 0x4F61C6BB, 0x3866F62D, 0xA16F6797, 0xD66C5701,
        0x576E4D90, 0x20693D06, 0xB9608CBC, 0xCE67BC2A, 0x5A625B4D, 0x2D656BDF, 0xB46C3A65, 0xC36B0AFF,
        0x5C2F7688, 0x2B28461E, 0xB221D7A4, 0xC526E732, 0x50E73A9B, 0x27E00A0D, 0xBEE75BB7, 0xC6E06B2F,
        0x5505D6B6, 0x2202E620, 0xBB0B57B6, 0xCC0C672E, 0x580FF355, 0x2F0CC3C3, 0xB6059259, 0xC106A2CF,
        0x5FCF1164, 0x28C821F6, 0xB1C1B06C, 0xC6C680FA, 0x53C2D03D, 0x24C5E0AB, 0xBDCE9131, 0xCACC81A7,
        0x528D9A1A, 0x258AAAC2, 0xBC833B78, 0xCB840BE6, 0x5E815E1F, 0x29866E87, 0xB08FFD33, 0xC788CDE5,
        0x7A81E49A, 0x0D86D40C, 0x948F65B6, 0xE3885520, 0x7BEE6E51, 0x0CEF5EC9, 0x95E6CF73, 0xE2E1FFEB,
        0x7F0C4F74, 0x080B7FEE, 0x9102E654, 0xE605F6C2, 0x7968313F, 0x0E6F41A7, 0x9769D51D, 0xE06EC58B,
        0x776B105C, 0x006C20CA, 0x99655D70, 0xEE626DE6, 0x74E57B3D, 0x03E24BA5, 0x9AEB1B3F, 0xED882BAF,
        0x71C95A86, 0x06CE6A10, 0x9FC1FBA4, 0xE8C6CB3C, 0x78431CE5, 0x0F442C7D, 0x964DBD07, 0xE14AED9F,
        0x7ECD34D2, 0x09CA0444, 0x90C395FE, 0xE7C4A56A, 0x78C3F5F3, 0x0FC0C565, 0x96C974DF, 0xE1CE444B,
        0x7386F55E, 0x0481C5C8, 0x9D8A5472, 0xEA8D64E4, 0x700A7417, 0x070D4481, 0x9E04353B, 0xE90305AD,
        0x76C01EFA, 0x01C72E6C, 0x98CE5ED6, 0xEF896E40, 0x7F4E35D3, 0x08490545, 0x914090FF, 0xE647A069,
        0x7DCE1330, 0x0ACD23A6, 0x93C4B21C, 0xE4C3828A, 0x72471F15, 0x05402F83, 0x9C49BF39, 0xE54E8FAF,
        0x7B09BFBE, 0x0C0E8B2A, 0x95071A90, 0xE2002A06, 0x7EA71199, 0x09A0210F, 0x90A9B0B5, 0xE7AE8023,
        0x74EF658E, 0x03E85518, 0x9AE1C4A2, 0xEDE6F434, 0x78692CEB, 0x0F6E1C7D, 0x9667AD07, 0xE1609D91,
        0x70E80D66, 0x07EF3DFC, 0x9EFC6E46, 0xE8FB5EDA, 0x7C8F0ED3, 0x0B883E45, 0x9281AFFB, 0xE5869F6D,
        0x73466F1A, 0x04415F8C, 0x9D482E36, 0xEA4F1EAA, 0x7A2C7E7F, 0x0D2B4E09, 0x9422DFF3, 0xE325EFA5
    };

    uint32_t crc = 0xFFFFFFFF;
    const unsigned char* data = reinterpret_cast<const unsigned char*>(address);

    for (std::size_t i = 0; i < size; ++i) {
        crc = (crc >> 8) ^ crcTable[(crc ^ data[i]) & 0xFF];
    }

    return ~crc;
}

const std::vector<ModuleIntegrityInfo>& ModuleIntegrity::GetRegisteredModules() const noexcept {
    return modules_;
}

void ModuleIntegrity::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    modules_.clear();
}

// =============================================================================
// Startup Integrity Verification
// =============================================================================

bool RunStartupIntegrityChecks() {
    // 1. Timing checks
    if (!RunTimingChecks()) {
        return false;
    }

    // 2. Install memory guards
    InstallCriticalMemoryGuards();

    // 3. Module integrity (if any registered)
    ModuleIntegrity& integrity = ModuleIntegrity::Instance();
    std::vector<ModuleIntegrityInfo> results;
    if (!integrity.GetRegisteredModules().empty()) {
        if (!integrity.VerifyAll(results)) {
            return false;
        }
    }

    return true;
}

bool RunPeriodicIntegrityChecks() {
    // Verify memory guards
    if (!MemoryGuardRegistry::Instance().VerifyAll()) {
        return false;
    }

    // Quick timing check
    if (TimingCheck::RunAllChecks(50000, 5000)) {  // Higher thresholds for periodic
        return false;
    }

    return true;
}

void InstallCriticalMemoryGuards() {
    auto& registry = MemoryGuardRegistry::Instance();

    // Protect critical code sections
    // Note: In a real implementation, you'd protect specific functions/data
    // For now, we register the main module's .text section as read-only

    std::uintptr_t base = GetCurrentModuleBase();
    if (base != 0) {
        // Get module size from PE headers
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const IMAGE_NT_HEADERS64* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) {
                // Protect .text section (code)
                // In practice, you'd iterate sections and protect specific ones
                // This is a simplified example
            }
        }
    }
}

bool RunTimingChecks() {
    return !TimingCheck::RunAllChecks();
}

// =============================================================================
// Helper Functions
// =============================================================================

std::uintptr_t GetCurrentModuleBase() {
    HMODULE hModule = nullptr;
    GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCSTR>(&GetCurrentModuleBase), &hModule);
    return reinterpret_cast<std::uintptr_t>(hModule);
}

std::optional<ModuleInfo> GetModuleInfo(std::string_view moduleName) {
    HMODULE hModule = GetModuleHandleA(moduleName.data());
    if (!hModule) {
        return std::nullopt;
    }

    MODULEINFO modInfo{};
    if (!GetModuleInformation(GetCurrentProcess(), hModule, &modInfo, sizeof(modInfo))) {
        return std::nullopt;
    }

    char path[MAX_PATH];
    GetModuleFileNameA(hModule, path, MAX_PATH);

    ModuleInfo info;
    info.baseAddress = reinterpret_cast<std::uintptr_t>(hModule);
    info.size = modInfo.SizeOfImage;
    info.name = moduleName;
    info.path = path;

    return info;
}

// =============================================================================
// Singleton instances
// =============================================================================

ModuleIntegrity& ModuleIntegrity::Instance() noexcept {
    static ModuleIntegrity instance;
    return instance;
}

} // namespace OmniGhost::Security