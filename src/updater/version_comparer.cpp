#include "update_types.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <limits>

namespace OmniGhost::Update {
namespace {
bool Numeric(const std::string& value) {
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}
bool ParseNumber(const std::string& value, std::uint64_t& output) {
    if (!Numeric(value) || (value.size() > 1 && value.front() == '0')) return false;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), output);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size();
}
}

std::optional<SemVersion> SemVersion::Parse(std::string value) {
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) value.erase(value.begin());
    if (value.empty() || value.size() > 128) return std::nullopt;
    SemVersion result{};
    result.original = value;
    const auto plus = value.find('+');
    const std::string withoutBuild = value.substr(0, plus);
    const auto dash = withoutBuild.find('-');
    const std::string core = withoutBuild.substr(0, dash);
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= core.size()) {
        const auto end = core.find('.', start);
        parts.push_back(core.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    if (parts.size() != 3 || !ParseNumber(parts[0], result.major) ||
        !ParseNumber(parts[1], result.minor) || !ParseNumber(parts[2], result.patch)) return std::nullopt;
    if (dash != std::string::npos) {
        const std::string pre = withoutBuild.substr(dash + 1);
        if (pre.empty()) return std::nullopt;
        start = 0;
        while (start <= pre.size()) {
            const auto end = pre.find('.', start);
            std::string identifier = pre.substr(start, end == std::string::npos ? end : end - start);
            if (identifier.empty() || !std::all_of(identifier.begin(), identifier.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-';
            }) || (Numeric(identifier) && identifier.size() > 1 && identifier.front() == '0')) return std::nullopt;
            result.prerelease.push_back(std::move(identifier));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }
    return result;
}

int SemVersion::Compare(const SemVersion& other) const {
    if (major != other.major) return major < other.major ? -1 : 1;
    if (minor != other.minor) return minor < other.minor ? -1 : 1;
    if (patch != other.patch) return patch < other.patch ? -1 : 1;
    if (prerelease.empty() != other.prerelease.empty()) return prerelease.empty() ? 1 : -1;
    for (std::size_t i = 0; i < (std::min)(prerelease.size(), other.prerelease.size()); ++i) {
        if (prerelease[i] == other.prerelease[i]) continue;
        const bool leftNumeric = Numeric(prerelease[i]);
        const bool rightNumeric = Numeric(other.prerelease[i]);
        if (leftNumeric && rightNumeric) {
            std::uint64_t left{}, right{};
            ParseNumber(prerelease[i], left); ParseNumber(other.prerelease[i], right);
            return left < right ? -1 : 1;
        }
        if (leftNumeric != rightNumeric) return leftNumeric ? -1 : 1;
        return prerelease[i] < other.prerelease[i] ? -1 : 1;
    }
    if (prerelease.size() == other.prerelease.size()) return 0;
    return prerelease.size() < other.prerelease.size() ? -1 : 1;
}

const char* ChannelName(Channel channel) {
    switch (channel) { case Channel::Stable: return "stable"; case Channel::Beta: return "beta"; default: return "development"; }
}
std::optional<Channel> ParseChannel(const std::string& value) {
    if (value == "stable") return Channel::Stable;
    if (value == "beta") return Channel::Beta;
    if (value == "development") return Channel::Development;
    return std::nullopt;
}
}
