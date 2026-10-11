#include "Memory.h"
#include "MemoryDma.h"
#include "MemoryIO.cpp"
#include "MemoryInit.cpp"
#include "MemoryLifecycle.cpp"

namespace OmniGhost::MemoryCounters {
uint64_t GetScatterHandlesCreated() noexcept {
    // This would need access to the actual counter - simplified for now
    return 0;
}
uint64_t GetScatterHandlesDestroyed() noexcept { return 0; }
uint32_t GetScatterHandlesLive() noexcept { return 0; }
}

Memory::Memory()
    : dma(this)
    , device(this)
    , process(this)
    , scatterRegistry()
    , timeouts_()
    , modules{}
    , current_process{}
    , DMA_INITIALIZED(FALSE)
    , PROCESS_INITIALIZED(FALSE)
    , last_good_dtb(0)
    , pluginsInitializedFor_(nullptr)
    , pluginsInitialized_(false)
    , procInfoState_(Memory::ProcInfoState::Uninitialized)
    , procInfoProgress_(-1)
    , procInfoDtbFileSize_(0)
    , procInfoLastNt_(0)
    , procInfoSessionRecoveryRecommended_(false)
    , vmmMaintenanceActive_(false)
    , vmmMaintenanceOwnerThreadId_(0)
    , activeDataCalls_(0)
    , activeScatterHandles_(0)
    , blockedDataCalls_(0)
    , controlPlaneMutex_()
    , fpgaOpenInProgress_(false)
    , vmmLatencySampleIndex_(0)
    , generationProcessPid_(0)
    , dependencyIntegrityOk_(true)
    , runtimeDependenciesInitialized_(false)
    , appliedProcessDtb_(0)
    , appliedProcessPid_(0)
    , sessionGeneration_(0)
    , processGeneration_(0)
    , vmmOpenCount_(0)
    , pluginInitializationCount_(0)
    , deviceResetCount_(0)
    , processBindCount_(0)
    , vmmCallCount_(0)
    , readRequestCount_(0)
    , scatterReadBatchCount_(0)
    , maxVmmLatencyMs_(0)
    , startupDeviceProbeOk_(false)
    , cancelRequested_(false)
    , cancelEpoch_(0)
    , vadCacheTime_()
    , vHandle(nullptr)
    , key(std::make_shared<c_keys>(*this))
{
}

Memory::~Memory() {
    Close();
}

Memory::DiagnosticsSnapshot Memory::GetDiagnosticsSnapshot() const noexcept {
    DiagnosticsSnapshot snap{};
    snap.sessionGeneration = sessionGeneration_.load(std::memory_order_acquire);
    snap.processGeneration = processGeneration_.load(std::memory_order_acquire);
    snap.vmmOpenCount = vmmOpenCount_.load(std::memory_order_acquire);
    snap.pluginInitializationCount = pluginInitializationCount_.load(std::memory_order_acquire);
    snap.deviceResetCount = deviceResetCount_.load(std::memory_order_acquire);
    snap.processBindCount = processBindCount_.load(std::memory_order_acquire);
    snap.vmmCallCount = vmmCallCount_.load(std::memory_order_acquire);
    snap.readRequestCount = readRequestCount_.load(std::memory_order_acquire);
    snap.scatterReadBatchCount = scatterReadBatchCount_.load(std::memory_order_acquire);
    snap.scatterHandlesCreated = MemoryCounters::GetScatterHandlesCreated();
    snap.scatterHandlesDestroyed = MemoryCounters::GetScatterHandlesDestroyed();
    snap.scatterHandlesLive = MemoryCounters::GetScatterHandlesLive();
    snap.maxVmmLatencyMs = maxVmmLatencyMs_.load(std::memory_order_acquire);
    snap.maintenanceActive = vmmMaintenanceActive_.load(std::memory_order_acquire);
    snap.procInfoSessionRecoveryRecommended = procInfoSessionRecoveryRecommended_.load(std::memory_order_acquire);
    snap.deviceOpen = IsDeviceOpen();
    snap.deviceDetected = startupDeviceProbeOk_.load(std::memory_order_acquire);
    snap.processInitialized = PROCESS_INITIALIZED;
    snap.processId = current_process.PID;
    snap.procInfoState = static_cast<int>(procInfoState_.load(std::memory_order_acquire));
    snap.procInfoProgress = procInfoProgress_.load(std::memory_order_acquire);
    snap.procInfoDtbFileSize = procInfoDtbFileSize_.load(std::memory_order_acquire);
    snap.procInfoLastNt = procInfoLastNt_.load(std::memory_order_acquire);
    snap.dependencyIntegrityOk = dependencyIntegrityOk_;
    return snap;
}

void Memory::SetRenderThreadId(std::thread::id id) noexcept {
    // Thread-local storage for render thread detection
    static thread_local std::thread::id renderThreadId;
    renderThreadId = id;
}

void Memory::SetDmaCallTag(const char* tag) noexcept {
    thread_local const char* t_dmaTag = "untagged";
    t_dmaTag = tag;
}

void Memory::SetDmaScanId(uint64_t id) noexcept {
    thread_local uint64_t t_dmaScanId = 0;
    t_dmaScanId = id;
}

void Memory::SetDmaLane(const char* lane) noexcept {
    thread_local const char* t_dmaLane = "default";
    t_dmaLane = lane;
}

void Memory::SetDmaPriority(int priority) noexcept {
    thread_local int t_dmaPriority = 0;
    if (priority < 0) priority = 0;
    if (priority > 100) priority = 100;
    t_dmaPriority = priority;
}

int Memory::GetDmaPriority() noexcept {
    thread_local int t_dmaPriority = 0;
    return t_dmaPriority;
}

Memory::ScanDmaStats Memory::GetScanDmaStats() noexcept {
    ScanDmaStats stats{};
    thread_local uint64_t t_dmaScanId = 0;
    thread_local int t_qreadCalls = 0;
    thread_local double t_qreadTotalMs = 0.0;
    thread_local double t_qreadMaxMs = 0.0;
    thread_local int t_scatterCalls = 0;
    thread_local double t_scatterTotalMs = 0.0;
    thread_local double t_scatterMaxMs = 0.0;
    thread_local const char* t_topTag = "none";
    thread_local double t_topTagMs = 0.0;
    thread_local const char* t_top2Tag = "none";
    thread_local double t_top2TagMs = 0.0;
    thread_local const char* t_top3Tag = "none";
    thread_local double t_top3TagMs = 0.0;

    stats.scan_id = t_dmaScanId;
    stats.qread_calls = t_qreadCalls;
    stats.qread_total_ms = t_qreadTotalMs;
    stats.qread_max_ms = t_qreadMaxMs;
    stats.scatter_calls = t_scatterCalls;
    stats.scatter_total_ms = t_scatterTotalMs;
    stats.scatter_max_ms = t_scatterMaxMs;
    stats.top_tag = t_topTag;
    stats.top_tag_ms = t_topTagMs;
    stats.top2_tag = t_top2Tag;
    stats.top2_tag_ms = t_top2TagMs;
    stats.top3_tag = t_top3Tag;
    stats.top3_tag_ms = t_top3TagMs;
    return stats;
}

void Memory::ResetScanDmaStats() noexcept {
    thread_local uint64_t t_dmaScanId = 0;
    thread_local int t_qreadCalls = 0;
    thread_local double t_qreadTotalMs = 0.0;
    thread_local double t_qreadMaxMs = 0.0;
    thread_local int t_scatterCalls = 0;
    thread_local double t_scatterTotalMs = 0.0;
    thread_local double t_scatterMaxMs = 0.0;
    thread_local const char* t_topTag = "none";
    thread_local double t_topTagMs = 0.0;
    thread_local const char* t_top2Tag = "none";
    thread_local double t_top2TagMs = 0.0;
    thread_local const char* t_top3Tag = "none";
    thread_local double t_top3TagMs = 0.0;

    t_dmaScanId = 0;
    t_qreadCalls = 0; t_qreadTotalMs = 0.0; t_qreadMaxMs = 0.0;
    t_scatterCalls = 0; t_scatterTotalMs = 0.0; t_scatterMaxMs = 0.0;
    t_topTag = "none"; t_topTagMs = 0.0;
    t_top2Tag = "none"; t_top2TagMs = 0.0;
    t_top3Tag = "none"; t_top3TagMs = 0.0;
}

const char* Memory::GetDmaCallTag() noexcept {
    thread_local const char* t_dmaTag = "untagged";
    return t_dmaTag;
}

void Memory::ResetThreadDmaWaitUs() noexcept {
    thread_local uint64_t t_dmaWaitUs = 0;
    thread_local uint64_t t_lastDmaWaitUs = 0;
    t_dmaWaitUs = 0;
    t_lastDmaWaitUs = 0;
}

uint64_t Memory::ConsumeThreadDmaWaitUs() noexcept {
    thread_local uint64_t t_dmaWaitUs = 0;
    thread_local uint64_t t_lastDmaWaitUs = 0;
    const uint64_t v = t_dmaWaitUs;
    t_dmaWaitUs = 0;
    return v;
}

std::thread::id Memory::GetRenderThreadId() noexcept {
    static thread_local std::thread::id renderThreadId;
    return renderThreadId;
}

void Memory::SetTimeouts(const TimeoutConfig& config) noexcept { timeouts_ = config; }
const Memory::TimeoutConfig& Memory::GetTimeouts() const noexcept { return timeouts_; }

bool Memory::ProbeDeviceAvailability() { return device.ProbeDeviceAvailability(); }
bool Memory::ProbeDevicePresence() { return device.ProbeDevicePresence(); }
uint64_t Memory::SessionGeneration() const noexcept { return sessionGeneration_.load(std::memory_order_acquire); }
uint64_t Memory::ProcessGeneration() const noexcept { return processGeneration_.load(std::memory_order_acquire); }

// Delegate to sub-managers
bool Memory::Init(std::string process_name, bool memMap, bool debug, bool quickDeviceProbe) { return device.Init(std::move(process_name), memMap, debug, quickDeviceProbe); }
void Memory::InvalidateProcess() { device.InvalidateProcess(); }
void Memory::ResetDevice() { device.ResetDevice(); }
void Memory::Close() noexcept { device.Close(); }
bool Memory::Reconnect(bool memMap, bool debug) { return device.Reconnect(memMap, debug); }
bool Memory::Rebind(std::string process_name, bool memMap, bool debug) { return device.Rebind(std::move(process_name), memMap, debug); }
bool Memory::BeginVmmMaintenance(const char* reason, DWORD drainTimeoutMs) { return device.BeginVmmMaintenance(reason, drainTimeoutMs); }
void Memory::EndVmmMaintenance() noexcept { device.EndVmmMaintenance(); }
bool Memory::IsVmmMaintenanceActive() const noexcept { return device.IsVmmMaintenanceActive(); }
bool Memory::ReopenVmmSession(bool memMap, bool debug) { return device.ReopenVmmSession(memMap, debug); }
bool Memory::ProbePhysicalDataPath() { return device.ProbePhysicalDataPath(); }
bool Memory::ProcInfoSessionRecoveryRecommended() const noexcept { return device.ProcInfoSessionRecoveryRecommended(); }
void Memory::RequestCancel() noexcept { device.RequestCancel(); }
void Memory::ClearCancel() noexcept { device.ClearCancel(); }
uint64_t Memory::CancellationEpoch() const noexcept { return device.CancellationEpoch(); }
bool Memory::IsCancellationRequested() const noexcept { return device.IsCancellationRequested(); }
bool Memory::IsCancellationRequested(uint64_t operation_epoch) const noexcept { return device.IsCancellationRequested(operation_epoch); }

DWORD Memory::GetPidFromName(std::string process_name) { return process.GetPidFromName(std::move(process_name)); }
std::vector<std::string> Memory::GetProcessNames() { return process.GetProcessNames(); }
std::vector<int> Memory::GetPidListFromName(std::string process_name) { return process.GetPidListFromName(std::move(process_name)); }
std::vector<std::string> Memory::GetModuleList(std::string process_name) { return process.GetModuleList(std::move(process_name)); }
VMMDLL_PROCESS_INFORMATION Memory::GetProcessInformation() { return process.GetProcessInformation(); }
bool Memory::IsValidReadAddress(uint64_t address, int pid) const { return process.IsValidReadAddress(address, pid); }
bool Memory::RefreshVadCache(int pid) const { return process.RefreshVadCache(pid); }
PEB Memory::GetProcessPeb() { return process.GetProcessPeb(); }
size_t Memory::GetBaseDaddy(std::string module_name) { return process.GetBaseDaddy(std::move(module_name)); }
size_t Memory::GetBaseSize(std::string module_name) { return process.GetBaseSize(std::move(module_name)); }
uintptr_t Memory::GetExportTableAddress(std::string import, std::string process_name, std::string module) { return process.GetExportTableAddress(std::move(import), std::move(process_name), std::move(module)); }
uintptr_t Memory::GetImportTableAddress(std::string import, std::string process_name, std::string module) { return process.GetImportTableAddress(std::move(import), std::move(process_name), std::move(module)); }
bool Memory::FixCr3() { return process.FixCr3(); }
bool Memory::DumpMemory(uintptr_t address, std::string path) { return process.DumpMemory(address, std::move(path)); }
uint64_t Memory::FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID) { return process.FindSignature(signature, range_start, range_end, PID); }

bool Memory::Write(uintptr_t address, void* buffer, size_t size) const { return dma.Write(address, buffer, size); }
bool Memory::Write(uintptr_t address, void* buffer, size_t size, int pid) const { return dma.Write(address, buffer, size, pid); }
bool Memory::Read(uintptr_t address, void* buffer, size_t size) const { return dma.Read(address, buffer, size); }
bool Memory::TryRead(uintptr_t address, void* buffer, size_t size, bool& attempted) const { return dma.TryRead(address, buffer, size, attempted); }
bool Memory::Read(uintptr_t address, void* buffer, size_t size, int pid) const { return dma.Read(address, buffer, size, pid); }
bool Memory::ReadWithCount(uintptr_t address, void* buffer, size_t size, size_t& bytesRead) const { return dma.ReadWithCount(address, buffer, size, bytesRead); }

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle() const { return dma.CreateScatterHandle(); }
VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(int pid) const { return dma.CreateScatterHandle(pid); }
void Memory::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle) { dma.CloseScatterHandle(handle); }
void Memory::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size) { dma.AddScatterReadRequest(handle, address, buffer, size); }
void Memory::AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size) { dma.AddScatterWriteRequest(handle, address, buffer, size); }
bool Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid) { return dma.ExecuteReadScatter(handle, pid); }
void Memory::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid) { dma.ExecuteWriteScatter(handle, pid); }