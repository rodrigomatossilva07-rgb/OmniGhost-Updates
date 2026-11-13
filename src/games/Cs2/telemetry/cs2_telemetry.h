#pragma once

#include <atomic>
#include <cstdint>

namespace CS2::Telemetry {

struct PhaseTiming {
    uint64_t scan_id = 0;
    float entity_ms = 0.f;
    float local_ms = 0.f;
    float core_scatter_ms = 0.f;
    float positions_ms = 0.f;
    float bones_ms = 0.f;
    float weapon_ms = 0.f;
    float ammo_ms = 0.f;
    float flags_ms = 0.f;
    float visibility_ms = 0.f;
    float spectator_ms = 0.f;
    float bomb_ms = 0.f;
    float publish_ms = 0.f;
    float early_publish_ms = 0.f;
    float json_ms = 0.f;
    float cache_prune_ms = 0.f;
    float vector_copy_ms = 0.f;
    float visibility_submit_ms = 0.f;
    float cleanup_ms = 0.f;
    float other_ms = 0.f;
    float lock_wait_ms = 0.f;
    float optional_budget_left_ms = 0.f;
    int entity_full_probe = 0;
    int spectator_refresh = 0;
    int bomb_refresh = 0;
    int cache_cleanup = 0;
    int bones_players = 0;
    int bones_skipped_budget = 0;
    int visibility_players_tested = 0;
    int visibility_rays = 0;
};

extern std::atomic<uint64_t> scan_id;
extern PhaseTiming phase;
extern std::atomic<int> pressure_level;
extern std::atomic<uint64_t> heavy_dma_cooldown_until_ms;
extern std::atomic<float> last_slow_dma_ms;
extern std::atomic<float> last_weapon_ms;
extern std::atomic<float> last_ammo_ms;
extern std::atomic<float> last_flags_ms;

[[nodiscard]] bool HeavyDmaCooldownActive() noexcept;
[[nodiscard]] uint64_t HeavyDmaCooldownRemainingMs() noexcept;
void NotePossibleDeviceStall(float duration_ms) noexcept;
void ResetSession() noexcept;

}
