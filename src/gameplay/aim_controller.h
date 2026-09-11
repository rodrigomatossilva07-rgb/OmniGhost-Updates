#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace OmniGhost::Gameplay {

struct AimMotionSettings {
    float smooth = 0.f;              // 0 = full assist, 100 = disabled
    float deadzone = 0.f;
    bool humanize = false;

    // Optional advanced motion shaping. These defaults keep the behaviour of
    // older configurations unless the corresponding feature is enabled.
    bool permanent_humanize = false;
    int reaction_delay_ms_min = 35;
    int reaction_delay_ms_max = 85;
    float overshoot_px = 0.65f;
    bool prediction = false;
    float prediction_lead = 0.02f;
    float micro_jitter_px = 0.08f;
    float max_deg_per_ms = 6.f;

    float minimum_strength = 0.02f;  // low floor so held assist does not fight the mouse
    float minimum_error = 0.45f;
    float output_scale = 1.f;        // device counts per screen-space correction
    float reversal_damping = 0.18f;  // kills overshoot when error sign flips
    int max_step = 0;                // 0 keeps the smooth-derived limit
    int minimum_interval_ms = 0;     // prevents serial command backlog
    float approach_softness = 18.f;  // px radius where strength eases down (human track)
    float ema_alpha = 0.42f;         // temporal smoothing of desired move (0.2-0.6)
};

struct AimMotion {
    int x = 0;
    int y = 0;
    float error = 0.f;
    float strength = 0.f;

    explicit operator bool() const noexcept { return x != 0 || y != 0; }
};

// Shared distance profile for every game adapter. Far targets get smaller,
// capped steps so a held bind cannot accelerate into an oscillation.
inline void ApplyStableDistanceProfile(AimMotionSettings& settings,
                                       float distance_meters) noexcept {
    settings.minimum_interval_ms = 6;
    settings.reversal_damping = 0.16f;
    settings.approach_softness = 16.f;
    settings.ema_alpha = 0.40f;
    if (distance_meters > 180.f) {
        settings.output_scale = 0.12f;
        settings.max_step = 5;
        settings.approach_softness = 28.f;
        settings.ema_alpha = 0.32f;
    } else if (distance_meters > 110.f) {
        settings.output_scale = 0.16f;
        settings.max_step = 7;
        settings.approach_softness = 24.f;
        settings.ema_alpha = 0.36f;
    } else if (distance_meters > 55.f) {
        settings.output_scale = 0.22f;
        settings.max_step = 11;
        settings.approach_softness = 18.f;
    } else if (distance_meters > 25.f) {
        settings.output_scale = 0.30f;
        settings.max_step = 16;
    } else {
        settings.output_scale = 0.38f;
        settings.max_step = 22;
        settings.approach_softness = 12.f;
        settings.ema_alpha = 0.48f;
    }
}

// Shared by every game. Converts target error into continuous human-like
// correction: eases near the crosshair, damps reversals, keeps sub-pixel residual.
class ContinuousAimController {
public:
    void Reset() noexcept {
        residual_x_ = 0.f;
        residual_y_ = 0.f;
        ema_x_ = 0.f;
        ema_y_ = 0.f;
        target_id_ = 0;
        previous_error_x_ = 0.f;
        previous_error_y_ = 0.f;
        have_previous_error_ = false;
        last_output_ = {};
        acquire_time_ = {};
        reaction_delay_ms_ = 0;
        overshoot_remaining_ = 0.f;
        target_switched_ = false;
        flip_streak_ = 0;
    }

    void SetTarget(std::uint64_t target_id) noexcept {
        if (target_id_ == target_id)
            return;
        residual_x_ = 0.f;
        residual_y_ = 0.f;
        ema_x_ = 0.f;
        ema_y_ = 0.f;
        target_id_ = target_id;
        target_switched_ = true;
        overshoot_remaining_ = 0.f;
        previous_error_x_ = 0.f;
        previous_error_y_ = 0.f;
        have_previous_error_ = false;
        last_output_ = {};
        flip_streak_ = 0;
    }

    AimMotion Step(float error_x, float error_y, const AimMotionSettings& settings) noexcept {
        AimMotion result{};
        if (!std::isfinite(error_x) || !std::isfinite(error_y)) {
            residual_x_ = residual_y_ = 0.f;
            ema_x_ = ema_y_ = 0.f;
            return result;
        }

        result.error = std::sqrt(error_x * error_x + error_y * error_y);
        const float smooth = std::clamp(settings.smooth, 0.f, 100.f);

        // Base strength: 0 smooth = full, 100 = none.
        float strength = (100.f - smooth) / 100.f;

        // Soft ease near the crosshair so the assist lands like a human track
        // instead of oscillating left/right around the bone.
        const float soft = (std::max)(6.f, settings.approach_softness);
        if (strength > 0.f && result.error < soft) {
            const float t = result.error / soft;
            // Ease-in quadratic: very gentle when almost locked.
            strength *= t * t;
        }

        if (settings.humanize && strength > 0.05f && strength < 0.95f) {
            // Slightly softer curve; avoid noise near center (causes side-wobble).
            strength = std::pow(strength, 0.90f);
            if (result.error > 8.f) {
                const float n = 0.012f * strength;
                strength *= (1.f + n * (((float)(target_id_ % 7u) - 3.f) * 0.08f));
            }
        }

        if (strength > 0.f)
            strength = (std::max)(strength, std::clamp(settings.minimum_strength, 0.f, 0.15f));
        result.strength = strength;

        const float deadzone = smooth <= 3.f ? 0.f : (std::max)(0.f, settings.deadzone);
        const float stop_at = (std::max)(settings.minimum_error, deadzone);
        if (strength <= 0.0001f || result.error < stop_at) {
            residual_x_ = residual_y_ = 0.f;
            ema_x_ *= 0.5f;
            ema_y_ *= 0.5f;
            previous_error_x_ = error_x;
            previous_error_y_ = error_y;
            have_previous_error_ = true;
            flip_streak_ = 0;
            return result;
        }

        const auto now = std::chrono::steady_clock::now();
        if (settings.minimum_interval_ms > 0 && last_output_.time_since_epoch().count() != 0 &&
            now - last_output_ < std::chrono::milliseconds(settings.minimum_interval_ms)) {
            return result;
        }

        float scale_x = std::clamp(settings.output_scale, 0.01f, 1.f);
        float scale_y = scale_x;
        const float reversal = std::clamp(settings.reversal_damping, 0.05f, 1.f);

        // Detect left/right (or up/down) thrashing while the bind is held.
        if (have_previous_error_) {
            const bool flip_x = (error_x * previous_error_x_ < 0.f) &&
                                std::fabs(error_x) > 0.6f && std::fabs(previous_error_x_) > 0.6f;
            const bool flip_y = (error_y * previous_error_y_ < 0.f) &&
                                std::fabs(error_y) > 0.6f && std::fabs(previous_error_y_) > 0.6f;
            if (flip_x || flip_y) {
                ++flip_streak_;
                const float damp = reversal * (1.f + 0.35f * (float)(std::min)(flip_streak_, 4));
                if (flip_x) {
                    scale_x *= damp;
                    residual_x_ = 0.f;
                    ema_x_ *= 0.25f;
                }
                if (flip_y) {
                    scale_y *= damp;
                    residual_y_ = 0.f;
                    ema_y_ *= 0.25f;
                }
            } else {
                flip_streak_ = 0;
            }

            // Stance change (crouch/prone): head drops fast on screen. Boost vertical
            // tracking so the crosshair follows the new head instead of lagging at the
            // old standing height while the bind is held.
            const float dy_jump = std::fabs(error_y - previous_error_y_);
            if (dy_jump > 28.f && std::fabs(error_y) > 10.f) {
                scale_y = (std::min)(1.f, scale_y * 1.85f);
                ema_y_ = 0.f;
                residual_y_ = 0.f;
            }
        }
        const float previous_error_x = previous_error_x_;
        const float previous_error_y = previous_error_y_;
        const bool had_previous_error = have_previous_error_;
        previous_error_x_ = error_x;
        previous_error_y_ = error_y;
        have_previous_error_ = true;

        // Soft reaction delay on new target (permanent humanization).
        if (settings.permanent_humanize || settings.humanize) {
            if (target_switched_) {
                const int delay_min = (std::max)(0, (std::min)(settings.reaction_delay_ms_min, settings.reaction_delay_ms_max));
                const int delay_max = (std::max)(delay_min, (std::max)(settings.reaction_delay_ms_min, settings.reaction_delay_ms_max));
                const unsigned span = static_cast<unsigned>(delay_max - delay_min + 1);
                const int delay = delay_min +
                    static_cast<int>((target_id_ * 17u) % span);
                acquire_time_ = now;
                reaction_delay_ms_ = delay;
                overshoot_remaining_ = settings.overshoot_px;
                target_switched_ = false;
            }
            const auto since_acquire = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - acquire_time_).count();
            if (since_acquire < reaction_delay_ms_) {
                // Hold aim assist during reaction latency — mouse still works.
                residual_x_ = residual_y_ = 0.f;
                ema_x_ *= 0.7f;
                ema_y_ *= 0.7f;
                return result;
            }
        }

        // Desired continuous move (screen px * strength * scale).
        float desired_x = error_x * strength * scale_x;
        float desired_y = error_y * strength * scale_y;

        // Prediction lead (optional menu toggle): advance along last error delta.
        if (settings.prediction && had_previous_error) {
            const float vx = error_x - previous_error_x;
            const float vy = error_y - previous_error_y;
            desired_x += vx * (settings.prediction_lead * 60.f);
            desired_y += vy * (settings.prediction_lead * 60.f);
        }

        // Bezier-like ease on the strength curve even at smooth 0 (anti-linear).
        if (settings.permanent_humanize) {
            const float t = std::clamp(result.error / 120.f, 0.f, 1.f);
            // Hermite smoothstep: accelerate then decelerate.
            const float ease = t * t * (3.f - 2.f * t);
            desired_x *= (0.55f + 0.45f * ease);
            desired_y *= (0.55f + 0.45f * ease);

            // First-impulse overshoot then correction.
            if (overshoot_remaining_ > 0.05f && result.error > 6.f) {
                const float sign_x = error_x >= 0.f ? 1.f : -1.f;
                const float sign_y = error_y >= 0.f ? 1.f : -1.f;
                const float boost = (std::min)(overshoot_remaining_, 1.2f);
                desired_x += sign_x * boost * 0.35f;
                desired_y += sign_y * boost * 0.35f;
                overshoot_remaining_ *= 0.82f;
            }
        }

        // Temporal EMA so consecutive frames do not fight each other (side wobble).
        const float alpha = std::clamp(settings.ema_alpha, 0.15f, 0.85f);
        // Even at smooth 0, keep a light EMA so motion is never a pure step function.
        if (smooth <= 1.f && !settings.permanent_humanize) {
            ema_x_ = desired_x;
            ema_y_ = desired_y;
        } else {
            const float a = (smooth <= 1.f) ? (std::max)(alpha, 0.55f) : alpha;
            ema_x_ = ema_x_ * (1.f - a) + desired_x * a;
            ema_y_ = ema_y_ * (1.f - a) + desired_y * a;
        }

        // Micro-jitter (muscle tremor) — permanent, low amplitude.
        float jitter_x = 0.f, jitter_y = 0.f;
        if (settings.permanent_humanize && settings.micro_jitter_px > 0.f && result.error > 2.f) {
            const float phase = static_cast<float>((target_id_ + static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count())) % 1000u) * 0.006283185f;
            jitter_x = std::sin(phase * 1.7f) * settings.micro_jitter_px;
            jitter_y = std::cos(phase * 1.3f) * settings.micro_jitter_px * 0.85f;
        }

        const float move_x = ema_x_ + residual_x_ + jitter_x;
        const float move_y = ema_y_ + residual_y_ + jitter_y;
        int x = static_cast<int>(std::lround(move_x));
        int y = static_cast<int>(std::lround(move_y));
        residual_x_ = std::clamp(move_x - static_cast<float>(x), -1.25f, 1.25f);
        residual_y_ = std::clamp(move_y - static_cast<float>(y), -1.25f, 1.25f);

        if (smooth <= 1.f && !settings.permanent_humanize) {
            if (x == 0 && std::fabs(error_x) >= 0.5f) x = error_x > 0.f ? 1 : -1;
            if (y == 0 && std::fabs(error_y) >= 0.5f) y = error_y > 0.f ? 1 : -1;
            residual_x_ = residual_y_ = 0.f;
        } else {
            if (x == 0 && std::fabs(move_x) >= 0.28f) x = move_x > 0.f ? 1 : -1;
            if (y == 0 && std::fabs(move_y) >= 0.28f) y = move_y > 0.f ? 1 : -1;
        }

        // Rate limiter / anti-snap (always).
        int limit = smooth <= 1.f ? 48 : (smooth <= 12.f ? 36 : (smooth <= 40.f ? 28 : 20));
        if (settings.permanent_humanize) {
            const float rate = (std::max)(4.f, settings.max_deg_per_ms * 8.f);
            limit = (std::min)(limit, static_cast<int>(rate));
        }
        if (settings.max_step > 0)
            limit = (std::min)(limit, settings.max_step);
        result.x = std::clamp(x, -limit, limit);
        result.y = std::clamp(y, -limit, limit);
        if (result)
            last_output_ = now;
        return result;
    }

    void NotifyTarget(std::uint64_t target_id) noexcept {
        if (target_id != target_id_) {
            target_id_ = target_id;
            target_switched_ = true;
            overshoot_remaining_ = 0.f;
        }
    }

private:
    float residual_x_ = 0.f;
    float residual_y_ = 0.f;
    float ema_x_ = 0.f;
    float ema_y_ = 0.f;
    std::uint64_t target_id_ = 0;
    float previous_error_x_ = 0.f;
    float previous_error_y_ = 0.f;
    bool have_previous_error_ = false;
    int flip_streak_ = 0;
    std::chrono::steady_clock::time_point last_output_{};
    std::chrono::steady_clock::time_point acquire_time_{};
    int reaction_delay_ms_ = 0;
    float overshoot_remaining_ = 0.f;
    bool target_switched_ = false;
};

} // namespace OmniGhost::Gameplay
