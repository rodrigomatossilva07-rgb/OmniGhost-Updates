#pragma once




#include "../pch.h"
#include "Memory.h"

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>

namespace MemoryDetail {



class ScopedVmmTiming final
{
public:
	ScopedVmmTiming(const char* name,
		std::atomic<uint64_t>& callCount,
		std::atomic<uint64_t>& maxLatency,
		std::array<std::atomic<uint64_t>, 64>& latencySamples,
		std::atomic<uint64_t>& latencySampleIndex) noexcept
		: name_(name), callCount_(callCount), maxLatency_(maxLatency),
		  latencySamples_(latencySamples), latencySampleIndex_(latencySampleIndex),
		  started_(std::chrono::steady_clock::now())
	{
		callCount_.fetch_add(1, std::memory_order_relaxed);
	}

	~ScopedVmmTiming()
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

	ScopedVmmTiming(const ScopedVmmTiming&) = delete;
	ScopedVmmTiming& operator=(const ScopedVmmTiming&) = delete;

private:
	const char* name_;
	std::atomic<uint64_t>& callCount_;
	std::atomic<uint64_t>& maxLatency_;
	std::array<std::atomic<uint64_t>, 64>& latencySamples_;
	std::atomic<uint64_t>& latencySampleIndex_;
	std::chrono::steady_clock::time_point started_;
};




struct PassiveDmaPnpResult
{
	bool candidateFound = false;
	bool pnpStarted = false;
	unsigned long problemCode = 0;
};

[[nodiscard]] PassiveDmaPnpResult ProbePassiveDmaPnp() noexcept;


[[nodiscard]] bool ReadMzHeader(VMM_HANDLE vmm, DWORD pid, uint64_t base) noexcept;

}


#define OMNIGHOST_VMM_TIMING(label) \
	MemoryDetail::ScopedVmmTiming vmmTiming_(label, vmmCallCount_, maxVmmLatencyMs_, vmmLatencySamples_, vmmLatencySampleIndex_)
