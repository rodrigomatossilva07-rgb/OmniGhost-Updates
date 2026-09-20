// MemoryInit.cpp - runtime dependencies, FPGA/VMM session open, process bind,
// ProcInfo/DTB reconciliation (FixCr3) and session teardown.
#include "pch.h"
#include "Memory.h"
#include "MemoryInternal.h"
#include "../../src/platform/app_paths.h"
#include "../../src/platform/runtime_bootstrap.h"
#include "../../src/platform/session_log.h"

#include <leechcore.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SetupAPI.h>
#include <winver.h>

namespace
{
#ifndef LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR 0x00000100
#endif
#ifndef LOAD_LIBRARY_SEARCH_USER_DIRS
#define LOAD_LIBRARY_SEARCH_USER_DIRS 0x00000400
#endif
#ifndef LOAD_LIBRARY_SEARCH_SYSTEM32
#define LOAD_LIBRARY_SEARCH_SYSTEM32 0x00000800
#endif

constexpr const char* kProcInfoDir = "\\misc\\procinfo\\";
constexpr const char* kProcInfoProgressFile = "\\misc\\procinfo\\progress_percent.txt";
constexpr const char* kProcInfoDtbFile = "\\misc\\procinfo\\dtb.txt";

std::string Narrow(const std::wstring& text)
{
	std::string out;
	out.reserve(text.size());
	for (wchar_t ch : text)
		out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
	return out;
}

std::string HexOf(uint64_t value)
{
	std::ostringstream ss;
	ss << "0x" << std::hex << value;
	return ss.str();
}

std::string FileVersionString(const std::filesystem::path& file)
{
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeW(file.c_str(), &ignored);
    if (!bytes)
        return "unknown";
    std::vector<BYTE> data(bytes);
    if (!GetFileVersionInfoW(file.c_str(), 0, bytes, data.data()))
        return "unknown";
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoBytes = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<LPVOID*>(&info), &infoBytes) ||
        !info || infoBytes < static_cast<UINT>(sizeof(VS_FIXEDFILEINFO)))
        return "unknown";
    std::ostringstream out;
    out << HIWORD(info->dwFileVersionMS) << '.' << LOWORD(info->dwFileVersionMS) << '.'
        << HIWORD(info->dwFileVersionLS) << '.' << LOWORD(info->dwFileVersionLS);
    return out.str();
}

std::filesystem::path ModulePath(HMODULE module)
{
    if (!module)
        return {};
    std::wstring buffer(32768, L'\0');
    const DWORD len = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!len || static_cast<size_t>(len) >= buffer.size())
        return {};
    buffer.resize(len);
    return std::filesystem::path(buffer);
}

std::wstring QueryRegistryString(HKEY key, const wchar_t* name)
{
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < static_cast<DWORD>(sizeof(wchar_t)))
        return {};
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buffer.data()), &bytes) != ERROR_SUCCESS)
        return {};
    return std::wstring(buffer.data());
}

bool ContainsInsensitive(std::wstring text, std::wstring needle)
{
    const auto upper = [](wchar_t ch) noexcept {
        return static_cast<wchar_t>(std::towupper(static_cast<wint_t>(ch)));
    };
    std::transform(text.begin(), text.end(), text.begin(), upper);
    std::transform(needle.begin(), needle.end(), needle.begin(), upper);
    return text.find(needle) != std::wstring::npos;
}

struct FtdiDriverInfo
{
    bool devicePresent = false;
    bool driverFound = false;
    bool isWinUsbD3xx = false;
    bool isLegacyWdfD3xx = false;
    std::wstring description;
    std::wstring instanceId;
    std::wstring version;
    std::wstring provider;
    std::wstring publishedInf;
};

FtdiDriverInfo QueryInstalledFtdiD3xxDriver()
{
    FtdiDriverInfo result;
    int bestScore = -1;
    HDEVINFO devices = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (devices == INVALID_HANDLE_VALUE)
        return result;
    SP_DEVINFO_DATA deviceInfo{};
    deviceInfo.cbSize = static_cast<DWORD>(sizeof(deviceInfo));
    for (DWORD index = 0; SetupDiEnumDeviceInfo(devices, index, &deviceInfo); ++index) {
        wchar_t description[512]{};
        DWORD propertyType = 0;
        (void)SetupDiGetDeviceRegistryPropertyW(devices, &deviceInfo, SPDRP_DEVICEDESC,
            &propertyType, reinterpret_cast<PBYTE>(description), static_cast<DWORD>(sizeof(description)), nullptr);
        DWORD required = 0;
        (void)SetupDiGetDeviceRegistryPropertyW(devices, &deviceInfo, SPDRP_HARDWAREID,
            &propertyType, nullptr, 0, &required);
        std::vector<BYTE> hwidBytes(required + sizeof(wchar_t), 0);
        if (required) {
            (void)SetupDiGetDeviceRegistryPropertyW(devices, &deviceInfo, SPDRP_HARDWAREID,
                &propertyType, hwidBytes.data(), static_cast<DWORD>(hwidBytes.size()), nullptr);
        }
        std::wstring hwids;
        if (required) {
            const wchar_t* cursor = reinterpret_cast<const wchar_t*>(hwidBytes.data());
            while (*cursor) {
                if (!hwids.empty()) hwids += L';';
                hwids += cursor;
                cursor += std::wcslen(cursor) + 1;
            }
        }
        const bool looksLikeFt60x =
            ContainsInsensitive(description, L"FT600") || ContainsInsensitive(description, L"FT601") ||
            ContainsInsensitive(description, L"FT60") || ContainsInsensitive(description, L"D3XX") ||
            ContainsInsensitive(hwids, L"VID_0403&PID_601E") ||
            ContainsInsensitive(hwids, L"VID_0403&PID_601F");
        if (!looksLikeFt60x)
            continue;
        // A FT601 can sit below a Microsoft USB composite parent carrying the
        // same VID/PID.  Do not stop at the first match: prefer the actual
        // FTDI D3XX function node and retain a generic parent only as evidence
        // that the physical device is present.
        FtdiDriverInfo candidate;
        candidate.devicePresent = true;
        candidate.description = description;
        wchar_t instanceId[512]{};
        if (SetupDiGetDeviceInstanceIdW(devices, &deviceInfo, instanceId,
                static_cast<DWORD>(std::size(instanceId)), nullptr))
            candidate.instanceId = instanceId;

        HKEY driverKey = SetupDiOpenDevRegKey(devices, &deviceInfo,
            DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
        if (driverKey != INVALID_HANDLE_VALUE) {
            candidate.version = QueryRegistryString(driverKey, L"DriverVersion");
            candidate.provider = QueryRegistryString(driverKey, L"ProviderName");
            candidate.publishedInf = QueryRegistryString(driverKey, L"InfPath");
            candidate.driverFound = !candidate.version.empty() || !candidate.provider.empty() ||
                !candidate.publishedInf.empty();
            RegCloseKey(driverKey);
        }

        const bool ftdiProvider = ContainsInsensitive(candidate.provider, L"FTDI");
        const bool winUsbInf = ContainsInsensitive(candidate.publishedInf, L"FTD3XXWU.INF");
        const bool legacyInf = ContainsInsensitive(candidate.publishedInf, L"FTDIBUS3.INF");
        candidate.isWinUsbD3xx = ftdiProvider && winUsbInf;
        candidate.isLegacyWdfD3xx = ftdiProvider && legacyInf;

        int score = 10; // physical FT60x/VID-PID match only
        if (candidate.driverFound) score += 5;
        if (ftdiProvider) score += 40;
        if (winUsbInf || legacyInf) score += 100;
        if (ContainsInsensitive(candidate.description, L"FTDI FT60") ||
            ContainsInsensitive(candidate.description, L"D3XX")) score += 20;
        if (score > bestScore) {
            bestScore = score;
            result = std::move(candidate);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return result;
}

void LogFtdiDiagnostics()
{
    const wchar_t* names[] = { L"FTD3XXWU.dll", L"FTD3XX.dll" };
    for (const wchar_t* name : names) {
        const HMODULE module = GetModuleHandleW(name);
        if (!module)
            continue;
        const auto path = ModulePath(module);
        const std::string version = path.empty() ? "unknown" : FileVersionString(path);
        std::cout << "[DMA][FTDI] module=" << Narrow(name) << " version=" << version
            << " path=" << Narrow(path.wstring()) << "\n";
        OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::DMA, "FTDI application library loaded",
            {{"module", Narrow(name)}, {"version", version}, {"path", Narrow(path.wstring())}});
    }
    const FtdiDriverInfo driver = QueryInstalledFtdiD3xxDriver();
    if (driver.devicePresent && driver.isWinUsbD3xx) {
        std::cout << "[DMA][FTDI] driver_generation=WINUSB installed_driver=" << Narrow(driver.version)
            << " provider=" << Narrow(driver.provider)
            << " device=" << Narrow(driver.description)
            << " inf=" << Narrow(driver.publishedInf) << "\n";
        OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
            OmniGhost::SessionLog::Subsystem::DMA, "FTDI D3XX WinUSB driver detected",
            {{"version", Narrow(driver.version)}, {"provider", Narrow(driver.provider)},
             {"device", Narrow(driver.description)}, {"inf", Narrow(driver.publishedInf)}});
    } else if (driver.devicePresent && driver.isLegacyWdfD3xx) {
        std::cout << "[DMA][FTDI] driver_generation=WDF legacy_driver=" << Narrow(driver.version)
            << " provider=" << Narrow(driver.provider) << " inf=" << Narrow(driver.publishedInf) << "\n";
        OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::DMA, "FTDI legacy WDF D3XX driver detected",
            {{"version", Narrow(driver.version)}, {"provider", Narrow(driver.provider)},
             {"device", Narrow(driver.description)}, {"inf", Narrow(driver.publishedInf)}});
    } else if (driver.devicePresent) {
        // This is intentionally not treated as a D3XX driver.  A Microsoft
        // composite parent is not enough evidence to offer or perform update.
        std::cout << "[DMA][FTDI] FT60x present but D3XX function node unresolved; provider="
            << Narrow(driver.provider) << " device=" << Narrow(driver.description)
            << " inf=" << Narrow(driver.publishedInf) << "\n";
        OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::DMA, "FT60x present but D3XX driver node unresolved",
            {{"provider", Narrow(driver.provider)}, {"device", Narrow(driver.description)},
             {"instance_id", Narrow(driver.instanceId)}, {"inf", Narrow(driver.publishedInf)}});
    } else {
        std::cout << "[DMA][FTDI] installed D3XX/FT60x driver version could not be resolved via SetupAPI\n";
        OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::DMA, "FTDI D3XX installed driver not resolved");
    }
}

ULONG64 ReadEnvironmentU64(const wchar_t* name, ULONG64 fallback)
{
    wchar_t buffer[64]{};
    const DWORD len = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (!len || len >= static_cast<DWORD>(std::size(buffer)))
        return fallback;
    wchar_t* end = nullptr;
    const unsigned long long value = std::wcstoull(buffer, &end, 10);
    return (end && end != buffer && *end == L'\0') ? static_cast<ULONG64>(value) : fallback;
}

bool EnvironmentFlagEnabled(const wchar_t* name)
{
    wchar_t buffer[16]{};
    const DWORD len = GetEnvironmentVariableW(name, buffer, static_cast<DWORD>(std::size(buffer)));
    if (!len || len >= static_cast<DWORD>(std::size(buffer)))
        return false;
    return _wcsicmp(buffer, L"1") == 0 || _wcsicmp(buffer, L"true") == 0 ||
        _wcsicmp(buffer, L"yes") == 0 || _wcsicmp(buffer, L"on") == 0;
}

void ConfigureVmmLowLatencyRefresh(VMM_HANDLE handle)
{
    if (!handle)
        return;
    ULONG64 enabled = 0, tickMs = 0, partialTicks = 0, totalTicks = 0;
    const bool haveEnabled = VMMDLL_ConfigGet(handle, VMMDLL_OPT_CONFIG_IS_REFRESH_ENABLED, &enabled) != FALSE;
    const bool haveTick = VMMDLL_ConfigGet(handle, VMMDLL_OPT_CONFIG_TICK_PERIOD, &tickMs) != FALSE && tickMs != 0;
    const bool havePartial = VMMDLL_ConfigGet(handle, VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_PARTIAL, &partialTicks) != FALSE;
    const bool haveTotal = VMMDLL_ConfigGet(handle, VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_TOTAL, &totalTicks) != FALSE;

    // The upstream full/MEDIUM process refresh is ~15 s. Move only that
    // expensive refresh out to 60 s; keep Memory/TLB/FAST refreshes enabled.
    constexpr ULONG64 kDefaultMediumRefreshMs = 60000;
    ULONG64 targetMs = ReadEnvironmentU64(L"OMNIGHOST_VMM_MEDIUM_REFRESH_MS", kDefaultMediumRefreshMs);
    bool changed = false;
    ULONG64 appliedTicks = totalTicks;
    if (haveTick && targetMs != 0) {
        targetMs = std::clamp<ULONG64>(targetMs, 15000, 600000);
        appliedTicks = std::max<ULONG64>(1, (targetMs + tickMs - 1) / tickMs);
        changed = VMMDLL_ConfigSet(handle, VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_TOTAL, appliedTicks) != FALSE;
    }
    const ULONG64 oldTotalMs = (haveTick && haveTotal) ? totalTicks * tickMs : 0;
    const ULONG64 newTotalMs = (haveTick && changed) ? appliedTicks * tickMs : oldTotalMs;
    const ULONG64 partialMs = (haveTick && havePartial) ? partialTicks * tickMs : 0;
    std::cout << "[VMM][RefreshPolicy] enabled=" << (haveEnabled ? enabled : 0)
        << " tick_ms=" << (haveTick ? tickMs : 0)
        << " partial_ticks=" << (havePartial ? partialTicks : 0)
        << " partial_ms=" << partialMs
        << " medium_ticks_before=" << (haveTotal ? totalTicks : 0)
        << " medium_ms_before=" << oldTotalMs
        << " medium_ticks_after=" << (changed ? appliedTicks : totalTicks)
        << " medium_ms_after=" << newTotalMs
        << " changed=" << (changed ? "YES" : "NO")
        << (targetMs == 0 ? " mode=UPSTREAM_DEFAULT" : " mode=LOW_LATENCY") << "\n";
    OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::VMM, "refresh policy configured",
        {{"enabled", haveEnabled ? std::to_string(enabled) : "unknown"},
         {"tick_ms", haveTick ? std::to_string(tickMs) : "unknown"},
         {"partial_ms", havePartial && haveTick ? std::to_string(partialMs) : "unknown"},
         {"medium_ms_before", haveTotal && haveTick ? std::to_string(oldTotalMs) : "unknown"},
         {"medium_ms_after", haveTotal && haveTick ? std::to_string(newTotalMs) : "unknown"},
         {"changed", changed ? "yes" : "no"}});
}

// Loads one private-runtime library from its absolute path only. Never searches
// PATH or the directory beside a portable EXE for a same-named DLL.
HMODULE LoadPrivateLibrary(const std::filesystem::path& file)
{
	HMODULE existing = GetModuleHandleW(file.filename().c_str());
	if (existing)
		return existing;
	return LoadLibraryExW(file.c_str(), nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS | LOAD_LIBRARY_SEARCH_SYSTEM32);
}

} // namespace

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// Runtime dependencies
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

bool Memory::EnsureRuntimeDependencies()
{
	if (runtimeDependenciesInitialized_)
		return dependencyIntegrityOk_;
	runtimeDependenciesInitialized_ = true;

	using OmniGhost::RuntimeBootstrap::PrivateRuntimePath;
	using OmniGhost::RuntimeBootstrap::ValidatePrivateRuntimeFile;

	dependencyIntegrityOk_ = true;
	dependencyIntegrityMessage_.clear();
	std::wstring error;

	// FTDI bridge policy (matches EXTERNAL_FTD3XXWU_ON_FPGA_OPEN):
	// 1) Prefer private-runtime FTD3XX.dll / FTD3XXWU.dll when the embedded
	//    manifest owns them (portable Publish builds).
	// 2) Otherwise accept a side-by-side copy under NativeRuntime/libs or next
	//    to the executable (dev machines with vendor drivers installed).
	// 3) If nothing is present, do NOT hard-fail here - LeechCore loads the
	//    FTDI DLL only when the FPGA device is opened. A missing bridge is
	//    reported as a warning so static-VMM / PnP-only probes can continue.
	auto ftdiOk = false;
	std::string ftdiDetail;
	const wchar_t* const kFtdiCandidates[] = {
		L"libs/FTD3XXWU.dll", // WinUSB D3XX 1.4 preferred
		L"libs/FTD3XX.dll",   // legacy WDF fallback
	};
	for (const wchar_t* relative : kFtdiCandidates) {
		if (ValidatePrivateRuntimeFile(relative, error)) {
			ftdiOk = true;
			ftdiDetail = Narrow(std::wstring(relative)) + ": private-runtime OK";
			break;
		}
	}
	if (!ftdiOk) {
		namespace fs = std::filesystem;
		const fs::path searchRoots[] = {
			OmniGhost::Paths::NativeRuntime() / L"libs",
			OmniGhost::Paths::InstallDirectory() / L"libs",
			OmniGhost::Paths::InstallDirectory(),
		};
		const wchar_t* names[] = { L"FTD3XX.dll", L"FTD3XXWU.dll" };
		for (const auto& root : searchRoots) {
			for (const wchar_t* name : names) {
				const fs::path candidate = root / name;
				std::error_code ec;
				if (fs::is_regular_file(candidate, ec) && !ec && fs::file_size(candidate, ec) > 0) {
					ftdiOk = true;
					ftdiDetail = Narrow(candidate.wstring()) + ": side-by-side OK";
					break;
				}
			}
			if (ftdiOk) break;
		}
	}
	if (!ftdiOk) {
		// Soft-fail: integrity still OK for session bootstrap; FPGA open will
		// surface a real device error if the driver is truly unavailable.
		ftdiDetail = "FTD3XX/FTD3XXWU absent from private runtime and side-by-side paths "
			"(EXTERNAL_FTD3XXWU_ON_FPGA_OPEN - deferred to device open)";
		std::cout << "[DMA][Init] FTDI bridge not pre-validated: " << ftdiDetail << "\n";
	} else {
		std::cout << "[DMA][Init] FTDI bridge: " << ftdiDetail << "\n";
	}

#if !defined(OMNIGHOST_PRIVATE_STATIC_VMM)
	// Release/Tester delay-load leechcore.dll + vmm.dll. Load them explicitly by
	// absolute path from the validated private runtime so the delay-load thunk
	// can never resolve a same-named DLL from an untrusted directory.
	const wchar_t* const kRequired[] = { L"libs/leechcore.dll", L"libs/vmm.dll" };
	for (const wchar_t* relative : kRequired) {
		if (!ValidatePrivateRuntimeFile(relative, error)) {
			dependencyIntegrityOk_ = false;
			if (!dependencyIntegrityMessage_.empty()) dependencyIntegrityMessage_ += "; ";
			dependencyIntegrityMessage_ += Narrow(std::wstring(relative)) + ": " + Narrow(error);
		}
	}
	if (dependencyIntegrityOk_) {
		modules.LEECHCORE = LoadPrivateLibrary(PrivateRuntimePath(L"libs/leechcore.dll"));
		modules.VMM = LoadPrivateLibrary(PrivateRuntimePath(L"libs/vmm.dll"));
		if (!modules.LEECHCORE || !modules.VMM) {
			dependencyIntegrityOk_ = false;
			dependencyIntegrityMessage_ = "LoadLibraryEx failed win32=" + std::to_string(GetLastError())
				+ " leech=" + (modules.LEECHCORE ? "OK" : "FAIL")
				+ " vmm=" + (modules.VMM ? "OK" : "FAIL");
		}
	}
#endif

	// pdbcrust is optional (symbol support); a missing copy never blocks DMA.
#if !defined(OMNIGHOST_DISABLE_VMM_SYMBOLS)
	if (dependencyIntegrityOk_) {
		std::wstring pdbError;
		if (ValidatePrivateRuntimeFile(L"libs/pdbcrust.dll", pdbError))
			modules.PDBCRUST = LoadPrivateLibrary(PrivateRuntimePath(L"libs/pdbcrust.dll"));
		else
			std::wcout << L"[DMA][Init] pdbcrust optional: " << pdbError << L"\n";
	}
#endif

	if (dependencyIntegrityOk_) {
		dependencyIntegrityMessage_ = "runtime DMA SHA-256 validation passed";
		std::cout << "[DMA] canonical runtime libraries validated (AMD64 + absolute-path loading)"
			<< " ftdi=" << (ftdiOk ? "OK" : "DEFERRED")
#if !defined(OMNIGHOST_PRIVATE_STATIC_VMM)
			<< " leech=" << (modules.LEECHCORE ? "OK" : "FAIL")
			<< " vmm=" << (modules.VMM ? "OK" : "FAIL")
#else
			<< " vmm=STATIC leech=STATIC"
#endif
			<< " pdbcrust=" << (modules.PDBCRUST ? "OK" : "absent") << "\n";
	} else {
		std::cout << "[DMA] FATAL canonical runtime dependency missing or invalid: "
			<< dependencyIntegrityMessage_ << "\n";
	}
	std::cout << "[DMA][INTEGRITY] result=" << (dependencyIntegrityOk_ ? "OK" : "FAIL")
		<< " detail=" << dependencyIntegrityMessage_ << "\n";
	return dependencyIntegrityOk_;
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// State reset helpers
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

void Memory::ResetProcessState(bool preserveAppliedOverride) noexcept
{
	InvalidateScatterHandles("process context reset");
	current_process = CurrentProcessInformation{};
	PROCESS_INITIALIZED = FALSE;
	generationProcessPid_ = 0;
	processGeneration_.fetch_add(1, std::memory_order_acq_rel);
	if (!preserveAppliedOverride) {
		appliedProcessDtb_ = 0;
		appliedProcessPid_ = 0;
	}
}

void Memory::ResetVmmState() noexcept
{
	pluginsInitialized_ = false;
	pluginsInitializedFor_ = nullptr;
	procInfoState_ = ProcInfoState::Uninitialized;
	procInfoProgress_ = -1;
	procInfoDtbFileSize_ = 0;
	procInfoLastNt_ = 0;
	procInfoSessionRecoveryRecommended_.store(false, std::memory_order_release);
	// PROCESS_DTB lives inside the VMM session; a new session starts clean.
	appliedProcessDtb_ = 0;
	appliedProcessPid_ = 0;
}

void Memory::ResetDeviceState() noexcept
{
	ResetProcessState(false);
	ResetVmmState();
	if (vHandle) {
		std::cout << "[VMM] closing session generation=" << SessionGeneration() << "\n";
		vHandle.reset();
	}
	DMA_INITIALIZED = FALSE;
	sessionGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

void Memory::InvalidateProcess()
{
	std::scoped_lock controlPlaneLock(controlPlaneMutex_);
	// The VMM session (and its PROCESS_DTB override) stays alive; only OmniGhost's
	// process binding is dropped so the next Init() re-attaches PID/modules.
	ResetProcessState(true);
}

void Memory::ResetDevice()
{
	std::scoped_lock controlPlaneLock(controlPlaneMutex_);
	RequestCancel();
	deviceResetCount_.fetch_add(1, std::memory_order_relaxed);
	ResetDeviceState();
	// ResetDevice is a completed recovery boundary: the epoch increment above
	// cancels older operations while a subsequent Init may proceed normally.
	ClearCancel();
}

void Memory::Close() noexcept
{
	std::scoped_lock controlPlaneLock(controlPlaneMutex_);
	RequestCancel();
	ResetDeviceState();
}

bool Memory::Reconnect(bool memMap, bool debug)
{
	std::scoped_lock controlPlaneLock(controlPlaneMutex_);
	ResetDevice();
	return Init(std::string(), memMap, debug);
}

bool Memory::Rebind(std::string process_name, bool memMap, bool debug)
{
	std::scoped_lock controlPlaneLock(controlPlaneMutex_);
	InvalidateProcess();
	return Init(std::move(process_name), memMap, debug);
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// FPGA / VMM session open + process bind
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

bool Memory::SetFPGA()
{
	// Match UC DMA base behaviour: query FPGA id/version, then clear the PCIe
	// auto-clear register on modern firmware (same LcCommand the working base uses).
	if (!vHandle)
		return false;
	OMNIGHOST_VMM_TIMING("SetFPGA");
	ULONG64 qwID = 0, qwVersionMajor = 0, qwVersionMinor = 0, deviceId = 0;
	const bool idOk = VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_FPGA_ID, &qwID) != FALSE;
	const bool verOk = VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_VERSION_MAJOR, &qwVersionMajor) != FALSE
		&& VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_VERSION_MINOR, &qwVersionMinor) != FALSE;
	(void)VMMDLL_ConfigGet(vHandle, LC_OPT_FPGA_DEVICE_ID, &deviceId);
	if (!idOk && !verOk) {
		std::cout << "[!] Failed to lookup FPGA device, attempting to proceed" << std::endl;
		return true;
	}
	std::cout << "[+] FPGA_ID=" << qwID << " VERSION=" << qwVersionMajor << "." << qwVersionMinor
		<< " device=" << HexOf(deviceId) << std::endl;

	// Same threshold as the working UC base (major>=5 or major==4 && minor>=7).
	if ((qwVersionMajor >= 4) && ((qwVersionMajor >= 5) || (qwVersionMinor >= 7))) {
		LC_CONFIG config{};
		config.dwVersion = LC_CONFIG_VERSION;
		std::snprintf(config.szDevice, sizeof(config.szDevice), "existing");
		const HANDLE handle = LcCreate(&config);
		if (!handle) {
			std::cout << "[!] Failed to create FPGA device for register clear - continuing" << std::endl;
			return true;
		}
		DWORD abort2 = 0x10;
		LcCommand(handle, LC_CMD_FPGA_CFGREGPCIE_MARKWR | 0x002, 4,
			reinterpret_cast<PBYTE>(&abort2), nullptr, nullptr);
		std::cout << "[-] Register auto cleared (UC-base SetFPGA path)" << std::endl;
		LcClose(handle);
	}
	return true;
}


static bool PreloadFtdiFromPath(const std::filesystem::path& file) noexcept
{
	std::error_code ec;
	if (!std::filesystem::is_regular_file(file, ec) || ec || std::filesystem::file_size(file, ec) == 0)
		return false;
	HMODULE mod = LoadLibraryExW(
		file.c_str(),
		nullptr,
		LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (mod) {
		std::wcout << L"[DMA][Init] LoadLibrary FTDI OK: " << file.wstring() << L"\n";
		return true;
	}
	std::wcout << L"[DMA][Init] LoadLibrary FTDI FAIL: " << file.wstring()
		<< L" win32=" << GetLastError() << L"\n";
	return false;
}

static bool PreloadAllFtdiBridges() noexcept
{
	namespace fs = std::filesystem;
	const fs::path roots[] = {
		OmniGhost::Paths::NativeRuntime() / L"libs",
		OmniGhost::Paths::InstallDirectory() / L"libs",
		OmniGhost::Paths::InstallDirectory() / L"third_party" / L"dma_stack" / L"bin",
		OmniGhost::Paths::InstallDirectory(),
	};
	// LeechCore 2.22+ prefers the WinUSB bridge. Keep the old WDF DLL as fallback.
	for (const auto& root : roots) {
		if (PreloadFtdiFromPath(root / L"FTD3XXWU.dll"))
			return true;
	}
	for (const auto& root : roots) {
		if (PreloadFtdiFromPath(root / L"FTD3XX.dll"))
			return true;
	}
	return false;
}

static bool HasFtdiBridge() noexcept
{
	namespace fs = std::filesystem;
	std::wstring err;
	using OmniGhost::RuntimeBootstrap::ValidatePrivateRuntimeFile;
	using OmniGhost::RuntimeBootstrap::MaterializePrivateRuntimeFile;

	// Always try to materialize from the embedded PE first.
	(void)MaterializePrivateRuntimeFile(L"libs/FTD3XX.dll", err);
	(void)MaterializePrivateRuntimeFile(L"libs/FTD3XXWU.dll", err);

	if (GetModuleHandleW(L"FTD3XXWU.dll") || GetModuleHandleW(L"FTD3XX.dll"))
		return true;

	bool any = false;
	const fs::path roots[] = {
		OmniGhost::Paths::NativeRuntime() / L"libs",
		OmniGhost::Paths::InstallDirectory() / L"libs",
		OmniGhost::Paths::InstallDirectory() / L"third_party" / L"dma_stack" / L"bin",
		OmniGhost::Paths::InstallDirectory(),
	};
	for (const auto& root : roots) {
		if (PreloadFtdiFromPath(root / L"FTD3XXWU.dll")) { any = true; break; }
	}
	if (!any) {
		for (const auto& root : roots) {
			if (PreloadFtdiFromPath(root / L"FTD3XX.dll")) { any = true; break; }
		}
	}
	if (ValidatePrivateRuntimeFile(L"libs/FTD3XXWU.dll", err) ||
	    ValidatePrivateRuntimeFile(L"libs/FTD3XX.dll", err))
		any = true;
	return any;
}


// UC-base style: plain VMMDLL_Initialize (no errorinfo out-param).
static VMM_HANDLE SafeVmmInitialize(DWORD argc, LPCSTR argv[], DWORD* outSehCode) noexcept
{
	if (outSehCode)
		*outSehCode = 0;
	VMM_HANDLE handle = nullptr;
	__try {
		handle = VMMDLL_Initialize(argc, argv);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		if (outSehCode)
			*outSehCode = GetExceptionCode();
		handle = nullptr;
	}
	return handle;
}

bool Memory::Init(std::string process_name, bool memMap, bool debug, bool quickDeviceProbe)
{
	// One control-plane owner at a time. Concurrent VMMDLL/LeechCore opens against
	// the same FPGA are a driver/firmware stability risk and never necessary.
	std::scoped_lock controlPlaneLock(controlPlaneMutex_);

	if (!EnsureRuntimeDependencies()) {
		OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Error,
			OmniGhost::SessionLog::Subsystem::DMA, "Memory::Init runtime dependencies failed",
			{{"detail", dependencyIntegrityMessage_}});
		last_attach_result = AttachResult::Failed;
		return false;
	}
	if (!dependencyIntegrityOk_) {
		std::cout << "[DMA][INTEGRITY] refusing initialization: " << dependencyIntegrityMessage_ << "\n";
		OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Error,
			OmniGhost::SessionLog::Subsystem::DMA, "Memory::Init dependency integrity failed",
			{{"detail", dependencyIntegrityMessage_}});
		last_attach_result = AttachResult::Failed;
		return false;
	}
	if (IsCancellationRequested()) {
		std::cout << "[DMA][Init] cancelled before start\n";
		last_attach_result = AttachResult::Waiting;
		return false;
	}

	// A closed handle with a stale flag means a previous session died.
	if (DMA_INITIALIZED && !vHandle) {
		DMA_INITIALIZED = FALSE;
		PROCESS_INITIALIZED = FALSE;
	}

	// Phase 1: open FPGA + VMM once. Device lifecycle is separate from process bind.
	if (!DMA_INITIALIZED) {
		OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
			OmniGhost::SessionLog::Subsystem::DMA, "FPGA open started",
			{{"memmap_requested", memMap ? "yes" : "no"},
			 {"debug", debug ? "yes" : "no"}});
		std::string mmapPath;
		if (memMap) {
			std::error_code ec;
			const auto candidate = OmniGhost::Paths::NativeRuntime() / L"mmap.txt";
			if (std::filesystem::is_regular_file(candidate, ec) && !ec)
				mmapPath = candidate.string();
		}

		std::string firstError;
		std::string lastError;
		std::string firstApiMessage;
		std::string lastApiMessage;
		bool sehFaultObserved = false;

		// UC-base compatible open: exactly like the working base
		// Minimal argv: "", "-device", "fpga://algo=0" [+ optional -memmap / -v / -printf]
			auto tryOpen = [&](const char* device, bool useMmap, int attemptNo, int attemptMax) -> bool {
				std::vector<LPCSTR> args;
				args.push_back("");
				args.push_back("-device");
				args.push_back(device);
#if defined(OMNIGHOST_DISABLE_VMM_INFODB)
				args.push_back("-disable-infodb");
#endif
#if defined(OMNIGHOST_DISABLE_VMM_SYMBOLS)
				args.push_back("-disable-symbols");
#endif
			if (EnvironmentFlagEnabled(L"OMNIGHOST_VMM_NOREFRESH"))
				args.push_back("-norefresh"); // diagnostic A/B only
			if (useMmap && !mmapPath.empty()) {
				args.push_back("-memmap");
				args.push_back(mmapPath.c_str());
			}
			if (debug) {
				args.push_back("-v");
				args.push_back("-printf");
			}

			std::cout << "[DMA][Init] attempt=" << attemptNo << "/" << attemptMax
				<< " device=" << device
				<< " infodb="
#if defined(OMNIGHOST_DISABLE_VMM_INFODB)
				<< "DISABLED"
#else
				<< "ENABLED"
#endif
				<< " symbols="
#if defined(OMNIGHOST_DISABLE_VMM_SYMBOLS)
				<< "DISABLED"
#else
				<< "ENABLED"
#endif
				<< " refresh=" << (EnvironmentFlagEnabled(L"OMNIGHOST_VMM_NOREFRESH") ? "DISABLED_DIAGNOSTIC" : "ENABLED_TUNED")
				<< (useMmap && !mmapPath.empty() ? " memmap=YES" : " memmap=NO") << "\n";
			OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
				OmniGhost::SessionLog::Subsystem::DMA, "FPGA open attempt",
				{{"attempt", std::to_string(attemptNo)}, {"attempt_max", std::to_string(attemptMax)},
				 {"device", device}, {"memmap", (useMmap && !mmapPath.empty()) ? "yes" : "no"}});

			// The bridges were already loaded from validated absolute paths above.
			// Do not call LoadLibraryA here: that would reintroduce an unrestricted
			// search and could select a stale vendor DLL from the current directory.

			DWORD sehCode = 0;
			VMM_HANDLE handle = SafeVmmInitialize(static_cast<DWORD>(args.size()), args.data(), &sehCode);
			if (sehCode != 0) {
				sehFaultObserved = true;
				std::cout << "[DMA][Init] SEH fault during VMMDLL_Initialize code=0x"
					<< std::hex << sehCode << std::dec
					<< " (runtime validated; fault originated in the VMM/vendor-device stack)\n";
				const std::string message = std::string(device) + " Initialize SEH fault code=" + HexOf(sehCode);
				if (firstError.empty()) firstError = message;
				lastError = message;
				if (firstApiMessage.empty()) firstApiMessage = message;
				lastApiMessage = message;
				OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Critical,
					OmniGhost::SessionLog::Subsystem::DMA, "FPGA open SEH fault",
					{{"device", device}, {"seh_code", HexOf(sehCode)}});
				return false;
			}

			if (!handle) {
				const std::string message = std::string(device) + " Initialize FAIL";
				if (firstError.empty()) firstError = message;
				lastError = message;
				std::cout << "[DMA][Init] " << message << "\n";
				OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Error,
					OmniGhost::SessionLog::Subsystem::DMA, "FPGA open failed",
					{{"device", device}, {"result", "VMMDLL_Initialize returned null"}});
				return false;
			}
			vHandle = handle;
			std::cout << "[DMA][Init] OPEN OK device=" << device << "\n";
			OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Info,
				OmniGhost::SessionLog::Subsystem::DMA, "FPGA open succeeded", {{"device", device}});
			return true;
		};


				// Preload FTDI (best-effort). UC base does LoadLibraryA("FTD3XX.dll") and
		// continues even if lookup is soft — do not hard-fail before open.
		std::cout << "[DMA][Init] Preloading FTDI bridges (UC-compatible)...\n";
		PreloadAllFtdiBridges();
		if (!HasFtdiBridge()) {
			std::cout << "[DMA][Init] WARN: FTD3XX not pre-validated — still trying open like UC base\n";
		}
		try {
			LogFtdiDiagnostics();
		} catch (const std::exception& ex) {
			std::cout << "[DMA][FTDI] diagnostics skipped: " << ex.what() << "\n";
		} catch (...) {
			std::cout << "[DMA][FTDI] diagnostics skipped: unknown error\n";
		}

		// Exact match with working UC base: only fpga://algo=0, no extended flags
		struct Candidate { const char* device; bool useMmap; };
		const Candidate candidates[] = {
			{ "fpga://algo=0", false }, // Primary (matches working base)
			{ "fpga://algo=0", true  }, // + memmap fallback
			{ "fpga",          false }, // Bare fpga fallback
		};
		constexpr int kAttemptsPerDevice = 1; // Single attempt like working base
		const int attemptMax = static_cast<int>(std::size(candidates)) * kAttemptsPerDevice;
		int attemptNo = 0;
		bool opened = false;
		for (const Candidate& candidate : candidates) {
			if (sehFaultObserved) {
				std::cout << "[DMA][Init] stopping fallback attempts after an SEH fault to avoid reopening an unstable device stack\n";
				break;
			}
			if (candidate.useMmap && mmapPath.empty())
				continue;
			if (attemptNo > 0)
				std::cout << "[DMA][Init] fallback device=" << candidate.device
					<< (candidate.useMmap ? "+mmap" : "") << "\n";
			for (int retry = 0; retry < kAttemptsPerDevice && !opened; ++retry) {
				if (IsCancellationRequested()) {
					std::cout << "[DMA][Init] cancelled during FPGA retry loop\n";
					last_attach_result = AttachResult::Waiting;
					return false;
				}
				++attemptNo;
				opened = tryOpen(candidate.device, candidate.useMmap, attemptNo, attemptMax);
				if (!opened)
					std::this_thread::sleep_for(std::chrono::milliseconds(400));
			}
			if (opened)
				break;
		}

		if (!opened) {
			std::cout << "[DMA][Init] FAILED attempts=" << attemptNo
				<< " first_error=" << firstError << " last_error=" << lastError << "\n";
			std::cout << "[DMA][Init] first_api_message=\"" << firstApiMessage
				<< "\" last_api_message=\"" << lastApiMessage << "\"\n";
			std::cout << "[DMA] ===============================================================\n";
			OmniGhost::SessionLog::Write(OmniGhost::SessionLog::Severity::Critical,
				OmniGhost::SessionLog::Subsystem::DMA, "FPGA open exhausted all candidates",
				{{"attempts", std::to_string(attemptNo)}, {"first_error", firstError},
				 {"last_error", lastError}, {"first_api_message", firstApiMessage},
				 {"last_api_message", lastApiMessage}, {"seh_fault", sehFaultObserved ? "yes" : "no"}});
			std::cout << "[DMA] Falha na comunicacao com o DMA/FPGA.\n";
			std::cout << "[DMA] Possiveis causas e solucoes:\n";
			std::cout << "[DMA]   1) FPGA nao conectada ou nao reconhecida pelo Windows\n";
			std::cout << "[DMA]      -> Verifique no Gerenciador de Dispositivos: 'FTDI FT600/FT601'\n";
			std::cout << "[DMA]   2) Driver FTDI desatualizado ou corrompido\n";
			std::cout << "[DMA]      -> Reinstale o driver FTDI D3XX do site da FTDI\n";
			std::cout << "[DMA]   3) Outro programa usando a FPGA (PCILeech, MemProcFS, etc.)\n";
			std::cout << "[DMA]      -> Feche outros programas DMA e reinicie o PC\n";
			std::cout << "[DMA]   4) Falha no link PCIe (cabo/riser frouxo ou danificado)\n";
			std::cout << "[DMA]      -> Reencaixe a placa FPGA e verifique cabos de alimentacao\n";
			std::cout << "[DMA]   5) Alimentacao insuficiente para a placa FPGA\n";
			std::cout << "[DMA]      -> Verifique conexoes de 12V/3.3V e cabos PCIe power\n";
			std::cout << "[DMA]   6) Conflito de versao LeechCore/MemProcFS\n";
			std::cout << "[DMA]      -> Verifique se vmm.dll e leechcore.dll sao da mesma versao\n";
			std::cout << "[DMA]   7) Secure Boot ativo impede driver FTDI de carregar\n";
			std::cout << "[DMA]      -> Desative Secure Boot na BIOS/UEFI\n";
			std::cout << "[DMA]   8) Virtualizacao (Hyper-V/VBS/WSL2) bloqueia acesso DMA\n";
			std::cout << "[DMA]      -> Desative Hyper-V, VBS/Memory Integrity, WSL2\n";
			std::cout << "[DMA] ===============================================================\n";
			last_attach_result = AttachResult::Failed;
			return false;
		}

		if (IsCancellationRequested()) {
			std::cout << "[DMA][Init] cancelled before device open completed\n";
			vHandle.reset();
			last_attach_result = AttachResult::Waiting;
			return false;
		}

		DMA_INITIALIZED = TRUE;
		ResetVmmState();
		vmmOpenCount_.fetch_add(1, std::memory_order_relaxed);
		sessionGeneration_.fetch_add(1, std::memory_order_acq_rel);
		InvalidateScatterHandles("vmm session opened");

		if (!EnvironmentFlagEnabled(L"OMNIGHOST_VMM_NOREFRESH"))
			ConfigureVmmLowLatencyRefresh(vHandle);
		else
			std::cout << "[VMM][RefreshPolicy] mode=NOREFRESH_DIAGNOSTIC (background refresh disabled at initialization)\n";

		ULONG64 major = 0, minor = 0, revision = 0;
		(void)VMMDLL_ConfigGet(vHandle, VMMDLL_OPT_CONFIG_VMM_VERSION_MAJOR, &major);
		(void)VMMDLL_ConfigGet(vHandle, VMMDLL_OPT_CONFIG_VMM_VERSION_MINOR, &minor);
		(void)VMMDLL_ConfigGet(vHandle, VMMDLL_OPT_CONFIG_VMM_VERSION_REVISION, &revision);
		std::cout << "[+] DMA opened (UC-compatible path) attempts=" << attemptNo << "\n";
		std::cout << "[VMM] version=" << major << "." << minor << "." << revision
			<< " session_generation=" << SessionGeneration()
			<< " open_count=" << vmmOpenCount_.load(std::memory_order_relaxed) << "\n";
		(void)SetFPGA();

		if (quickDeviceProbe) {
			std::cout << "[DMA][Probe] device open confirmed; deferred VMM/plugin preparation\n";
			last_attach_result = AttachResult::Success;
			return true;
		}
	}

	if (process_name.empty()) {
		last_attach_result = AttachResult::Success;
		return true;
	}

	// Phase 2: bind to the target process.
	std::cout << "[DMA][Bind] pid lookup start process=" << process_name << "\n";
	DWORD pid = 0;
	{
		OMNIGHOST_VMM_TIMING("PidGetFromName(bind)");
		if (!VMMDLL_PidGetFromName(vHandle, const_cast<LPSTR>(process_name.c_str()), &pid))
			pid = 0;
	}
	std::cout << "[DMA][Bind] pid lookup done pid=" << pid << "\n";
	if (!pid) {
		PROCESS_INITIALIZED = FALSE;
		last_attach_result = AttachResult::Waiting;
		return false;
	}

	if (current_process.PID != static_cast<int>(pid) || current_process.process_name != process_name) {
		if (appliedProcessPid_ != 0 && appliedProcessPid_ != static_cast<int>(pid)) {
			std::cout << "[VMM] applied PROCESS_DTB tracking reset (PID changed "
				<< appliedProcessPid_ << " -> " << pid << ")\n";
			appliedProcessDtb_ = 0;
			appliedProcessPid_ = 0;
		}
		InvalidateScatterHandles("process bind");
		current_process = CurrentProcessInformation{};
		current_process.PID = static_cast<int>(pid);
		current_process.process_name = process_name;
		generationProcessPid_ = static_cast<int>(pid);
		processGeneration_.fetch_add(1, std::memory_order_acq_rel);
		processBindCount_.fetch_add(1, std::memory_order_relaxed);
		std::cout << "[PROCESS] generation=" << ProcessGeneration() << " pid=" << pid
			<< " name=" << process_name << "\n";
	}

	std::cout << "[DMA][Bind] FixCr3 start pid=" << pid << "\n";
	const bool ok = FixCr3();
	std::cout << "[DMA][Bind] FixCr3 done result=" << LastAttachResultName() << "\n";
	if (!ok) {
		PROCESS_INITIALIZED = FALSE;
		if (last_attach_result == AttachResult::Waiting)
			std::cout << "[DMA] device=READY VMM=READY process=WAITING (MZ/DTB not valid)\n";
		return false;
	}

	if (!current_process.base_size && !current_process.process_name.empty()) {
		PVMMDLL_MAP_MODULEENTRY raw = nullptr;
		if (VMMDLL_Map_GetModuleFromNameU(vHandle, current_process.PID,
			const_cast<LPSTR>(current_process.process_name.c_str()), &raw, VMMDLL_MODULE_FLAG_NORMAL) && raw) {
			VmmOwned<VMMDLL_MAP_MODULEENTRY> owned(raw);
			current_process.base_size = static_cast<size_t>(raw->cbImageSize);
		}
	}
	PROCESS_INITIALIZED = TRUE;
	last_attach_result = AttachResult::Success;
	std::cout << "[DMA] attach validate MZ @0x" << std::hex << current_process.base_address << std::dec
		<< " pid=" << current_process.PID << " size=" << HexOf(current_process.base_size) << "\n";
	return true;
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// Plugins / ProcInfo / PROCESS_DTB
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

bool Memory::EnsurePluginsInitialized()
{
	if (!vHandle)
		return false;
	if (pluginsInitialized_ && pluginsInitializedFor_ == vHandle.get())
		return true;
	OMNIGHOST_VMM_TIMING("InitializePlugins");
	if (!VMMDLL_InitializePlugins(vHandle)) {
		std::cout << "[VMM] InitializePlugins FAILED\n";
		pluginsInitialized_ = false;
		pluginsInitializedFor_ = nullptr;
		return false;
	}
	pluginsInitialized_ = true;
	pluginsInitializedFor_ = vHandle.get();
	pluginInitializationCount_.fetch_add(1, std::memory_order_relaxed);
	procInfoState_ = ProcInfoState::Generating;
	procInfoProgress_ = 0;
	procInfoSessionRecoveryRecommended_.store(false, std::memory_order_release);
	std::cout << "[VMM] plugins=READY (InitializePlugins once for this handle)"
		<< " plugin_init_count=" << pluginInitializationCount_.load(std::memory_order_relaxed)
		<< " session_generation=" << SessionGeneration() << "\n";
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	return true;
}

bool Memory::ApplyProcessDtbOverride(uint64_t dtb)
{
	if (!vHandle || !dtb || !current_process.PID)
		return false;
	if (IsCancellationRequested()) {
		std::cout << "[VMM] PROCESS_DTB override skipped: operation cancelled\n";
		return false;
	}
	// Re-applying the identical override is redundant and VMMDLL_OPT_PROCESS_DTB
	// has an implicit SLOW refresh side effect: avoid another procinfo invalidation.
	if (appliedProcessPid_ == current_process.PID && appliedProcessDtb_ == dtb) {
		std::cout << "[VMM] PROCESS_DTB override already active for PID=" << current_process.PID
			<< " dtb=" << HexOf(dtb) << " - no ConfigSet/no additional SLOW refresh\n";
		return true;
	}

	std::cout << "[VMM] PROCESS_DTB override requested pid=" << current_process.PID
		<< " dtb=" << HexOf(dtb) << "\n";
	BOOL ok = FALSE;
	{
		OMNIGHOST_VMM_TIMING("ConfigSet(PROCESS_DTB)");
		ok = VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_PROCESS_DTB | static_cast<ULONG64>(current_process.PID), dtb);
	}
	std::cout << "[VMM] ConfigSet PROCESS_DTB result=" << (ok ? "OK" : "FAIL") << "\n";
	if (!ok)
		return false;

	appliedProcessPid_ = current_process.PID;
	appliedProcessDtb_ = dtb;
	// MemProcFS: OPT_PROCESS_DTB writes paDTB_Override and runs VmmProcRefresh_Slow.
	// misc/procinfo may discard its completed context and restart (progress 0).
	std::cout << "[VMM] NOTE: PROCESS_DTB triggers internal SLOW process refresh\n";
	std::cout << "[VFS] procinfo state marked INVALIDATED\n";
	procInfoState_ = ProcInfoState::Invalidated;
	procInfoProgress_ = -1;
	procInfoDtbFileSize_ = 0;
	return true;
}

bool Memory::ClearProcessDtbOverride()
{
	if (!vHandle || !current_process.PID)
		return false;
	if (IsCancellationRequested()) {
		std::cout << "[VMM] PROCESS_DTB clear skipped: operation cancelled\n";
		return false;
	}
	std::cout << "[VMM] PROCESS_DTB clear requested pid=" << current_process.PID
		<< " tracked_dtb=" << HexOf(appliedProcessDtb_) << "\n";
	BOOL ok = FALSE;
	{
		OMNIGHOST_VMM_TIMING("ConfigSet(PROCESS_DTB=0)");
		ok = VMMDLL_ConfigSet(vHandle, VMMDLL_OPT_PROCESS_DTB | static_cast<ULONG64>(current_process.PID), 0);
	}
	std::cout << "[VMM] ConfigSet PROCESS_DTB=0 result=" << (ok ? "OK" : "FAIL") << "\n";
	if (!ok)
		return false;
	appliedProcessPid_ = current_process.PID;
	appliedProcessDtb_ = 0;
	last_good_dtb = 0;
	procInfoState_ = ProcInfoState::Invalidated;
	procInfoProgress_ = -1;
	procInfoDtbFileSize_ = 0;
	procInfoSessionRecoveryRecommended_.store(false, std::memory_order_release);
	std::cout << "[VMM] PROCESS_DTB override cleared; SLOW refresh requested by MemProcFS\n";
	return true;
}

Memory::VfsProbeResult Memory::ProbeProcInfoDtb(const char* timingLabel)
{
	VfsProbeResult result{};
	if (!vHandle)
		return result;
	struct Ctx { bool found = false; uint64_t size = 0; } ctx;
	VMMDLL_VFS_FILELIST2 list{};
	list.dwVersion = VMMDLL_VFS_FILELIST_VERSION;
	list.h = reinterpret_cast<HANDLE>(&ctx);
	list.pfnAddDirectory = [](HANDLE, LPCSTR, PVMMDLL_VFS_FILELIST_EXINFO) {};
	list.pfnAddFile = [](HANDLE h, LPCSTR name, ULONG64 cb, PVMMDLL_VFS_FILELIST_EXINFO) {
		auto* c = reinterpret_cast<Ctx*>(h);
		if (c && name && _stricmp(name, "dtb.txt") == 0) { c->found = true; c->size = cb; }
	};
	{
		OMNIGHOST_VMM_TIMING(timingLabel ? timingLabel : "VfsList(procinfo)");
		result.listed = VMMDLL_VfsListU(vHandle, const_cast<LPSTR>(kProcInfoDir), &list) != FALSE;
	}
	result.found = ctx.found;
	result.size = ctx.size;
	procInfoDtbFileSize_.store(ctx.size, std::memory_order_release);
	return result;
}

bool Memory::WaitForProcInfo(int timeout_sec, bool* out_stuck, bool* out_cancelled)
{
	if (out_stuck) *out_stuck = false;
	if (out_cancelled) *out_cancelled = false;
	if (!vHandle)
		return false;

	const uint64_t epoch = CancellationEpoch();
	const auto started = std::chrono::steady_clock::now();
	auto lastHeartbeat = started;
	auto zeroSince = started;
	int lastProgress = -1;
	procInfoState_ = ProcInfoState::Generating;

	const int effectiveTimeoutSec = timeout_sec > 0 ? timeout_sec : kDefaultProcInfoTimeoutSeconds;
	const auto zeroStallMs = timeouts_.procInfoZeroStallMs.count() > 0
		? timeouts_.procInfoZeroStallMs.count()
		: kDefaultProcInfoZeroStallMs;
	const auto heartbeatMs = timeouts_.procInfoHeartbeatMs.count() > 0
		? timeouts_.procInfoHeartbeatMs.count()
		: kDefaultProcInfoHeartbeatMs;
	const auto pollMs = timeouts_.procInfoPollMs.count() > 0
		? timeouts_.procInfoPollMs.count()
		: kDefaultProcInfoPollMs;
	const auto settleMs = timeouts_.procInfoSettleMs.count() > 0
		? timeouts_.procInfoSettleMs.count()
		: kDefaultProcInfoSettleMs;

	std::cout << "[VFS][procinfo] state=GENERATING timeout=" << effectiveTimeoutSec << "s\n";

	while (true) {
		if (IsCancellationRequested(epoch)) {
			procInfoState_ = ProcInfoState::Cancelled;
			std::cout << "[VFS][procinfo] state=CANCELLED progress=" << lastProgress << "\n";
			if (out_cancelled) *out_cancelled = true;
			return false;
		}
		const auto now = std::chrono::steady_clock::now();
		const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - started).count();
		if (elapsedMs > static_cast<long long>(effectiveTimeoutSec) * 1000LL) {
			procInfoState_ = ProcInfoState::ProgressTimeout;
			std::cout << "[VFS][procinfo] state=TIMEOUT progress=" << lastProgress
				<< " elapsed_ms=" << elapsedMs << "\n";
			(void)ProbeProcInfoDtb("VfsList(procinfo/timeout)");
			return false;
		}

		char buffer[16]{};
		DWORD read = 0;
		NTSTATUS nt = 0;
		{
			OMNIGHOST_VMM_TIMING("VfsRead(procinfo/progress)");
			nt = VMMDLL_VfsReadU(vHandle, const_cast<LPSTR>(kProcInfoProgressFile),
				reinterpret_cast<PBYTE>(buffer), static_cast<DWORD>(sizeof(buffer) - 1), &read, 0);
		}
		procInfoLastNt_.store(static_cast<unsigned long>(nt), std::memory_order_release);
		int progress = -1;
		if (nt == 0 && read > 0) {
			buffer[read] = '\0';
			progress = std::atoi(buffer);
		} else if (nt != 0) {
			std::cout << "[VFS][procinfo] progress_read_status=" << HexOf(static_cast<uint32_t>(nt)) << "\n";
		}

		if (progress != lastProgress) {
			if (progress > 0)
				zeroSince = now;
			lastProgress = progress;
			procInfoProgress_.store(progress, std::memory_order_release);
		}

		if (progress >= 100) {
			// Let the plugin publish dtb.txt before we list it.
			std::this_thread::sleep_for(std::chrono::milliseconds(settleMs));
			VfsProbeResult probe = ProbeProcInfoDtb("VfsList(procinfo/settle)");
			if (!probe.found || probe.size == 0) {
				std::this_thread::sleep_for(std::chrono::milliseconds(settleMs));
				probe = ProbeProcInfoDtb("VfsList(procinfo/settle)");
			}
			procInfoState_ = probe.found
				? (probe.size ? ProcInfoState::Ready : ProcInfoState::ReadyButFileEmpty)
				: ProcInfoState::ReadyButFileMissing;
			std::cout << "[VFS][procinfo] state=READY progress=100 dtb_size=" << probe.size
				<< " dtb_found=" << (probe.found ? "yes" : "no") << " elapsed_ms=" << elapsedMs << "\n";
			return probe.found && probe.size > 0;
		}

		if (progress <= 0) {
			const auto zeroMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - zeroSince).count();
			if (zeroMs >= zeroStallMs) {
				procInfoState_ = ProcInfoState::Stuck;
				procInfoSessionRecoveryRecommended_.store(true, std::memory_order_release);
				std::cout << "[VFS][procinfo] state=ZERO_STALL progress=0 elapsed=" << zeroMs
					<< "ms recovery=REBUILD_VMM_SESSION\n";
				(void)ProbeProcInfoDtb("VfsList(procinfo/zero-stall)");
				if (out_stuck) *out_stuck = true;
				return false;
			}
		}

		if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHeartbeat).count() >= heartbeatMs) {
			lastHeartbeat = now;
			std::cout << "[VFS][procinfo] state=GENERATING progress=" << progress
				<< " elapsed_ms=" << elapsedMs << "\n";
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
	}
}

// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ
// FixCr3 - EAC CR3 shuffle reconciliation through MemProcFS misc/procinfo
// ÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇÔöÇ

bool Memory::FixCr3()
{
	const uint64_t operation_epoch = CancellationEpoch();
	if (IsCancellationRequested(operation_epoch)) {
		std::cout << "[DMA] FixCr3: cancelled before start\n";
		last_attach_result = AttachResult::Waiting;
		return false;
	}
	if (!vHandle || !current_process.PID) {
		last_attach_result = AttachResult::Failed;
		return false;
	}

	// Lifecycle rules:
	// 1) If module map + MZ already work -> done (no PROCESS_DTB touch).
	// 2) If last_good is applied and the live MZ gate failed, explicitly clear
	//    PROCESS_DTB inside MemProcFS (clearing only our cache leaves paDTB_Override stale).
	// 3) After any real override invalidation: WaitForProcInfo (no MEDIUM-refresh thrash).
	// 4) Parse dtb.txt only when READY; test candidates through the MZ gate.
	last_attach_result = AttachResult::Failed;
	const auto started = std::chrono::steady_clock::now();
	const DWORD pid = static_cast<DWORD>(current_process.PID);

	auto resolve_module_base = [&](const char* name) -> uintptr_t {
		if (!name || !name[0]) return 0;
		PVMMDLL_MAP_MODULEENTRY raw = nullptr;
		if (!VMMDLL_Map_GetModuleFromNameU(vHandle, pid, const_cast<LPSTR>(name), &raw, VMMDLL_MODULE_FLAG_NORMAL) || !raw)
			return 0;
		VmmOwned<VMMDLL_MAP_MODULEENTRY> owned(raw);
		return static_cast<uintptr_t>(raw->vaBase);
	};

	auto resolve_validation_base = [&]() -> uintptr_t {
		// 1) Process image (cod.exe, cs2.exe, RustClient.exe, ...)
		const std::string& proc = current_process.process_name;
		if (!proc.empty()) {
			if (uintptr_t b = resolve_module_base(proc.c_str())) return b;
			const auto dot = proc.find_last_of('.');
			if (dot != std::string::npos) {
				if (uintptr_t b = resolve_module_base(proc.substr(0, dot).c_str())) return b;
			}
		}
		// 2) Game-specific secondary modules (only if mapped)
		static const char* const kSecondary[] = { "GameAssembly.dll", "client.dll", "GTA5.exe", "GTAProcess.exe" };
		for (const char* name : kSecondary) {
			if (uintptr_t b = resolve_module_base(name)) return b;
		}
		// 3) Already-known base from an earlier bind
		return current_process.base_address;
	};

	auto commit_base = [&](uintptr_t validated) {
		uintptr_t procBase = 0;
		if (!current_process.process_name.empty())
			procBase = resolve_module_base(current_process.process_name.c_str());
		current_process.base_address = procBase ? procBase : validated;
	};

	// Unified validation: apply override + MZ of a process-appropriate module.
	// After PROCESS_DTB MemProcFS runs a SLOW refresh; module map/user pages can lag.
	auto dtb_works = [&](uint64_t dtb) -> bool {
		if (!dtb || IsCancellationRequested(operation_epoch))
			return false;
		if (!ApplyProcessDtbOverride(dtb))
			return false;
		uintptr_t base = 0;
		for (int attempt = 0; attempt < 4; ++attempt) {
			if (attempt > 0)
				std::this_thread::sleep_for(std::chrono::milliseconds(40 + attempt * 20));
			if (IsCancellationRequested(operation_epoch))
				return false;
			base = resolve_validation_base();
			if (!base) {
				if (attempt == 0)
					std::cout << "[DMA] FixCr3: no validation module mapped yet for process='"
						<< current_process.process_name << "' (will retry)\n";
				continue;
			}
			if (MemoryDetail::ReadMzHeader(vHandle, pid, base)) {
				commit_base(base);
				return true;
			}
			uint16_t raw = 0;
			DWORD n = 0;
			VMMDLL_MemReadEx(vHandle, pid, base, reinterpret_cast<PBYTE>(&raw), sizeof(raw), &n, VMMDLL_FLAG_NOCACHE);
			std::cout << "[DMA] FixCr3: MZ fail attempt=" << (attempt + 1) << " base=" << HexOf(base)
				<< " raw=" << HexOf(raw) << " n=" << n << " process=" << current_process.process_name << "\n";
		}
		if (!base)
			std::cout << "[DMA] FixCr3: no validation module mapped for process='"
				<< current_process.process_name << "' after retries\n";
		else
			std::cout << "[DMA] MZ FAIL - process mapped but user pages not readable yet\n";
		return false;
	};

	// Fast path: OK only if MZ reads. Never touches PROCESS_DTB (preserves procinfo).
	{
		const uintptr_t base = resolve_validation_base();
		if (base && MemoryDetail::ReadMzHeader(vHandle, pid, base)) {
			std::cout << "[PROCESS] memory_validation=OK module=" << HexOf(base)
				<< " process=" << current_process.process_name << "\n";
			commit_base(base);
			last_attach_result = AttachResult::Success;
			return true;
		}
	}

	// Soft path: reuse last_good conservatively.
	if (last_good_dtb) {
		std::cout << "[DMA] FixCr3: considering last_good_dtb=" << HexOf(last_good_dtb) << "\n";
		if (appliedProcessPid_ == current_process.PID && appliedProcessDtb_ == last_good_dtb) {
			std::cout << "[DMA] FixCr3: last_good already applied but live MZ failed - clearing PROCESS_DTB in VMM\n";
			if (!ClearProcessDtbOverride()) {
				std::cout << "[DMA] FixCr3: failed to clear stale PROCESS_DTB; fresh VMM session required\n";
				procInfoSessionRecoveryRecommended_.store(true, std::memory_order_release);
				last_attach_result = AttachResult::Waiting;
				return false;
			}
		} else {
			std::cout << "[DMA] FixCr3: applying last_good once for validation\n";
			if (dtb_works(last_good_dtb)) {
				std::cout << "[DMA] FixCr3: last_good still valid\n";
				last_attach_result = AttachResult::Success;
				return true;
			}
			std::cout << "[DMA] FixCr3: last_good invalid (MZ fail) - clearing PROCESS_DTB in VMM\n";
			if (!ClearProcessDtbOverride()) {
				std::cout << "[DMA] FixCr3: failed to clear invalid PROCESS_DTB; fresh VMM session required\n";
				procInfoSessionRecoveryRecommended_.store(true, std::memory_order_release);
				last_attach_result = AttachResult::Waiting;
				return false;
			}
		}
	}

	// A previously rejected candidate can leave PROCESS_DTB active without a
	// last_good marker. Reconcile that persistent VMM state before ProcInfo.
	if (!last_good_dtb && appliedProcessPid_ == current_process.PID && appliedProcessDtb_ != 0) {
		std::cout << "[DMA] FixCr3: stale applied PROCESS_DTB exists without last_good - clearing\n";
		if (!ClearProcessDtbOverride()) {
			procInfoSessionRecoveryRecommended_.store(true, std::memory_order_release);
			last_attach_result = AttachResult::Waiting;
			return false;
		}
	}

	if (!EnsurePluginsInitialized()) {
		std::cout << "[DMA] FixCr3: plugins not ready\n";
		last_attach_result = AttachResult::Failed;
		return false;
	}

	// Wait for a fresh procinfo generation. REFRESH_FREQ_MEDIUM is NOT a
	// "restart procinfo" switch and is deliberately never used here.
	bool stuck = false;
	bool cancelled = false;
	const int timeoutSec = timeouts_.procInfoTimeout.count() > 0
		? static_cast<int>(timeouts_.procInfoTimeout.count() / 1000)
		: kDefaultProcInfoTimeoutSeconds;
	if (!WaitForProcInfo(timeoutSec, &stuck, &cancelled)) {
		if (cancelled) {
			std::cout << "[DMA] FixCr3: procinfo wait CANCELLED\n";
		} else if (stuck) {
			std::cout << "[DMA] FixCr3: procinfo STUCK after " << timeoutSec
				<< "s - backend should enter WAITING (FPGA still OK)\n";
		} else {
			std::cout << "[DMA] FixCr3: procinfo not READY after wait\n";
		}
		last_attach_result = AttachResult::Waiting;
		return false;
	}

	// dtb.txt: distinguish missing vs empty vs sized.
	const VfsProbeResult probe = ProbeProcInfoDtb("VfsList(procinfo)");
	if (!probe.listed)
		std::cout << "[VFS] VfsList procinfo FAIL\n";
	if (!probe.found)
		std::cout << "[VFS] dtb.txt found=no\n";
	else
		std::cout << "[VFS] dtb.txt found=yes size=" << probe.size << "\n";
	if (!probe.found || probe.size < 64) {
		std::cout << "[VFS][procinfo] dtb.txt unavailable after wait - no blind refresh thrash\n";
		procInfoState_ = probe.found ? ProcInfoState::ReadyButFileEmpty : ProcInfoState::ReadyButFileMissing;
		last_attach_result = AttachResult::Waiting;
		return false;
	}

	std::vector<uint64_t> candidates;
	auto push = [&](uint64_t d) {
		if (!d || (d & 0xFFF) != 0) return; // DTBs are page aligned
		if (std::find(candidates.begin(), candidates.end(), d) == candidates.end())
			candidates.push_back(d);
	};

	// Process-reported DTB (may be stale after the CR3 shuffle, still a useful seed).
	{
		VMMDLL_PROCESS_INFORMATION info{};
		info.magic = VMMDLL_PROCESS_INFORMATION_MAGIC;
		info.wVersion = VMMDLL_PROCESS_INFORMATION_VERSION;
		info.wSize = sizeof(info);
		SIZE_T size = sizeof(info);
		if (VMMDLL_ProcessGetInformation(vHandle, pid, &info, &size)) {
			std::cout << "[DMA] process paDTB=" << HexOf(info.paDTB) << " paDTB_UserOpt=" << HexOf(info.paDTB_UserOpt) << "\n";
			push(info.paDTB);
			push(info.paDTB_UserOpt);
		}
	}

	// dtb.txt lines: "%04x%7i %16llx %16llx %s" -> index, pid, dtb, eprocess, name.
	// EAC-style CR3 shuffles leave the real DTB attached to PID 0 ("---").
	size_t pid0Candidates = 0;
	{
		const DWORD size = static_cast<DWORD>((std::min<uint64_t>)(probe.size, 8ull << 20));
		std::vector<char> text(static_cast<size_t>(size) + 1);
		DWORD read = 0;
		NTSTATUS nt = 0;
		{
			OMNIGHOST_VMM_TIMING("VfsRead(procinfo/dtb)");
			nt = VMMDLL_VfsReadU(vHandle, const_cast<LPSTR>(kProcInfoDtbFile),
				reinterpret_cast<PBYTE>(text.data()), size, &read, 0);
		}
		procInfoLastNt_.store(static_cast<unsigned long>(nt), std::memory_order_release);
		if (nt != 0 || read == 0) {
			std::cout << "[VFS] dtb.txt VfsRead FAILED nt=" << HexOf(static_cast<uint32_t>(nt)) << "\n";
			procInfoState_ = ProcInfoState::VfsReadFailure;
			last_attach_result = AttachResult::Waiting;
			return false;
		}
		text[read] = '\0';
		std::cout << "[VFS] dtb.txt read " << read << " bytes\n";

		std::istringstream lines(std::string(text.data(), read));
		std::string line;
		std::vector<uint64_t> pid0;
		std::vector<uint64_t> samePid;
		while (std::getline(lines, line)) {
			if (line.size() < 12) continue;
			std::istringstream fields(line);
			std::string index, pidText, dtbText;
			if (!(fields >> index >> pidText >> dtbText)) continue;
			// "%04x%7i" may glue index and pid together when pid has 7 digits; tolerate both.
			int entryPid = -1;
			try { entryPid = std::stoi(pidText); }
			catch (const std::exception& ex) {
				std::cerr << "[DMA] FixCr3: Failed to parse PID from '" << pidText << "': " << ex.what() << "\n";
				continue;
			}
			catch (...) {
				std::cerr << "[DMA] FixCr3: Unknown exception parsing PID from '" << pidText << "'\n";
				continue;
			}
			uint64_t dtb = 0;
			try { dtb = std::stoull(dtbText, nullptr, 16); }
			catch (const std::exception& ex) {
				std::cerr << "[DMA] FixCr3: Failed to parse DTB from '" << dtbText << "': " << ex.what() << "\n";
				continue;
			}
			catch (...) {
				std::cerr << "[DMA] FixCr3: Unknown exception parsing DTB from '" << dtbText << "'\n";
				continue;
			}
			if (entryPid == 0) pid0.push_back(dtb);
			else if (entryPid == static_cast<int>(pid)) samePid.push_back(dtb);
		}
		pid0Candidates = pid0.size();
		for (uint64_t d : samePid) push(d);
		for (uint64_t d : pid0) push(d);
	}
	std::cout << "[DMA] dtb.txt candidates pid0=" << pid0Candidates << " total=" << candidates.size() << "\n";

	if (candidates.empty()) {
		std::cout << "[DMA] FixCr3: ZERO DTB candidates\n";
		last_attach_result = AttachResult::Waiting;
		return false;
	}

	size_t tried = 0;
	for (uint64_t dtb : candidates) {
		if (IsCancellationRequested(operation_epoch)) {
			std::cout << "[DMA] FixCr3: cancelled during candidate validation\n";
			last_attach_result = AttachResult::Waiting;
			return false;
		}
		++tried;
		std::cout << "[DMA] FixCr3 try " << tried << "/" << candidates.size()
			<< " testing DTB=" << HexOf(dtb) << "\n";
		if (dtb_works(dtb)) {
			last_good_dtb = dtb;
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - started).count();
			std::cout << "[DMA] FixCr3 SUCCESS DTB=" << HexOf(dtb) << " after " << tried
				<< " candidates " << ms << "ms\n";
			last_attach_result = AttachResult::Success;
			return true;
		}
	}

	// Every candidate was rejected: do not leave the last rejected override active.
	if (appliedProcessPid_ == current_process.PID && appliedProcessDtb_ != 0) {
		if (!ClearProcessDtbOverride()) {
			std::cout << "[DMA] FixCr3: failed to clear final rejected DTB - fresh VMM session recommended\n";
			procInfoSessionRecoveryRecommended_.store(true, std::memory_order_release);
		}
	}
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - started).count();
	std::cout << "[DMA] FixCr3 FAILED after " << tried << " candidates " << ms
		<< "ms dtb_list=" << candidates.size() << "\n";
	last_attach_result = AttachResult::Waiting;
	return false;
}
