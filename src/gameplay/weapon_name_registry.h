#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace OmniGhost::Gameplay {

struct WeaponInfo {
    uint32_t hash = 0;
    std::string name;
    std::string category;
};

class WeaponNameRegistry {
public:
    static WeaponNameRegistry& Instance() {
        static WeaponNameRegistry instance;
        return instance;
    }

    bool LoadFromJson(const std::string& path);
    bool LoadFromEmbedded();

    const char* GetWeaponName(uint32_t hash) const;
    const WeaponInfo* GetWeaponInfo(uint32_t hash) const;

    bool IsLoaded() const { return loaded_; }

private:
    WeaponNameRegistry() = default;
    std::unordered_map<uint32_t, WeaponInfo> weapon_map_;
    mutable std::mutex mutex_;
    bool loaded_ = false;
};

}