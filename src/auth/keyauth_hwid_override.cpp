#include "project/third_party/keyauth/x64/lib/utils.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <sddl.h>
#pragma comment(lib, "advapi32.lib")

#include <iomanip>
#include <sstream>
#include <string>
#include "platform/strprotect/strprotect.h"

#pragma comment(lib, "bcrypt.lib")

namespace {



std::string NarrowGuid(const std::wstring& text) {
    std::string out;
    out.reserve(text.size());
    for (const wchar_t c : text)
        out.push_back(static_cast<char>((c >= 0 && c < 0x80) ? c : '?'));
    return out;
}

bool ReadMachineGuid(std::wstring& out) {
    wchar_t buffer[256]{};
    DWORD size = sizeof(buffer);
    const LSTATUS status = RegGetValueW(HKEY_LOCAL_MACHINE,
        OGS_WCSTR("SOFTWARE\\Microsoft\\Cryptography"), OGS_WCSTR("MachineGuid"),
        RRF_RT_REG_SZ, nullptr, buffer, &size);
    if (status != ERROR_SUCCESS)
        return false;
    out.assign(buffer, size / sizeof(wchar_t));
    while (!out.empty() &&
           (out.back() == L'\0' || out.back() == L'\n' || out.back() == L'\r'))
        out.pop_back();
    return !out.empty();
}

bool Sha256Hex(const std::string& input, std::string& outHex) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status != 0 || !algorithm)
        return false;

    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hashBytes = 0;
    DWORD hashBytesResult = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
        reinterpret_cast<PUCHAR>(&hashBytes), sizeof(hashBytes),
        &hashBytesResult, 0);
    if (status == 0)
        status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (status != 0 || !hash) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return false;
    }

    std::string digest(static_cast<std::size_t>(hashBytes), '\0');
    status = BCryptHashData(hash,
        reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())),
        static_cast<ULONG>(input.size()), 0);
    if (status == 0)
        status = BCryptFinishHash(hash,
            reinterpret_cast<PUCHAR>(digest.data()),
            static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status != 0)
        return false;

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (const unsigned char byte : digest)
        hex << std::setw(2) << static_cast<int>(byte);
    outHex = hex.str();
    return true;
}

}









std::string utils::get_hwid() {
    std::string sidText;


    HANDLE hToken = nullptr;
    std::string sidTextLocal;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_READ | TOKEN_QUERY, &hToken)) {
        DWORD dwSize = 0;
        GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize);
        if (dwSize > 0) {
            std::vector<BYTE> tokenInfo(dwSize);
            if (GetTokenInformation(hToken, TokenUser, tokenInfo.data(), dwSize, &dwSize)) {
                PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(tokenInfo.data());
                LPWSTR sidString = nullptr;
                if (ConvertSidToStringSidW(pTokenUser->User.Sid, &sidString)) {
                    sidTextLocal = NarrowGuid(std::wstring(sidString));
                    LocalFree(sidString);
                }
            }
        }
        CloseHandle(hToken);
    }

    std::wstring machineGuid;
    const bool haveGuid = ReadMachineGuid(machineGuid);
    const bool haveSid = !sidTextLocal.empty();

    if (!haveSid && !haveGuid)
        return {};

    std::ostringstream mixer;
    mixer << sidTextLocal << '|';
    if (haveGuid)
        mixer << NarrowGuid(machineGuid);
    std::string hex;
    if (!Sha256Hex(mixer.str(), hex))
        return haveSid ? sidTextLocal : NarrowGuid(machineGuid);
    return hex;
}
