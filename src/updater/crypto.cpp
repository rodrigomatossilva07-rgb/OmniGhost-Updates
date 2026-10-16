#include "crypto.h"
#include "../platform/file_integrity.h"
#include <Windows.h>
#include <bcrypt.h>
#include <vector>
#pragma comment(lib, "bcrypt.lib")

namespace OmniGhost::Update {

bool Sha256File(const std::filesystem::path& file, std::string& hexDigest, std::string& error) {
    return OmniGhost::Platform::Sha256File(file, hexDigest, error);
}

bool ConstantTimeEquals(const std::string& left, const std::string& right) {
    return OmniGhost::Platform::ConstantTimeEquals(left, right);
}

bool RandomTokenHex(std::size_t byteCount, std::wstring& token, std::string& error) {
    if (byteCount == 0 || byteCount > 1024) {
        error = "Tamanho de token aleatório inválido.";
        return false;
    }

    std::vector<unsigned char> bytes(byteCount);
    const NTSTATUS status = BCryptGenRandom(
        nullptr,
        bytes.data(),
        static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        error = "Não foi possível obter aleatoriedade criptográfica do Windows.";
        return false;
    }

    static constexpr wchar_t hex[] = L"0123456789abcdef";
    token.clear();
    token.reserve(bytes.size() * 2);
    for (const unsigned char value : bytes) {
        token.push_back(hex[(value >> 4) & 0x0f]);
        token.push_back(hex[value & 0x0f]);
    }
    return true;
}

}
