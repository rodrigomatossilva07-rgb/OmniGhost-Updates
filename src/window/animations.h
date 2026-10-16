#pragma once
#include "third_party/ImGui/imgui.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>

namespace CyberAnimations {


    enum class Easing {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
        EaseOutBack,
        EaseOutElastic,
        EaseOutBounce
    };

    float Ease(float t, Easing easing);


    struct SpringConfig {
        float stiffness = 150.0f;
        float damping = 15.0f;
        float mass = 1.0f;
        float rest_threshold = 0.01f;
        float rest_velocity_threshold = 0.01f;
    };

    struct SpringState {
        float position = 0.0f;
        float velocity = 0.0f;
        float target = 0.0f;
        SpringConfig config;
        bool settled = true;
    };

    void SpringUpdate(SpringState& spring, float dt);
    void SpringSetTarget(SpringState& spring, float target);
    bool SpringIsSettled(const SpringState& spring);


    void SetHover(ImGuiID id, bool hovered);
    float GetHover(ImGuiID id);
    void SetHover(ImGuiID id, bool hovered, float speed);
    float GetHover(ImGuiID id, float speed);


    struct AnimatedFloat {
        SpringState spring;
        float value = 0.0f;

        void Update(float dt);
        void SetTarget(float target);
        float Get() const { return value; }
        bool IsAnimating() const { return !spring.settled; }
    };


    class AnimationPool {
    public:
        static AnimationPool& Instance();


        ImGuiID Register(float* value_ptr, float target, const SpringConfig& config = {});
        void Unregister(ImGuiID id);


        void Update(float dt);


        void SetTarget(ImGuiID id, float target);

    private:
        struct Entry {
            float* value_ptr = nullptr;
            SpringState spring;
        };

        std::unordered_map<ImGuiID, Entry> entries_;
        ImGuiID next_id_ = 1;
    };


    struct StaggeredAnimation {
        float base_delay = 0.05f;
        float item_delay = 0.03f;
        int count = 0;
        float elapsed = 0.0f;

        void Start(int item_count);
        void Update(float dt);
        float GetProgress(int index) const;
        bool IsComplete() const;
    };


    struct PageTransition {
        float progress = 0.0f;
        float target = 0.0f;
        float duration = 0.2f;
        Easing easing = Easing::EaseOut;
        bool reverse = false;

        void Start(bool forward);
        void Update(float dt);
        float GetProgress() const { return progress; }
        bool IsComplete() const;
    };


    struct InteractionFeedback {
        enum class Type { None, Click, Hover, Focus, Success, Error, Warning };

        Type type = Type::None;
        float intensity = 0.0f;
        float duration = 0.15f;
        float elapsed = 0.0f;

        void Trigger(Type t, float intensity = 1.0f);
        void Update(float dt);
        float GetIntensity() const { return intensity; }
    };


    class AccessibilityAnnouncer {
    public:
        static AccessibilityAnnouncer& Instance();

        void Announce(const char* message, bool polite = true);
        void Update(float dt);

    private:
        struct Announcement {
            std::string message;
            bool polite = true;
            float lifetime = 3.0f;
            float elapsed = 0.0f;
        };

        std::vector<Announcement> queue_;
        std::string current_;
    };

}