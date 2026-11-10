#include "process.hpp"

#include <Psapi.h>

#pragma comment(lib, "Psapi.lib")

namespace process {
namespace {
HANDLE OpenReadHandle(DWORD pid) {
    return OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
}
}

uintptr_t get_base_address(DWORD pid) {
    return get_main_module(pid).base;
}

std::string get_name(DWORD pid) {
    HANDLE process = OpenReadHandle(pid);
    if (!process) return {};
    HMODULE module = nullptr;
    DWORD needed = 0;
    char name[MAX_PATH]{};
    if (EnumProcessModules(process, &module, sizeof(module), &needed) && module)
        GetModuleBaseNameA(process, module, name, static_cast<DWORD>(std::size(name)));
    CloseHandle(process);
    return name;
}

module_info get_main_module(DWORD pid) {
    module_info result{};
    HANDLE process = OpenReadHandle(pid);
    if (!process) return result;

    HMODULE module = nullptr;
    DWORD needed = 0;
    MODULEINFO info{};
    char name[MAX_PATH]{};
    if (EnumProcessModules(process, &module, sizeof(module), &needed) && module &&
        GetModuleInformation(process, module, &info, sizeof(info))) {
        GetModuleBaseNameA(process, module, name, static_cast<DWORD>(std::size(name)));
        result.base = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);
        result.size = static_cast<std::size_t>(info.SizeOfImage);
        result.name = name;
    }
    CloseHandle(process);
    return result;
}
}
