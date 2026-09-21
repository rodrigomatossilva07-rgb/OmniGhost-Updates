#include "embedded_resources.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <compressapi.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iostream>
#include <limits>
#include <mutex>
#include <unordered_set>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "cabinet.lib")

namespace OmniGhost {
namespace {

constexpr std::uint32_t kMagic = 0x5352474Fu; // "OGRS", little endian
constexpr std::uint16_t kFormatVersion = 1;
constexpr std::size_t kHeaderSize = 52;
// The PE resource API and our blob header carry uint32_t lengths.  This
// replaces the old artificial 64 MiB policy, which rejected valid CS2 maps.
constexpr std::size_t kMaxEmbeddedResourceSize = (std::numeric_limits<std::uint32_t>::max)();

std::uint16_t Read16(EmbeddedByteView bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes.data()[offset]) |
        static_cast<std::uint16_t>(bytes.data()[offset + 1]) << 8;
}
std::uint32_t Read32(EmbeddedByteView bytes, std::size_t offset) noexcept {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(bytes.data()[offset + i]) << (i * 8);
    return value;
}

bool Sha256(EmbeddedByteView bytes, std::array<std::uint8_t, 32>& digest) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD received = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &received, 0) >= 0;
    std::vector<std::uint8_t> object(ok ? objectLength : 0);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0;
    if (ok && !bytes.empty()) ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()),
        static_cast<ULONG>(bytes.size()), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}

bool EqualDigest(const std::array<std::uint8_t, 32>& actual, EmbeddedByteView expected) noexcept {
    if (expected.size() != actual.size()) return false;
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < actual.size(); ++i) difference |= actual[i] ^ expected.data()[i];
    return difference == 0;
}

bool DecompressPackBits(EmbeddedByteView input, std::size_t expected,
                        std::vector<std::uint8_t>& output) {
    output.clear();
    output.reserve(expected);
    for (std::size_t cursor = 0; cursor < input.size();) {
        const std::uint8_t control = input.data()[cursor++];
        if ((control & 0x80u) == 0) {
            const std::size_t length = static_cast<std::size_t>(control) + 1;
            if (length > input.size() - cursor || length > expected - output.size()) return false;
            output.insert(output.end(), input.data() + cursor, input.data() + cursor + length);
            cursor += length;
        } else {
            const std::size_t length = static_cast<std::size_t>(control & 0x7fu) + 3;
            if (cursor >= input.size() || length > expected - output.size()) return false;
            output.insert(output.end(), length, input.data()[cursor++]);
        }
    }
    return output.size() == expected;
}

bool DecompressLzms(EmbeddedByteView input, std::size_t expected,
                    std::vector<std::uint8_t>& output) {
    DECOMPRESSOR_HANDLE handle = nullptr;
    if (!CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &handle)) return false;
    output.assign(expected, 0);
    SIZE_T written = 0;
    const bool ok = Decompress(handle, input.data(), input.size(), output.data(), output.size(), &written) &&
        written == expected;
    CloseDecompressor(handle);
    if (!ok) output.clear();
    return ok;
}

bool EqualLogicalName(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i) {
        char a = left[i] == '\\' ? '/' : left[i];
        char b = right[i] == '\\' ? '/' : right[i];
        if (std::tolower(static_cast<unsigned char>(a)) !=
            std::tolower(static_cast<unsigned char>(b))) return false;
    }
    return true;
}

void LogSuccessfulLoadOnce(const EmbeddedResourceDescriptor& descriptor) {
    static std::mutex mutex;
    static std::unordered_set<std::uint16_t> loggedIds;
    const auto numericId = static_cast<std::uint16_t>(descriptor.id);
    std::lock_guard<std::mutex> lock(mutex);
    if (!loggedIds.insert(numericId).second) return;
    std::clog << "[RESOURCE] mode=EMBEDDED id=" << descriptor.logicalName
              << " found=YES header=PASS integrity=PASS decompress=PASS load=PASS\n";
}

} // namespace

std::optional<EmbeddedByteView> GetPeResourceView(int resourceId) noexcept {
    if (resourceId <= 0 || resourceId > 0xffff) return std::nullopt;
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!resource) return std::nullopt;
    const DWORD size = SizeofResource(nullptr, resource);
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto* bytes = loaded ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
    if (!bytes || size == 0) return std::nullopt;
    return EmbeddedByteView(bytes, size);
}

const EmbeddedResourceDescriptor* FindEmbeddedResource(EmbeddedResourceId id) noexcept {
    for (const auto& descriptor : kEmbeddedResourceCatalog)
        if (descriptor.id == id) return &descriptor;
    return nullptr;
}

const EmbeddedResourceDescriptor* FindEmbeddedResource(std::string_view logicalName) noexcept {
    for (const auto& descriptor : kEmbeddedResourceCatalog)
        if (EqualLogicalName(descriptor.logicalName, logicalName)) return &descriptor;
    return nullptr;
}

bool ParseEmbeddedResourceBlob(EmbeddedByteView blob, std::vector<std::uint8_t>& output,
                               EmbeddedResourceDiagnostics& diagnostics) {
    diagnostics = {};
    output.clear();
    diagnostics.resourceSize = blob.size();
    if (blob.size() < kHeaderSize) { diagnostics.error = "truncated header"; return false; }
    if (Read32(blob, 0) != kMagic) { diagnostics.error = "bad magic"; return false; }
    if (Read16(blob, 4) != kFormatVersion) { diagnostics.error = "unsupported format version"; return false; }
    if (Read16(blob, 6) != 0 || blob.data()[9] || blob.data()[10] || blob.data()[11]) {
        diagnostics.error = "unsupported resource type/reserved flags"; return false;
    }
    diagnostics.headerValid = true;
    const auto compression = static_cast<EmbeddedCompression>(blob.data()[8]);
    if (compression != EmbeddedCompression::None && compression != EmbeddedCompression::PackBits &&
        compression != EmbeddedCompression::Lzms) {
        diagnostics.error = "unsupported compression mode"; return false;
    }
    const std::size_t originalSize = Read32(blob, 12);
    const std::size_t storedSize = Read32(blob, 16);
    if (originalSize == 0 || storedSize == 0 || originalSize > kMaxEmbeddedResourceSize ||
        storedSize > kMaxEmbeddedResourceSize || storedSize > blob.size() - kHeaderSize ||
        kHeaderSize + storedSize != blob.size()) {
        diagnostics.error = "invalid declared size"; return false;
    }
    diagnostics.sizeValid = true;
    const EmbeddedByteView payload = blob.subview(kHeaderSize, storedSize);
    try {
        if (compression == EmbeddedCompression::None) {
            if (storedSize != originalSize) { diagnostics.error = "raw size mismatch"; return false; }
            output.assign(payload.begin(), payload.end());
        } else if (compression == EmbeddedCompression::PackBits && !DecompressPackBits(payload, originalSize, output)) {
            diagnostics.error = "corrupt compressed payload"; return false;
        } else if (compression == EmbeddedCompression::Lzms && !DecompressLzms(payload, originalSize, output)) {
            diagnostics.error = "corrupt LZMS payload"; return false;
        }
    } catch (const std::bad_alloc&) {
        diagnostics.error = "allocation failed"; return false;
    }
    diagnostics.decompressed = true;
    std::array<std::uint8_t, 32> digest{};
    if (!Sha256(EmbeddedByteView(output.data(), output.size()), digest) ||
        !EqualDigest(digest, blob.subview(20, 32))) {
        output.clear(); diagnostics.error = "sha256 mismatch"; return false;
    }
    diagnostics.integrityValid = true;
    return true;
}

std::optional<std::vector<std::uint8_t>> LoadEmbeddedResource(
    EmbeddedResourceId id, EmbeddedResourceDiagnostics* diagnostics) {
    EmbeddedResourceDiagnostics local;
    auto& diag = diagnostics ? *diagnostics : local;
    diag = {};
    const auto* descriptor = FindEmbeddedResource(id);
    if (!descriptor) { diag.error = "unknown resource id"; return std::nullopt; }
    const auto raw = GetPeResourceView(static_cast<int>(id));
    if (!raw) { diag.error = "resource not found"; return std::nullopt; }
    diag.found = true;
    std::vector<std::uint8_t> output;
    if (!ParseEmbeddedResourceBlob(*raw, output, diag)) return std::nullopt;
    if (output.size() != descriptor->originalSize) {
        output.clear(); diag.error = "catalog size mismatch"; return std::nullopt;
    }
    LogSuccessfulLoadOnce(*descriptor);
    return output;
}

std::optional<std::vector<std::uint8_t>> LoadEmbeddedResource(
    std::string_view logicalName, EmbeddedResourceDiagnostics* diagnostics) {
    const auto* descriptor = FindEmbeddedResource(logicalName);
    if (!descriptor) {
        if (diagnostics) { *diagnostics = {}; diagnostics->error = "unknown resource name"; }
        return std::nullopt;
    }
    return LoadEmbeddedResource(descriptor->id, diagnostics);
}

} // namespace OmniGhost
