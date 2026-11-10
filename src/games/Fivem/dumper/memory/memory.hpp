#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace memory {
std::vector<std::uint8_t> snapshot_module(HANDLE process_handle, uintptr_t base_address,
                                          std::size_t module_size);
std::vector<uintptr_t> pattern_scan_all(const std::vector<std::uint8_t>& image,
                                        uintptr_t base_address, const std::string& pattern,
                                        std::size_t maximum_results = 16);
std::vector<uintptr_t> pattern_scan_all(HANDLE process_handle, uintptr_t base_address,
                                        std::size_t module_size, const std::string& pattern,
                                        std::size_t maximum_results = 16);
uintptr_t get_rip_relative_address(HANDLE process_handle, uintptr_t instruction_address,
                                   std::size_t displacement_offset, std::size_t instruction_length);
uintptr_t dereference_pointer(HANDLE process_handle, uintptr_t address);
bool is_readable(HANDLE process_handle, uintptr_t address, std::size_t size = sizeof(uintptr_t));
}
