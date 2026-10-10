#include "hardware_fingerprint.h"

#include "../platform/file_integrity.h"

#include <Windows.h>
#include <intrin.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace OmniGhost::Auth {
namespace {

using Component = std::pair<std::string, std::string>;

constexpr DWORD FirmwareProvider(char a, char b, char c, char d) noexcept {
    return static_cast<DWORD>(static_cast<unsigned char>(a)) |
           (static_cast<DWORD>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<DWORD>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<DWORD>(static_cast<unsigned char>(d)) << 24);
}

void AddBytes(std::vector<Component>& components, std::string tag,
              const void* data, std::size_t size) {
    if (data && size != 0)
        components.emplace_back(std::move(tag), std::string(static_cast<const char*>(data), size));
}

std::string NormalizedAscii(const char* value, std::size_t capacity) {
    if (!value || capacity == 0)
        return {};
    const auto* end = static_cast<const char*>(std::memchr(value, '\0', capacity));
    std::string result(value, end ? static_cast<std::size_t>(end - value) : capacity);
    const auto first = std::find_if_not(result.begin(), result.end(), [](unsigned char c) {
        return std::isspace(c) != 0;
    });
    const auto last = std::find_if_not(result.rbegin(), result.rend(), [](unsigned char c) {
        return std::isspace(c) != 0;
    }).base();
    if (first >= last)
        return {};
    result = std::string(first, last);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return result;
}

void AddFirmware(std::vector<Component>& components) {
    constexpr DWORD kRawSmbios = FirmwareProvider('R', 'S', 'M', 'B');
    const UINT size = GetSystemFirmwareTable(kRawSmbios, 0, nullptr, 0);
    if (size < 8 || size > 4u * 1024u * 1024u)
        return;
    std::vector<std::byte> bytes(size);
    if (GetSystemFirmwareTable(kRawSmbios, 0, bytes.data(), size) != size)
        return;

    // RawSMBIOSData starts with four version bytes followed by a DWORD table
    // length. Hash only hardware records: boot-status and firmware bookkeeping
    // can change without the machine changing.
    DWORD tableLength = 0;
    std::memcpy(&tableLength, bytes.data() + 4, sizeof(tableLength));
    if (tableLength > bytes.size() - 8)
        return;
    const std::byte* cursor = bytes.data() + 8;
    const std::byte* tableEnd = cursor + tableLength;
    while (cursor + 4 <= tableEnd) {
        const auto type = static_cast<unsigned char>(cursor[0]);
        const auto formattedLength = static_cast<unsigned char>(cursor[1]);
        if (formattedLength < 4 || cursor + formattedLength > tableEnd)
            break;
        const std::byte* recordEnd = cursor + formattedLength;
        while (recordEnd + 1 < tableEnd &&
               (recordEnd[0] != std::byte{} || recordEnd[1] != std::byte{}))
            ++recordEnd;
        if (recordEnd + 1 >= tableEnd)
            break;
        recordEnd += 2;
        if (type == 1 || type == 2 || type == 3 || type == 4 || type == 17)
            AddBytes(components, "smbios-" + std::to_string(type), cursor,
                     static_cast<std::size_t>(recordEnd - cursor));
        cursor = recordEnd;
        if (type == 127)
            break;
    }
}

void AddCpu(std::vector<Component>& components) {
    std::array<int, 16> words{};
    __cpuidex(words.data(), 0, 0);
    __cpuidex(words.data() + 4, 1, 0);
    __cpuidex(words.data() + 8, 7, 0);
    __cpuidex(words.data() + 12, static_cast<int>(0x80000001u), 0);
    AddBytes(components, "cpu", words.data(), sizeof(words));
}

void AddDisplayAdapters(std::vector<Component>& components) {
    for (DWORD index = 0;; ++index) {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, index, &adapter, EDD_GET_DEVICE_INTERFACE_NAME))
            break;
        if ((adapter.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
            continue;
        std::wstring identity(adapter.DeviceID);
        if (identity.empty())
            identity.assign(adapter.DeviceKey);
        if (!identity.empty())
            AddBytes(components, "gpu", identity.data(), identity.size() * sizeof(wchar_t));
    }
}

void AppendStorageField(std::string& identity, const std::vector<std::byte>& descriptor,
                        DWORD offset) {
    if (offset == 0 || offset >= descriptor.size())
        return;
    const char* value = reinterpret_cast<const char*>(descriptor.data() + offset);
    const std::string normalized = NormalizedAscii(value, descriptor.size() - offset);
    if (!normalized.empty()) {
        identity.push_back('|');
        identity.append(normalized);
    }
}

void AddStorageDevices(std::vector<Component>& components) {
    STORAGE_PROPERTY_QUERY query{StorageDeviceProperty, PropertyStandardQuery, {0}};
    for (unsigned index = 0; index < 32; ++index) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(index);
        const HANDLE drive = CreateFileW(path.c_str(), 0,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         nullptr, OPEN_EXISTING, 0, nullptr);
        if (drive == INVALID_HANDLE_VALUE)
            continue;

        STORAGE_DESCRIPTOR_HEADER header{};
        DWORD returned = 0;
        const BOOL headerOk = DeviceIoControl(
            drive, IOCTL_STORAGE_QUERY_PROPERTY,
            &query, sizeof(query),
            &header, sizeof(header), &returned, nullptr);
        if (!headerOk || header.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR) || header.Size > 64u * 1024u) {
            CloseHandle(drive);
            continue;
        }

        std::vector<std::byte> bytes(header.Size);
        const BOOL descriptorOk = DeviceIoControl(
            drive, IOCTL_STORAGE_QUERY_PROPERTY,
            &query, sizeof(query),
            bytes.data(), static_cast<DWORD>(bytes.size()), &returned, nullptr);
        CloseHandle(drive);
        if (!descriptorOk || returned < sizeof(STORAGE_DEVICE_DESCRIPTOR))
            continue;

        bytes.resize((std::min)(bytes.size(), static_cast<std::size_t>(returned)));
        const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(bytes.data());
        std::string identity = std::to_string(static_cast<unsigned>(descriptor->BusType));
        AppendStorageField(identity, bytes, descriptor->VendorIdOffset);
        AppendStorageField(identity, bytes, descriptor->ProductIdOffset);
        AppendStorageField(identity, bytes, descriptor->ProductRevisionOffset);
        AppendStorageField(identity, bytes, descriptor->SerialNumberOffset);
        if (identity.find('|') != std::string::npos)
            components.emplace_back("storage", std::move(identity));
    }
}

void AddSystemVolume(std::vector<Component>& components) {
    std::array<wchar_t, MAX_PATH> windowsPath{};
    std::array<wchar_t, MAX_PATH> volumeRoot{};
    if (GetWindowsDirectoryW(windowsPath.data(), static_cast<UINT>(windowsPath.size())) == 0 ||
        !GetVolumePathNameW(windowsPath.data(), volumeRoot.data(), static_cast<DWORD>(volumeRoot.size())))
        return;
    DWORD serial = 0;
    if (GetVolumeInformationW(volumeRoot.data(), nullptr, 0, &serial, nullptr, nullptr, nullptr, 0))
        AddBytes(components, "system-volume", &serial, sizeof(serial));
}

void AddMachineGuid(std::vector<Component>& components) {
    DWORD bytes = 0;
    constexpr const wchar_t* kPath = L"SOFTWARE\\Microsoft\\Cryptography";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kPath, L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, nullptr, &bytes) != ERROR_SUCCESS ||
        bytes <= sizeof(wchar_t) || bytes > 4096)
        return;
    std::vector<wchar_t> value(bytes / sizeof(wchar_t));
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kPath, L"MachineGuid",
                     RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, value.data(), &bytes) == ERROR_SUCCESS &&
        bytes >= sizeof(wchar_t))
        AddBytes(components, "machine-guid", value.data(), bytes - sizeof(wchar_t));
}

void AddUserSid(std::vector<Component>& components) {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return;
    DWORD bytes = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
    std::vector<std::byte> buffer(bytes);
    if (bytes != 0 && GetTokenInformation(token, TokenUser, buffer.data(), bytes, &bytes)) {
        const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
        if (IsValidSid(user->User.Sid))
            AddBytes(components, "user-sid", user->User.Sid, GetLengthSid(user->User.Sid));
    }
    CloseHandle(token);
}

void AppendLength(std::string& output, std::uint64_t length) {
    for (unsigned shift = 0; shift < 64; shift += 8)
        output.push_back(static_cast<char>((length >> shift) & 0xffu));
}

std::string Digest(std::vector<Component> components) {
    if (components.empty())
        return {};
    std::sort(components.begin(), components.end());

    std::string material = "OmniGhost hardware identity v2";
    for (const auto& [tag, value] : components) {
        AppendLength(material, tag.size());
        material.append(tag);
        AppendLength(material, value.size());
        material.append(value);
    }

    std::string digest;
    std::string error;
    if (!Platform::Sha256Text(material, digest, error) || digest.size() != 64)
        return {};
    return "oghw2:" + digest;
}

} // namespace

std::string ComputeHardwareFingerprint() noexcept {
    try {
        std::vector<Component> components;
        components.reserve(16);
        AddFirmware(components); // motherboard, BIOS, processor and memory devices
        AddCpu(components);
        AddDisplayAdapters(components);
        AddStorageDevices(components);
        AddSystemVolume(components);
        AddMachineGuid(components);
        AddUserSid(components);
        return Digest(std::move(components));
    } catch (...) {
        return {};
    }
}

} // namespace OmniGhost::Auth
