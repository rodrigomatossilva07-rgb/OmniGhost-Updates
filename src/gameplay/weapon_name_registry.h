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

    // Devolve o nome por VALOR (cópia sob lock): o retorno por ponteiro
    // expunha c_str() de uma entry que LoadFromJson pode substituir.
    // Domínio: hashes joaat (FiveM). O CS2 usa def-index e tem a sua
    // própria tabela em cs2_weapons.h — nunca misturar os domínios.
    std::string GetWeaponName(uint32_t hash) const;

    bool IsLoaded() const { return loaded_; }

private:
    WeaponNameRegistry() = default;
    std::unordered_map<uint32_t, WeaponInfo> weapon_map_;
    mutable std::mutex mutex_;
    bool loaded_ = false;
};

}