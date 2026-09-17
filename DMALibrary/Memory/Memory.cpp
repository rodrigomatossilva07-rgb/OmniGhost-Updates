// Memory.cpp - construction, process/module/symbol queries and the passive
// startup device probe. Session lifecycle lives in MemoryInit.cpp, data-plane
// reads/scatter in MemoryIO.cpp and maintenance/recovery in MemoryLifecycle.cpp.
#include "pch.h"
#include "Memory.h"
#include "MemoryInternal.h"

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <SetupAPI.h>
#include <cfgmgr32.h>
#include <cfg.h>

#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Cfgmgr32.lib")

namespace MemoryDetail {

namespace {

bool ContainsInsensitive(const std::wstring& haystack, const wchar_t* needle)
{
	if (!needle || !*needle) return false;
	std::wstring upper(haystack);
	for (auto& ch : upper) ch = static_cast<wchar_t>(std::towupper(ch));
	std::wstring needleUpper(needle);
	for (auto& ch : needleUpper) ch = static_cast<wchar_t>(std::towupper(ch));
	return upper.find(needleUpper) != std::wstring::npos;
}

bool IsFt60xHardwareId(const std::wstring& id)
{
	// FTDI FT600/FT601 (USB 3.0 FIFO bridge used by PCILeech FPGA boards).
	if (!ContainsInsensitive(id, L"VID_0403")) return false;
	return ContainsInsensitive(id, L"PID_601E") || ContainsInsensitive(id, L"PID_601F");
}

} // namespace

PassiveDmaPnpResult ProbePassiveDmaPnp() noexcept
{
	PassiveDmaPnpResult result{};
	const HDEVINFO devs = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
	if (devs == INVALID_HANDLE_VALUE) {
		std::cout << "[DMA][StartupProbe] SetupDiGetClassDevs failed win32=" << GetLastError() << "\n";
		return result;
	}

	SP_DEVINFO_DATA info{};
	info.cbSize = sizeof(info);
	for (DWORD index = 0;; ++index) {
		if (!SetupDiEnumDeviceInfo(devs, index, &info)) {
			const DWORD err = GetLastError();
			if (err != ERROR_NO_MORE_ITEMS)
				std::cout << "[DMA][StartupProbe] SetupDiEnumDeviceInfo failed win32=" << err << "\n";
			break;
		}

		std::vector<wchar_t> buffer(4096);
		DWORD required = 0;
		if (!SetupDiGetDeviceRegistryPropertyW(devs, &info, SPDRP_HARDWAREID, nullptr,
			reinterpret_cast<PBYTE>(buffer.data()),
			static_cast<DWORD>(buffer.size() * sizeof(wchar_t)), &required))
			continue;

		// REG_MULTI_SZ: iterate every hardware id string.
		bool match = false;
		for (const wchar_t* cursor = buffer.data(); *cursor;) {
			const std::wstring id(cursor);
			if (IsFt60xHardwareId(id)) { match = true; break; }
			cursor += id.size() + 1;
		}
		if (!match)
			continue;

		result.candidateFound = true;
		ULONG status = 0;
		ULONG problem = 0;
		if (CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0) == CR_SUCCESS) {
			result.problemCode = problem;
			result.pnpStarted = (status & DN_STARTED) != 0 && (status & DN_HAS_PROBLEM) == 0;
		}
		if (result.pnpStarted)
			break;
	}
	SetupDiDestroyDeviceInfoList(devs);
	return result;
}

bool ReadMzHeader(VMM_HANDLE vmm, DWORD pid, uint64_t base) noexcept
{
	if (!vmm || !base) return false;
	BYTE header[2]{};
	DWORD read = 0;
	if (!VMMDLL_MemReadEx(vmm, pid, base, header, sizeof(header), &read, VMMDLL_FLAG_NOCACHE))
		return false;
	return read == sizeof(header) && header[0] == 'M' && header[1] == 'Z';
}

} // namespace MemoryDetail

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

Memory::Memory()
{
	// Runtime files are installed by RuntimeBootstrap after the entrypoint.
	// Loading them inside this global object's constructor would be too early
	// for the portable, single-EXE first run.
	dependencyIntegrityMessage_ = "runtime dependencies not initialized yet";
	this->key = std::make_shared<c_keys>();
#ifndef OMNIGHOST_READONLY_MODE
	this->registry = c_registry();
	this->shellcode = c_shellcode();
#endif
	this->vHandle = nullptr;
}

Memory::~Memory()
{
	// The VmmSession member closes the VMM handle; scatter handles are already
	// invalidated by ResetDeviceState during Close()/Shutdown paths.
}

// ─────────────────────────────────────────────────────────────────────────────
// Passive startup probe (PnP only, no device open)
// ─────────────────────────────────────────────────────────────────────────────

bool Memory::ProbeDeviceAvailability()
{
	startupDeviceProbeOk_.store(false, std::memory_order_release);

	if (vHandle) {
		startupDeviceProbeOk_.store(true, std::memory_order_release);
		std::cout << "[DMA][StartupProbe] mode=EXISTING_SESSION result=READY\n";
		return true;
	}

	const MemoryDetail::PassiveDmaPnpResult probe = MemoryDetail::ProbePassiveDmaPnp();
	const bool ready = probe.candidateFound && probe.pnpStarted;
	startupDeviceProbeOk_.store(ready, std::memory_order_release);

	std::cout << "[DMA][StartupProbe] mode=WINDOWS_PNP no_vendor_api=YES no_device_open=YES"
		<< " candidate=" << (probe.candidateFound ? "YES" : "NO")
		<< " pnp_started=" << (probe.pnpStarted ? "YES" : "NO")
		<< " problem_code=" << probe.problemCode << '\n';
	if (!ready) {
		std::cout << "[DMA][StartupProbe] NOTE: passive NOT_READY does not prove an FPGA failure;"
			" the real DMA data path is verified only after explicit game launch.\n";
	}
	return ready;
}

bool Memory::ProbeDevicePresence()
{
	const MemoryDetail::PassiveDmaPnpResult probe = MemoryDetail::ProbePassiveDmaPnp();
	return probe.candidateFound && probe.pnpStarted;
}

// ─────────────────────────────────────────────────────────────────────────────
// Process queries
// ─────────────────────────────────────────────────────────────────────────────

DWORD Memory::GetPidFromName(std::string process_name)
{
	if (!this->vHandle || process_name.empty())
		return 0;
	DataCallLease dataLease(this);
	if (!dataLease)
		return 0;
	OMNIGHOST_VMM_TIMING("PidGetFromName");
	DWORD pid = 0;
	if (!VMMDLL_PidGetFromName(this->vHandle, const_cast<LPSTR>(process_name.c_str()), &pid))
		return 0;
	return pid;
}

std::vector<std::string> Memory::GetProcessNames()
{
	std::vector<std::string> names;
	if (!this->vHandle || IsCancellationRequested())
		return names;
	DataCallLease dataLease(this);
	if (!dataLease)
		return names;
	OMNIGHOST_VMM_TIMING("ProcessGetInformationAll");

	PVMMDLL_PROCESS_INFORMATION raw = nullptr;
	DWORD total = 0;
	if (!VMMDLL_ProcessGetInformationAll(this->vHandle, &raw, &total) || !raw)
		return names;
	VmmOwned<VMMDLL_PROCESS_INFORMATION> owned(raw);
	names.reserve(total);
	for (DWORD index = 0; index < total; ++index) {
		const char* name = raw[index].szNameLong[0] ? raw[index].szNameLong : raw[index].szName;
		if (name && *name)
			names.emplace_back(name);
	}
	return names;
}

std::vector<int> Memory::GetPidListFromName(std::string process_name)
{
	std::vector<int> pids;
	if (!this->vHandle || process_name.empty())
		return pids;
	DataCallLease dataLease(this);
	if (!dataLease)
		return pids;
	OMNIGHOST_VMM_TIMING("ProcessGetInformationAll(list)");

	PVMMDLL_PROCESS_INFORMATION raw = nullptr;
	DWORD total = 0;
	if (!VMMDLL_ProcessGetInformationAll(this->vHandle, &raw, &total) || !raw)
		return pids;
	VmmOwned<VMMDLL_PROCESS_INFORMATION> owned(raw);
	for (DWORD index = 0; index < total; ++index) {
		if (_stricmp(raw[index].szNameLong, process_name.c_str()) == 0 ||
			_stricmp(raw[index].szName, process_name.c_str()) == 0)
			pids.push_back(static_cast<int>(raw[index].dwPID));
	}
	return pids;
}

VMMDLL_PROCESS_INFORMATION Memory::GetProcessInformation()
{
	VMMDLL_PROCESS_INFORMATION info{};
	info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
	info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
	info.wSize = sizeof(info);
	if (!this->vHandle || !current_process.PID)
		return info;
	DataCallLease dataLease(this);
	if (!dataLease)
		return info;
	OMNIGHOST_VMM_TIMING("ProcessGetInformation");
	SIZE_T size = sizeof(info);
	if (!VMMDLL_ProcessGetInformation(this->vHandle, current_process.PID, &info, &size)) {
		LOG("[!] Failed to find process information\n");
		return VMMDLL_PROCESS_INFORMATION{};
	}
	LOG("[+] Found process information\n");
	return info;
}

PEB Memory::GetProcessPeb()
{
	const auto info = GetProcessInformation();
	if (info.win.vaPEB) {
		LOG("[+] Found process PEB ptr at 0x%p\n", info.win.vaPEB);
		return Read<PEB>(info.win.vaPEB);
	}
	LOG("[!] Failed to find the processes PEB\n");
	return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Module queries
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> Memory::GetModuleList(std::string process_name)
{
	(void)process_name; // modules are always resolved against the bound process
	std::vector<std::string> list;
	if (!this->vHandle || !current_process.PID)
		return list;
	DataCallLease dataLease(this);
	if (!dataLease)
		return list;
	OMNIGHOST_VMM_TIMING("Map_GetModule");

	PVMMDLL_MAP_MODULE raw = nullptr;
	if (!VMMDLL_Map_GetModuleU(this->vHandle, current_process.PID, &raw, VMMDLL_MODULE_FLAG_NORMAL) || !raw) {
		LOG("[!] Failed to get module list\n");
		return list;
	}
	VmmOwned<VMMDLL_MAP_MODULE> owned(raw);
	list.reserve(raw->cMap);
	for (DWORD i = 0; i < raw->cMap; ++i) {
		if (raw->pMap[i].uszText)
			list.emplace_back(raw->pMap[i].uszText);
	}
	return list;
}

size_t Memory::GetBaseDaddy(std::string module_name)
{
	if (!this->vHandle || !current_process.PID || module_name.empty())
		return 0;
	DataCallLease dataLease(this);
	if (!dataLease)
		return 0;
	OMNIGHOST_VMM_TIMING("Map_GetModuleFromName(base)");

	PVMMDLL_MAP_MODULEENTRY raw = nullptr;
	if (!VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID,
		const_cast<LPSTR>(module_name.c_str()), &raw, VMMDLL_MODULE_FLAG_NORMAL) || !raw) {
		LOG("[!] Couldn't find Base Address for %s\n", module_name.c_str());
		return 0;
	}
	VmmOwned<VMMDLL_MAP_MODULEENTRY> owned(raw);
	const size_t base = static_cast<size_t>(raw->vaBase);
	LOG("[+] Found Base Address for %s at 0x%p\n", module_name.c_str(), raw->vaBase);
	return base;
}

size_t Memory::GetBaseSize(std::string module_name)
{
	if (!this->vHandle || !current_process.PID || module_name.empty())
		return 0;
	DataCallLease dataLease(this);
	if (!dataLease)
		return 0;
	OMNIGHOST_VMM_TIMING("Map_GetModuleFromName(size)");

	PVMMDLL_MAP_MODULEENTRY raw = nullptr;
	if (!VMMDLL_Map_GetModuleFromNameU(this->vHandle, current_process.PID,
		const_cast<LPSTR>(module_name.c_str()), &raw, VMMDLL_MODULE_FLAG_NORMAL) || !raw)
		return 0;
	VmmOwned<VMMDLL_MAP_MODULEENTRY> owned(raw);
	const size_t imageSize = static_cast<size_t>(raw->cbImageSize);
	LOG("[+] Found Base Size for %s at 0x%p\n", module_name.c_str(), raw->cbImageSize);
	return imageSize;
}

// ─────────────────────────────────────────────────────────────────────────────
// Symbol (EAT/IAT) queries
// ─────────────────────────────────────────────────────────────────────────────

uintptr_t Memory::GetExportTableAddress(std::string import, std::string process, std::string module)
{
	if (!this->vHandle)
		return 0;
	const DWORD pid = process.empty() ? static_cast<DWORD>(current_process.PID) : GetPidFromName(process);
	if (!pid)
		return 0;
	DataCallLease dataLease(this);
	if (!dataLease)
		return 0;
	OMNIGHOST_VMM_TIMING("Map_GetEAT");

	PVMMDLL_MAP_EAT raw = nullptr;
	if (!VMMDLL_Map_GetEATU(this->vHandle, pid, const_cast<LPSTR>(module.c_str()), &raw) || !raw) {
		LOG("[!] Failed to get Export Table\n");
		return 0;
	}
	VmmOwned<VMMDLL_MAP_EAT> owned(raw);
	if (raw->dwVersion != VMMDLL_MAP_EAT_VERSION) {
		LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}
	for (DWORD i = 0; i < raw->cMap; ++i) {
		const auto& entry = raw->pMap[i];
		if (entry.uszFunction && import == entry.uszFunction)
			return static_cast<uintptr_t>(entry.vaFunction);
	}
	return 0;
}

uintptr_t Memory::GetImportTableAddress(std::string import, std::string process, std::string module)
{
	if (!this->vHandle)
		return 0;
	const DWORD pid = process.empty() ? static_cast<DWORD>(current_process.PID) : GetPidFromName(process);
	if (!pid)
		return 0;
	DataCallLease dataLease(this);
	if (!dataLease)
		return 0;
	OMNIGHOST_VMM_TIMING("Map_GetIAT");

	PVMMDLL_MAP_IAT raw = nullptr;
	if (!VMMDLL_Map_GetIATU(this->vHandle, pid, const_cast<LPSTR>(module.c_str()), &raw) || !raw) {
		LOG("[!] Failed to get Import Table\n");
		return 0;
	}
	VmmOwned<VMMDLL_MAP_IAT> owned(raw);
	if (raw->dwVersion != VMMDLL_MAP_IAT_VERSION) {
		LOG("[!] Invalid VMM Map Version\n");
		return 0;
	}
	for (DWORD i = 0; i < raw->cMap; ++i) {
		const auto& entry = raw->pMap[i];
		if (entry.uszFunction && import == entry.uszFunction)
			return static_cast<uintptr_t>(entry.vaFunction);
	}
	return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics dumps (developer tooling; never used during normal sessions)
// ─────────────────────────────────────────────────────────────────────────────

bool Memory::DumpMemory(uintptr_t address, std::string path)
{
	if (!this->vHandle || !address || path.empty())
		return false;

	IMAGE_DOS_HEADER dos{};
	if (!Read(address, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
		LOG("[!] DumpMemory: invalid DOS header\n");
		return false;
	}
	IMAGE_NT_HEADERS64 nt{};
	if (!Read(address + static_cast<uintptr_t>(dos.e_lfanew), &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) {
		LOG("[!] DumpMemory: invalid NT header\n");
		return false;
	}
	const size_t imageSize = nt.OptionalHeader.SizeOfImage;
	if (!imageSize || imageSize > (512ull << 20))
		return false;

	std::vector<uint8_t> image(imageSize);
	constexpr size_t kPage = 0x1000;
	for (size_t offset = 0; offset < imageSize; offset += kPage) {
		const size_t chunk = (std::min)(kPage, imageSize - offset);
		(void)Read(address + offset, image.data() + offset, chunk); // unreadable pages stay zero
	}
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out)
		return false;
	out.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
	return static_cast<bool>(out);
}

bool Memory::DumpMemoryMap(bool debug)
{
	(void)debug;
	// OmniGhost opens the FPGA without a pre-dumped physical memory map
	// (MemProcFS resolves it live). Kept for API compatibility only.
	return false;
}

// Single definition of the process-wide Memory instance (header is extern only).
Memory mem;
