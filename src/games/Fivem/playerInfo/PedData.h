#pragma once

#include <unordered_map>
#include <vector>
#include <chrono>
#include <cstdint>
#include <shared_mutex>
#include "math/math.h"


inline constexpr bool PedVisibilityFlagMeansVisible(uint8_t flag) noexcept {
    return flag != 0 && flag != 4 && flag != 36;
}


struct PedData {
    float health;
    Vec3 position_origin;
    Vec3 velocity{};
    uintptr_t playerInfo;
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point health_updated{};
    std::chrono::steady_clock::time_point velocity_updated{};
    bool isValid;
    bool visible = false;
    bool visibility_known = false;
    uint8_t visibility_flag = 0;
    std::chrono::steady_clock::time_point visibility_updated{};
    uint64_t entry_generation = 0;

    PedData() : health(0.0f), position_origin{}, playerInfo(0),
        lastUpdate(std::chrono::steady_clock::now()), isValid(false) {
    }
};




class PedCacheManager {
private:
    std::unordered_map<uintptr_t, PedData> pedCache;
    mutable std::shared_mutex mutex_;
    std::atomic<uint64_t> currentGeneration{1};


    std::chrono::steady_clock::time_point lastSlowUpdate;

    static constexpr std::chrono::milliseconds SLOW_CACHE_INTERVAL{ 50 };


    void slowCache();
    void cleanupUnlocked();

public:
    PedCacheManager();
    ~PedCacheManager();


    void initialize();


    void update();


    void fastCache(const std::vector<uintptr_t>& validPeds,
        const std::vector<Vec3>& positions);

    void fastCacheCore(const std::vector<uintptr_t>& validPeds,
        const std::vector<Vec3>& positions,
        const std::vector<Vec3>& velocities,
        const std::vector<float>& healths);


    void setCurrentGeneration(uint64_t generation) { currentGeneration.store(generation, std::memory_order_release); }

    void manualCache();


    bool getPedData(uintptr_t pedId, PedData& outData) const;
    std::vector<uintptr_t> getValidPedIds() const;
    size_t getCacheSize() const;


    void updatePedPosition(uintptr_t pedId, const Vec3& position);
    void updatePedHealth(uintptr_t pedId, float health);
    void updatePedVisibilities(const std::vector<uintptr_t>& peds,
        const std::vector<uint8_t>& flags, uint8_t unreadFlag,
        std::chrono::steady_clock::time_point sampledAt);
    void updatePedRaycastVisibilities(const std::vector<uintptr_t>& peds,
        const std::vector<bool>& visible, const std::vector<bool>& known,
        std::chrono::steady_clock::time_point sampledAt);
    void clearPedVisibilities();
    void removePed(uintptr_t pedId);
    void clearCache();


    void cleanup();
};


extern PedCacheManager g_pedCacheManager;
