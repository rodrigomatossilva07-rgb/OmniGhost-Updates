#include "embedded_offsets.h"
#include "embedded_resources.h"
#include "resource.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

#pragma comment(lib, "bcrypt.lib")

namespace OmniGhost::EmbeddedOffsets {
namespace {
constexpr std::uint32_t kContainerMagic = 0x464F474Fu; // "OGOF" little-endian
constexpr std::uint32_t kPayloadMagic = 0x504E534Fu;   // "OSNP" little-endian
constexpr std::uint16_t kContainerVersion = 1;
constexpr std::uint16_t kPayloadVersion = 1;
constexpr std::uint8_t kCompressionPackBits = 1;
constexpr std::size_t kHeaderBytes = 52;
constexpr std::size_t kMaximumCompressedBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaximumUncompressedBytes = 8u * 1024u * 1024u;
constexpr std::size_t kMaximumMetadataStringBytes = 16u * 1024u;
constexpr std::size_t kMaximumEntries = 8192;

bool ReadU16(ByteView data, std::size_t& cursor, std::uint16_t& value) noexcept {
    if (cursor > data.size() || data.size() - cursor < 2) return false;
    value = static_cast<std::uint16_t>(data[cursor]) |
            (static_cast<std::uint16_t>(data[cursor + 1]) << 8);
    cursor += 2;
    return true;
}

bool ReadU32(ByteView data, std::size_t& cursor, std::uint32_t& value) noexcept {
    if (cursor > data.size() || data.size() - cursor < 4) return false;
    value = static_cast<std::uint32_t>(data[cursor]) |
            (static_cast<std::uint32_t>(data[cursor + 1]) << 8) |
            (static_cast<std::uint32_t>(data[cursor + 2]) << 16) |
            (static_cast<std::uint32_t>(data[cursor + 3]) << 24);
    cursor += 4;
    return true;
}

bool ReadU64(ByteView data, std::size_t& cursor, std::uint64_t& value) noexcept {
    if (cursor > data.size() || data.size() - cursor < 8) return false;
    value = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
        value |= static_cast<std::uint64_t>(data[cursor++]) << shift;
    return true;
}

bool ReadString(ByteView data, std::size_t& cursor, std::string& value) {
    std::uint16_t length = 0;
    if (!ReadU16(data, cursor, length)) return false;
    if (length > kMaximumMetadataStringBytes || cursor > data.size() || data.size() - cursor < length)
        return false;
    value.assign(reinterpret_cast<const char*>(data.data() + cursor), length);
    cursor += length;
    return true;
}

bool DecompressPackBits(ByteView input,
                        std::size_t expectedSize,
                        std::vector<std::uint8_t>& output) {
    output.clear();
    if (expectedSize > kMaximumUncompressedBytes) return false;
    output.reserve(expectedSize);
    std::size_t cursor = 0;
    while (cursor < input.size()) {
        const std::uint8_t token = input[cursor++];
        const std::size_t length = static_cast<std::size_t>(token & 0x7Fu) + 1u;
        if (token & 0x80u) {
            if (cursor >= input.size() || output.size() > expectedSize || expectedSize - output.size() < length)
                return false;
            const std::uint8_t byte = input[cursor++];
            output.insert(output.end(), length, byte);
        } else {
            if (cursor > input.size() || input.size() - cursor < length ||
                output.size() > expectedSize || expectedSize - output.size() < length)
                return false;
            output.insert(output.end(), input.begin() + static_cast<std::ptrdiff_t>(cursor),
                          input.begin() + static_cast<std::ptrdiff_t>(cursor + length));
            cursor += length;
        }
    }
    return output.size() == expectedSize;
}

bool Sha256(ByteView input, std::array<std::uint8_t, 32>& digest) noexcept {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<std::uint8_t> object;
    bool ok = false;
    do {
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) break;
        DWORD objectLength = 0;
        DWORD hashLength = 0;
        DWORD result = 0;
        if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &result, 0) < 0) break;
        if (BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength), &result, 0) < 0 ||
            hashLength != digest.size()) break;
        object.resize(objectLength);
        if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) break;
        if (!input.empty() && BCryptHashData(hash,
                const_cast<PUCHAR>(reinterpret_cast<const UCHAR*>(input.data())),
                static_cast<ULONG>(input.size()), 0) < 0) break;
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) break;
        ok = true;
    } while (false);
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!object.empty()) SecureZeroMemory(object.data(), object.size());
    return ok;
}

bool DigestEqual(const std::array<std::uint8_t, 32>& left,
                 ByteView right) noexcept {
    if (right.size() != left.size()) return false;
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<std::uint8_t>(left[i] ^ right[i]);
    return difference == 0;
}

bool ParsePayload(ByteView payload, Game expectedGame, Snapshot& snapshot) {
    std::size_t cursor = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t reserved = 0;
    if (!ReadU32(payload, cursor, magic) || magic != kPayloadMagic ||
        !ReadU16(payload, cursor, version) || version != kPayloadVersion ||
        !ReadU16(payload, cursor, reserved) || reserved != 0 ||
        !ReadU32(payload, cursor, snapshot.metadata.schemaVersion))
        return false;

    if (!ReadString(payload, cursor, snapshot.metadata.game) ||
        !ReadString(payload, cursor, snapshot.metadata.build) ||
        !ReadString(payload, cursor, snapshot.metadata.cl) ||
        !ReadString(payload, cursor, snapshot.metadata.module) ||
        !ReadString(payload, cursor, snapshot.metadata.source) ||
        !ReadString(payload, cursor, snapshot.metadata.generatedAt) ||
        !ReadString(payload, cursor, snapshot.metadata.note))
        return false;

    if (snapshot.metadata.schemaVersion == 0 || snapshot.metadata.game.empty() ||
        snapshot.metadata.build.empty() || snapshot.metadata.module.empty() || snapshot.metadata.source.empty())
        return false;
    const char* expectedSlug = nullptr;
    switch (expectedGame) {
    case Game::Fortnite: expectedSlug = "fortnite"; break;
    case Game::Warzone: expectedSlug = "warzone"; break;
    case Game::CS2: expectedSlug = "cs2"; break;
    case Game::FiveM: expectedSlug = "fivem"; break;
    case Game::Apex: expectedSlug = "apex"; break;
    default: return false;
    }
    if (snapshot.metadata.game != expectedSlug)
        return false;

    std::uint32_t count = 0;
    if (!ReadU32(payload, cursor, count) || count == 0 || count > kMaximumEntries)
        return false;

    snapshot.entries.clear();
    snapshot.entries.reserve(count);
    std::uint32_t previousHash = 0;
    bool havePrevious = false;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t hash = 0;
        std::uint32_t entryReserved = 0;
        std::uint64_t value = 0;
        if (!ReadU32(payload, cursor, hash) || !ReadU32(payload, cursor, entryReserved) ||
            entryReserved != 0 || !ReadU64(payload, cursor, value))
            return false;
        if (havePrevious && hash <= previousHash) return false;
        previousHash = hash;
        havePrevious = true;
        snapshot.entries.push_back({hash, value});
    }
    return cursor == payload.size();
}

} // namespace

std::uint32_t HashOffsetPath(std::string_view path) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : path) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

const char* GameName(Game game) noexcept {
    switch (game) {
    case Game::Fortnite: return "Fortnite";
    case Game::Warzone: return "Warzone";
    case Game::CS2: return "CS2";
    case Game::FiveM: return "FiveM";
    case Game::Apex: return "Apex";
    default: return "Unknown";
    }
}

int ResourceIdForGame(Game game) noexcept {
    switch (game) {
    case Game::Fortnite: return IDR_OFFSETS_FORTNITE;
    case Game::Warzone: return IDR_OFFSETS_WARZONE;
    case Game::CS2: return IDR_OFFSETS_CS2;
    case Game::FiveM: return IDR_OFFSETS_FIVEM;
    case Game::Apex: return IDR_OFFSETS_APEX;
    default: return 0;
    }
}

bool MetadataMatches(const Snapshot& snapshot,
                     std::string_view expectedGame,
                     std::string_view expectedBuild,
                     std::string_view expectedCl,
                     std::string_view expectedModule) noexcept {
    const auto& meta = snapshot.metadata;
    if (meta.game != expectedGame || meta.build != expectedBuild || meta.module != expectedModule)
        return false;
    if (!expectedCl.empty() && meta.cl != expectedCl)
        return false;
    return true;
}

bool Snapshot::TryGet(std::string_view path, std::uint64_t& value) const noexcept {
    const std::uint32_t hash = HashOffsetPath(path);
    const auto it = std::lower_bound(entries.begin(), entries.end(), hash,
        [](const Entry& entry, std::uint32_t target) { return entry.keyHash < target; });
    if (it == entries.end() || it->keyHash != hash) return false;
    value = it->value;
    return true;
}

bool ParseEmbeddedOffsetBlob(ByteView blob,
                             Game expectedGame,
                             Snapshot& snapshot,
                             Diagnostics& diagnostics) {
    diagnostics = {};
    snapshot = {};
    diagnostics.resourceFound = true;
    diagnostics.resourceSize = blob.size();
    if (blob.size() < kHeaderBytes) {
        diagnostics.error = "truncated header";
        return false;
    }

    std::size_t cursor = 0;
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t gameId = 0;
    if (!ReadU32(blob, cursor, magic) || magic != kContainerMagic ||
        !ReadU16(blob, cursor, version) || version != kContainerVersion ||
        !ReadU16(blob, cursor, gameId) || gameId != static_cast<std::uint16_t>(expectedGame)) {
        diagnostics.error = "invalid header/game/version";
        return false;
    }
    if (cursor >= blob.size()) {
        diagnostics.error = "truncated compression field";
        return false;
    }
    const std::uint8_t compression = blob[cursor++];
    if (cursor > blob.size() || blob.size() - cursor < 3) {
        diagnostics.error = "truncated reserved header";
        return false;
    }
    if (blob[cursor] != 0 || blob[cursor + 1] != 0 || blob[cursor + 2] != 0) {
        diagnostics.error = "nonzero reserved header";
        return false;
    }
    cursor += 3;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    if (!ReadU32(blob, cursor, compressedSize) || !ReadU32(blob, cursor, uncompressedSize)) {
        diagnostics.error = "truncated sizes";
        return false;
    }
    diagnostics.headerValid = true;
    diagnostics.compressedSize = compressedSize;
    diagnostics.uncompressedSize = uncompressedSize;
    if (compression != kCompressionPackBits || compressedSize == 0 || uncompressedSize == 0 ||
        compressedSize > kMaximumCompressedBytes || uncompressedSize > kMaximumUncompressedBytes ||
        blob.size() != kHeaderBytes + compressedSize) {
        diagnostics.error = "invalid declared sizes/compression";
        return false;
    }
    diagnostics.sizeValid = true;
    const auto expectedDigest = blob.subview(cursor, 32);
    cursor += 32;
    const auto compressed = blob.subview(cursor, compressedSize);

    std::vector<std::uint8_t> payload;
    if (!DecompressPackBits(compressed, uncompressedSize, payload)) {
        diagnostics.error = "decompression failed";
        return false;
    }
    diagnostics.decompressed = true;

    std::array<std::uint8_t, 32> actualDigest{};
    if (!Sha256(payload, actualDigest) || !DigestEqual(actualDigest, expectedDigest)) {
        if (!payload.empty()) SecureZeroMemory(payload.data(), payload.size());
        diagnostics.error = "sha256 mismatch";
        return false;
    }
    diagnostics.integrityValid = true;

    if (!ParsePayload(payload, expectedGame, snapshot)) {
        if (!payload.empty()) SecureZeroMemory(payload.data(), payload.size());
        diagnostics.error = "invalid snapshot payload";
        return false;
    }
    diagnostics.parsed = true;
    if (!payload.empty()) SecureZeroMemory(payload.data(), payload.size());
    return true;
}

bool Load(Game game, Snapshot& snapshot, Diagnostics& diagnostics) {
    diagnostics = {};
    snapshot = {};
    const int resourceId = ResourceIdForGame(game);
    if (resourceId == 0) {
        diagnostics.error = "unknown game/resource id";
        return false;
    }
    const auto resource = OmniGhost::GetPeResourceView(resourceId);
    if (!resource) {
        diagnostics.error = "resource not found";
        return false;
    }
    diagnostics.resourceFound = true;
    return ParseEmbeddedOffsetBlob(ByteView(resource->data(), resource->size()), game, snapshot, diagnostics);
}

} // namespace OmniGhost::EmbeddedOffsets
