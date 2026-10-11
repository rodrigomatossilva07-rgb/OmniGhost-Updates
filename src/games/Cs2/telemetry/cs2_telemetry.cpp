#include "cs2_telemetry.h"

#include "../cs2_game.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>

namespace CS2::Telemetry {

std::atomic<uint64_t> scan_id{0};
PhaseTiming phase{};
std::atomic<int> pressure_level{0};
std::atomic<uint64_t> heavy_dma_cooldown_until_ms{0};
std::atomic<float> last_slow_dma_ms{0.f};
std::atomic<float> last_weapon_ms{0.f};
std::atomic<float> last_ammo_ms{0.f};
std::atomic<float> last_flags_ms{0.f};

bool HeavyDmaCooldownActive() noexcept {
    return GetTickCount64() <
        heavy_dma_cooldown_until_ms.load(std::memory_order_acquire);
}

void NotePossibleDeviceStall(float duration_ms) noexcept {
    // Only transport-scale stalls trigger recovery. Normal 20-120 ms scans on
    // slower boards must not poison the optional-lane scheduler.
    constexpr float threshold = 300.f;
    if (!std::isfinite(duration_ms) || duration_ms < threshold)
        return;

    last_slow_dma_ms.store(duration_ms, std::memory_order_relaxed);
    const uint64_t cooldown = static_cast<uint64_t>(
        (std::max)(40.f, (std::min)(150.f, duration_ms * 0.15f)));
    const uint64_t until = GetTickCount64() + cooldown;
    uint64_t current = heavy_dma_cooldown_until_ms.load(std::memory_order_relaxed);
    while (until > current &&
           !heavy_dma_cooldown_until_ms.compare_exchange_weak(
               current, until, std::memory_order_relaxed)) {
    }
}

void ResetSession() noexcept {
    pressure_level.store(0, std::memory_order_relaxed);
    heavy_dma_cooldown_until_ms.store(0, std::memory_order_relaxed);
    last_slow_dma_ms.store(0.f, std::memory_order_relaxed);
    last_weapon_ms.store(0.f, std::memory_order_relaxed);
    last_ammo_ms.store(0.f, std::memory_order_relaxed);
    last_flags_ms.store(0.f, std::memory_order_relaxed);
    phase = {};
}

} // namespace CS2::Telemetry
