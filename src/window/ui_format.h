#pragma once

#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace UiFormat {

inline std::string Version(std::string_view raw) {
    while (!raw.empty() && (raw.front() == 'v' || raw.front() == 'V' || raw.front() == ' '))
        raw.remove_prefix(1);
    if (raw.empty())
        return "v—";
    return std::string("v") + std::string(raw);
}

inline std::string Date(std::string_view iso) {
    // Release metadata is ISO-8601. Product UI always renders a compact,
    // locale-independent DD/MM/YYYY form so the same date never changes style
    // from page to page.
    if (iso.size() >= 10 && iso[4] == '-' && iso[7] == '-') {
        const bool digits =
            iso[0] >= '0' && iso[0] <= '9' && iso[1] >= '0' && iso[1] <= '9' &&
            iso[2] >= '0' && iso[2] <= '9' && iso[3] >= '0' && iso[3] <= '9' &&
            iso[5] >= '0' && iso[5] <= '9' && iso[6] >= '0' && iso[6] <= '9' &&
            iso[8] >= '0' && iso[8] <= '9' && iso[9] >= '0' && iso[9] <= '9';
        if (digits) {
            char out[16]{};
            std::snprintf(out, sizeof(out), "%c%c/%c%c/%c%c%c%c",
                          iso[8], iso[9], iso[5], iso[6],
                          iso[0], iso[1], iso[2], iso[3]);
            return out;
        }
    }
    return iso.empty() ? "—" : std::string(iso);
}

inline std::string DateTime(std::string_view iso) {
    if (iso.size() >= 16 && iso[4] == '-' && iso[7] == '-' && (iso[10] == 'T' || iso[10] == ' ') && iso[13] == ':') {
        const std::string date = Date(iso.substr(0, 10));
        if (date != "—") {
            std::string out = date + " · " + std::string(iso.substr(11, 5));
            if (!iso.empty() && (iso.back() == 'Z' || iso.find("UTC") != std::string_view::npos))
                out += " UTC";
            return out;
        }
    }
    return Date(iso);
}

inline std::string DateTimeLocal(std::time_t timestamp) {
    if (timestamp <= 0)
        return "—";
    std::tm local{};
#if defined(_WIN32)
    if (localtime_s(&local, &timestamp) != 0)
        return "—";
#else
    if (localtime_r(&timestamp, &local) == nullptr)
        return "—";
#endif
    char out[32]{};
    if (std::strftime(out, sizeof(out), "%d/%m/%Y · %H:%M", &local) == 0)
        return "—";
    return out;
}

} // namespace UiFormat
