#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace FiveM::ObjectExplorer {

struct Config {
    std::unordered_set<std::uint32_t> favorites;
    std::unordered_set<std::uint32_t> hiddenModels;
    std::unordered_set<std::uint32_t> ignoredModels;
};

extern Config config;

[[nodiscard]] bool IsFavorite(std::uint32_t hash) noexcept;
[[nodiscard]] bool IsVisible(std::uint32_t hash) noexcept;
[[nodiscard]] bool IsIgnored(std::uint32_t hash) noexcept;
[[nodiscard]] bool MatchesSearch(const std::string& modelName, std::uint32_t hash,
    const std::string& query);
void SetFavorite(std::uint32_t hash, bool enabled);
void SetVisible(std::uint32_t hash, bool enabled);
void SetIgnored(std::uint32_t hash, bool enabled);
void Reset();

[[nodiscard]] std::string SerializeHashSet(const std::unordered_set<std::uint32_t>& values);
bool DeserializeHashSet(const std::string& text,
    std::unordered_set<std::uint32_t>& destination);

}
