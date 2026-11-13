#pragma warning(disable: 4100 4458 4244)
#include "animations.h"
#include "../config/app_settings.h"
#include <cmath>
#include <unordered_map>
#include <algorithm>

namespace CyberAnimations {

    namespace {
        std::unordered_map<ImGuiID, float> g_hover;
        std::unordered_map<ImGuiID, SpringState> g_springs;

        struct HoverNudgeState {
            bool hovered = false;
            float elapsed = 1.0f;
        };
        std::unordered_map<ImGuiID, HoverNudgeState> g_hover_nudges;
    }

    float Ease(float t, Easing easing) {
        t = std::clamp(t, 0.0f, 1.0f);
        switch (easing) {
            case Easing::Linear:
                return t;
            case Easing::EaseIn:
                return t * t;
            case Easing::EaseOut:
                return 1.0f - (1.0f - t) * (1.0f - t);
            case Easing::EaseInOut:
                return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
            case Easing::EaseOutBack: {
                const float c1 = 1.70158f;
                const float c3 = c1 + 1.0f;
                return 1.0f + c3 * std::pow(t - 1.0f, 3.0f) + c1 * std::pow(t - 1.0f, 2.0f);
            }
            case Easing::EaseOutElastic: {
                const float c4 = (2.0f * 3.14159265f) / 3.0f;
                if (t == 0.0f) return 0.0f;
                if (t == 1.0f) return 1.0f;
                return std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
            }
            case Easing::EaseOutBounce: {
                const float n1 = 7.5625f;
                const float d1 = 2.75f;
                if (t < 1.0f / d1) {
                    return n1 * t * t;
                } else if (t < 2.0f / d1) {
                    return n1 * (t -= 1.5f / d1) * t + 0.75f;
                } else if (t < 2.5f / d1) {
                    return n1 * (t -= 2.25f / d1) * t + 0.9375f;
                } else {
                    return n1 * (t -= 2.625f / d1) * t + 0.984375f;
                }
            }
        }
        return t;
    }

    void SpringUpdate(SpringState& spring, float dt) {
        if (spring.settled) return;

        const float x = spring.position - spring.target;
        const float a = -spring.config.stiffness * x - spring.config.damping * spring.velocity;
        spring.velocity += a / spring.config.mass * dt;
        spring.position += spring.velocity * dt;

        if (std::abs(spring.position - spring.target) < spring.config.rest_threshold &&
            std::abs(spring.velocity) < spring.config.rest_velocity_threshold) {
            spring.position = spring.target;
            spring.velocity = 0.0f;
            spring.settled = true;
        }
    }

    void SpringSetTarget(SpringState& spring, float target) {
        spring.target = target;
        spring.settled = false;
    }

    bool SpringIsSettled(const SpringState& spring) {
        return spring.settled;
    }

    void SetHover(ImGuiID id, bool hovered) {
        float& progress = g_hover[id];
        const float target = hovered ? 1.0f : 0.0f;
        const float motion = app_settings::AnimationScale();
        if (motion <= 0.0f) {
            progress = target;
            return;
        }
        const float dt = ImGui::GetIO().DeltaTime;
        const float amount = 1.0f - std::exp(-13.0f * dt * (0.65f + motion));
        progress += (target - progress) * amount;
    }

    float GetHover(ImGuiID id) {
        const auto it = g_hover.find(id);
        return it != g_hover.end() ? it->second : 0.0f;
    }

    void SetHover(ImGuiID id, bool hovered, float speed) {
        float& progress = g_hover[id];
        const float target = hovered ? 1.0f : 0.0f;
        const float motion = app_settings::AnimationScale();
        if (motion <= 0.0f) {
            progress = target;
            return;
        }
        const float dt = ImGui::GetIO().DeltaTime;
        const float amount = 1.0f - std::exp(-speed * dt * (0.65f + motion));
        progress += (target - progress) * amount;
    }

    float GetHover(ImGuiID id, float speed) {
        (void)speed; return GetHover(id);
    }

    float HoverNudge(ImGuiID id, bool hovered, float amplitude) {
        HoverNudgeState& state = g_hover_nudges[id];
        if (!app_settings::MotionEnabled()) {
            state.hovered = hovered;
            state.elapsed = 1.0f;
            return 0.0f;
        }
        if (hovered && !state.hovered)
            state.elapsed = 0.0f;
        state.hovered = hovered;
        state.elapsed = std::min(1.0f, state.elapsed + std::min(ImGui::GetIO().DeltaTime, 0.05f) / 0.17f);
        if (state.elapsed >= 1.0f)
            return 0.0f;
        const float envelope = (1.0f - state.elapsed) * (1.0f - state.elapsed);
        return std::sin(state.elapsed * 6.0f * 3.14159265f) * amplitude * envelope *
            app_settings::AnimationScale();
    }

    void AnimatedFloat::Update(float dt) {
        SpringUpdate(spring, dt);
        value = spring.position;
    }

    void AnimatedFloat::SetTarget(float target) {
        SpringSetTarget(spring, target);
    }

    AnimationPool& AnimationPool::Instance() {
        static AnimationPool pool;
        return pool;
    }

    ImGuiID AnimationPool::Register(float* value_ptr, float target, const SpringConfig& config) {
        ImGuiID id = next_id_++;
        Entry entry;
        entry.value_ptr = value_ptr;
        entry.spring = SpringState{};
        entry.spring.position = *value_ptr;
        entry.spring.target = target;
        entry.spring.config = config;
        entries_[id] = entry;
        return id;
    }

    void AnimationPool::Unregister(ImGuiID id) {
        entries_.erase(id);
    }

    void AnimationPool::Update(float dt) {
        for (auto& [id, entry] : entries_) {
            if (!entry.spring.settled) {
                SpringUpdate(entry.spring, dt);
                *entry.value_ptr = entry.spring.position;
            }
        }
    }

    void AnimationPool::SetTarget(ImGuiID id, float target) {
        auto it = entries_.find(id);
        if (it != entries_.end()) {
            SpringSetTarget(it->second.spring, target);
        }
    }

    void StaggeredAnimation::Start(int item_count) {
        count = item_count;
        elapsed = 0.0f;
    }

    void StaggeredAnimation::Update(float dt) {
        elapsed += dt;
    }

    float StaggeredAnimation::GetProgress(int index) const {
        float delay = base_delay + index * item_delay;
        float progress = (elapsed - delay) / 0.3f;
        return std::clamp(progress, 0.0f, 1.0f);
    }

    bool StaggeredAnimation::IsComplete() const {
        return elapsed > base_delay + count * item_delay + 0.3f;
    }

    void PageTransition::Start(bool forward) {
        target = forward ? 1.0f : 0.0f;
        reverse = !forward;
        progress = reverse ? 1.0f : 0.0f;
    }

    void PageTransition::Update(float dt) {
        float speed = 1.0f / duration;
        if (reverse) {
            progress = std::max(0.0f, progress - speed * dt);
        } else {
            progress = std::min(1.0f, progress + speed * dt);
        }
        progress = Ease(progress, easing);
    }

    bool PageTransition::IsComplete() const {
        return (reverse && progress <= 0.0f) || (!reverse && progress >= 1.0f);
    }

    void InteractionFeedback::Trigger(Type t, float intensity) {
        type = t;
        this->intensity = intensity;
        elapsed = 0.0f;
    }

    void InteractionFeedback::Update(float dt) {
        if (type == Type::None) return;
        elapsed += dt;
        float progress = elapsed / duration;
        if (progress >= 1.0f) {
            type = Type::None;
            intensity = 0.0f;
        } else {
            intensity = (1.0f - progress) * intensity;
        }
    }

    AccessibilityAnnouncer& AccessibilityAnnouncer::Instance() {
        static AccessibilityAnnouncer announcer;
        return announcer;
    }

    void AccessibilityAnnouncer::Announce(const char* message, bool polite) {
        if (!message || !*message) return;
        queue_.push_back(Announcement{std::string(message), polite, 3.0f, 0.0f});
    }

    void AccessibilityAnnouncer::Update(float dt) {
        if (current_.empty() && !queue_.empty()) {
            current_ = queue_.front().message;
            current_lifetime_ = queue_.front().lifetime;
            queue_.erase(queue_.begin());
        }

        if (!current_.empty()) {
            current_lifetime_ -= dt;
            if (current_lifetime_ <= 0.0f)
                current_.clear();
        }
    }

}
