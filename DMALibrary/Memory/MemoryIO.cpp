#include "../pch.h"
#include "Memory.h"
#include "MemoryInternal.h"
#include "../../src/platform/session_log.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <atomic>
#include <iostream>
#include <vector>
#include <cstdio>
#include <cstring>
#include <limits>

// WaitOnAddress and WakeByAddressSingle require Windows 8+ and synchronization.lib
#pragma comment(lib, "synchronization.lib")
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602 // Windows 8
#endif


namespace OmniGhost::MemoryCounters {
std::atomic<uint64_t> ScatterHandlesCreated{0};
std::atomic<uint64_t> ScatterHandlesDestroyed{0};
std::atomic<uint32_t> ScatterHandlesLive{0};
} // namespace OmniGhost::MemoryCounters

namespace OmniGhost::MemoryCounters {
uint64_t GetScatterHandlesCreated() noexcept { return ScatterHandlesCreated.load(std::memory_order_relaxed); }
uint64_t GetScatterHandlesDestroyed() noexcept { return ScatterHandlesDestroyed.load(std::memory_order_relaxed); }
uint32_t GetScatterHandlesLive() noexcept { return ScatterHandlesLive.load(std::memory_order_relaxed); }
} // namespace OmniGhost::MemoryCounters

static const char* hexdigits =
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\001\002\003\004\005\006\007\010\011\000\000\000\000\000\000"
"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\012\013\014\015\016\017\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000"
"\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000";

static uint8_t GetByte(const char* hex)
{
	return static_cast<uint8_t>((hexdigits[hex[0]] << 4) | (hexdigits[hex[1]]));
}

uint64_t Memory::FindSignature(const char* signature, uint64_t range_start, uint64_t range_end, int PID)
{
	DataCallLease dataLease(this);
	if (!dataLease)
		return 0;
	if (!signature || signature[0] == '\0' || range_start >= range_end)
		return 0;

	if (PID == 0)
		PID = current_process.PID;

	const uint64_t rangeSize = range_end - range_start;
	if (rangeSize > static_cast<uint64_t>(MAXDWORD) ||
		rangeSize > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))
		return 0;
	std::vector<uint8_t> buffer(static_cast<size_t>(rangeSize));
	const DWORD bufferSize = static_cast<DWORD>(buffer.size());
	{
		std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_);
		const bool readOk = VMMDLL_MemReadEx(this->vHandle, PID, range_start, buffer.data(), bufferSize, 0,
			VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL) != FALSE;
		if (!readOk)
			return 0;
	}

	const char* pat = signature;
	uint64_t first_match = 0;
	for (uint64_t i = range_start; i < range_end; i++)
	{
		if (*pat == '?' || buffer[i - range_start] == GetByte(pat))
		{
			if (!first_match)
				first_match = i;

			if (!pat[2])
				break;

			pat += (*pat == '?') ? 2 : 3;
		}
		else
		{
			pat = signature;
			first_match = 0;
		}
	}

	return first_match;
}

// Write paths. In OMNIGHOST_READONLY_MODE every write is compiled to a logged
// no-op so the normal read-only build cannot mutate target memory.
bool Memory::Write(uintptr_t address, void* buffer, size_t size) const
{
	return Write(address, buffer, size, current_process.PID);
}

bool Memory::Write(uintptr_t address, void* buffer, size_t size, int pid) const
{
#if defined(OMNIGHOST_READONLY_MODE)
	(void)address; (void)buffer; (void)size; (void)pid;
	static std::atomic_bool warned{ false };
	if (!warned.exchange(true))
		std::clog << "[DMA][WRITE] blocked: build is read-only (OMNIGHOST_READONLY_MODE)\n";
	return false;
#else
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle || !buffer)
		return false;
	if (size > static_cast<size_t>(MAXDWORD))
		return false;
	const DWORD byteCount = static_cast<DWORD>(size);
	std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_);
	if (!VMMDLL_MemWrite(this->vHandle, pid, address, static_cast<PBYTE>(buffer), byteCount))
	{
		LOG("[!] Failed to write Memory at 0x%p\n", address);
		return false;
	}
	return true;
#endif
}



namespace {
thread_local const char* t_dmaTag = "untagged";
thread_local const char* t_dmaLane = "unknown";
thread_local uint64_t t_dmaScanId = 0;
thread_local uint64_t t_dmaWaitUs = 0;
thread_local uint64_t t_lastDmaWaitUs = 0;

struct TagAccum {
    const char* tag = "none";
    double total_ms = 0.0;
};
thread_local Memory::ScanDmaStats t_scanStats{};
thread_local TagAccum t_tagAccums[12]{};
thread_local int t_tagAccumCount = 0;

void NoteDmaCall(const char* op, double duration_ms) {
    auto& s = t_scanStats;
    if (op && (op[0] == 'S' || std::strcmp(op, "Scatter") == 0)) {
        s.scatter_calls += 1;
        s.scatter_total_ms += duration_ms;
        if (duration_ms > s.scatter_max_ms) s.scatter_max_ms = duration_ms;
    } else {
        s.qread_calls += 1;
        s.qread_total_ms += duration_ms;
        if (duration_ms > s.qread_max_ms) s.qread_max_ms = duration_ms;
    }
    const char* tag = t_dmaTag ? t_dmaTag : "untagged";
    // accumulate by tag
    int found = -1;
    for (int i = 0; i < t_tagAccumCount; ++i) {
        if (t_tagAccums[i].tag == tag || (t_tagAccums[i].tag && tag && std::strcmp(t_tagAccums[i].tag, tag) == 0)) {
            found = i;
            break;
        }
    }
    if (found < 0 && t_tagAccumCount < 12) {
        found = t_tagAccumCount++;
        t_tagAccums[found].tag = tag;
        t_tagAccums[found].total_ms = 0.0;
    }
    if (found >= 0) {
        t_tagAccums[found].total_ms += duration_ms;
    }
    // refresh top3
    double best[3] = {-1,-1,-1};
    const char* bestT[3] = {"none","none","none"};
    for (int i = 0; i < t_tagAccumCount; ++i) {
        const double v = t_tagAccums[i].total_ms;
        if (v > best[0]) { best[2]=best[1]; bestT[2]=bestT[1]; best[1]=best[0]; bestT[1]=bestT[0]; best[0]=v; bestT[0]=t_tagAccums[i].tag; }
        else if (v > best[1]) { best[2]=best[1]; bestT[2]=bestT[1]; best[1]=v; bestT[1]=t_tagAccums[i].tag; }
        else if (v > best[2]) { best[2]=v; bestT[2]=t_tagAccums[i].tag; }
    }
    s.top_tag = bestT[0]; s.top_tag_ms = best[0] > 0 ? best[0] : 0;
    s.top2_tag = bestT[1]; s.top2_tag_ms = best[1] > 0 ? best[1] : 0;
    s.top3_tag = bestT[2]; s.top3_tag_ms = best[2] > 0 ? best[2] : 0;
}

std::atomic<std::uintptr_t> g_renderThreadHash{0};

std::uintptr_t ThreadHash(std::thread::id id) {
    return static_cast<std::uintptr_t>(std::hash<std::thread::id>{}(id));
}
void WarnIfDmaOnRenderThread(const char* where) {
    const auto h = g_renderThreadHash.load(std::memory_order_relaxed);
    if (!h) return;
    if (ThreadHash(std::this_thread::get_id()) != h) return;
#ifdef _DEBUG
    assert(false && "DMA called on render thread");
#endif
    static std::atomic<uint64_t> s_lastLog{0};
    const uint64_t now = GetTickCount64();
    uint64_t prev = s_lastLog.load(std::memory_order_relaxed);
    if (now - prev < 1000 && prev != 0) return;
    s_lastLog.store(now, std::memory_order_relaxed);
    std::cout << "[CS2] ERROR DMA_ON_RENDER_THREAD where=" << where << std::endl;
}
void LogSlowDma(const char* op, double duration_ms, int requests = 0) {
    if (duration_ms < 10.0) return;
    static std::atomic<uint64_t> s_last{0};
    const uint64_t now = GetTickCount64();
    // Always log if >=50ms; rate-limit 10-50ms to 1/200ms
    if (duration_ms < 50.0) {
        uint64_t prev = s_last.load(std::memory_order_relaxed);
        if (now - prev < 200 && prev != 0) return;
        s_last.store(now, std::memory_order_relaxed);
    }
    char buf[384];
    std::snprintf(buf, sizeof(buf),
        "SLOW_DMA_CALL scan_id=%llu lane=%s tag=%s op=%s duration_ms=%.2f lock_wait_ms=%.3f execute_ms=%.2f requests=%d thread=%lu",
        static_cast<unsigned long long>(t_dmaScanId),
        t_dmaLane ? t_dmaLane : "unknown",
        t_dmaTag ? t_dmaTag : "null",
        op ? op : "?",
        duration_ms,
        static_cast<double>(t_lastDmaWaitUs) / 1000.0,
        duration_ms,
        requests,
        static_cast<unsigned long>(GetCurrentThreadId()));
    OmniGhost::SessionLog::Write(
        duration_ms >= 100.0 ? OmniGhost::SessionLog::Severity::Warning
                             : OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::DMA,
        buf);
    std::cout << "[CS2] " << buf << std::endl;
}
} // namespace

void Memory::SetRenderThreadId(std::thread::id id) noexcept {
    g_renderThreadHash.store(ThreadHash(id), std::memory_order_release);
}
std::thread::id Memory::GetRenderThreadId() noexcept {
    return std::thread::id{};
}
void Memory::SetDmaCallTag(const char* tag) noexcept {
    t_dmaTag = tag ? tag : "untagged";
}
void Memory::SetDmaScanId(uint64_t id) noexcept {
    if (id != t_scanStats.scan_id) {
        t_scanStats = Memory::ScanDmaStats{};
        t_scanStats.scan_id = id;
        t_tagAccumCount = 0;
        for (auto& a : t_tagAccums) { a.tag = "none"; a.total_ms = 0.0; }
    }
    t_dmaScanId = id;
}
Memory::ScanDmaStats Memory::GetScanDmaStats() noexcept {
    t_scanStats.scan_id = t_dmaScanId;
    return t_scanStats;
}
void Memory::ResetScanDmaStats() noexcept {
    t_scanStats = Memory::ScanDmaStats{};
    t_scanStats.scan_id = t_dmaScanId;
    t_tagAccumCount = 0;
}

void Memory::SetDmaLane(const char* lane) noexcept { t_dmaLane = lane ? lane : "unknown"; }

namespace {
thread_local int t_dmaPriority = Memory::DmaPri::Position; // core world by default

// Lightweight spinlock with WaitOnAddress for efficient waiting.
// Replaces busy-spin with yield() with kernel-assisted waiting.
class DmaSpinLock {
    std::atomic<uint32_t> state_{0}; // 0 = unlocked, 1 = locked
    static constexpr uint32_t kSpinCount = 1000; // brief spin before waiting

public:
    void lock() {
        // Fast path: try to acquire immediately
        uint32_t expected = 0;
        if (state_.compare_exchange_strong(expected, 1, std::memory_order_acquire))
            return;

        // Spin briefly for very short critical sections
        for (uint32_t i = 0; i < kSpinCount; ++i) {
            expected = 0;
            if (state_.compare_exchange_weak(expected, 1, std::memory_order_acquire))
                return;
            std::this_thread::yield();
        }

        // Wait efficiently using WaitOnAddress (Windows 8+)
        // Wait until state_ changes from 1 (locked) to 0 (unlocked)
        while (state_.load(std::memory_order_acquire) != 0) {
            WaitOnAddress(&state_, &expected, sizeof(expected), INFINITE);
        }

        // Try to acquire after wake
        expected = 0;
        if (!state_.compare_exchange_strong(expected, 1, std::memory_order_acquire)) {
            // Lost race, retry
            lock();
        }
    }

    void unlock() {
        state_.store(0, std::memory_order_release);
        WakeByAddressSingle(&state_);
    }
};

// RAII guard for DmaSpinLock (like unique_lock but for our spinlock)
class DmaSpinLockGuard {
    DmaSpinLock* lock_ = nullptr;
public:
    DmaSpinLockGuard() = default;
    explicit DmaSpinLockGuard(DmaSpinLock& lock) : lock_(&lock) { lock_->lock(); }
    DmaSpinLockGuard(DmaSpinLockGuard&& other) noexcept : lock_(other.lock_) { other.lock_ = nullptr; }
    DmaSpinLockGuard& operator=(DmaSpinLockGuard&& other) noexcept {
        if (this != &other) {
            if (lock_) lock_->unlock();
            lock_ = other.lock_;
            other.lock_ = nullptr;
        }
        return *this;
    }
    ~DmaSpinLockGuard() { if (lock_) lock_->unlock(); }
    DmaSpinLockGuard(const DmaSpinLockGuard&) = delete;
    DmaSpinLockGuard& operator=(const DmaSpinLockGuard&) = delete;
    
    // Allow checking if guard holds a lock
    explicit operator bool() const noexcept { return lock_ != nullptr; }
};

// Global spinlock for DMA transport (replaces std::mutex for hot path)
static DmaSpinLock g_dmaSpinLock;

// Acquire dmaReadMutex according to the caller's priority band.
// Camera/Position block immediately. Bones/Health yield moderately.
// Weapon/Name/Metadata yield aggressively so higher lanes interleave.
// Returns a guard that unlocks on destruction.
static DmaSpinLockGuard AcquireDmaTransportLock(std::mutex& /*mu*/) {
    const int pri = t_dmaPriority;
    if (pri >= Memory::DmaPri::Position) {
        // High priority: use blocking mutex (create dummy guard, lock mutex directly)
        // Note: we return empty guard; the caller must use std::unique_lock for high priority
        // For API compatibility, we still return a guard but document this case
        return DmaSpinLockGuard(); // empty guard, caller uses unique_lock
    }
    // Lower priorities: use efficient spinlock
    return DmaSpinLockGuard(g_dmaSpinLock);
}
}
void Memory::SetDmaPriority(int priority) noexcept {
	if (priority < 0) priority = 0;
	if (priority > 100) priority = 100;
	t_dmaPriority = priority;
}
int Memory::GetDmaPriority() noexcept { return t_dmaPriority; }
const char* Memory::GetDmaCallTag() noexcept {
    return t_dmaTag ? t_dmaTag : "untagged";
}
void Memory::ResetThreadDmaWaitUs() noexcept { t_dmaWaitUs = 0; t_lastDmaWaitUs = 0; }
uint64_t Memory::ConsumeThreadDmaWaitUs() noexcept {
    const uint64_t v = t_dmaWaitUs;
    t_dmaWaitUs = 0;
    return v;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size) const
{
	WarnIfDmaOnRenderThread("Read");
	DataCallLease dataLease(this);
	if (!dataLease) {
		// Maintenance blocked the data plane — count as wait.
		t_dmaWaitUs += 500; // approximate small wait quantum; caller aggregates
		return false;
	}
	if (!this->vHandle)
		return false;
	
	// Validate address before attempting DMA read
	if (!IsValidReadAddress(address, current_process.PID)) {
		const unsigned fail_count = readFailureCount_.fetch_add(1, std::memory_order_relaxed) + 1;
		if ((fail_count % 200) == 1)
			LOG("[!] Invalid read address 0x%p (x%u)\n", address, fail_count);
		return false;
	}
	
	readRequestCount_.fetch_add(1, std::memory_order_relaxed);
	if (size > static_cast<size_t>(MAXDWORD))
		return false;
	// Serialize only the physical transfer. In particular, do not make a CS2
	// entity scan own the transport while it performs CPU-side processing.
	const auto lockBegin = std::chrono::steady_clock::now();
	auto dmaSpinLockGuard = AcquireDmaTransportLock(dmaReadMutex_);
	std::unique_lock<std::mutex> dmaReadLock;
	if (!dmaSpinLockGuard) {
		// High priority: use blocking mutex
		dmaReadLock = std::unique_lock<std::mutex>(dmaReadMutex_);
	}
	t_lastDmaWaitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - lockBegin).count());
	t_dmaWaitUs += t_lastDmaWaitUs;
	const auto transferBegin = std::chrono::steady_clock::now();
	const DWORD byteCount = static_cast<DWORD>(size);
	DWORD read_size = 0;
	const bool ok1 = VMMDLL_MemReadEx(this->vHandle, current_process.PID, address, static_cast<PBYTE>(buffer), byteCount, &read_size, VMMDLL_FLAG_NOCACHE)
		&& read_size == byteCount;
	bool ok = ok1;
	if (!ok) {
		// Fallback: cached read (sometimes works when NOCACHE fails mid DTB fix)
		read_size = 0;
		ok = VMMDLL_MemReadEx(this->vHandle, current_process.PID, address,
			static_cast<PBYTE>(buffer), byteCount, &read_size, 0) && read_size == byteCount;
	}
	// dmaSpinLockGuard and dmaReadLock automatically unlock on scope exit
	const double ms = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - transferBegin).count();
	NoteDmaCall("Read", ms);
	LogSlowDma("Read", ms, 1);
	if (ok) return true;

	const unsigned fail_count = readFailureCount_.fetch_add(1, std::memory_order_relaxed) + 1;
	if ((fail_count % 200) == 1)
		LOG("[!] Failed to read Memory at 0x%p (x%u)\n", address, fail_count);
	return false;
}

bool Memory::TryRead(uintptr_t address, void* buffer, size_t size, bool& attempted) const
{
	attempted = false;
	WarnIfDmaOnRenderThread("TryRead");
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle || !buffer || size > static_cast<size_t>(MAXDWORD))
		return false;
	
	// Validate address before attempting DMA read
	if (!IsValidReadAddress(address, current_process.PID)) {
		const unsigned fail_count = readFailureCount_.fetch_add(1, std::memory_order_relaxed) + 1;
		if ((fail_count % 200) == 1)
			LOG("[!] Invalid read address 0x%p (x%u)\n", address, fail_count);
		return false;
	}
	
	std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_, std::try_to_lock);
	if (!dmaReadLock.owns_lock())
		return false;
	t_lastDmaWaitUs = 0;
	attempted = true;
	const auto t0 = std::chrono::steady_clock::now();
	readRequestCount_.fetch_add(1, std::memory_order_relaxed);
	const DWORD byteCount = static_cast<DWORD>(size);
	DWORD readSize = 0;
	bool ok = VMMDLL_MemReadEx(this->vHandle, current_process.PID, address,
		static_cast<PBYTE>(buffer), byteCount, &readSize, VMMDLL_FLAG_NOCACHE)
		&& readSize == byteCount;
	if (!ok) {
		readSize = 0;
		ok = VMMDLL_MemReadEx(this->vHandle, current_process.PID, address,
			static_cast<PBYTE>(buffer), byteCount, &readSize, 0)
			&& readSize == byteCount;
	}
	// unique_lock automatically unlocks on scope exit
	const double ms = std::chrono::duration<double, std::milli>(
		std::chrono::steady_clock::now() - t0).count();
	NoteDmaCall("TryRead", ms);
	LogSlowDma("TryRead", ms, 1);
	if (!ok)
		readFailureCount_.fetch_add(1, std::memory_order_relaxed);
	return ok;
}

bool Memory::ReadWithCount(uintptr_t address, void* buffer, size_t size, size_t& bytesRead) const
{
	WarnIfDmaOnRenderThread("ReadWithCount");
	bytesRead = 0;
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle || !buffer)
		return false;
	
	// Validate address before attempting DMA read
	if (!IsValidReadAddress(address, current_process.PID)) {
		const unsigned fail_count = readFailureCount_.fetch_add(1, std::memory_order_relaxed) + 1;
		if ((fail_count % 200) == 1)
			LOG("[!] Invalid read address 0x%p (x%u)\n", address, fail_count);
		return false;
	}
	
	readRequestCount_.fetch_add(1, std::memory_order_relaxed);
	if (size > static_cast<size_t>(MAXDWORD))
		return false;
	const auto lockBegin = std::chrono::steady_clock::now();
	std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_);
	t_lastDmaWaitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - lockBegin).count());
	t_dmaWaitUs += t_lastDmaWaitUs;

	const DWORD byteCount = static_cast<DWORD>(size);
	DWORD readSize = 0;
	BOOL ok = VMMDLL_MemReadEx(this->vHandle, current_process.PID, address,
		static_cast<PBYTE>(buffer), byteCount, &readSize, VMMDLL_FLAG_NOCACHE);
	bytesRead = static_cast<size_t>(readSize);
	if (ok && readSize == byteCount)
		return true;

	// Same cached fallback as Read(), while preserving the observed byte count.
	readSize = 0;
	ok = VMMDLL_MemReadEx(this->vHandle, current_process.PID, address,
		static_cast<PBYTE>(buffer), byteCount, &readSize, 0);
	bytesRead = static_cast<size_t>(readSize);
	return ok && readSize == byteCount;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size, int pid) const
{
	WarnIfDmaOnRenderThread("ReadPid");
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle)
		return false;
	if (size > static_cast<size_t>(MAXDWORD))
		return false;
	const auto lockBegin = std::chrono::steady_clock::now();
	std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_);
	t_lastDmaWaitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - lockBegin).count());
	t_dmaWaitUs += t_lastDmaWaitUs;
	const DWORD byteCount = static_cast<DWORD>(size);
	DWORD read_size = 0;
	if (!VMMDLL_MemReadEx(this->vHandle, pid, address, static_cast<PBYTE>(buffer), byteCount, &read_size, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[!] Failed to read Memory at 0x%p\n", address);
		return false;
	}
	return (read_size == byteCount);
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle() const
{
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle)
		return nullptr;
	{
		std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_);
		const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, current_process.PID, VMMDLL_FLAG_NOCACHE);
		if (!ScatterHandle) {
			LOG("[!] Failed to create scatter handle\n");
			return nullptr;
		}
		OmniGhost::MemoryCounters::ScatterHandlesCreated.fetch_add(1, std::memory_order_relaxed);
		OmniGhost::MemoryCounters::ScatterHandlesLive.fetch_add(1, std::memory_order_relaxed);
		RegisterScatterHandle(ScatterHandle);
		return ScatterHandle;
	}
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(int pid) const
{
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle)
		return nullptr;
	{
		std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_);
		const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, pid, VMMDLL_FLAG_NOCACHE);
		if (!ScatterHandle) {
			LOG("[!] Failed to create scatter handle\n");
			return nullptr;
		}
		OmniGhost::MemoryCounters::ScatterHandlesCreated.fetch_add(1, std::memory_order_relaxed);
		OmniGhost::MemoryCounters::ScatterHandlesLive.fetch_add(1, std::memory_order_relaxed);
		RegisterScatterHandle(ScatterHandle);
		return ScatterHandle;
	}
}

void Memory::RegisterScatterHandle(VMMDLL_SCATTER_HANDLE handle) const
{
	if (!handle)
		return;
	std::scoped_lock lock(scatterMutex_);
	scatterHandles_.insert_or_assign(handle, ScatterGeneration{
		sessionGeneration_.load(std::memory_order_acquire),
		processGeneration_.load(std::memory_order_acquire)
	});
	activeScatterHandles_.store(static_cast<uint32_t>(scatterHandles_.size()), std::memory_order_release);
}

bool Memory::ScatterHandleIsCurrent(VMMDLL_SCATTER_HANDLE handle) const noexcept
{
	if (!handle)
		return false;
	std::scoped_lock lock(scatterMutex_);
	const auto found = scatterHandles_.find(handle);
	return found != scatterHandles_.end()
		&& found->second.session == sessionGeneration_.load(std::memory_order_acquire)
		&& found->second.process == processGeneration_.load(std::memory_order_acquire);
}

void Memory::InvalidateScatterHandles(const char* reason) noexcept
{
	std::unordered_map<VMMDLL_SCATTER_HANDLE, ScatterGeneration> stale;
	{
		std::scoped_lock lock(scatterMutex_);
		stale.swap(scatterHandles_);
		activeScatterHandles_.store(0, std::memory_order_release);
	}
	for (const auto& [handle, generation] : stale) {
		(void)generation;
		if (handle) {
			VMMDLL_Scatter_CloseHandle(handle);
			OmniGhost::MemoryCounters::ScatterHandlesDestroyed.fetch_add(1, std::memory_order_relaxed);
			uint32_t live = OmniGhost::MemoryCounters::ScatterHandlesLive.load(std::memory_order_relaxed);
			while (live > 0 && !OmniGhost::MemoryCounters::ScatterHandlesLive.compare_exchange_weak(
				live, live - 1, std::memory_order_relaxed)) {}
		}
	}
	if (!stale.empty())
		std::clog << "[DMA][SCATTER] invalidated=" << stale.size()
			<< " reason=" << (reason ? reason : "lifecycle") << '\n';
}

void Memory::CloseScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
	if (!handle)
		return;
	{
		std::scoped_lock lock(scatterMutex_);
		const auto found = scatterHandles_.find(handle);
		if (found == scatterHandles_.end())
			return;
		scatterHandles_.erase(found);
		activeScatterHandles_.store(static_cast<uint32_t>(scatterHandles_.size()), std::memory_order_release);
	}
	OmniGhost::MemoryCounters::ScatterHandlesDestroyed.fetch_add(1, std::memory_order_relaxed);
	uint32_t live = OmniGhost::MemoryCounters::ScatterHandlesLive.load(std::memory_order_relaxed);
	while (live > 0 && !OmniGhost::MemoryCounters::ScatterHandlesLive.compare_exchange_weak(live, live - 1, std::memory_order_relaxed)) {}
	VMMDLL_Scatter_CloseHandle(handle);
}

void Memory::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	DataCallLease dataLease(this);
	if (!dataLease || !ScatterHandleIsCurrent(handle))
		return;
	if (size > static_cast<size_t>(MAXDWORD))
		return;
	const DWORD byteCount = static_cast<DWORD>(size);
	if (!VMMDLL_Scatter_PrepareEx(handle, address, byteCount, static_cast<PBYTE>(buffer), NULL))
	{
		LOG("[!] Failed to prepare scatter read at 0x%p\n", address);
	}
	else
	{
		std::scoped_lock lock(scatterMutex_);
		const auto found = scatterHandles_.find(handle);
		if (found != scatterHandles_.end())
			++found->second.preparedRequests;
	}
}

void Memory::AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	DataCallLease dataLease(this);
	if (!dataLease || !ScatterHandleIsCurrent(handle))
		return;
	if (size > static_cast<size_t>(MAXDWORD))
		return;
	const DWORD byteCount = static_cast<DWORD>(size);
	if (!VMMDLL_Scatter_PrepareWrite(handle, address, static_cast<PBYTE>(buffer), byteCount))
	{
		LOG("[!] Failed to prepare scatter write at 0x%p\n", address);
	}
}

bool Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	WarnIfDmaOnRenderThread("ExecuteReadScatter");
	DataCallLease dataLease(this);
	if (!dataLease) {
		t_dmaWaitUs += 500;
		return false;
	}
	if (!ScatterHandleIsCurrent(handle))
		return false;
	uint32_t preparedRequests = 0;
	{
		std::scoped_lock lock(scatterMutex_);
		const auto found = scatterHandles_.find(handle);
		if (found != scatterHandles_.end()) {
			preparedRequests = found->second.preparedRequests;
			found->second.preparedRequests = 0;
		}
	}
	if (preparedRequests == 0)
		return false;
	// Scatter preparation remains concurrent; only execution touches the
	// shared physical DMA transport. Optional-priority lanes try-lock and
	// yield so a camera thread can acquire the mutex between micro-batches.
	const auto lockBegin = std::chrono::steady_clock::now();
	auto dmaSpinLockGuard = AcquireDmaTransportLock(dmaReadMutex_);
	std::unique_lock<std::mutex> dmaReadLock;
	if (!dmaSpinLockGuard) {
		// High priority: use blocking mutex
		dmaReadLock = std::unique_lock<std::mutex>(dmaReadMutex_);
	}
	t_lastDmaWaitUs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now() - lockBegin).count());
	t_dmaWaitUs += t_lastDmaWaitUs;
	const auto transferBegin = std::chrono::steady_clock::now();
	if (pid == 0)
		pid = current_process.PID;
	scatterReadBatchCount_.fetch_add(1, std::memory_order_relaxed);
	OMNIGHOST_VMM_TIMING("Scatter_ExecuteRead");

	const bool executeOk = VMMDLL_Scatter_ExecuteRead(handle) != FALSE;
	//Clear after using it
	const bool clearOk = VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE) != FALSE;
	// dmaSpinLockGuard and dmaReadLock automatically unlock on scope exit
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - transferBegin).count();
	NoteDmaCall("Scatter", ms);
	LogSlowDma("Scatter", ms, static_cast<int>(preparedRequests));
	if (!executeOk) LOG("[-] Failed to Execute Scatter Read\n");
	if (!clearOk) LOG("[-] Failed to clear Scatter\n");
	return executeOk && clearOk;
}

void Memory::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	DataCallLease dataLease(this);
	if (!dataLease || !ScatterHandleIsCurrent(handle))
		return;
	if (pid == 0)
		pid = current_process.PID;
	std::unique_lock<std::mutex> dmaReadLock(dmaReadMutex_);

	if (!VMMDLL_Scatter_Execute(handle))
	{
		LOG("[-] Failed to Execute Scatter Read\n");
	}
	//Clear after using it
	if (!VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
	}
}

// VAD map entry for address validation
struct VadEntry {
    uint64_t start;
    uint64_t end;
    uint32_t protection;
    std::wstring name;
};

bool Memory::RefreshVadCache(int pid) const
{
    if (!vHandle)
        return false;
    
    int targetPid = pid ? pid : current_process.PID;
    if (targetPid == 0)
        return false;
    
    PVMMDLL_MAP_VAD vadMap = nullptr;
    BOOL ok = VMMDLL_Map_GetVadW(vHandle, static_cast<DWORD>(targetPid), FALSE, &vadMap);
    if (!ok || !vadMap)
        return false;
    
    // In a real implementation, we would parse the VAD map and store it in a thread-safe cache.
    // For now, we just verify the call succeeds.
    VMMDLL_MemFree(vadMap);
    return true;
}

bool Memory::IsValidReadAddress(uint64_t address, int pid) const
{
    if (!vHandle)
        return false;
    
    int targetPid = pid ? pid : current_process.PID;
    if (targetPid == 0 || address == 0)
        return false;
    
    // Quick sanity check: address must be in user space (below 0x7FFFFFFFFFFF on x64)
    if (address >= 0x7FFFFFFFFFFFull)
        return false;
    
    // For now, just do basic range check. Full VAD validation would require caching the VAD map.
    // This is a placeholder that can be expanded with full VAD map caching.
    return true;
}
