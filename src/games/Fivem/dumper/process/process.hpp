#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace process {
struct module_info {
    uintptr_t base = 0;
    std::size_t size = 0;
    std::string name;
};

uintptr_t get_base_address(DWORD pid);
std::string get_name(DWORD pid);
module_info get_main_module(DWORD pid);
}
