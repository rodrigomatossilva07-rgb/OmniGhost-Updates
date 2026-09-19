#include "cs2_config.h"
#include "../src/config/config_manager.h"

#include <string>

namespace CS2 {

bool SaveConfig(const char* name) {
    const std::string requested = (name && *name)
        ? std::string(name)
        : std::string("cs2_default");
    return config_manager::SaveToFile(requested);
}

bool LoadConfig(const char* name) {
    std::string requested = (name && *name)
        ? std::string(name)
        : std::string("cs2_default");
    if (requested.rfind("cs2_", 0) != 0)
        requested = "cs2_" + requested;
    return config_manager::LoadFromFile(requested);
}

} // namespace CS2
