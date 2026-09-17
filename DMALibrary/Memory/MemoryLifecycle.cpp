#include "pch.h"
#include "Memory.h"



#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
	class LifecycleScopedVmmTiming final
	{
	public:
		LifecycleScopedVmmTiming(const char* name,
			std::atomic<uint64_t>& callCount,
			std::atomic<uint64_t>& maxLatency,
			std::array<std::atomic<uint64_t>, 64>& latencySamples,
			std::atomic<uint64_t>& latencySampleIndex)
			: name_(name), callCount_(callCount), maxLatency_(maxLatency),
			  latencySamples_(latencySamples), latencySampleIndex_(latencySampleIndex),
			  started_(std::chrono::steady_clock::now())
		{
			callCount_.fetch_add(1, std::memory_order_relaxed);
		}

		~LifecycleScopedVmmTiming()
		{
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started_).count();
			const uint64_t value = elapsed < 0 ? 0ULL : static_cast<uint64_t>(elapsed);
			uint64_t current = maxLatency_.load(std::memory_order_relaxed);
			while (value > current && !maxLatency_.compare_exchange_weak(
				current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
			}
			const uint64_t sample = latencySampleIndex_.fetch_add(1, std::memory_order_relaxed);
			latencySamples_[sample % latencySamples_.size()].store(value, std::memory_order_relaxed);
			if (value >= 250)
				std::cout << "[VMM][SLOW] call=" << name_ << " duration_ms=" << value << '\n';
		}

	private:
		const char* name_;
		std::atomic<uint64_t>& callCount_;
		std::atomic<uint64_t>& maxLatency_;
		std::array<std::atomic<uint64_t>, 64>& latencySamples_;
		std::atomic<uint64_t>& latencySampleIndex_;
		std::chrono::steady_clock::time_point started_;
	};
}

Memory::DiagnosticsSnapshot Memory::GetDiagnosticsSnapshot() const noexcept
{
	DiagnosticsSnapshot snapshot{};
	snapshot.sessionGeneration = sessionGeneration_.load(std::memory_order_acquire);
	snapshot.processGeneration = processGeneration_.load(std::memory_order_acquire);
	snapshot.vmmOpenCount = vmmOpenCount_.load(std::memory_order_relaxed);
	snapshot.pluginInitializationCount = pluginInitializationCount_.load(std::memory_order_relaxed);
	snapshot.deviceResetCount = deviceResetCount_.load(std::memory_order_relaxed);
	snapshot.processBindCount = processBindCount_.load(std::memory_order_relaxed);
	snapshot.vmmCallCount = vmmCallCount_.load(std::memory_order_relaxed);
	snapshot.readRequestCount = readRequestCount_.load(std::memory_order_relaxed);
	snapshot.scatterReadBatchCount = scatterReadBatchCount_.load(std::memory_order_relaxed);
	snapshot.scatterHandlesCreated = OmniGhost::MemoryCounters::GetScatterHandlesCreated();
	snapshot.scatterHandlesDestroyed = OmniGhost::MemoryCounters::GetScatterHandlesDestroyed();
	snapshot.scatterHandlesLive = OmniGhost::MemoryCounters::GetScatterHandlesLive();
	snapshot.maxVmmLatencyMs = maxVmmLatencyMs_.load(std::memory_order_relaxed);
	const uint64_t sampleIndex = vmmLatencySampleIndex_.load(std::memory_order_acquire);
	const size_t sampleCount = static_cast<size_t>((std::min)(sampleIndex, static_cast<uint64_t>(vmmLatencySamples_.size())));
	if (sampleCount > 0) {
		std::array<uint64_t, kVmmLatencySampleCapacity> samples{};
		uint64_t total = 0;
		for (size_t i = 0; i < sampleCount; ++i)
			total += (samples[i] = vmmLatencySamples_[i].load(std::memory_order_relaxed));
		snapshot.vmmLatencyAverageMs = total / sampleCount;
		std::sort(samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(sampleCount));
		auto percentile = [&](size_t numerator, size_t denominator) -> uint64_t {
			const size_t index = ((sampleCount - 1) * numerator) / denominator;
			return samples[index];
		};
		snapshot.vmmLatencyP50Ms = percentile(50, 100);
		snapshot.vmmLatencyP95Ms = percentile(95, 100);
	}
	snapshot.readFailureCount = readFailureCount_.load(std::memory_order_relaxed);
	snapshot.blockedDataCalls = blockedDataCalls_.load(std::memory_order_relaxed);
	snapshot.activeDataCalls = activeDataCalls_.load(std::memory_order_acquire);
	snapshot.activeScatterHandles = activeScatterHandles_.load(std::memory_order_acquire);
	snapshot.maintenanceActive = vmmMaintenanceActive_.load(std::memory_order_acquire);
	snapshot.procInfoSessionRecoveryRecommended = procInfoSessionRecoveryRecommended_.load(std::memory_order_acquire);
	snapshot.deviceOpen = vHandle != nullptr;
	snapshot.deviceDetected = snapshot.deviceOpen || startupDeviceProbeOk_.load(std::memory_order_acquire);
	snapshot.processInitialized = PROCESS_INITIALIZED != FALSE;
	snapshot.processId = current_process.PID;
	snapshot.procInfoState = static_cast<int>(procInfoState_.load(std::memory_order_acquire));
	snapshot.procInfoProgress = procInfoProgress_.load(std::memory_order_acquire);
	snapshot.procInfoDtbFileSize = procInfoDtbFileSize_.load(std::memory_order_acquire);
	snapshot.procInfoLastNt = procInfoLastNt_.load(std::memory_order_acquire);
	snapshot.dependencyIntegrityOk = dependencyIntegrityOk_;
	return snapshot;
}


bool Memory::IsVmmMaintenanceOwner() const noexcept
{
	return vmmMaintenanceActive_.load(std::memory_order_acquire)
		&& vmmMaintenanceOwnerThreadId_.load(std::memory_order_acquire) == GetCurrentThreadId();
}

bool Memory::TryEnterDataCall(bool& counted) const noexcept
{
	counted = false;
	if (IsVmmMaintenanceOwner())
		return true;

	if (vmmMaintenanceActive_.load(std::memory_order_acquire)) {
		blockedDataCalls_.fetch_add(1, std::memory_order_relaxed);
		return false;
	}

	activeDataCalls_.fetch_add(1, std::memory_order_acq_rel);
	if (vmmMaintenanceActive_.load(std::memory_order_acquire)) {
		activeDataCalls_.fetch_sub(1, std::memory_order_acq_rel);
		blockedDataCalls_.fetch_add(1, std::memory_order_relaxed);
		return false;
	}
	counted = true;
	return true;
}

void Memory::LeaveDataCall(bool counted) const noexcept
{
	if (counted)
		activeDataCalls_.fetch_sub(1, std::memory_order_acq_rel);
}

bool Memory::BeginVmmMaintenance(const char* reason, DWORD drainTimeoutMs)
{
	const DWORD tid = GetCurrentThreadId();
	if (IsVmmMaintenanceOwner())
		return true;

	bool expected = false;
	if (!vmmMaintenanceActive_.compare_exchange_strong(
			expected, true, std::memory_order_acq_rel, std::memory_order_acquire)) {
		std::cout << "[VMM][Maintenance] busy owner_tid="
			<< vmmMaintenanceOwnerThreadId_.load(std::memory_order_acquire) << "\n";
		return false;
	}
	vmmMaintenanceOwnerThreadId_.store(tid, std::memory_order_release);

	std::cout << "[VMM][Maintenance] begin reason="
		<< (reason && reason[0] ? reason : "unspecified")
		<< " owner_tid=" << tid
		<< " drain_timeout_ms=" << drainTimeoutMs << "\n";

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(drainTimeoutMs);
	while (activeDataCalls_.load(std::memory_order_acquire) != 0
		|| activeScatterHandles_.load(std::memory_order_acquire) != 0) {
		if (std::chrono::steady_clock::now() >= deadline) {
			std::cout << "[VMM][Maintenance] drain TIMEOUT active_calls="
				<< activeDataCalls_.load(std::memory_order_acquire)
				<< " active_scatter=" << activeScatterHandles_.load(std::memory_order_acquire)
				<< "\n";
			vmmMaintenanceOwnerThreadId_.store(0, std::memory_order_release);
			vmmMaintenanceActive_.store(false, std::memory_order_release);
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}

	std::cout << "[VMM][Maintenance] drained normal data-plane calls\n";
	return true;
}

void Memory::EndVmmMaintenance() noexcept
{
	if (!IsVmmMaintenanceOwner())
		return;
	std::cout << "[VMM][Maintenance] end blocked_calls="
		<< blockedDataCalls_.load(std::memory_order_relaxed) << "\n";
	vmmMaintenanceOwnerThreadId_.store(0, std::memory_order_release);
	vmmMaintenanceActive_.store(false, std::memory_order_release);
}

bool Memory::ReopenVmmSession(bool memMap, bool debug)
{
	if (!IsVmmMaintenanceOwner()) {
		std::cout << "[VMM][Recovery] refused: caller does not own maintenance gate\n";
		return false;
	}

	const auto oldSession = SessionGeneration();
	std::cout << "[VMM][Recovery] rebuilding full VMM session old_generation="
		<< oldSession << "\n";
	deviceResetCount_.fetch_add(1, std::memory_order_relaxed);

	// This is an in-operation recovery boundary, so do not mutate cancellationEpoch_.
	// Closing the VMM session destroys any stuck procinfo/plugin context.
	ResetDeviceState();
	const auto settleMs = timeouts_.deviceReopenSettleMs.count() > 0
		? timeouts_.deviceReopenSettleMs
		: std::chrono::milliseconds(250);
	std::this_thread::sleep_for(settleMs);

	if (IsCancellationRequested()) {
		std::cout << "[VMM][Recovery] cancelled before reopen\n";
		return false;
	}

	if (!Init(std::string(), memMap, debug)) {
		std::cout << "[VMM][Recovery] fresh session open FAILED\n";
		return false;
	}

	procInfoSessionRecoveryRecommended_.store(false, std::memory_order_release);
	std::cout << "[VMM][Recovery] fresh session READY new_generation="
		<< SessionGeneration() << "\n";
	return true;
}

bool Memory::ProbePhysicalDataPath()
{
	if (!this->vHandle)
		return false;
	if (vmmMaintenanceActive_.load(std::memory_order_acquire) && !IsVmmMaintenanceOwner())
		return false;

	PVMMDLL_MAP_PHYSMEM rawMap = nullptr;
	{
		LifecycleScopedVmmTiming timing("Map_GetPhysMem(probe)", vmmCallCount_, maxVmmLatencyMs_,
			vmmLatencySamples_, vmmLatencySampleIndex_);
		if (!VMMDLL_Map_GetPhysMem(this->vHandle, &rawMap) || !rawMap) {
			std::cout << "[DMA][DataPath] physical map unavailable\n";
			return false;
		}
	}
	VmmOwned<VMMDLL_MAP_PHYSMEM> physMap(rawMap);

	BYTE buffer[64]{};
	const DWORD physicalPid = static_cast<DWORD>(-1);
	const DWORD maxProbeRanges = (std::min)(rawMap->cMap, static_cast<DWORD>(8));
	for (DWORD i = 0; i < maxProbeRanges; ++i) {
		const auto& range = rawMap->pMap[i];
		if (range.cb < sizeof(buffer))
			continue;

		DWORD bytesRead = 0;
		const ULONG64 pa = range.pa;
		const auto t0 = std::chrono::steady_clock::now();
		const BOOL ok = VMMDLL_MemReadEx(this->vHandle, physicalPid, pa,
			buffer, static_cast<DWORD>(sizeof(buffer)), &bytesRead, VMMDLL_FLAG_NOCACHE);
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - t0).count();

		std::cout << "[DMA][DataPath] physical_read pa=0x" << std::hex << pa << std::dec
			<< " result=" << (ok ? "OK" : "FAIL")
			<< " bytes=" << bytesRead
			<< " duration_ms=" << elapsed << "\n";

		if (ok && bytesRead == sizeof(buffer))
			return true;
	}

	std::cout << "[DMA][DataPath] physical acquisition probe FAILED across "
		<< maxProbeRanges << " ranges\n";
	return false;
}
