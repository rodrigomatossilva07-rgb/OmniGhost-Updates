#include "object_explorer.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <vector>

namespace FiveM::ObjectExplorer {

Config config;

bool IsFavorite(std::uint32_t hash) noexcept {
    return hash != 0 && config.favorites.contains(hash);
}

bool IsVisible(std::uint32_t hash) noexcept {
    return hash != 0 && !config.hiddenModels.contains(hash) &&
        !config.ignoredModels.contains(hash);
}

bool IsIgnored(std::uint32_t hash) noexcept {
    return hash != 0 && config.ignoredModels.contains(hash);
}

bool MatchesSearch(const std::string& modelName, std::uint32_t hash,
    const std::string& query) {
    if (query.empty()) return true;
    auto lower = [](std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    };
    const std::string needle = lower(query);
    if (lower(modelName).find(needle) != std::string::npos) return true;
    std::ostringstream formatted;
    formatted << "0x" << std::uppercase << std::hex << std::setw(8)
        << std::setfill('0') << hash;
    return lower(formatted.str()).find(needle) != std::string::npos;
}

void SetFavorite(std::uint32_t hash, bool enabled) {
    if (!hash) return;
    if (enabled) config.favorites.insert(hash);
    else config.favorites.erase(hash);
}

void SetVisible(std::uint32_t hash, bool enabled) {
    if (!hash) return;
    if (enabled) config.hiddenModels.erase(hash);
    else config.hiddenModels.insert(hash);
}

void SetIgnored(std::uint32_t hash, bool enabled) {
    if (!hash) return;
    if (enabled) config.ignoredModels.insert(hash);
    else config.ignoredModels.erase(hash);
}

void Reset() {
    config = Config{};
}

std::string SerializeHashSet(const std::unordered_set<std::uint32_t>& values) {
    std::vector<std::uint32_t> sorted(values.begin(), values.end());
    std::sort(sorted.begin(), sorted.end());
    std::ostringstream out;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (i) out << ',';
        out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << sorted[i];
    }
    return out.str();
}

bool DeserializeHashSet(const std::string& text,
    std::unordered_set<std::uint32_t>& destination) {
    constexpr std::size_t kMaximumEntries = 4096;
    std::unordered_set<std::uint32_t> parsed;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(',', start);
        std::string_view token(text.data() + start,
            (end == std::string::npos ? text.size() : end) - start);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.remove_suffix(1);
        if (!token.empty()) {
            if (token.size() > 2 && token[0] == '0' && (token[1] == 'x' || token[1] == 'X'))
                token.remove_prefix(2);
            std::uint32_t value = 0;
            const auto result = std::from_chars(token.data(), token.data() + token.size(), value, 16);
            if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || !value)
                return false;
            parsed.insert(value);
            if (parsed.size() > kMaximumEntries) return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    destination = std::move(parsed);
    return true;
}

}
