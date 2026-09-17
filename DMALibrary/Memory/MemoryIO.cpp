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

	std::vector<uint8_t> buffer(range_end - range_start);
	if (buffer.size() > static_cast<size_t>(MAXDWORD))
		return 0;
	const DWORD bufferSize = static_cast<DWORD>(buffer.size());
	if (!VMMDLL_MemReadEx(this->vHandle, PID, range_start, buffer.data(), bufferSize, 0,
		VMMDLL_FLAG_NOCACHE | VMMDLL_FLAG_ZEROPAD_ON_FAIL))
		return 0;

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
        static_cast<double>(t_dmaWaitUs) / 1000.0,
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
void Memory::SetDmaScanId(uint64_t id) noexcept { t_dmaScanId = id; }
void Memory::SetDmaLane(const char* lane) noexcept { t_dmaLane = lane ? lane : "unknown"; }
const char* Memory::GetDmaCallTag() noexcept {
    return t_dmaTag ? t_dmaTag : "untagged";
}
void Memory::ResetThreadDmaWaitUs() noexcept { t_dmaWaitUs = 0; }
uint64_t Memory::ConsumeThreadDmaWaitUs() noexcept {
    const uint64_t v = t_dmaWaitUs;
    t_dmaWaitUs = 0;
    return v;
}

bool Memory::Read(uintptr_t address, void* buffer, size_t size) const
{
	WarnIfDmaOnRenderThread("Read");
	const auto t0 = std::chrono::steady_clock::now();
	DataCallLease dataLease(this);
	if (!dataLease) {
		// Maintenance blocked the data plane — count as wait.
		t_dmaWaitUs += 500; // approximate small wait quantum; caller aggregates
		return false;
	}
	if (!this->vHandle)
		return false;
	readRequestCount_.fetch_add(1, std::memory_order_relaxed);
	if (size > static_cast<size_t>(MAXDWORD))
		return false;
	const DWORD byteCount = static_cast<DWORD>(size);
	DWORD read_size = 0;
	const bool ok1 = VMMDLL_MemReadEx(this->vHandle, current_process.PID, address, static_cast<PBYTE>(buffer), byteCount, &read_size, VMMDLL_FLAG_NOCACHE)
		&& read_size == byteCount;
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	LogSlowDma("Read", ms, 1);
	if (ok1)
		return true;

	// Fallback: cached read (sometimes works when NOCACHE fails mid DTB fix)
	read_size = 0;
	if (VMMDLL_MemReadEx(this->vHandle, current_process.PID, address, static_cast<PBYTE>(buffer), byteCount, &read_size, 0)
		&& read_size == byteCount)
		return true;

	const unsigned fail_count = readFailureCount_.fetch_add(1, std::memory_order_relaxed) + 1;
	if ((fail_count % 200) == 1)
		LOG("[!] Failed to read Memory at 0x%p (x%u)\n", address, fail_count);
	return false;
}

bool Memory::ReadWithCount(uintptr_t address, void* buffer, size_t size, size_t& bytesRead) const
{
	bytesRead = 0;
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle || !buffer)
		return false;
	readRequestCount_.fetch_add(1, std::memory_order_relaxed);
	if (size > static_cast<size_t>(MAXDWORD))
		return false;

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
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle)
		return false;
	if (size > static_cast<size_t>(MAXDWORD))
		return false;
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
	scatterHandlesCreated_.fetch_add(1, std::memory_order_relaxed);
	scatterHandlesLive_.fetch_add(1, std::memory_order_relaxed);
	const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, current_process.PID, VMMDLL_FLAG_NOCACHE);
	if (!ScatterHandle)
		LOG("[!] Failed to create scatter handle\n");
	else
		RegisterScatterHandle(ScatterHandle);
	return ScatterHandle;
}

VMMDLL_SCATTER_HANDLE Memory::CreateScatterHandle(int pid) const
{
	DataCallLease dataLease(this);
	if (!dataLease || !this->vHandle)
		return nullptr;
	scatterHandlesCreated_.fetch_add(1, std::memory_order_relaxed);
	scatterHandlesLive_.fetch_add(1, std::memory_order_relaxed);
	const VMMDLL_SCATTER_HANDLE ScatterHandle = VMMDLL_Scatter_Initialize(this->vHandle, pid, VMMDLL_FLAG_NOCACHE);
	if (!ScatterHandle)
		LOG("[!] Failed to create scatter handle\n");
	else
		RegisterScatterHandle(ScatterHandle);
	return ScatterHandle;
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
		if (handle)
			VMMDLL_Scatter_CloseHandle(handle);
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
	scatterHandlesDestroyed_.fetch_add(1, std::memory_order_relaxed);
	uint32_t live = scatterHandlesLive_.load(std::memory_order_relaxed);
	while (live > 0 && !scatterHandlesLive_.compare_exchange_weak(live, live - 1, std::memory_order_relaxed)) {}
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

void Memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	WarnIfDmaOnRenderThread("ExecuteReadScatter");
	const auto t0 = std::chrono::steady_clock::now();
	DataCallLease dataLease(this);
	if (!dataLease) {
		t_dmaWaitUs += 500;
		return;
	}
	if (!ScatterHandleIsCurrent(handle))
		return;
	if (pid == 0)
		pid = current_process.PID;
	scatterReadBatchCount_.fetch_add(1, std::memory_order_relaxed);
	OMNIGHOST_VMM_TIMING("Scatter_ExecuteRead");

	if (!VMMDLL_Scatter_ExecuteRead(handle))
	{
		LOG("[-] Failed to Execute Scatter Read\n");
	}
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	LogSlowDma("Scatter", ms, 0);
	//Clear after using it
	if (!VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
	}
}

void Memory::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid)
{
	DataCallLease dataLease(this);
	if (!dataLease || !ScatterHandleIsCurrent(handle))
		return;
	if (pid == 0)
		pid = current_process.PID;

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
