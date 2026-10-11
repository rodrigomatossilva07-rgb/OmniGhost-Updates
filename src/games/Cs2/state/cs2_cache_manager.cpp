#include "cs2_cache_manager.h"

namespace CS2::CacheManager {
namespace {
Sizes g_sizes;
}

std::atomic<uint64_t> session_generation{1};

Sizes& Metrics() noexcept {
    return g_sizes;
}

uint64_t CurrentGeneration() noexcept {
    return session_generation.load(std::memory_order_acquire);
}

uint64_t AdvanceGeneration() noexcept {
    return session_generation.fetch_add(1, std::memory_order_acq_rel) + 1u;
}

void ResetMetrics() noexcept {
    g_sizes.recent_players.store(0, std::memory_order_relaxed);
    g_sizes.bones.store(0, std::memory_order_relaxed);
    g_sizes.controller_pawns.store(0, std::memory_order_relaxed);
    g_sizes.scene_bones.store(0, std::memory_order_relaxed);
    g_sizes.dropped_weapons.store(0, std::memory_order_relaxed);
    g_sizes.spectators.store(0, std::memory_order_relaxed);
}

} // namespace CS2::CacheManager
