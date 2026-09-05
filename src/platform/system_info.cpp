#include "system_info.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <winternl.h>

#include <sstream>

namespace OmniGhost::Platform {
namespace {

std::string MachineName(USHORT machine) {
    switch (machine) {
    case IMAGE_FILE_MACHINE_I386: return "x86";
    case IMAGE_FILE_MACHINE_AMD64: return "x64";
    case IMAGE_FILE_MACHINE_ARM64: return "arm64";
    case IMAGE_FILE_MACHINE_ARMNT: return "arm";
    case IMAGE_FILE_MACHINE_UNKNOWN: return sizeof(void*) == 8 ? "x64" : "x86";
    default: {
        std::ostringstream out;
        out << "machine-0x" << std::hex << machine;
        return out.str();
    }
    }
}

} // namespace

SystemInfo CaptureSystemInfo() noexcept {
    SystemInfo info{};

    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtlGetVersion) {
            RTL_OSVERSIONINFOW version{};
            version.dwOSVersionInfoSize = sizeof(version);
            if (rtlGetVersion(&version) == 0) {
                info.windowsMajor = version.dwMajorVersion;
                info.windowsMinor = version.dwMinorVersion;
                info.windowsBuild = version.dwBuildNumber;
            }
        }
    }

    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    if (HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll")) {
        const auto isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
            GetProcAddress(kernel32, "IsWow64Process2"));
        if (isWow64Process2) {
            USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
            if (isWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine)) {
                info.processArchitecture = MachineName(processMachine);
                info.nativeArchitecture = MachineName(nativeMachine);
            }
        }
    }

    if (info.processArchitecture.empty())
        info.processArchitecture = sizeof(void*) == 8 ? "x64" : "x86";
    if (info.nativeArchitecture.empty())
        info.nativeArchitecture = info.processArchitecture;
    return info;
}

std::string FormatSystemInfo(const SystemInfo& info) {
    std::ostringstream out;
    out << "windows=" << info.windowsMajor << '.' << info.windowsMinor
        << " build=" << info.windowsBuild
        << " process_arch=" << info.processArchitecture
        << " native_arch=" << info.nativeArchitecture;
    return out.str();
}

} // namespace OmniGhost::Platform
