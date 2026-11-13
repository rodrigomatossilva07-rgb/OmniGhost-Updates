#include "crash_handler.h"
#include "build_info.h"

#include "app_paths.h"
#include "unique_handle.h"
#include "scope_exit.h"
#include "text_encoding.h"
#include "../app_version.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <DbgHelp.h>
#include <dpapi.h>
#pragma comment(lib, "crypt32.lib")

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <exception>
#include <fstream>
#include <mutex>
#include <string>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace OmniGhost::CrashHandler {
namespace {

constexpr size_t kMaximumCrashDumps = 5;
std::atomic_flag g_dumpInProgress = ATOMIC_FLAG_INIT;

HMODULE g_dbghelp = nullptr;
std::mutex g_stateMutex;
PreviousRunState g_previousRun = PreviousRunState::Clean;
bool g_installed = false;

fs::path CrashDirectory() {
    return Paths::LocalData() / L"crash-dumps";
}

std::wstring RunStateValueName() {
    return L"RunState-" + Paths::InstallIdentity();
}

std::wstring Timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t buffer[40]{};
    swprintf_s(buffer, L"%04u%02u%02u-%02u%02u%02u-%03u",
        time.wYear, time.wMonth, time.wDay,
        time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    return buffer;
}

bool EncryptDumpInPlace(const fs::path& dumpPath) noexcept {
    try {
        std::ifstream input(dumpPath, std::ios::binary);
        if (!input) return false;
        std::vector<unsigned char> bytes(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (bytes.empty() || bytes.size() > 64ull * 1024ull * 1024ull)
            return false;

        DATA_BLOB plain{};
        plain.pbData = bytes.data();
        plain.cbData = static_cast<DWORD>(bytes.size());
        DATA_BLOB encrypted{};
        if (!CryptProtectData(&plain, L"OmniGhostCrashDump", nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &encrypted))
            return false;

        Platform::UniqueHandle file(CreateFileW(
            dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!file) {
            LocalFree(encrypted.pbData);
            return false;
        }
        DWORD written = 0;
        const BOOL ok = WriteFile(file.get(), encrypted.pbData, encrypted.cbData,
                                  &written, nullptr) && written == encrypted.cbData;
        FlushFileBuffers(file.get());
        LocalFree(encrypted.pbData);
        return ok == TRUE;
    } catch (...) {
        return false;
    }
}

void PruneCrashDumps() noexcept {
    try {
        std::error_code error;
        const fs::path directory = CrashDirectory();

        if (!fs::is_directory(directory, error))
            return;

        struct Entry { fs::path path; fs::file_time_type time{}; };
        std::vector<Entry> entries;
        for (fs::directory_iterator it(directory, fs::directory_options::skip_permission_denied, error), end;
             it != end;
             it.increment(error)) {
            if (error) {
                error.clear();
                continue;
            }
            if (!it->is_regular_file(error) || it->path().extension() != L".dmp") {
                error.clear();
                continue;
            }
            Entry item{it->path(), it->last_write_time(error)};
            if (!error)
                entries.push_back(std::move(item));
            error.clear();
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            return a.time > b.time;
        });
        for (size_t i = kMaximumCrashDumps; i < entries.size(); ++i) {
            fs::remove(entries[i].path, error);
            error.clear();
        }
    } catch (const std::exception& ex) {
        OutputDebugStringW(L"[OmniGhost CrashHandler] Falha ao limpar dumps antigos: ");
        OutputDebugStringA(ex.what());
        OutputDebugStringW(L"\n");
    } catch (...) {
        OutputDebugStringW(L"[OmniGhost CrashHandler] Falha ao limpar dumps antigos: exceção não identificada.\n");
    }
}

bool WriteMarker(bool startupComplete) noexcept {
    try {
        HKEY key = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\OmniGhost\\Runtime", 0, nullptr,
                REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS)
            return false;
        const DWORD state = startupComplete ? 2u : 1u;
        const std::wstring name = RunStateValueName();
        const LSTATUS result = RegSetValueExW(key, name.c_str(), 0, REG_DWORD,
            reinterpret_cast<const BYTE*>(&state), sizeof(state));
        RegCloseKey(key);
        return result == ERROR_SUCCESS;
    } catch (const std::exception& ex) {
        OutputDebugStringA("[OmniGhost CrashHandler] WriteMarker failed: ");
        OutputDebugStringA(ex.what());
        OutputDebugStringA("\n");
        return false;
    } catch (...) {
        OutputDebugStringW(L"[OmniGhost CrashHandler] WriteMarker failed: exceção não identificada.\n");
        return false;
    }
}

PreviousRunState ReadPreviousMarker() noexcept {
    try {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OmniGhost\\Runtime", 0,
                KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
            return PreviousRunState::Clean;
        DWORD state = 0;
        DWORD type = 0;
        DWORD size = sizeof(state);
        const std::wstring name = RunStateValueName();
        const LSTATUS result = RegQueryValueExW(key, name.c_str(), nullptr, &type,
            reinterpret_cast<BYTE*>(&state), &size);
        RegCloseKey(key);
        if (result != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(state))
            return PreviousRunState::Clean;
        return state == 2u ? PreviousRunState::RuntimeInterrupted
                           : PreviousRunState::StartupInterrupted;
    } catch (const std::exception& ex) {
        OutputDebugStringA("[OmniGhost CrashHandler] ReadPreviousMarker failed: ");
        OutputDebugStringA(ex.what());
        OutputDebugStringA("\n");
        return PreviousRunState::Clean;
    } catch (...) {
        OutputDebugStringW(L"[OmniGhost CrashHandler] ReadPreviousMarker failed: exceção não identificada.\n");
        return PreviousRunState::Clean;
    }
}

bool WriteMiniDump(EXCEPTION_POINTERS* exceptionPointers) noexcept {
    if (g_dumpInProgress.test_and_set())
        return false;

    bool success = false;
    try {
        Paths::EnsureUserDirectories();
        std::error_code error;
        const fs::path directory = CrashDirectory();
        fs::create_directories(directory, error);
        if (error)
            throw std::runtime_error("crash directory unavailable");

        const std::wstring version = Platform::Utf8ToWide(OmniGhost::Version);
        std::wstring buildId = Platform::Utf8ToWide(OmniGhost::BuildInfo::BuildId);
        if (buildId.empty()) buildId = L"unknown-build";
        for (wchar_t& ch : buildId) {
            if (!(iswalnum(ch) || ch == L'-' || ch == L'_')) ch = L'_';
        }
        const fs::path dumpPath = directory /
            (L"OmniGhost-" + (version.empty() ? std::wstring(L"unknown") : version) +
             L"-" + buildId + L"-" + Timestamp() + L"-pid" +
             std::to_wstring(GetCurrentProcessId()) + L".dmp");
        Platform::UniqueHandle dumpFile(CreateFileW(
            dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (!dumpFile)
            throw std::runtime_error("dump file unavailable");

        HMODULE dbghelp = g_dbghelp;
        if (!dbghelp)
            throw std::runtime_error("system dbghelp unavailable (not preloaded)");

        using MiniDumpWriteDumpFn = BOOL(WINAPI*)(
            HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
            PMINIDUMP_EXCEPTION_INFORMATION,
            PMINIDUMP_USER_STREAM_INFORMATION,
            PMINIDUMP_CALLBACK_INFORMATION);
        const auto writeDump = reinterpret_cast<MiniDumpWriteDumpFn>(
            GetProcAddress(dbghelp, "MiniDumpWriteDump"));
        if (!writeDump)
            throw std::runtime_error("MiniDumpWriteDump unavailable");

        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        PMINIDUMP_EXCEPTION_INFORMATION exceptionInfoPtr = nullptr;
        if (exceptionPointers) {
            exceptionInfo.ThreadId = GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = exceptionPointers;
            exceptionInfo.ClientPointers = FALSE;
            exceptionInfoPtr = &exceptionInfo;
        }

        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        success = writeDump(
            GetCurrentProcess(), GetCurrentProcessId(), dumpFile.get(), type,
            exceptionInfoPtr, nullptr, nullptr) == TRUE;
        dumpFile.reset();
        if (!success) {
            fs::remove(dumpPath, error);
        } else if (!EncryptDumpInPlace(dumpPath)) {
            fs::remove(dumpPath, error);
            success = false;
        }
        PruneCrashDumps();
    } catch (const std::exception& ex) {
        OutputDebugStringA("[OmniGhost CrashHandler] WriteMiniDump failed: ");
        OutputDebugStringA(ex.what());
        OutputDebugStringA("\n");
        success = false;
    } catch (...) {
        OutputDebugStringW(L"[OmniGhost CrashHandler] WriteMiniDump failed: exceção não identificada.\n");
        success = false;
    }

    g_dumpInProgress.clear();
    return success;
}

LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) noexcept {
    (void)WriteMiniDump(exceptionPointers);
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void TerminateHandler() noexcept {
    (void)WriteMiniDump(nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
    __assume(0);
}

}

bool Install() noexcept {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    if (g_installed)
        return true;
    Paths::EnsureUserDirectories();




    g_dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    g_previousRun = ReadPreviousMarker();
    PruneCrashDumps();
    const bool markerWritten = WriteMarker(false);
    SetUnhandledExceptionFilter(UnhandledExceptionFilter);
    std::set_terminate(TerminateHandler);
    g_installed = true;
    return markerWritten;
}

void MarkStartupComplete() noexcept {
    (void)WriteMarker(true);
}

void MarkCleanShutdown() noexcept {
    try {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\OmniGhost\\Runtime", 0,
                KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            const std::wstring name = RunStateValueName();
            RegDeleteValueW(key, name.c_str());
            RegCloseKey(key);
        }
    } catch (const std::exception& ex) {
        OutputDebugStringA("[OmniGhost CrashHandler] MarkCleanShutdown failed: ");
        OutputDebugStringA(ex.what());
        OutputDebugStringA("\n");
    } catch (...) {
        OutputDebugStringW(L"[OmniGhost CrashHandler] MarkCleanShutdown failed: exceção não identificada.\n");
    }
}

PreviousRunState PreviousRun() noexcept {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    return g_previousRun;
}

const char* PreviousRunStateName(PreviousRunState state) noexcept {
    switch (state) {
    case PreviousRunState::Clean: return "clean";
    case PreviousRunState::StartupInterrupted: return "startup_interrupted";
    case PreviousRunState::RuntimeInterrupted: return "runtime_interrupted";
    default: return "unknown";
    }
}

}
