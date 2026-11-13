#include "memory.hpp"

#include <sstream>
#include <vector>

namespace {
struct WildcardByte {
    bool wildcard = true;
    std::uint8_t value = 0;
    WildcardByte() = default;
    explicit WildcardByte(std::uint8_t byte) : wildcard(false), value(byte) {}
};

std::vector<WildcardByte> BuildSignature(const std::string& input) {
    std::vector<WildcardByte> signature;
    std::istringstream parser(input);
    std::string part;
    while (parser >> part) {
        if (part == "?" || part == "??") signature.emplace_back();
        else signature.emplace_back(static_cast<std::uint8_t>(std::stoul(part, nullptr, 16)));
    }
    return signature;
}

void FindInRegion(HANDLE process, uintptr_t region_start, std::size_t region_size,
                  const std::vector<WildcardByte>& signature,
                  std::vector<uintptr_t>& results, std::size_t maximum_results) {
    if (signature.empty() || region_size < signature.size()) return;
    std::vector<std::uint8_t> data(region_size);
    SIZE_T read_count = 0;
    if (!ReadProcessMemory(process, reinterpret_cast<LPCVOID>(region_start), data.data(),
                           region_size, &read_count) || read_count < signature.size()) return;

    const std::size_t maximum = read_count - signature.size();
    for (std::size_t position = 0; position <= maximum; ++position) {
        bool matched = true;
        for (std::size_t index = 0; index < signature.size(); ++index) {
            if (!signature[index].wildcard && signature[index].value != data[position + index]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            results.push_back(region_start + position);
            if (results.size() >= maximum_results) return;
        }
    }
}
}

namespace memory {
std::vector<std::uint8_t> snapshot_module(HANDLE process_handle, uintptr_t base_address,
                                          std::size_t module_size) {
    std::vector<std::uint8_t> image(module_size, 0);
    if (!process_handle || !base_address || !module_size) return {};

    MEMORY_BASIC_INFORMATION info{};
    uintptr_t cursor = base_address;
    const uintptr_t module_end = base_address + module_size;
    while (cursor < module_end &&
           VirtualQueryEx(process_handle, reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info))) {
        const uintptr_t start = (std::max)(cursor, reinterpret_cast<uintptr_t>(info.BaseAddress));
        const uintptr_t end = (std::min)(module_end,
            reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize);
        const DWORD inaccessible = PAGE_NOACCESS | PAGE_GUARD;
        if (end > start && info.State == MEM_COMMIT && !(info.Protect & inaccessible)) {
            SIZE_T read = 0;
            ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(start),
                image.data() + (start - base_address), end - start, &read);
        }
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) break;
        cursor = next;
    }
    return image;
}

std::vector<uintptr_t> pattern_scan_all(const std::vector<std::uint8_t>& image,
                                        uintptr_t base_address, const std::string& pattern,
                                        std::size_t maximum_results) {
    std::vector<uintptr_t> results;
    const auto signature = BuildSignature(pattern);
    if (image.empty() || signature.empty() || image.size() < signature.size() || !maximum_results)
        return results;
    const std::size_t maximum = image.size() - signature.size();
    for (std::size_t position = 0; position <= maximum; ++position) {
        bool matched = true;
        for (std::size_t index = 0; index < signature.size(); ++index) {
            if (!signature[index].wildcard && signature[index].value != image[position + index]) {
                matched = false;
                break;
            }
        }
        if (matched) {
            results.push_back(base_address + position);
            if (results.size() >= maximum_results) break;
        }
    }
    return results;
}

std::vector<uintptr_t> pattern_scan_all(HANDLE process_handle, uintptr_t base_address,
                                        std::size_t module_size, const std::string& pattern,
                                        std::size_t maximum_results) {
    std::vector<uintptr_t> results;
    if (!process_handle || !base_address || !module_size || pattern.empty() || !maximum_results)
        return results;
    const auto signature = BuildSignature(pattern);
    if (signature.empty()) return results;

    MEMORY_BASIC_INFORMATION info{};
    uintptr_t cursor = base_address;
    const uintptr_t module_end = base_address + module_size;
    while (cursor < module_end &&
           VirtualQueryEx(process_handle, reinterpret_cast<LPCVOID>(cursor), &info, sizeof(info))) {
        const DWORD inaccessible = PAGE_NOACCESS | PAGE_GUARD;
        if (info.State == MEM_COMMIT && !(info.Protect & inaccessible) && info.RegionSize > 0) {
            const uintptr_t start = (std::max)(cursor, reinterpret_cast<uintptr_t>(info.BaseAddress));
            const uintptr_t end = (std::min)(module_end,
                reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize);
            if (end > start)
                FindInRegion(process_handle, start, static_cast<std::size_t>(end - start),
                             signature, results, maximum_results);
            if (results.size() >= maximum_results) break;
        }
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) break;
        cursor = next;
    }
    return results;
}

uintptr_t get_rip_relative_address(HANDLE process_handle, uintptr_t instruction_address,
                                   std::size_t displacement_offset, std::size_t instruction_length) {
    if (!process_handle || !instruction_address) return 0;
    std::int32_t displacement = 0;
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(process_handle,
                           reinterpret_cast<LPCVOID>(instruction_address + displacement_offset),
                           &displacement, sizeof(displacement), &bytes_read) ||
        bytes_read != sizeof(displacement)) return 0;
    return instruction_address + instruction_length + displacement;
}

uintptr_t dereference_pointer(HANDLE process_handle, uintptr_t address) {
    if (!process_handle || !address) return 0;
    uintptr_t target = 0;
    SIZE_T bytes_read = 0;
    if (!ReadProcessMemory(process_handle, reinterpret_cast<LPCVOID>(address),
                           &target, sizeof(target), &bytes_read) ||
        bytes_read != sizeof(target)) return 0;
    return target;
}

bool is_readable(HANDLE process_handle, uintptr_t address, std::size_t size) {
    if (!process_handle || !address || !size) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQueryEx(process_handle, reinterpret_cast<LPCVOID>(address), &info, sizeof(info)))
        return false;
    const DWORD inaccessible = PAGE_NOACCESS | PAGE_GUARD;
    const uintptr_t end = address + size;
    const uintptr_t region_end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    return info.State == MEM_COMMIT && !(info.Protect & inaccessible) && end >= address && end <= region_end;
}
}
