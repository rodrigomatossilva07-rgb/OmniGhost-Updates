#pragma once

#include <array>
#include <cstddef>

namespace CS2::RCS {

// Runtime-independent RCS settings. Strength values are multipliers, smoothing
// is 0 (immediate) to 0.95 (very gradual), and maxCorrection is the maximum
// relative mouse movement emitted by one update.
struct RCSConfig {
    bool enabled = false;
    float horizontalStrength = 1.f;
    float verticalStrength = 1.f;
    float recoilScale = 1.f;
    float smoothing = .35f;
    int startAfterShots = 2;
    float maxCorrection = 8.f;
    bool weaponSpecific = true;
    bool debugMode = false;
    std::array<float, 65> weaponScale = [] {
        std::array<float, 65> values{};
        values.fill(1.f);
        return values;
    }();

    [[nodiscard]] float ScaleForWeapon(int definition) const noexcept {
        if (!weaponSpecific || definition < 0 ||
            definition >= static_cast<int>(weaponScale.size()))
            return 1.f;
        return weaponScale[static_cast<std::size_t>(definition)];
    }
};

} // namespace CS2::RCS
