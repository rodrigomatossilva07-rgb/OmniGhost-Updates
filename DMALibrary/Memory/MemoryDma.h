#pragma once
#include "Memory.h"
#include <cstdint>
#include <mutex>
#include <atomic>

namespace OmniGhost::Memory {

// Forward declarations
class ScatterHandleRegistry;

// ============================================================
// DmaTransport - encapsula operações de transporte DMA
// ============================================================
class DmaTransport final {
public:
    explicit DmaTransport(Memory* owner) noexcept : owner_(owner) {}

    bool Read(uintptr_t address, void* buffer, size_t size) const noexcept;
    bool TryRead(uintptr_t address, void* buffer, size_t size, bool& attempted) const noexcept;
    bool Write(uintptr_t address, void* buffer, size_t size) const noexcept;
    
    VMMDLL_SCATTER_HANDLE CreateScatterHandle(int pid = 0) const noexcept;
    void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle) const noexcept;
    void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size) const noexcept;
    void AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size) const noexcept;
    bool ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0) const noexcept;
    void ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0) const noexcept;

    void SetDmaCallTag(const char* tag) const noexcept;
    void SetDmaScanId(uint64_t id) const noexcept;
    void SetDmaLane(const char* lane) const noexcept;
    void SetDmaPriority(int priority) const noexcept;
    [[nodiscard]] int GetDmaPriority() const noexcept;

private:
    Memory* owner_;
};

// ============================================================
// DeviceManager - gerencia lifecycle do FPGA/VMM
// ============================================================
class DeviceManager final {
public:
    explicit DeviceManager(Memory* owner) noexcept : owner_(owner) {}

    bool Init(std::string process_name, bool memMap, bool debug, bool quickDeviceProbe);
    void ResetDevice();
    void Close() noexcept;
    bool Reconnect(bool memMap, bool debug);
    bool Rebind(std::string process_name, bool memMap, bool debug);
    
    [[nodiscard]] bool IsDeviceOpen() const noexcept { return owner_->vHandle != nullptr; }
    
    bool BeginVmmMaintenance(const char* reason = nullptr, DWORD drainTimeoutMs = 2500);
    void EndVmmMaintenance() noexcept;
    [[nodiscard]] bool IsVmmMaintenanceActive() const noexcept;
    bool ReopenVmmSession(bool memMap, bool debug);
    bool ProbePhysicalDataPath();
    
    [[nodiscard]] bool ProcInfoSessionRecoveryRecommended() const noexcept;
    [[nodiscard]] bool DependencyIntegrityOk() const noexcept;
    [[nodiscard]] const std::string& DependencyIntegrityMessage() const noexcept;

    void RequestCancel() noexcept;
    void ClearCancel() noexcept;
    [[nodiscard]] uint64_t CancellationEpoch() const noexcept;
    [[nodiscard]] bool IsCancellationRequested() const noexcept;
    [[nodiscard]] bool IsCancellationRequested(uint64_t operation_epoch) const noexcept;

private:
    Memory* owner_;
};

// ============================================================
// ProcessManager - operações de processo
// ============================================================
class ProcessManager final {
public:
    explicit ProcessManager(Memory* owner) noexcept : owner_(owner) {}

    DWORD GetPidFromName(std::string process_name) const noexcept;
    std::vector<std::string> GetProcessNames() const noexcept;
    std::vector<int> GetPidListFromName(std::string process_name) const noexcept;
    std::vector<std::string> GetModuleList(std::string process_name) const noexcept;
    VMMDLL_PROCESS_INFORMATION GetProcessInformation() const noexcept;
    [[nodiscard]] bool IsValidReadAddress(uint64_t address, int pid = 0) const noexcept;
    bool RefreshVadCache(int pid = 0) const noexcept;
    PEB GetProcessPeb() const noexcept;
    size_t GetBaseDaddy(std::string module_name) const noexcept;
    size_t GetBaseSize(std::string module_name) const noexcept;
    uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module) const noexcept;
    uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module) const noexcept;
    bool FixCr3() noexcept;
    bool DumpMemory(uintptr_t address, std::string path) const noexcept;
    uint64_t FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID = 0) const noexcept;

private:
    Memory* owner_;
};

// ============================================================
// ScatterHandleRegistry - gerencia scatter handles com contadores atômicos
// ============================================================
class ScatterHandleRegistry final {
public:
    void Register(VMMDLL_SCATTER_HANDLE handle, uint64_t sessionGen, uint64_t processGen);
    void Unregister(VMMDLL_SCATTER_HANDLE handle);
    [[nodiscard]] bool IsCurrent(VMMDLL_SCATTER_HANDLE handle) const noexcept;
    void InvalidateAll(const char* reason) noexcept;
    [[nodiscard]] uint32_t LiveCount() const noexcept;

private:
    mutable std::mutex mutex_;
    std::unordered_map<VMMDLL_SCATTER_HANDLE, ScatterGeneration> handles_;
    std::atomic<uint32_t> liveCount_{0};
    std::atomic<uint64_t> created_{0};
    std::atomic<uint64_t> destroyed_{0};
    friend class Memory;
};

} // namespace OmniGhost::Memory