#include "file_integrity.h"
#include "scope_exit.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace OmniGhost::Platform {

bool Sha256File(const std::filesystem::path& file, std::string& hexDigest, std::string& error) {
    hexDigest.clear();
    error.clear();

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto cleanup = MakeScopeExit([&]() noexcept {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    });
    (void)cleanup;

    DWORD objectLength = 0;
    DWORD resultLength = 0;
    DWORD hashLength = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        error = "Não foi possível iniciar SHA-256.";
        return false;
    }
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) < 0 ||
        objectLength == 0 || hashLength == 0) {
        error = "Não foi possível obter propriedades SHA-256.";
        return false;
    }

    std::vector<unsigned char> object(objectLength);
    std::vector<unsigned char> digest(hashLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) {
        error = "Não foi possível criar SHA-256.";
        return false;
    }

    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        error = "Não foi possível abrir o ficheiro para SHA-256.";
        return false;
    }

    std::array<char, 64 * 1024> buffer{};
    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = stream.gcount();
        if (count > 0 && BCryptHashData(hash,
                reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) < 0) {
            error = "Falha ao calcular SHA-256.";
            return false;
        }
    }
    if (!stream.eof() || BCryptFinishHash(hash, digest.data(), hashLength, 0) < 0) {
        error = "Falha ao concluir SHA-256.";
        return false;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest)
        out << std::setw(2) << static_cast<int>(byte);
    hexDigest = out.str();
    return true;
}


bool Sha256Text(std::string_view text, std::string& hexDigest, std::string& error) {
    hexDigest.clear();
    error.clear();

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    auto cleanup = MakeScopeExit([&]() noexcept {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    });
    (void)cleanup;

    DWORD objectLength = 0;
    DWORD resultLength = 0;
    DWORD hashLength = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
        error = "Não foi possível iniciar SHA-256.";
        return false;
    }
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &resultLength, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
            reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &resultLength, 0) < 0 ||
        objectLength == 0 || hashLength == 0) {
        error = "Não foi possível obter propriedades SHA-256.";
        return false;
    }

    std::vector<unsigned char> object(objectLength);
    std::vector<unsigned char> digest(hashLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) {
        error = "Não foi possível criar SHA-256.";
        return false;
    }
    if (!text.empty() && BCryptHashData(hash,
            reinterpret_cast<PUCHAR>(const_cast<char*>(text.data())),
            static_cast<ULONG>(text.size()), 0) < 0) {
        error = "Falha ao calcular SHA-256.";
        return false;
    }
    if (BCryptFinishHash(hash, digest.data(), hashLength, 0) < 0) {
        error = "Falha ao concluir SHA-256.";
        return false;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (unsigned char byte : digest)
        out << std::setw(2) << static_cast<int>(byte);
    hexDigest = out.str();
    return true;
}

bool ConstantTimeEquals(std::string_view left, std::string_view right) noexcept {
    unsigned char difference = static_cast<unsigned char>(left.size() ^ right.size());
    const std::size_t size = (std::max)(left.size(), right.size());
    for (std::size_t i = 0; i < size; ++i) {
        const unsigned char a = i < left.size() ? static_cast<unsigned char>(left[i]) : 0;
        const unsigned char b = i < right.size() ? static_cast<unsigned char>(right[i]) : 0;
        difference |= static_cast<unsigned char>(a ^ b);
    }
    return difference == 0;
}

} // namespace OmniGhost::Platform
