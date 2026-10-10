#pragma once

#include <cmath>

namespace FiveM {
enum class PedHealthState { Unknown, Alive, Dead };

inline PedHealthState ClassifyPedHealth(float health) noexcept {
    if (!std::isfinite(health) || health < 0.f || health >= 1000.f)
        return PedHealthState::Unknown;
    return health == 0.f ? PedHealthState::Dead : PedHealthState::Alive;
}
}
