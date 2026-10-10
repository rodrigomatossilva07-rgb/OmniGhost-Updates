#pragma once

#include <string_view>

namespace CS2::WebRadar::Http {
inline bool EqualAsciiInsensitive(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        if (lower(static_cast<unsigned char>(a[i])) != lower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

inline std::string_view Trim(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}

inline bool TokenOk(std::string_view request, std::string_view expected) noexcept {
    if (expected.empty()) return false;
    const auto headerEnd = request.find("\r\n\r\n");
    auto pos = request.find("\r\n");
    if (headerEnd == std::string_view::npos || pos == std::string_view::npos) return false;
    std::string_view supplied;
    bool found = false;
    for (pos += 2; pos < headerEnd;) {
        const auto end = request.find("\r\n", pos);
        const auto line = request.substr(pos, end - pos);
        const auto colon = line.find(':');
        if (colon != std::string_view::npos && EqualAsciiInsensitive(line.substr(0, colon), "Authorization")) {
            if (found) return false;
            found = true;
            const auto value = Trim(line.substr(colon + 1));
            const auto space = value.find_first_of(" \t");
            if (space == std::string_view::npos || !EqualAsciiInsensitive(value.substr(0, space), "Bearer")) return false;
            supplied = Trim(value.substr(space + 1));
        }
        pos = end + 2;
    }
    if (!found || supplied.size() != expected.size()) return false;
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        difference |= static_cast<unsigned char>(supplied[i] ^ expected[i]);
    return difference == 0;
}
}
