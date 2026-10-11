#pragma once
#include "../pch.h"
#include "InputManager.h"
#include "Registry.h"
#include "Shellcode.h"
#include "../nt/structs.h"
#include "MemoryDma.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <array>
#include <mutex>
#include <unordered_map>
#include <memory>


class VmmSession
{
public:
	VmmSession() noexcept = default;
	explicit VmmSession(VMM_HANDLE handle) noexcept : handle_(handle) {}
	~VmmSession() { reset(); }

	VmmSession(const VmmSession&) = delete;
	VmmSession& operator=(const VmmSession&) = delete;
	VmmSession(VmmSession&& other) noexcept : handle_(other.release()) {}
	VmmSession& operator=(VmmSession&& other) noexcept
	{
		if (this != &other)
			reset(other.release());
		return *this;
	}
	VmmSession& operator=(VMM_HANDLE handle) noexcept
	{
		reset(handle);
		return *this;
	}
	VmmSession& operator=(std::nullptr_t) noexcept
	{
		reset();
		return *this;
	}

	[[nodiscard]] VMM_HANDLE get() const noexcept { return handle_; }
	[[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
	[[nodiscard]] operator VMM_HANDLE() const noexcept { return handle_; }

	void reset(VMM_HANDLE replacement = nullptr) noexcept
	{
		if (handle_ && handle_ != replacement)
			VMMDLL_Close(handle_);
		handle_ = replacement;
	}

	[[nodiscard]] VMM_HANDLE release() noexcept
	{
		VMM_HANDLE handle = handle_;
		handle_ = nullptr;
		return handle;
	}

private:
	VMM_HANDLE handle_ = nullptr;
};

namespace OmniGhost::MemoryCounters {
uint64_t GetScatterHandlesCreated() noexcept;
uint64_t GetScatterHandlesDestroyed() noexcept;
uint32_t GetScatterHandlesLive() noexcept;
} // namespace OmniGhost::MemoryCounters

class Memory
{
public:
	// Composed sub-managers (single-responsibility)
	MemoryDma::DmaTransport dma;
	MemoryDma::DeviceManager device;
	MemoryDma::ProcessManager process;
	MemoryDma::ScatterHandleRegistry scatterRegistry;

	// Constructor/Destructor
	Memory();
	~Memory();
	Memory(const Memory&) = delete;
	Memory& operator=(const Memory&) = delete;
	Memory(Memory&&) = delete;
	Memory& operator=(Memory&&) = delete;

	// Registry/Shellcode access (legacy compatibility)
	c_registry GetRegistry() { return registry; }
	c_keys* GetKeyboard() { return key.get(); }
	c_shellcode GetShellcode() { return shellcode; }

	// Diagnostics
	struct DiagnosticsSnapshot
	{
		uint64_t sessionGeneration = 0;
		uint64_t processGeneration = 0;
		uint64_t vmmOpenCount = 0;
		uint64_t pluginInitializationCount = 0;
		uint64_t deviceResetCount = 0;
		uint64_t processBindCount = 0;
		uint64_t vmmCallCount = 0;
		uint64_t readRequestCount = 0;
		uint64_t scatterReadBatchCount = 0;
		uint64_t scatterHandlesCreated = 0;
		uint64_t scatterHandlesDestroyed = 0;
		uint32_t scatterHandlesLive = 0;
		uint64_t maxVmmLatencyMs = 0;
		uint64_t vmmLatencyAverageMs = 0;
		uint64_t vmmLatencyP50Ms = 0;
		uint64_t vmmLatencyP95Ms = 0;
		uint64_t vmmLatencyP99Ms = 0;
		uint64_t readFailureCount = 0;
		uint64_t blockedDataCalls = 0;
		uint32_t activeDataCalls = 0;
		uint32_t activeScatterHandles = 0;
		bool maintenanceActive = false;
		bool procInfoSessionRecoveryRecommended = false;
		bool deviceOpen = false;
		bool deviceDetected = false;
		bool processInitialized = false;
		int processId = 0;
		int procInfoState = 0;
		int procInfoProgress = -1;
		uint64_t procInfoDtbFileSize = 0;
		unsigned long procInfoLastNt = 0;
		bool dependencyIntegrityOk = true;
	};

	[[nodiscard]] DiagnosticsSnapshot GetDiagnosticsSnapshot() const noexcept;

	// Thread debugging
	static void SetRenderThreadId(std::thread::id id) noexcept;
	static void SetDmaCallTag(const char* tag) noexcept;
	static void SetDmaScanId(uint64_t id) noexcept;
	static void SetDmaLane(const char* lane) noexcept;

	// DMA priority (thread-local)
	struct DmaPri {
		static constexpr int OtherMetadata = 10;
		static constexpr int WeaponNameTeam = 30;
		static constexpr int Health = 50;
		static constexpr int Bones = 60;
		static constexpr int Position = 80;
		static constexpr int Camera = 100;
	};
	static void SetDmaPriority(int priority) noexcept;
	[[nodiscard]] static int GetDmaPriority() noexcept;

	// Scan stats
	struct ScanDmaStats {
		uint64_t scan_id = 0;
		int qread_calls = 0;
		double qread_total_ms = 0.0;
		double qread_max_ms = 0.0;
		int scatter_calls = 0;
		double scatter_total_ms = 0.0;
		double scatter_max_ms = 0.0;
		const char* top_tag = "none";
		double top_tag_ms = 0.0;
		const char* top2_tag = "none";
		double top2_tag_ms = 0.0;
		const char* top3_tag = "none";
		double top3_tag_ms = 0.0;
	};
	static ScanDmaStats GetScanDmaStats() noexcept;
	static void ResetScanDmaStats() noexcept;
	static const char* GetDmaCallTag() noexcept;
	static void ResetThreadDmaWaitUs() noexcept;
	static uint64_t ConsumeThreadDmaWaitUs() noexcept;
	[[nodiscard]] static std::thread::id GetRenderThreadId() noexcept;

	// Lifecycle
	void SetTimeouts(const struct TimeoutConfig& config) noexcept;
	[[nodiscard]] const struct TimeoutConfig& GetTimeouts() const noexcept;
	bool ProbeDeviceAvailability();
	[[nodiscard]] static bool ProbeDevicePresence();
	[[nodiscard]] uint64_t SessionGeneration() const noexcept;
	[[nodiscard]] uint64_t ProcessGeneration() const noexcept;

	// Public API (delegates to sub-managers)
	bool Init(std::string process_name, bool memMap = true, bool debug = false, bool quickDeviceProbe = false);
	void InvalidateProcess();
	void ResetDevice();
	void Close() noexcept;
	bool Reconnect(bool memMap = true, bool debug = false);
	bool Rebind(std::string process_name, bool memMap = true, bool debug = false);
	[[nodiscard]] bool IsDeviceOpen() const noexcept { return vHandle != nullptr; }
	bool BeginVmmMaintenance(const char* reason = nullptr, DWORD drainTimeoutMs = 2500);
	void EndVmmMaintenance() noexcept;
	[[nodiscard]] bool IsVmmMaintenanceActive() const noexcept;
	bool ReopenVmmSession(bool memMap = true, bool debug = false);
	bool ProbePhysicalDataPath();
	[[nodiscard]] bool ProcInfoSessionRecoveryRecommended() const noexcept;
	[[nodiscard]] bool DependencyIntegrityOk() const noexcept { return dependencyIntegrityOk_; }
	[[nodiscard]] const std::string& DependencyIntegrityMessage() const noexcept { return dependencyIntegrityMessage_; }
	void RequestCancel() noexcept;
	void ClearCancel() noexcept;
	[[nodiscard]] uint64_t CancellationEpoch() const noexcept;
	[[nodiscard]] bool IsCancellationRequested() const noexcept;
	[[nodiscard]] bool IsCancellationRequested(uint64_t operation_epoch) const noexcept;

	// Process queries
	DWORD GetPidFromName(std::string process_name);
	std::vector<std::string> GetProcessNames();
	std::vector<int> GetPidListFromName(std::string process_name);
	std::vector<std::string> GetModuleList(std::string process_name);
	VMMDLL_PROCESS_INFORMATION GetProcessInformation();
	[[nodiscard]] bool IsValidReadAddress(uint64_t address, int pid = 0) const;
	bool RefreshVadCache(int pid = 0) const;
	PEB GetProcessPeb();
	size_t GetBaseDaddy(std::string module_name);
	size_t GetBaseSize(std::string module_name);
	uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module);
	uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module);
	bool FixCr3();
	bool DumpMemory(uintptr_t address, std::string path);
	uint64_t FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID = 0);

	// Memory operations
	bool Write(uintptr_t address, void* buffer, size_t size) const;
	bool Write(uintptr_t address, void* buffer, size_t size, int pid) const;
	template <typename T> bool Write(void* address, T value) { return Write(reinterpret_cast<uintptr_t>(address), &value, sizeof(T)); }
	template <typename T> bool Write(uintptr_t address, T value) { return Write(address, &value, sizeof(T)); }

	bool Read(uintptr_t address, void* buffer, size_t size) const;
	bool TryRead(uintptr_t address, void* buffer, size_t size, bool& attempted) const;
	bool Read(uintptr_t address, void* buffer, size_t size, int pid) const;
	bool ReadWithCount(uintptr_t address, void* buffer, size_t size, size_t& bytesRead) const;
	template <typename T> T Read(void* address) { T buffer{}; memset(&buffer, 0, sizeof(T)); Read(reinterpret_cast<uint64_t>(address), &buffer, sizeof(T)); return buffer; }
	template <typename T> T Read(uint64_t address) { return Read<T>(reinterpret_cast<void*>(address)); }
	template <typename T> T Read(void* address, int pid) { T buffer{}; memset(&buffer, 0, sizeof(T)); Read(reinterpret_cast<uint64_t>(address), &buffer, sizeof(T), pid); return buffer; }
	template <typename T> T Read(uint64_t address, int pid) { return Read<T>(reinterpret_cast<void*>(address), pid); }

	uint64_t ReadChain(uint64_t base, const std::vector<uint64_t>& offsets) {
		uint64_t result = Read<uint64_t>(base + offsets.at(0));
		for (int i = 1; i < offsets.size(); i++) result = Read<uint64_t>(result + offsets.at(i));
		return result;
	}

	// Scatter
	VMMDLL_SCATTER_HANDLE CreateScatterHandle() const;
	VMMDLL_SCATTER_HANDLE CreateScatterHandle(int pid) const;

	class ScopedScatterHandle final {
	public:
		ScopedScatterHandle() noexcept = default;
		ScopedScatterHandle(Memory* owner, VMMDLL_SCATTER_HANDLE handle) noexcept : owner_(owner), handle_(handle) {}
		~ScopedScatterHandle() { reset(); }
		ScopedScatterHandle(const ScopedScatterHandle&) = delete;
		ScopedScatterHandle& operator=(const ScopedScatterHandle&) = delete;
		ScopedScatterHandle(ScopedScatterHandle&& other) noexcept : owner_(other.owner_), handle_(other.release()) { other.owner_ = nullptr; }
		ScopedScatterHandle& operator=(ScopedScatterHandle&& other) noexcept { if (this != &other) { reset(); owner_ = other.owner_; handle_ = other.release(); other.owner_ = nullptr; } return *this; }
		[[nodiscard]] VMMDLL_SCATTER_HANDLE get() const noexcept { return handle_; }
		[[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
		[[nodiscard]] operator VMMDLL_SCATTER_HANDLE() const noexcept { return handle_; }
		void reset() noexcept { if (owner_ && handle_) owner_->CloseScatterHandle(handle_); handle_ = nullptr; }
		[[nodiscard]] VMMDLL_SCATTER_HANDLE release() noexcept { auto h = handle_; handle_ = nullptr; return h; }
	private:
		Memory* owner_ = nullptr;
		VMMDLL_SCATTER_HANDLE handle_ = nullptr;
	};

	[[nodiscard]] ScopedScatterHandle CreateScopedScatterHandle() { return ScopedScatterHandle(this, CreateScatterHandle()); }
	[[nodiscard]] ScopedScatterHandle CreateScopedScatterHandle(int pid) { return ScopedScatterHandle(this, CreateScatterHandle(pid)); }

	void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);
	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);
	void AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);
	bool ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);
	void ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);

	// Attach result
	enum class AttachResult : int { Success = 0, Waiting = 1, Failed = 2 };
	AttachResult last_attach_result = AttachResult::Failed;
	const char* LastAttachResultName() const {
		switch (last_attach_result) {
		case AttachResult::Success: return "Success";
		case AttachResult::Waiting: return "Waiting";
		case AttachResult::Failed: return "Failed";
		}
		return "?";
	}

	// Vector read helper
	template <typename T> std::vector<T> ReadVector(uintptr_t address, size_t count) {
		std::vector<T> buffer(count);
		if (Read(address, buffer.data(), sizeof(T) * count)) return buffer;
		return {};
	}

private:
	// Configuration
	struct TimeoutConfig {
		std::chrono::milliseconds initTimeout;
		std::chrono::milliseconds procInfoTimeout;
		std::chrono::milliseconds procInfoZeroStallMs;
		std::chrono::milliseconds procInfoHeartbeatMs;
		std::chrono::milliseconds procInfoPollMs;
		std::chrono::milliseconds procInfoSettleMs;
		std::chrono::milliseconds vmmMaintenanceDrainMs;
		std::chrono::milliseconds deviceReopenSettleMs;
		std::chrono::milliseconds readTimeout;
		std::chrono::milliseconds scatterTimeout;
		TimeoutConfig() noexcept : initTimeout(30000), procInfoTimeout(100000), procInfoZeroStallMs(15000),
			procInfoHeartbeatMs(5000), procInfoPollMs(500), procInfoSettleMs(1500),
			vmmMaintenanceDrainMs(2500), deviceReopenSettleMs(250), readTimeout(5000), scatterTimeout(5000) {}
	} timeouts_;

	// Runtime DLLs
	struct LibModules { HMODULE VMM = nullptr; HMODULE LEECHCORE = nullptr; HMODULE PDBCRUST = nullptr; };
	inline static LibModules modules{};

	// Process info
	struct CurrentProcessInformation { int PID = 0; size_t base_address = 0; size_t base_size = 0; std::string process_name = ""; };
	inline static CurrentProcessInformation current_process{};

	// State
	inline static BOOLEAN DMA_INITIALIZED = FALSE;
	inline static BOOLEAN PROCESS_INITIALIZED = FALSE;
	inline static uint64_t last_good_dtb = 0;
	VMM_HANDLE pluginsInitializedFor_ = nullptr;
	bool pluginsInitialized_ = false;

	enum class ProcInfoState : int { Uninitialized = 0, Generating, Ready, Invalidated, Stuck, NeverStarted, ProgressTimeout, ReadyButFileMissing, ReadyButFileEmpty, VfsReadFailure, Failed, Cancelled };
	std::atomic<ProcInfoState> procInfoState_{ ProcInfoState::Uninitialized };
	std::atomic<int> procInfoProgress_{ -1 };
	std::atomic<uint64_t> procInfoDtbFileSize_{ 0 };
	std::atomic<unsigned long> procInfoLastNt_{ 0 };
	std::atomic_bool procInfoSessionRecoveryRecommended_{ false };
	std::atomic_bool vmmMaintenanceActive_{ false };
	std::atomic<DWORD> vmmMaintenanceOwnerThreadId_{ 0 };
	mutable std::atomic<uint32_t> activeDataCalls_{ 0 };
	mutable std::atomic<uint32_t> activeScatterHandles_{ 0 };
	mutable std::atomic<uint64_t> blockedDataCalls_{ 0 };
	mutable std::mutex dmaReadMutex_;
	mutable std::recursive_mutex controlPlaneMutex_;
	std::atomic_bool fpgaOpenInProgress_{ false };
	static constexpr size_t kVmmLatencySampleCapacity = 64;
	std::array<std::atomic<uint64_t>, kVmmLatencySampleCapacity> vmmLatencySamples_{};
	std::atomic<uint64_t> vmmLatencySampleIndex_{ 0 };
	int generationProcessPid_ = 0;
	bool dependencyIntegrityOk_ = true;
	std::string dependencyIntegrityMessage_;
	bool runtimeDependenciesInitialized_ = false;
	bool EnsureRuntimeDependencies();

	uint64_t appliedProcessDtb_ = 0;
	int appliedProcessPid_ = 0;
	mutable std::atomic_uint readFailureCount_{ 0 };
	std::atomic<uint64_t> sessionGeneration_{ 0 };
	std::atomic<uint64_t> processGeneration_{ 0 };
	std::atomic<uint64_t> vmmOpenCount_{ 0 };
	std::atomic<uint64_t> pluginInitializationCount_{ 0 };
	std::atomic<uint64_t> deviceResetCount_{ 0 };
	std::atomic<uint64_t> processBindCount_{ 0 };
	std::atomic<uint64_t> vmmCallCount_{ 0 };
	mutable std::atomic<uint64_t> readRequestCount_{ 0 };
	mutable std::atomic<uint64_t> scatterReadBatchCount_{ 0 };
	std::atomic<uint64_t> maxVmmLatencyMs_{ 0 };
	std::atomic_bool startupDeviceProbeOk_{ false };

	// Cancellation
	std::atomic_bool cancelRequested_{ false };
	std::atomic<uint64_t> cancelEpoch_{ 0 };

	// VAD cache
	mutable std::mutex vadMutex_;
	mutable std::vector<VMMDLL_VAD_EX> vadCache_;
	mutable std::chrono::steady_clock::time_point vadCacheTime_;
	static constexpr std::chrono::seconds VAD_CACHE_TTL{ 30 };

	// Scatter generation tracking
	struct ScatterGeneration final { uint64_t session = 0; uint64_t process = 0; uint32_t preparedRequests = 0; };
	mutable std::mutex scatterMutex_;
	mutable std::unordered_map<VMMDLL_SCATTER_HANDLE, ScatterGeneration> scatterHandles_;

	// Implementation details
	struct VfsProbeResult { bool listed = false; bool found = false; uint64_t size = 0; };
	[[nodiscard]] VfsProbeResult ProbeProcInfoDtb(const char* timingLabel);
	void ResetProcessState(bool preserveAppliedOverride) noexcept;
	void ResetVmmState() noexcept;
	void ResetDeviceState() noexcept;
	void RegisterScatterHandle(VMMDLL_SCATTER_HANDLE handle) const;
	[[nodiscard]] bool ScatterHandleIsCurrent(VMMDLL_SCATTER_HANDLE handle) const noexcept;
	void InvalidateScatterHandles(const char* reason) noexcept;
	bool EnsurePluginsInitialized();
	bool ApplyProcessDtbOverride(uint64_t dtb);
	bool ClearProcessDtbOverride();
	bool WaitForProcInfo(int timeout_sec, bool* out_stuck = nullptr, bool* out_cancelled = nullptr);
	[[nodiscard]] bool IsVmmMaintenanceOwner() const noexcept;
	[[nodiscard]] bool TryEnterDataCall(bool& counted) const noexcept;
	void LeaveDataCall(bool counted) const noexcept;

	class DataCallLease { public: explicit DataCallLease(const Memory* owner) noexcept : owner_(owner) { allowed_ = owner_ && owner_->TryEnterDataCall(counted_); } ~DataCallLease() { if (owner_) owner_->LeaveDataCall(counted_); } DataCallLease(const DataCallLease&) = delete; DataCallLease& operator=(const DataCallLease&) = delete; [[nodiscard]] explicit operator bool() const noexcept { return allowed_; } private: const Memory* owner_ = nullptr; bool counted_ = false; bool allowed_ = false; };

	bool DumpMemoryMap(bool debug = false);
	bool SetFPGA();

	std::shared_ptr<c_keys> key;
	c_registry registry;
	c_shellcode shellcode;

	// FPGA handle
	VmmSession vHandle{};

	friend class MemoryDma::DmaTransport;
	friend class MemoryDma::DeviceManager;
	friend class MemoryDma::ProcessManager;
	friend class MemoryDma::ScatterHandleRegistry;
};

extern Memory mem;