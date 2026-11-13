#pragma once

#include "embedded_offsets.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

















namespace OmniGhost::OffsetSource {

enum class GameId : std::uint16_t {
    Fortnite = 1,
    Warzone = 2,
    CS2 = 3,
    Rust = 4,
    FiveM = 5,
    Apex = 6,
};

struct LoadResult {
    bool ok = false;
    bool from_external_json = false;
    std::string storage;
    std::string detail;
    EmbeddedOffsets::Snapshot snapshot;
    EmbeddedOffsets::Diagnostics diagnostics;
};

[[nodiscard]] EmbeddedOffsets::Game ToEmbeddedGame(GameId id) noexcept;
[[nodiscard]] const char* GameSlug(GameId id) noexcept;
[[nodiscard]] const char* CanonicalJsonName(GameId id) noexcept;


[[nodiscard]] std::vector<std::string> ExternalJsonCandidates(GameId id);


[[nodiscard]] LoadResult LoadSnapshot(GameId id, const char* explicit_path = nullptr);


[[nodiscard]] bool TryGetAny(const EmbeddedOffsets::Snapshot& snapshot,
                             std::initializer_list<const char*> keys,
                             std::uint64_t& value) noexcept;


void LogLoadResult(GameId id, const LoadResult& result);

}
