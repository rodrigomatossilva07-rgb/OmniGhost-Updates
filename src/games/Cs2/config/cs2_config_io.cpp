#include "cs2_config.h"
#include "../src/config/config_manager.h"

#include <string>
#include <cctype>

namespace CS2 {

static bool IsValidConfigName(std::string_view name) {
    if (name.empty() || name.size() > 64) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') return false;
    }
    return true;
}

bool SaveConfig(const char* name) {
    std::string requested = (name && *name && IsValidConfigName(name))
        ? std::string(name)
        : std::string("cs2_default");
    const std::string normalized = requested.rfind("cs2_", 0) == 0
        ? requested : "cs2_" + requested;
    return config_manager::SaveToFile(normalized);
}

bool LoadConfig(const char* name) {
    std::string requested = (name && *name && IsValidConfigName(name))
        ? std::string(name)
        : std::string("cs2_default");
    if (requested.rfind("cs2_", 0) != 0)
        requested = "cs2_" + requested;
    return config_manager::LoadFromFile(requested);
}

}
