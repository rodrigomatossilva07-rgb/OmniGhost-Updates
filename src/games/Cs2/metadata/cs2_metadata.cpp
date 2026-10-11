#include "cs2_metadata.h"

#include <Windows.h>

#include <cctype>
#include <cstdio>
#include <cstring>

namespace CS2::Metadata {
namespace {

void LowerCopy(const char* source, char* output, std::size_t output_size) noexcept {
    if (!output || output_size == 0)
        return;
    std::snprintf(output, output_size, "%s", source ? source : "");
    for (char* cursor = output; *cursor; ++cursor) {
        *cursor = static_cast<char>(
            std::tolower(static_cast<unsigned char>(*cursor)));
    }
}

} // namespace

bool IsBotPlayerName(const char* name) noexcept {
    return name &&
        (_strnicmp(name, "BOT ", 4) == 0 || _stricmp(name, "BOT") == 0);
}

bool IsDroppedWeaponClass(const char* class_name) noexcept {
    if (!class_name || !*class_name)
        return false;
    char lower[64]{};
    LowerCopy(class_name, lower, sizeof(lower));
    return std::strstr(lower, "weapon_") != nullptr ||
        (std::strstr(lower, "c_weapon") != nullptr &&
         std::strstr(lower, "grenade") == nullptr) ||
        std::strstr(lower, "c_ak47") != nullptr ||
        std::strstr(lower, "c_deagle") != nullptr ||
        std::strstr(lower, "c_awp") != nullptr ||
        std::strstr(lower, "c_m4a1") != nullptr ||
        std::strstr(lower, "c_glock") != nullptr ||
        std::strstr(lower, "c_usp") != nullptr ||
        std::strstr(lower, "c_p250") != nullptr ||
        std::strstr(lower, "c_mp9") != nullptr ||
        std::strstr(lower, "c_mac10") != nullptr ||
        std::strstr(lower, "c_famas") != nullptr ||
        std::strstr(lower, "c_galilar") != nullptr ||
        std::strstr(lower, "c_ssg08") != nullptr ||
        std::strstr(lower, "c_aug") != nullptr ||
        std::strstr(lower, "c_sg556") != nullptr;
}

bool DroppedWeaponAllowed(const char* class_name, const Config& settings) noexcept {
    char lower[64]{};
    LowerCopy(class_name, lower, sizeof(lower));
    const auto has = [&](const char* token) {
        return std::strstr(lower, token) != nullptr;
    };
    if (has("glock") || has("usp") || has("p2000") || has("p250") ||
        has("deagle") || has("revolver") || has("tec9") ||
        has("fiveseven") || has("cz75") || has("elite"))
        return settings.dropped_weapon_pistols;
    if (has("mac10") || has("mp9") || has("mp7") || has("mp5") ||
        has("ump") || has("p90") || has("bizon"))
        return settings.dropped_weapon_smgs;
    if (has("nova") || has("xm") || has("mag7") || has("sawed") ||
        has("m249") || has("negev"))
        return settings.dropped_weapon_heavy;
    return settings.dropped_weapon_rifles;
}

void FormatDroppedWeaponName(const char* source, char* output,
                             std::size_t output_size) noexcept {
    if (!output || output_size == 0)
        return;
    std::snprintf(output, output_size, "%s", source && *source ? source : "Arma");
    for (char* cursor = output; *cursor; ++cursor) {
        if (*cursor == '_')
            *cursor = ' ';
    }
    constexpr const char* prefixes[] = {"weapon ", "c weapon ", "c ", "item "};
    for (const char* prefix : prefixes) {
        const std::size_t length = std::strlen(prefix);
        if (_strnicmp(output, prefix, length) == 0) {
            std::memmove(output, output + length, std::strlen(output + length) + 1);
            break;
        }
    }
}

} // namespace CS2::Metadata
