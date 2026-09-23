#pragma once
#include "../pch.h"
#include "InputManager.h"
#include "Registry.h"
#include "Shellcode.h"
#include "../nt/structs.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <array>
#include <mutex>
#include <unordered_map>


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
// Process-wide (not part of sizeof(Memory) — avoids ODR/C4743 on partial rebuilds).
uint64_t GetScatterHandlesCreated() noexcept;
uint64_t GetScatterHandlesDestroyed() noexcept;
uint32_t GetScatterHandlesLive() noexcept;
} // namespace OmniGhost::MemoryCounters

class Memory
{
private:
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

		TimeoutConfig() noexcept
			: initTimeout(30000), procInfoTimeout(100000), procInfoZeroStallMs(15000),
			  procInfoHeartbeatMs(5000), procInfoPollMs(500), procInfoSettleMs(1500),
			  vmmMaintenanceDrainMs(2500), deviceReopenSettleMs(250), readTimeout(5000),
			  scatterTimeout(5000) {}
	};

	// Runtime DLLs are process-lifetime dependencies. Keep every successful load
	// explicitly tracked so optional support DLLs are not anonymous leaked refs.
	// They intentionally remain loaded until process teardown: MemProcFS/LeechCore
	// may retain transitive references while the VMM session is alive.
	struct LibModules
	{
		HMODULE VMM = nullptr;
		HMODULE LEECHCORE = nullptr;
		HMODULE PDBCRUST = nullptr;
	};

	static inline LibModules modules { };

	struct CurrentProcessInformation
	{
		int PID = 0;
		size_t base_address = 0;
		size_t base_size = 0;
		std::string process_name = "";
	};

	static inline CurrentProcessInformation current_process { };

	static inline BOOLEAN DMA_INITIALIZED = FALSE;
	static inline BOOLEAN PROCESS_INITIALIZED = FALSE;
	static inline uint64_t last_good_dtb = 0;

	// Plugins must be tied to the live VMM handle generation (not a free static).
	VMM_HANDLE pluginsInitializedFor_ = nullptr;
	bool pluginsInitialized_ = false;

	// misc/procinfo lifecycle (PROCESS_DTB triggers internal SLOW refresh → invalidates).
	enum class ProcInfoState : int {
		Uninitialized = 0,
		Generating,
		Ready,
		Invalidated,
		Stuck,
		NeverStarted,
		ProgressTimeout,
		ReadyButFileMissing,
		ReadyButFileEmpty,
		VfsReadFailure,
		Failed,
		Cancelled
	};
	std::atomic<ProcInfoState> procInfoState_{ ProcInfoState::Uninitialized };
	std::atomic<int> procInfoProgress_{ -1 };
	std::atomic<uint64_t> procInfoDtbFileSize_{ 0 };
	std::atomic<unsigned long> procInfoLastNt_{ 0 }; // NTSTATUS from last VfsRead
	std::atomic_bool procInfoSessionRecoveryRecommended_{ false };

	// During a VMM recovery/reinit the owning worker may keep using Memory, while
	// normal frame reads are rejected. This prevents UI/ESP/scatter traffic from
	// competing with ProcInfo or racing a VMM handle replacement.
	std::atomic_bool vmmMaintenanceActive_{ false };
	std::atomic<DWORD> vmmMaintenanceOwnerThreadId_{ 0 };
	mutable std::atomic<uint32_t> activeDataCalls_{ 0 };
	mutable std::atomic<uint32_t> activeScatterHandles_{ 0 };
	mutable std::atomic<uint64_t> blockedDataCalls_{ 0 };
	struct ScatterGeneration final
	{
		uint64_t session = 0;
		uint64_t process = 0;
	};
	mutable std::mutex scatterMutex_;
	mutable std::unordered_map<VMMDLL_SCATTER_HANDLE, ScatterGeneration> scatterHandles_;

	// Operation lifecycle. Cancellation is owned by Memory so callers do not
	// have to expose their globals inside Memory.cpp.
	std::atomic_bool cancelRequested_{ false };
	std::atomic<uint64_t> cancelEpoch_{ 0 };

	// Track the override already applied to the current PID. This prevents a redundant
	// PROCESS_DTB ConfigSet (and its implicit SLOW refresh) when the exact same override
	// is already active and a live MZ validation has just failed.
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
	// Data-plane counters are independent from VMM control-plane timing. The
	// diagnostics UI turns these monotonic values into rates without extra reads.
	mutable std::atomic<uint64_t> readRequestCount_{ 0 };
	mutable std::atomic<uint64_t> scatterReadBatchCount_{ 0 };
	std::atomic<uint64_t> maxVmmLatencyMs_{ 0 };
	// Passive startup/status result only. OmniGhost deliberately does not open
	// the FPGA during startup just to paint a status indicator; real device open
	// happens after an explicit game launch.
	std::atomic_bool startupDeviceProbeOk_{ false };
	// Serializes FPGA/VMM control-plane lifecycle operations. Concurrent opens/resets
	// against the same physical board can wedge vendor drivers or firmware.
	// Recursive only so compound lifecycle operations (Reconnect/Rebind) can own
	// one uninterrupted control-plane transaction while reusing Init/Reset.
	mutable std::recursive_mutex controlPlaneMutex_;
	static constexpr size_t kVmmLatencySampleCapacity = 64;
	std::array<std::atomic<uint64_t>, kVmmLatencySampleCapacity> vmmLatencySamples_{};
	std::atomic<uint64_t> vmmLatencySampleIndex_{ 0 };
	int generationProcessPid_ = 0;
	bool dependencyIntegrityOk_ = true;
	std::string dependencyIntegrityMessage_;
	bool runtimeDependenciesInitialized_ = false;
	bool EnsureRuntimeDependencies();

private:
	TimeoutConfig timeouts_{};
	static constexpr int kDefaultProcInfoTimeoutSeconds = 100;
	static constexpr int kDefaultProcInfoZeroStallMs = 15000;
	static constexpr int kDefaultProcInfoHeartbeatMs = 5000;
	static constexpr int kDefaultProcInfoPollMs = 500;
	static constexpr int kDefaultProcInfoSettleMs = 1500;

	struct VfsProbeResult
	{
		bool listed = false;
		bool found = false;
		uint64_t size = 0;
	};

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

	class DataCallLease
	{
	public:
		explicit DataCallLease(const Memory* owner) noexcept : owner_(owner) {
			allowed_ = owner_ && owner_->TryEnterDataCall(counted_);
		}
		~DataCallLease() {
			if (owner_)
				owner_->LeaveDataCall(counted_);
		}
		DataCallLease(const DataCallLease&) = delete;
		DataCallLease& operator=(const DataCallLease&) = delete;
		[[nodiscard]] explicit operator bool() const noexcept { return allowed_; }
	private:
		const Memory* owner_ = nullptr;
		bool counted_ = false;
		bool allowed_ = false;
	};
	/**
	*Dumps the systems Current physical memory pages
	*To a file so we can use it in our DMA (:
	*This file it created to %temp% folder
	*@return true if successful, false if not.
	*/
	bool DumpMemoryMap(bool debug = false);

	/**
	* brief Removes basic information related to the FPGA device
	* This is required before any DMA operations can be done.
	* To ensure the optimal safety in game cheating.
	* @return true if successful, false if not.
	*/
	bool SetFPGA();

	//shared pointer
	std::shared_ptr<c_keys> key;
#ifndef OMNIGHOST_READONLY_MODE
	c_registry registry;
	c_shellcode shellcode;
#endif

	/*this->registry_ptr = std::make_shared<c_registry>(*this);
	this->key_ptr = std::make_shared<c_keys>(*this);*/

public:
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

	// Debug/diagnostics: mark the ImGui/render thread. DMA calls from that
	// thread are logged (Release) / asserted (Debug) but never crash Release.
	static void SetRenderThreadId(std::thread::id id) noexcept;
	// Optional tag for SLOW_DMA_CALL attribution (thread-local, acquisition path).
	static void SetDmaCallTag(const char* tag) noexcept;
	static void SetDmaScanId(uint64_t id) noexcept;
	static void SetDmaLane(const char* lane) noexcept;
	// Per-scan DMA accumulators (thread-local; reset when scan_id changes).
	struct ScanDmaStats {
		uint64_t scan_id = 0;
		int qread_calls = 0;
		double qread_total_ms = 0.0;
		double qread_max_ms = 0.0;
		int scatter_calls = 0;
		double scatter_total_ms = 0.0;
		double scatter_max_ms = 0.0;
		// Top callsite by total time
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
	// Cumulative lock/maintenance block wait (microseconds) for current thread.
	static void ResetThreadDmaWaitUs() noexcept;
	static uint64_t ConsumeThreadDmaWaitUs() noexcept;
	[[nodiscard]] static std::thread::id GetRenderThreadId() noexcept;

	void SetTimeouts(const TimeoutConfig& config) noexcept { timeouts_ = config; }
	[[nodiscard]] const TimeoutConfig& GetTimeouts() const noexcept { return timeouts_; }
	// Passive startup check only: inspects the Windows PnP device tree for the
	// FT60x/D3XX bridge and verifies that its device node is started. It never
	// opens LeechCore/VMMDLL/the FPGA and performs no DMA transaction.
	bool ProbeDeviceAvailability();
	/** Passive PnP-only probe safe for an isolated startup worker. */
	[[nodiscard]] static bool ProbeDevicePresence();
	[[nodiscard]] uint64_t SessionGeneration() const noexcept { return sessionGeneration_.load(std::memory_order_acquire); }
	[[nodiscard]] uint64_t ProcessGeneration() const noexcept { return processGeneration_.load(std::memory_order_acquire); }

	/**
	 * brief Constructor takes a wide string of the process.
	 * Expects that all the libraries are in the root dir
	 */
	Memory();
	~Memory();
	Memory(const Memory&) = delete;
	Memory& operator=(const Memory&) = delete;
	Memory(Memory&&) = delete;
	Memory& operator=(Memory&&) = delete;

	/**
	* @brief Gets the registry object
	* @return registry class
	*/
#ifndef OMNIGHOST_READONLY_MODE
	c_registry GetRegistry() { return registry; }
#else
	c_registry GetRegistry() { return c_registry(); } // Return empty registry in read-only mode
#endif

	/**
	* @brief Gets the key object
	* @return key class
	*/
	c_keys* GetKeyboard() { return key.get(); }

	/**
	* @brief Gets the shellcode object
	* @return shellcode class
	*/
#ifndef OMNIGHOST_READONLY_MODE
	c_shellcode GetShellcode() { return shellcode; }
#else
	c_shellcode GetShellcode() { return c_shellcode(); } // Return empty shellcode in read-only mode
#endif

	/**
	* brief Initializes the DMA
	* This is required before any DMA operations can be done.
	* @param process_name the name of the process
	* @param memMap if true, will dump the memory map to a file	& make the DMA use it.
	* @return true if successful, false if not.
	*/
	bool Init(std::string process_name, bool memMap = true, bool debug = false,
		bool quickDeviceProbe = false);

	/** Clear process bind so next Init() re-attaches PID/modules (city change / server join). */
	void InvalidateProcess();

	/** Close FPGA and allow a full device reopen on next Init. */
	void ResetDevice();

	/** Close the current device/process session without scheduling a reopen. */
	void Close() noexcept;

	/** Explicit full device reset followed by a fresh open. */
	bool Reconnect(bool memMap = true, bool debug = false);

	/** Keep the current device session and bind a fresh process context. */
	bool Rebind(std::string process_name, bool memMap = true, bool debug = false);

	/** True if VMM handle is open. */
	bool IsDeviceOpen() const { return vHandle != nullptr; }

	/**
	 * Pause normal data-plane reads while the calling worker performs a VMM
	 * maintenance/recovery operation. Returns false if another maintenance owner
	 * already exists or active scatter/read operations do not drain in time.
	 */
	bool BeginVmmMaintenance(const char* reason = nullptr, DWORD drainTimeoutMs = 2500);
	void EndVmmMaintenance() noexcept;
	[[nodiscard]] bool IsVmmMaintenanceActive() const noexcept {
		return vmmMaintenanceActive_.load(std::memory_order_acquire);
	}

	/** Close only the VMM/device session and create a completely fresh session. */
	bool ReopenVmmSession(bool memMap = true, bool debug = false);

	/** Read-only physical-memory acquisition probe used after a VMM session rebuild. */
	bool ProbePhysicalDataPath();

	/** True when ProcInfo stayed at 0% and the current VMM session should be rebuilt. */
	[[nodiscard]] bool ProcInfoSessionRecoveryRecommended() const noexcept {
		return procInfoSessionRecoveryRecommended_.load(std::memory_order_acquire);
	}

	[[nodiscard]] bool DependencyIntegrityOk() const noexcept { return dependencyIntegrityOk_; }
	[[nodiscard]] const std::string& DependencyIntegrityMessage() const noexcept { return dependencyIntegrityMessage_; }

	/** Request cancellation of long-running attach/procinfo waits. Thread-safe. */
	void RequestCancel() noexcept {
		cancelEpoch_.fetch_add(1, std::memory_order_acq_rel);
		cancelRequested_.store(true, std::memory_order_release);
	}

	/** Clear a previous cancellation before starting a new lifecycle operation. */
	void ClearCancel() noexcept { cancelRequested_.store(false, std::memory_order_release); }

	/** Snapshot used by a long-running operation so a later cancel cannot be hidden by a new ClearCancel(). */
	uint64_t CancellationEpoch() const noexcept {
		return cancelEpoch_.load(std::memory_order_acquire);
	}

	/** True when the current Memory operation should stop as soon as practical. */
	bool IsCancellationRequested() const noexcept {
		return cancelRequested_.load(std::memory_order_acquire);
	}

	/** Epoch-aware cancellation check for long-running operations. */
	bool IsCancellationRequested(uint64_t operation_epoch) const noexcept {
		return cancelRequested_.load(std::memory_order_acquire)
			|| cancelEpoch_.load(std::memory_order_acquire) != operation_epoch;
	}


	/*This part here is things related to the process information such as Base daddy, Size ect.*/

	/**
	* brief Gets the process id of the process
	* @param process_name the name of the process
	* @return the process id of the process
	*/
	DWORD GetPidFromName(std::string process_name);

	/** Enumerate remote process names with one VMM request. Intended for
	 * lightweight launcher presence detection, never process attachment. */
	std::vector<std::string> GetProcessNames();

	/**
	* brief Gets all the processes id(s) of the process
	* @param process_name the name of the process
	* @returns all the processes id(s) of the process
	*/
	std::vector<int> GetPidListFromName(std::string process_name);

	/**
	* \brief Gets the module list of the process
	* \param process_name the name of the process 
	* \return all the module names of the process 
	*/
	std::vector<std::string> GetModuleList(std::string process_name);

	/**
	* \brief Gets the process information
	* \return the process information
	*/
	VMMDLL_PROCESS_INFORMATION GetProcessInformation();

	/**
	* \brief Gets the process peb
	* \return the process peb 
	*/
	PEB GetProcessPeb();

	/**
	* brief Gets the base address of the process
	* @param module_name the name of the module
	* @return the base address of the process
	*/
	size_t GetBaseDaddy(std::string module_name);

	/**
	* brief Gets the base size of the process
	* @param module_name the name of the module
	* @return the base size of the process
	*/
	size_t GetBaseSize(std::string module_name);

	/**
	* brief Gets the export table address of the process
	* @param import the name of the export
	* @param process the name of the process
	* @param module the name of the module that you wanna find the export in
	* @return the export table address of the export
	*/
	uintptr_t GetExportTableAddress(std::string import, std::string process, std::string module);

	/**
	* brief Gets the import table address of the process
	* @param import the name of the import
	* @param process the name of the process
	* @param module the name of the module that you wanna find the import in
	* @return the import table address of the import
	*/
	uintptr_t GetImportTableAddress(std::string import, std::string process, std::string module);

	/**
	 * \brief This fixes the CR3 fuckery that EAC does.
	 * It fixes it by iterating over all DTB's that exist within your system and looks for specific ones
	 * that nolonger have a PID assigned to them, aka their pid is 0
	 * it then puts it in a vector to later try each possible DTB to find the DTB of the process.
	 * NOTE: FixCr3 itself uses the MemProcFS process-info/DTB path. Optional
	 * symbol/PDB facilities are handled separately and OmniGhost disables InfoDB.
	 */
	bool FixCr3();

	/**
	 * \brief Dumps the process memory at address (requires to be a valid PE Header) to the path
	 * \param address the address to the PE Header(BaseAddress)
	 * \param path the path where you wanna save dump to
	 */
	bool DumpMemory(uintptr_t address, std::string path);

	/*This part is where all memory operations are done, such as read, write.*/

	/**
	 * \brief Scans the process for the signature.
	 * \param signature the signature example "48 ? ? ?"
	 * \param range_start Region to start scan from 
	 * \param range_end Region up to where it should scan
	 * \param PID (OPTIONAL) where to read to?
	 * \return address of signature
	 */
	uint64_t FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID = 0);

	/**
	 * \brief Writes memory to the process.
	 * In OMNIGHOST_READONLY_MODE builds these always return false (logged no-op);
	 * the normal read-only distribution never mutates target memory.
	 * \param address The address to write to
	 * \param buffer The buffer to write
	 * \param size The size of the buffer
	 * \return true when the VMM accepted the write
	 */
	bool Write(uintptr_t address, void* buffer, size_t size) const;
	bool Write(uintptr_t address, void* buffer, size_t size, int pid) const;

	/**
	 * \brief Writes memory to the process using a template
	 * \param address to write to
	 * \param value the value you'll write to the address
	 */
	template <typename T>
	bool Write(void* address, T value)
	{
		return Write(reinterpret_cast<uintptr_t>(address), &value, sizeof(T));
	}

	template <typename T>
	bool Write(uintptr_t address, T value)
	{
		return Write(address, &value, sizeof(T));
	}

	/**
	* brief Reads memory from the process
	* @param address The address to read from
	* @param buffer The buffer to read to
	* @param size The size of the buffer
	* @return true if successful, false if not.
	*/
	bool Read(uintptr_t address, void* buffer, size_t size) const;
	bool Read(uintptr_t address, void* buffer, size_t size, int pid) const;
	// Diagnostic variant used by runtime probes. Reports the exact number of bytes
	// returned by the successful/final VMM read attempt; never writes target memory.
	bool ReadWithCount(uintptr_t address, void* buffer, size_t size, size_t& bytesRead) const;

	/**
	* brief Reads memory from the process using a template
	* @param address The address to read from
	* @return the value read from the process
	*/
	template <typename T>
	T Read(void* address)
	{
		T buffer { };
		memset(&buffer, 0, sizeof(T));
		Read(reinterpret_cast<uint64_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T));

		return buffer;
	}

	template <typename T>
	T Read(uint64_t address)
	{
		return Read<T>(reinterpret_cast<void*>(address));
	}

	/**
	* brief Reads memory from the process using a template and pid
	* @param address The address to read from
	* @param pid The process id of the process
	* @return the value read from the process
	*/
	template <typename T>
	T Read(void* address, int pid)
	{
		T buffer { };
		memset(&buffer, 0, sizeof(T));
		Read(reinterpret_cast<uint64_t>(address), reinterpret_cast<void*>(&buffer), sizeof(T), pid);

		return buffer;
	}

	template <typename T>
	T Read(uint64_t address, int pid)
	{
		return Read<T>(reinterpret_cast<void*>(address), pid);
	}

	/**
	* brief Reads a chain of offsets from the address
	* @param address The address to read from
	* @param a vector of offset values to read through
	* @return the value read from the chain
	*/
	uint64_t ReadChain(uint64_t base, const std::vector<uint64_t>& offsets)
	{
		uint64_t result = Read<uint64_t>(base + offsets.at(0));
		for (int i = 1; i < offsets.size(); i++) result = Read<uint64_t>(result + offsets.at(i));
		return result;
	}

	/**
	 * \brief Create a scatter handle, this is used for scatter read/write requests
	 * \return Scatter handle
	 */
	VMMDLL_SCATTER_HANDLE CreateScatterHandle() const;
	VMMDLL_SCATTER_HANDLE CreateScatterHandle(int pid) const;

	class ScopedScatterHandle final
	{
	public:
		ScopedScatterHandle() noexcept = default;
		ScopedScatterHandle(Memory* owner, VMMDLL_SCATTER_HANDLE handle) noexcept
			: owner_(owner), handle_(handle) {}
		~ScopedScatterHandle() { reset(); }
		ScopedScatterHandle(const ScopedScatterHandle&) = delete;
		ScopedScatterHandle& operator=(const ScopedScatterHandle&) = delete;
		ScopedScatterHandle(ScopedScatterHandle&& other) noexcept
			: owner_(other.owner_), handle_(other.release()) { other.owner_ = nullptr; }
		ScopedScatterHandle& operator=(ScopedScatterHandle&& other) noexcept {
			if (this != &other) {
				reset();
				owner_ = other.owner_;
				handle_ = other.release();
				other.owner_ = nullptr;
			}
			return *this;
		}
		[[nodiscard]] VMMDLL_SCATTER_HANDLE get() const noexcept { return handle_; }
		[[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }
		[[nodiscard]] operator VMMDLL_SCATTER_HANDLE() const noexcept { return handle_; }
		void reset() noexcept {
			if (owner_ && handle_)
				owner_->CloseScatterHandle(handle_);
			handle_ = nullptr;
		}
		[[nodiscard]] VMMDLL_SCATTER_HANDLE release() noexcept {
			auto handle = handle_;
			handle_ = nullptr;
			return handle;
		}
	private:
		Memory* owner_ = nullptr;
		VMMDLL_SCATTER_HANDLE handle_ = nullptr;
	};

	[[nodiscard]] ScopedScatterHandle CreateScopedScatterHandle() {
		return ScopedScatterHandle(this, CreateScatterHandle());
	}
	[[nodiscard]] ScopedScatterHandle CreateScopedScatterHandle(int pid) {
		return ScopedScatterHandle(this, CreateScatterHandle(pid));
	}

	/**
	 * \brief Closes the scatter handle
	 * \param handle
	 */
	void CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle);

	/**
	 * \brief Adds a scatter read/write request to the handle
	 * \param handle the handle
	 * \param address the address to read/write to 
	 * \param buffer the buffer to read/write to
	 * \param size the size of buffer
	 */
	void AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);
	void AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size);

	/**
	 * \brief Executes all prepared scatter requests, note if you created a scatter handle with a pid
	 * you'll need to specify the pid in the execute function. so we can clear the scatters from the handle.
	 * \param handle 
	 * \param pid 
	 */
	void ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);
	void ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid = 0);

	/*the FPGA handle — always explicit nullptr until Init succeeds*/
	VmmSession vHandle{};

	/** Last FixCr3 / attach outcome for callers (Waiting vs hard Failed). */
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


	/* 
	THIS IS JUST SOMETHING I ADDED FOR THE TRANSFORM TO VECTOR THINGY	
	*/
	template <typename T>
	std::vector<T> ReadVector(uintptr_t address, size_t count)
	{
		std::vector<T> buffer(count);
		if (Read(address, buffer.data(), sizeof(T) * count))
			return buffer;
		return {};
	}
};

extern Memory mem;
