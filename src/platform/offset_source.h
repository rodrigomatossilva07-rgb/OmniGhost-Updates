#pragma once

#include "embedded_offsets.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Unified offset source policy for all games:
//
//   SOURCE OF TRUTH (edit only these):
//     data/<game>_offsets.json   (and data/cs2_offsets.json / data/offsets.json for CS2)
//
//   BUILD:
//     tools/Build-EmbeddedOffsets.ps1 packs those JSON files into RCDATA.
//
//   RUNTIME:
//     Release  -> EmbeddedOffsets::Load only (no plaintext JSON beside the EXE)
//     Tester   -> OMNIGHOST_DEV_EXTERNAL_OFFSETS: try data/*.json first, then embedded
//
// Game adapters must call OffsetSource::LoadSnapshot + apply keys into their
// Offsets structs. Do not keep a second authoritative table of RVAs in .cpp/.h
// beyond compile-time seeds that are overwritten at load.

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
    std::string storage; // "EMBEDDED" | "EXTERNAL_JSON" | "FAIL"
    std::string detail;
    EmbeddedOffsets::Snapshot snapshot;
    EmbeddedOffsets::Diagnostics diagnostics;
};

[[nodiscard]] EmbeddedOffsets::Game ToEmbeddedGame(GameId id) noexcept;
[[nodiscard]] const char* GameSlug(GameId id) noexcept;
[[nodiscard]] const char* CanonicalJsonName(GameId id) noexcept;

// Candidate paths for Tester/dev external load (never used as silent Release fallback).
[[nodiscard]] std::vector<std::string> ExternalJsonCandidates(GameId id);

// Loads the snapshot for a game according to build policy.
[[nodiscard]] LoadResult LoadSnapshot(GameId id, const char* explicit_path = nullptr);

// Helper: first matching key wins.
[[nodiscard]] bool TryGetAny(const EmbeddedOffsets::Snapshot& snapshot,
                             std::initializer_list<const char*> keys,
                             std::uint64_t& value) noexcept;

// Log one standard block to logs (no full offset dump in Release).
void LogLoadResult(GameId id, const LoadResult& result);

} // namespace OmniGhost::OffsetSource
