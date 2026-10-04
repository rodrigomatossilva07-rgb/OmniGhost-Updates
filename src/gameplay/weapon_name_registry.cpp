#include "weapon_name_registry.h"
#include "../../platform/embedded_resources.h"
#include <fstream>
#include <sstream>
#include <iostream>

namespace OmniGhost::Gameplay {

bool WeaponNameRegistry::LoadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        std::cerr << "[WeaponRegistry] Failed to open: " << path << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Simple JSON parsing for our known format
    // {"weapons": [{ "hash": 0x..., "name": "...", "category": "...", "games": [...] }, ...]}
    size_t weaponsPos = content.find("\"weapons\"");
    if (weaponsPos == std::string::npos) {
        std::cerr << "[WeaponRegistry] No 'weapons' array found" << std::endl;
        return false;
    }

    size_t arrayStart = content.find('[', weaponsPos);
    size_t arrayEnd = content.rfind(']');
    if (arrayStart == std::string::npos || arrayEnd == std::string::npos || arrayEnd <= arrayStart) {
        std::cerr << "[WeaponRegistry] Invalid weapons array" << std::endl;
        return false;
    }

    std::string weaponsJson = content.substr(arrayStart, arrayEnd - arrayStart + 1);
    int count = 0;

    size_t pos = 0;
    while ((pos = weaponsJson.find("{", pos)) != std::string::npos) {
        size_t objEnd = weaponsJson.find("}", pos);
        if (objEnd == std::string::npos) break;
        std::string obj = weaponsJson.substr(pos, objEnd - pos + 1);

        uint32_t hash = 0;
        std::string name;
        std::string category;

        // Parse hash
        size_t hashPos = obj.find("\"hash\"");
        if (hashPos != std::string::npos) {
            size_t colon = obj.find(':', hashPos);
            if (colon != std::string::npos) {
                std::string hashStr = obj.substr(colon + 1);
                hashStr = hashStr.substr(0, hashStr.find_first_of(",}"));
                hashStr.erase(0, hashStr.find_first_not_of(" \t\n\r\""));
                try {
                    hash = static_cast<uint32_t>(std::stoul(hashStr, nullptr, 0));
                } catch (...) {}
            }
        }

        // Parse name
        size_t namePos = obj.find("\"name\"");
        if (namePos != std::string::npos) {
            size_t colon = obj.find(':', namePos);
            if (colon != std::string::npos) {
                size_t quote1 = obj.find('"', colon + 1);
                size_t quote2 = obj.find('"', quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    name = obj.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            }
        }

        // Parse category
        size_t catPos = obj.find("\"category\"");
        if (catPos != std::string::npos) {
            size_t colon = obj.find(':', catPos);
            if (colon != std::string::npos) {
                size_t quote1 = obj.find('"', colon + 1);
                size_t quote2 = obj.find('"', quote1 + 1);
                if (quote1 != std::string::npos && quote2 != std::string::npos) {
                    category = obj.substr(quote1 + 1, quote2 - quote1 - 1);
                }
            }
        }

        if (hash != 0 && !name.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            weapon_map_[hash] = {hash, name, category};
            ++count;
        }
        pos = objEnd + 1;
    }

    loaded_ = (count > 0);
    std::cout << "[WeaponRegistry] Loaded " << count << " weapons from " << path << std::endl;
    return loaded_;
}

bool WeaponNameRegistry::LoadFromEmbedded() {
    // EmbeddedOffsets contains schema offsets, not the weapons.json catalog.
    // The CS2-specific definition table remains the fallback when the JSON
    // catalog is unavailable; do not report a successful empty registry.
    return false;
}

const char* WeaponNameRegistry::GetWeaponName(uint32_t hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = weapon_map_.find(hash);
    return it != weapon_map_.end() ? it->second.name.c_str() : nullptr;
}

const WeaponInfo* WeaponNameRegistry::GetWeaponInfo(uint32_t hash) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = weapon_map_.find(hash);
    return it != weapon_map_.end() ? &it->second : nullptr;
}

} // namespace OmniGhost::Gameplay
