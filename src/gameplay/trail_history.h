#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace OmniGhost::Gameplay {

struct TrailPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
    double time = 0.0;
};

// Fixed-size, allocation-free history used by every game adapter. Samples are
// rate- and distance-limited so an enabled trail never creates DMA reads or a
// growing vector per player.
template <std::size_t Capacity = 18>
class FixedTrailHistory {
public:
    static_assert(Capacity >= 2, "A trail needs at least two samples");

    bool Push(float x, float y, float z, double now,
              float minimum_distance, double minimum_interval) noexcept {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            !std::isfinite(now))
            return false;

        if (count_ != 0) {
            const TrailPoint& last = points_[(start_ + count_ - 1) % Capacity];
            if (now - last.time < minimum_interval)
                return false;
            const float dx = x - last.x;
            const float dy = y - last.y;
            const float dz = z - last.z;
            if (dx * dx + dy * dy + dz * dz < minimum_distance * minimum_distance)
                return false;
        }

        const TrailPoint point{ x, y, z, now };
        if (count_ < Capacity) {
            points_[(start_ + count_) % Capacity] = point;
            ++count_;
        } else {
            points_[start_] = point;
            start_ = (start_ + 1) % Capacity;
        }
        last_seen_ = now;
        return true;
    }

    void Touch(double now) noexcept { last_seen_ = now; }
    bool Stale(double now, double seconds) const noexcept {
        return count_ == 0 || now - last_seen_ > seconds;
    }
    std::size_t Size() const noexcept { return count_; }

    const TrailPoint& At(std::size_t index) const noexcept {
        return points_[(start_ + index) % Capacity];
    }

private:
    std::array<TrailPoint, Capacity> points_{};
    std::size_t start_ = 0;
    std::size_t count_ = 0;
    double last_seen_ = 0.0;
};

} // namespace OmniGhost::Gameplay
