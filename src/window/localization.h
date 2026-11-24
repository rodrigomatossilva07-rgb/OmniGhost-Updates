#pragma once

#include <cstdint>
#include <string_view>

namespace Loc {

enum class Language : uint8_t {
    Portuguese = 0,
    English    = 1,
    German     = 2,
    Spanish    = 3,
    French     = 4,
    Italian    = 5,
    Russian    = 6,
    Count      = 7
};

inline constexpr const char* LanguageCode(Language lang) noexcept {
    switch (lang) {
        case Language::Portuguese: return "pt";
        case Language::English:    return "en";
        case Language::German:     return "de";
        case Language::Spanish:    return "es";
        case Language::French:     return "fr";
        case Language::Italian:    return "it";
        case Language::Russian:    return "ru";
        default:                   return "en";
    }
}

inline constexpr const char* LanguageName(Language lang) noexcept {
    switch (lang) {
        case Language::Portuguese: return "Português";
        case Language::English:    return "English";
        case Language::German:     return "Deutsch";
        case Language::Spanish:    return "Español";
        case Language::French:     return "Français";
        case Language::Italian:    return "Italiano";
        case Language::Russian:    return "Русский";
        default:                   return "English";
    }
}

inline constexpr int LanguageCount() noexcept {
    return static_cast<int>(Language::Count);
}

const char* Tr(std::string_view key) noexcept;
const char* TrID(std::string_view key);

} // namespace Loc
