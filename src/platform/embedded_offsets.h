#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include "memory_span.h"

namespace OmniGhost::EmbeddedOffsets {


using ByteView = OmniGhost::ByteSpan;
using MutableByteView = OmniGhost::MutableByteSpan;


[[nodiscard]] inline ByteView MakeByteView(const std::vector<std::uint8_t>& data) noexcept {
    return OmniGhost::AsBytes(data);
}

[[nodiscard]] inline ByteView MakeByteView(const std::uint8_t* data, std::size_t size) noexcept {
    return OmniGhost::AsBytes(data, size);
}

template <std::size_t N>
[[nodiscard]] inline ByteView MakeByteView(const std::uint8_t (&data)[N]) noexcept {
    return OmniGhost::AsSpan(data);
}

enum class Game : std::uint16_t {
    Fortnite = 1,
    Warzone = 2,
    CS2 = 3,
    Rust = 4,
    FiveM = 5,
    Apex = 6,
};

struct Metadata {
    std::uint32_t schemaVersion{};
    std::string game;
    std::string build;
    std::string cl;
    std::string module;
    std::string source;
    std::string generatedAt;
    std::string note;
};

struct Entry {
    std::uint32_t keyHash{};
    std::uint64_t value{};
};

struct Snapshot {
    Metadata metadata;
    std::vector<Entry> entries;

    [[nodiscard]] bool TryGet(std::string_view path, std::uint64_t& value) const noexcept;
};

struct Diagnostics {
    bool resourceFound{};
    bool headerValid{};
    bool sizeValid{};
    bool decompressed{};
    bool integrityValid{};
    bool parsed{};
    std::size_t resourceSize{};
    std::size_t compressedSize{};
    std::size_t uncompressedSize{};
    std::string error;
};

[[nodiscard]] std::uint32_t HashOffsetPath(std::string_view path) noexcept;
[[nodiscard]] const char* GameName(Game game) noexcept;
[[nodiscard]] int ResourceIdForGame(Game game) noexcept;

[[nodiscard]] bool MetadataMatches(const Snapshot& snapshot,
                                   std::string_view expectedGame,
                                   std::string_view expectedBuild,
                                   std::string_view expectedCl,
                                   std::string_view expectedModule) noexcept;



[[nodiscard]] bool ParseEmbeddedOffsetBlob(ByteView blob,
                                           Game expectedGame,
                                           Snapshot& snapshot,
                                           Diagnostics& diagnostics);


[[nodiscard]] bool Load(Game game, Snapshot& snapshot, Diagnostics& diagnostics);

}
