// peddata.cpp
#include "peddata.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../game/game.h"
#include "globals.h"
#include "../esp/esp.h"
#include "../aimbot/aimbot.h"
#include <iostream>
#include <unordered_set>

// Global cache manager instance
PedCacheManager g_pedCacheManager;

PedCacheManager::PedCacheManager()
    : lastSlowUpdate(std::chrono::steady_clock::now()) {
    pedCache.reserve(256);
}

PedCacheManager::~PedCacheManager() {
    // No thread to stop
}

void PedCacheManager::initialize() {
    lastSlowUpdate = std::chrono::steady_clock::now();
}

void PedCacheManager::update() {
    auto now = std::chrono::steady_clock::now();

    // Check if it's time for slow cache update
    if (now - lastSlowUpdate >= SLOW_CACHE_INTERVAL) {
        slowCache();
        lastSlowUpdate = now;
    }
}

void PedCacheManager::slowCache() {
    // Light pass: health + playerInfo (cheap scatter, runs at SLOW_CACHE_INTERVAL)
    static std::vector<uintptr_t> pedIds;
    static std::vector<float> healthValues;
    static std::vector<uintptr_t> playerInfoValues;
    pedIds.clear();
    pedIds.reserve(pedCache.size());
    for (auto& [pedId, pedData] : pedCache) {
        if (pedData.isValid)
            pedIds.push_back(pedId);
    }

    if (!pedIds.empty()) {
        healthValues.resize(pedIds.size());
        playerInfoValues.resize(pedIds.size());

        auto handle = mem.CreateScatterHandle();
        for (size_t i = 0; i < pedIds.size(); ++i) {
            mem.AddScatterReadRequest(handle, pedIds[i] + FiveM::offset::playerHealth,
                &healthValues[i], sizeof(float));
            mem.AddScatterReadRequest(handle, pedIds[i] + FiveM::offset::playerInfo,
                &playerInfoValues[i], sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(handle);
        mem.CloseScatterHandle(handle);

        for (size_t i = 0; i < pedIds.size(); ++i) {
            auto it = pedCache.find(pedIds[i]);
            if (it != pedCache.end()) {
                it->second.health = healthValues[i];
                it->second.playerInfo = playerInfoValues[i];
            }
        }

    }

    cleanup();
}

void PedCacheManager::fastCache(const std::vector<uintptr_t>& validPeds,
    const std::vector<Vec3>& positions) {
    auto now = std::chrono::steady_clock::now();

    // O(1) membership for this frame (avoids O(n*m) scan below)
    static std::unordered_set<uintptr_t> present;
    present.clear();
    present.reserve(validPeds.size() * 2 + 16);

    static std::vector<uintptr_t> newcomers;
    newcomers.clear();
    newcomers.reserve(8);

    for (size_t i = 0; i < validPeds.size() && i < positions.size(); ++i) {
        uintptr_t pedId = validPeds[i];
        present.insert(pedId);
        auto it = pedCache.find(pedId);
        if (it != pedCache.end()) {
            it->second.position_origin = positions[i];
            it->second.lastUpdate = now;
            it->second.isValid = true;
        } else {
            PedData pd{};
            pd.position_origin = positions[i];
            pd.lastUpdate = now;
            pd.isValid = true;
            pedCache.emplace(pedId, pd);
            newcomers.push_back(pedId);
        }
    }

    if (!newcomers.empty()) {
        static std::vector<float> hp;
        static std::vector<uintptr_t> info;
        hp.resize(newcomers.size());
        info.resize(newcomers.size());
        auto handle = mem.CreateScatterHandle();
        for (size_t i = 0; i < newcomers.size(); ++i) {
            mem.AddScatterReadRequest(handle, newcomers[i] + FiveM::offset::playerHealth,
                &hp[i], sizeof(float));
            mem.AddScatterReadRequest(handle, newcomers[i] + FiveM::offset::playerInfo,
                &info[i], sizeof(uintptr_t));
        }
        mem.ExecuteReadScatter(handle);
        mem.CloseScatterHandle(handle);
        for (size_t i = 0; i < newcomers.size(); ++i) {
            auto it = pedCache.find(newcomers[i]);
            if (it != pedCache.end()) {
                it->second.health = hp[i];
                it->second.playerInfo = info[i];
            }
        }
    }

    // Invalidate stale entries without nested loops
    for (auto& [pedId, pedData] : pedCache) {
        if (present.find(pedId) != present.end())
            continue;
        if (now - pedData.lastUpdate > std::chrono::seconds(5))
            pedData.isValid = false;
    }
}

void PedCacheManager::manualCache() {
    // Full reinitialization - rare operation
    // Clear all cache data
    pedCache.clear();

    // Reset timing
    lastSlowUpdate = std::chrono::steady_clock::now();

}

bool PedCacheManager::getPedData(uintptr_t pedId, PedData& outData) const {
    auto it = pedCache.find(pedId);
    if (it != pedCache.end() && it->second.isValid) {
        outData = it->second;
        return true;
    }
    return false;
}

std::vector<uintptr_t> PedCacheManager::getValidPedIds() const {
    std::vector<uintptr_t> validIds;
    validIds.reserve(pedCache.size());

    for (const auto& [pedId, pedData] : pedCache) {
        if (pedData.isValid) {
            validIds.push_back(pedId);
        }
    }

    return validIds;
}

size_t PedCacheManager::getCacheSize() const {
    return pedCache.size();
}

void PedCacheManager::updatePedPosition(uintptr_t pedId, const Vec3& position) {
    auto& pedData = pedCache[pedId];
    pedData.position_origin = position;
    pedData.lastUpdate = std::chrono::steady_clock::now();
    pedData.isValid = true;
}

void PedCacheManager::updatePedHealth(uintptr_t pedId, float health) {
    auto it = pedCache.find(pedId);
    if (it != pedCache.end()) {
        it->second.health = health;
        it->second.lastUpdate = std::chrono::steady_clock::now();
    }
}

void PedCacheManager::removePed(uintptr_t pedId) {
    pedCache.erase(pedId);
}

void PedCacheManager::clearCache() {
    pedCache.clear();
}

void PedCacheManager::cleanup() {
    // Remove invalid or old entries
    auto now = std::chrono::steady_clock::now();

    for (auto it = pedCache.begin(); it != pedCache.end(); ) {
        if (!it->second.isValid ||
            (now - it->second.lastUpdate) > std::chrono::seconds(15)) {
            it = pedCache.erase(it);
        }
        else {
            ++it;
        }
    }
}
