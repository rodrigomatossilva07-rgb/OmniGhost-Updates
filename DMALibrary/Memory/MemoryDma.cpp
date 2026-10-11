#include "MemoryDma.h"
#include "Memory.h"
#include "MemoryInternal.h"
#include <algorithm>

namespace OmniGhost::Memory {

// ============================================================
// DmaTransport implementation
// ============================================================
bool DmaTransport::Read(uintptr_t address, void* buffer, size_t size) const noexcept {
    return owner_->Read(address, buffer, size);
}

bool DmaTransport::TryRead(uintptr_t address, void* buffer, size_t size, bool& attempted) const noexcept {
    return owner_->TryRead(address, buffer, size, attempted);
}

bool DmaTransport::Write(uintptr_t address, void* buffer, size_t size) const noexcept {
    return owner_->Write(address, buffer, size);
}

VMMDLL_SCATTER_HANDLE DmaTransport::CreateScatterHandle(int pid) const noexcept {
    return owner_->CreateScatterHandle(pid);
}

void DmaTransport::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle) const noexcept {
    owner_->CloseScatterHandle(handle);
}

void DmaTransport::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size) const noexcept {
    owner_->AddScatterReadRequest(handle, address, buffer, size);
}

void DmaTransport::AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size) const noexcept {
    owner_->AddScatterWriteRequest(handle, address, buffer, size);
}

bool DmaTransport::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid) const noexcept {
    return owner_->ExecuteReadScatter(handle, pid);
}

void DmaTransport::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid) const noexcept {
    owner_->ExecuteWriteScatter(handle, pid);
}

void DmaTransport::SetDmaCallTag(const char* tag) const noexcept {
    owner_->SetDmaCallTag(tag);
}

void DmaTransport::SetDmaScanId(uint64_t id) const noexcept {
    owner_->SetDmaScanId(id);
}

void DmaTransport::SetDmaLane(const char* lane) const noexcept {
    owner_->SetDmaLane(lane);
}

void DmaTransport::SetDmaPriority(int priority) const noexcept {
    owner_->SetDmaPriority(priority);
}

int DmaTransport::GetDmaPriority() const noexcept {
    return owner_->GetDmaPriority();
}

// ============================================================
// DeviceManager implementation
// ============================================================
bool DeviceManager::Init(std::string process_name, bool memMap, bool debug, bool quickDeviceProbe) {
    return owner_->Init(std::move(process_name), memMap, debug, quickDeviceProbe);
}

void DeviceManager::ResetDevice() {
    owner_->ResetDevice();
}

void DeviceManager::Close() noexcept {
    owner_->Close();
}

bool DeviceManager::Reconnect(bool memMap, bool debug) {
    return owner_->Reconnect(memMap, debug);
}

bool DeviceManager::Rebind(std::string process_name, bool memMap, bool debug) {
    return owner_->Rebind(std::move(process_name), memMap, debug);
}

bool DeviceManager::BeginVmmMaintenance(const char* reason, DWORD drainTimeoutMs) {
    return owner_->BeginVmmMaintenance(reason, drainTimeoutMs);
}

void DeviceManager::EndVmmMaintenance() noexcept {
    owner_->EndVmmMaintenance();
}

bool DeviceManager::IsVmmMaintenanceActive() const noexcept {
    return owner_->IsVmmMaintenanceActive();
}

bool DeviceManager::ReopenVmmSession(bool memMap, bool debug) {
    return owner_->ReopenVmmSession(memMap, debug);
}

bool DeviceManager::ProbePhysicalDataPath() {
    return owner_->ProbePhysicalDataPath();
}

bool DeviceManager::ProcInfoSessionRecoveryRecommended() const noexcept {
    return owner_->ProcInfoSessionRecoveryRecommended();
}

bool DeviceManager::DependencyIntegrityOk() const noexcept {
    return owner_->DependencyIntegrityOk();
}

const std::string& DeviceManager::DependencyIntegrityMessage() const noexcept {
    return owner_->DependencyIntegrityMessage();
}

void DeviceManager::RequestCancel() noexcept {
    owner_->RequestCancel();
}

void DeviceManager::ClearCancel() noexcept {
    owner_->ClearCancel();
}

uint64_t DeviceManager::CancellationEpoch() const noexcept {
    return owner_->CancellationEpoch();
}

bool DeviceManager::IsCancellationRequested() const noexcept {
    return owner_->IsCancellationRequested();
}

bool DeviceManager::IsCancellationRequested(uint64_t operation_epoch) const noexcept {
    return owner_->IsCancellationRequested(operation_epoch);
}

// ============================================================
// ProcessManager implementation
// ============================================================
DWORD ProcessManager::GetPidFromName(std::string process_name) const noexcept {
    return owner_->GetPidFromName(std::move(process_name));
}

std::vector<std::string> ProcessManager::GetProcessNames() const noexcept {
    return owner_->GetProcessNames();
}

std::vector<int> ProcessManager::GetPidListFromName(std::string process_name) const noexcept {
    return owner_->GetPidListFromName(std::move(process_name));
}

std::vector<std::string> ProcessManager::GetModuleList(std::string process_name) const noexcept {
    return owner_->GetModuleList(std::move(process_name));
}

VMMDLL_PROCESS_INFORMATION ProcessManager::GetProcessInformation() const noexcept {
    return owner_->GetProcessInformation();
}

bool ProcessManager::IsValidReadAddress(uint64_t address, int pid) const noexcept {
    return owner_->IsValidReadAddress(address, pid);
}

bool ProcessManager::RefreshVadCache(int pid) const noexcept {
    return owner_->RefreshVadCache(pid);
}

PEB ProcessManager::GetProcessPeb() const noexcept {
    return owner_->GetProcessPeb();
}

size_t ProcessManager::GetBaseDaddy(std::string module_name) const noexcept {
    return owner_->GetBaseDaddy(std::move(module_name));
}

size_t ProcessManager::GetBaseSize(std::string module_name) const noexcept {
    return owner_->GetBaseSize(std::move(module_name));
}

uintptr_t ProcessManager::GetExportTableAddress(std::string import, std::string process, std::string module) const noexcept {
    return owner_->GetExportTableAddress(std::move(import), std::move(process), std::move(module));
}

uintptr_t ProcessManager::GetImportTableAddress(std::string import, std::string process, std::string module) const noexcept {
    return owner_->GetImportTableAddress(std::move(import), std::move(process), std::move(module));
}

bool ProcessManager::FixCr3() noexcept {
    return owner_->FixCr3();
}

bool ProcessManager::DumpMemory(uintptr_t address, std::string path) const noexcept {
    return owner_->DumpMemory(address, std::move(path));
}

uint64_t ProcessManager::FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID) const noexcept {
    return owner_->FindSignature(signature, range_start, range_end, PID);
}

// ============================================================
// ScatterHandleRegistry implementation
// ============================================================
void ScatterHandleRegistry::Register(VMMDLL_SCATTER_HANDLE handle, uint64_t sessionGen, uint64_t processGen) {
    if (!handle) return;
    std::scoped_lock lock(mutex_);
    handles_.insert_or_assign(handle, ScatterGeneration{sessionGen, processGen, 0});
    liveCount_.store(static_cast<uint32_t>(handles_.size()), std::memory_order_release);
    created_.fetch_add(1, std::memory_order_relaxed);
}

void ScatterHandleRegistry::Unregister(VMMDLL_SCATTER_HANDLE handle) {
    if (!handle) return;
    std::scoped_lock lock(mutex_);
    auto it = handles_.find(handle);
    if (it != handles_.end()) {
        handles_.erase(it);
        liveCount_.store(static_cast<uint32_t>(handles_.size()), std::memory_order_release);
        destroyed_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool ScatterHandleRegistry::IsCurrent(VMMDLL_SCATTER_HANDLE handle) const noexcept {
    if (!handle) return false;
    std::scoped_lock lock(mutex_);
    const auto found = handles_.find(handle);
    // Note: session/process generation check would need access to Memory's atomics
    return found != handles_.end();
}

void ScatterHandleRegistry::InvalidateAll(const char* reason) noexcept {
    std::unordered_map<VMMDLL_SCATTER_HANDLE, ScatterGeneration> stale;
    {
        std::scoped_lock lock(mutex_);
        stale.swap(handles_);
        liveCount_.store(0, std::memory_order_release);
    }
    for (const auto& [handle, generation] : stale) {
        (void)generation;
        if (handle) {
            // Close happens outside lock to avoid hierarchy violation
            // Caller must handle VMMDLL_Scatter_CloseHandle
        }
    }
    if (!stale.empty()) {
        std::clog << "[DMA][SCATTER] invalidated=" << stale.size()
                  << " reason=" << (reason ? reason : "lifecycle") << '\n';
    }
}

uint32_t ScatterHandleRegistry::LiveCount() const noexcept {
    return liveCount_.load(std::memory_order_acquire);
}

} // namespace OmniGhost::Memory