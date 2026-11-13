// ============================================================================
// OmniGhost local secret store (SEC-004) - DPAPI-backed persistence.
// ============================================================================

#include "platform/secret_store.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

namespace OmniGhost::Secrets {
namespace {

std::filesystem::path SecretDirectory()
{
    std::filesystem::path dir;
    wchar_t appData[MAX_PATH] = {};
    const DWORD len = ::GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        dir = std::filesystem::path(appData) / "OmniGhost" / "secrets";
    else
        dir = std::filesystem::path(".") / "OmniGhost" / "secrets";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

std::filesystem::path SecretPath(std::string_view name)
{
    // Sanitize: names are simple identifiers; strip anything path-like.
    std::string clean;
    clean.reserve(name.size());
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (ok)
            clean.push_back(c);
    }
    if (clean.empty())
        clean = "unnamed";
    return SecretDirectory() / (clean + ".dpapi");
}

std::vector<unsigned char> Seal(std::string_view plaintext)
{
    std::vector<unsigned char> sealed;
    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plaintext.data()));
    input.cbData = static_cast<DWORD>(plaintext.size());
    DATA_BLOB output{};
    if (!::CryptProtectData(&input, L"OmniGhost.Secret", nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &output))
        return sealed;
    sealed.assign(output.pbData, output.pbData + output.cbData);
    ::LocalFree(output.pbData);
    return sealed;
}

std::vector<unsigned char> Unseal(const std::vector<unsigned char>& sealed)
{
    std::vector<unsigned char> plain;
    if (sealed.empty())
        return plain;
    DATA_BLOB input{};
    input.pbData = const_cast<BYTE*>(sealed.data());
    input.cbData = static_cast<DWORD>(sealed.size());
    DATA_BLOB output{};
    if (!::CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                              CRYPTPROTECT_UI_FORBIDDEN, &output))
        return plain;
    plain.assign(output.pbData, output.pbData + output.cbData);
    ::LocalFree(output.pbData);
    return plain;
}

std::vector<unsigned char> ReadFileBytes(const std::filesystem::path& path)
{
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || file == nullptr)
        return {};
    std::vector<unsigned char> bytes;
    unsigned char chunk[4096];
    std::size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0)
        bytes.insert(bytes.end(), chunk, chunk + read);
    std::fclose(file);
    return bytes;
}

bool WriteFileBytes(const std::filesystem::path& path, const std::vector<unsigned char>& bytes)
{
    std::FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || file == nullptr)
        return false;
    const bool ok = bytes.empty() || (std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size());
    std::fclose(file);
    return ok;
}

} // namespace

bool Store(std::string_view name, std::string_view plaintext)
{
    if (name.empty())
        return false;
    const std::vector<unsigned char> sealed = Seal(plaintext);
    if (sealed.empty())
        return false;
    return WriteFileBytes(SecretPath(name), sealed);
}

ogs::SecretText Load(std::string_view name)
{
    const std::vector<unsigned char> bytes = ReadFileBytes(SecretPath(name));
    std::vector<unsigned char> plain = Unseal(bytes);
    if (plain.empty())
        return ogs::SecretText(std::string_view{});
    // Wrap through SecretText so the plaintext buffer is zeroized on release.
    const std::string text(plain.begin(), plain.end());
    ogs::ZeroRange(plain.data(), plain.size());
    return ogs::SecretText(std::string_view{text});
}

bool Exists(std::string_view name)
{
    std::error_code ec;
    return std::filesystem::exists(SecretPath(name), ec);
}

void Delete(std::string_view name)
{
    std::error_code ec;
    std::filesystem::remove(SecretPath(name), ec);
}

} // namespace OmniGhost::Secrets
