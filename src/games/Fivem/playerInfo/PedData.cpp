// peddata.cpp
#include "peddata.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../game/game.h"
#include "globals.h"
#include "../esp/esp.h"
#include "../aimbot/aimbot.h"
#include <iostream>
#include <unordered_set>
#include <shared_mutex>
#include <cmath>
#include <limits>

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
    std::unique_lock lock(mutex_);
    lastSlowUpdate = std::chrono::steady_clock::now();
}

void PedCacheManager::update() {
    auto now = std::chrono::steady_clock::now();
    bool runSlow = false;
    {
        std::unique_lock lock(mutex_);
        if (now - lastSlowUpdate >= SLOW_CACHE_INTERVAL) {
            // Reserve this interval while protected.  The DMA work itself is
            // performed by slowCache, which owns the same cache lock.
            lastSlowUpdate = now;
            runSlow = true;
        }
    }
    if (runSlow)
        slowCache();
}

void PedCacheManager::slowCache() {
    // Core acquisition already samples health with positions. Only request a
    // health scatter for peds whose core sample is missing or older than 250 ms.
    static std::vector<uintptr_t> pedIds;
    static std::vector<float> healthValues;
    {
        std::shared_lock lock(mutex_);
        pedIds.clear();
        pedIds.reserve(pedCache.size());
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [pedId, pedData] : pedCache) {
            if (!pedData.isValid) continue;
            if (pedData.health_updated.time_since_epoch().count() != 0 &&
                now - pedData.health_updated < std::chrono::milliseconds(250))
                continue; // core sample is fresh — no duplicate DMA
            pedIds.push_back(pedId);
        }
    }

    if (!pedIds.empty()) {
        healthValues.assign(pedIds.size(), std::numeric_limits<float>::quiet_NaN());

        auto handle = mem.CreateScatterHandle();
        if (!handle) return;
        for (size_t i = 0; i < pedIds.size(); ++i) {
            mem.AddScatterReadRequest(handle, pedIds[i] + FiveM::offset::playerHealth,
                &healthValues[i], sizeof(float));
        }
        const auto readBegin = std::chrono::steady_clock::now();
        mem.ExecuteReadScatter(handle);
        const auto readEnd = std::chrono::steady_clock::now();
        mem.CloseScatterHandle(handle);

        // DMA completed without holding the cache mutex.  Publishing fields is
        // intentionally short so render/aim never waits behind an I/O stall.
        std::unique_lock lock(mutex_);
        const auto sampledAt = readBegin + (readEnd - readBegin) / 2;
        for (size_t i = 0; i < pedIds.size(); ++i) {
            auto it = pedCache.find(pedIds[i]);
            if (it != pedCache.end() && std::isfinite(healthValues[i]) &&
                healthValues[i] >= 0.f && healthValues[i] < 1000.f) {
                it->second.health = healthValues[i];
                it->second.health_updated = sampledAt;
            }
        }
        cleanupUnlocked();
        return;
    }
    std::unique_lock lock(mutex_);
    cleanupUnlocked();
}

void PedCacheManager::fastCache(const std::vector<uintptr_t>& validPeds,
    const std::vector<Vec3>& positions) {
    // Legacy entry point: positions only. Health/velocity come exclusively from
    // the core acquisition path via fastCacheCore — no duplicate DMA here.
    static const std::vector<Vec3> kEmptyVel;
    static const std::vector<float> kEmptyHp;
    fastCacheCore(validPeds, positions, kEmptyVel, kEmptyHp);
}

void PedCacheManager::fastCacheCore(const std::vector<uintptr_t>& validPeds,
    const std::vector<Vec3>& positions,
    const std::vector<Vec3>& velocities,
    const std::vector<float>& healths) {
    auto now = std::chrono::steady_clock::now();
    static std::unordered_set<uintptr_t> present;
    present.clear();
    present.reserve(validPeds.size() * 2 + 16);
    const uint64_t gen = currentGeneration.load(std::memory_order_acquire);
    std::unique_lock lock(mutex_);
    for (size_t i = 0; i < validPeds.size() && i < positions.size(); ++i) {
        const uintptr_t pedId = validPeds[i];
        present.insert(pedId);
        auto it = pedCache.find(pedId);
        if (it == pedCache.end()) {
            PedData pd{};
            pd.position_origin = positions[i];
            pd.lastUpdate = now;
            pd.isValid = true;
            pd.entry_generation = gen;
            if (i < velocities.size()) {
                pd.velocity = velocities[i];
                pd.velocity_updated = now;
            }
            if (i < healths.size() && std::isfinite(healths[i]) &&
                healths[i] >= 0.f && healths[i] < 1000.f) {
                pd.health = healths[i];
                pd.health_updated = now;
            }
            pedCache.emplace(pedId, std::move(pd));
        } else {
            // P0-2: Pointer reuse check — if generation mismatches, the ped pointer
            // was recycled and this is a different entity now.
            if (it->second.entry_generation != gen) {
                it->second = PedData{};
                it->second.entry_generation = gen;
            }
            it->second.position_origin = positions[i];
            it->second.lastUpdate = now;
            it->second.isValid = true;
            if (i < velocities.size()) {
                it->second.velocity = velocities[i];
                it->second.velocity_updated = now;
            }
            if (i < healths.size() && std::isfinite(healths[i]) &&
                healths[i] >= 0.f && healths[i] < 1000.f) {
                it->second.health = healths[i];
                it->second.health_updated = now;
            }
        }
    }
    // Invalidate peds not in this core sample
    for (auto it = pedCache.begin(); it != pedCache.end();) {
        if (!present.count(it->first)) {
            it->second.isValid = false;
            ++it;
        } else {
            ++it;
        }
    }
}


void PedCacheManager::manualCache() {
    std::unique_lock lock(mutex_);
    // Full reinitialization - rare operation
    // Clear all cache data
    pedCache.clear();

    // Reset timing
    lastSlowUpdate = std::chrono::steady_clock::now();

}

bool PedCacheManager::getPedData(uintptr_t pedId, PedData& outData) const {
    std::shared_lock lock(mutex_);
    const auto it = pedCache.find(pedId);
    const uint64_t gen = currentGeneration.load(std::memory_order_acquire);
    if (it != pedCache.end() && it->second.isValid &&
        it->second.entry_generation == gen) {
        outData = it->second;
        return true;
    }
    return false;
}

std::vector<uintptr_t> PedCacheManager::getValidPedIds() const {
    std::shared_lock lock(mutex_);
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
    std::shared_lock lock(mutex_);
    return pedCache.size();
}

void PedCacheManager::updatePedPosition(uintptr_t pedId, const Vec3& position) {
    std::unique_lock lock(mutex_);
    auto& pedData = pedCache[pedId];
    pedData.position_origin = position;
    pedData.lastUpdate = std::chrono::steady_clock::now();
    pedData.isValid = true;
}

void PedCacheManager::updatePedHealth(uintptr_t pedId, float health) {
    std::unique_lock lock(mutex_);
    auto it = pedCache.find(pedId);
    if (it != pedCache.end()) {
        it->second.health = health;
        it->second.health_updated = std::chrono::steady_clock::now();
    }
}

void PedCacheManager::updatePedVisibilities(const std::vector<uintptr_t>& peds,
    const std::vector<uint8_t>& flags, uint8_t unreadFlag,
    std::chrono::steady_clock::time_point sampledAt) {
    std::unique_lock lock(mutex_);
    for (size_t i = 0; i < peds.size() && i < flags.size(); ++i) {
        const auto it = pedCache.find(peds[i]);
        if (it == pedCache.end() || !it->second.isValid) continue;
        auto& data = it->second;
        data.visibility_flag = flags[i];
        data.visibility_known = flags[i] != unreadFlag;
        data.visible = data.visibility_known && PedVisibilityFlagMeansVisible(flags[i]);
        data.visibility_updated = sampledAt;
    }
}

void PedCacheManager::clearPedVisibilities() {
    std::unique_lock lock(mutex_);
    for (auto& [ped, data] : pedCache) {
        data.visible = false;
        data.visibility_known = false;
    }
}

void PedCacheManager::removePed(uintptr_t pedId) {
    std::unique_lock lock(mutex_);
    pedCache.erase(pedId);
}

void PedCacheManager::clearCache() {
    std::unique_lock lock(mutex_);
    pedCache.clear();
}

void PedCacheManager::cleanup() {
    std::unique_lock lock(mutex_);
    cleanupUnlocked();
}

void PedCacheManager::cleanupUnlocked() {
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
