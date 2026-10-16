#pragma once
#include "third_party/ImGui/imgui.h"
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <random>

namespace Gameplay::SmoothCurves {


    enum class CurveType : int {
        Linear = 0,
        EaseInQuad = 1,
        EaseOutQuad = 2,
        EaseInOutQuad = 3,
        EaseInCubic = 4,
        EaseOutCubic = 5,
        EaseInOutCubic = 6,
        EaseInQuart = 7,
        EaseOutQuart = 8,
        EaseInOutQuart = 9,
        EaseInQuint = 10,
        EaseOutQuint = 11,
        EaseInOutQuint = 12,
        EaseInSine = 13,
        EaseOutSine = 14,
        EaseInOutSine = 15,
        EaseInExpo = 16,
        EaseOutExpo = 17,
        EaseInOutExpo = 18,
        EaseInCirc = 19,
        EaseOutCirc = 20,
        EaseInOutCirc = 21,
        EaseInBack = 22,
        EaseOutBack = 23,
        EaseInOutBack = 24,
        EaseInElastic = 25,
        EaseOutElastic = 26,
        EaseInOutElastic = 27,
        EaseInBounce = 28,
        EaseOutBounce = 29,
        EaseInOutBounce = 30,
        Bezier = 31,
        CatmullRom = 32,
        Custom = 99
    };


    struct CurveContext {
        float input;
        float distance;
        float velocity;
        float error;
        float time_delta;
        uint64_t entity_id;
    };


    struct BezierCurve {
        ImVec2 p0 = ImVec2(0.0f, 0.0f);
        ImVec2 p1 = ImVec2(0.25f, 0.1f);
        ImVec2 p2 = ImVec2(0.75f, 0.9f);
        ImVec2 p3 = ImVec2(1.0f, 1.0f);


        float Evaluate(float t) const;


        std::array<ImVec2, 4> GetControlPoints() const {
            return {p0, p1, p2, p3};
        }

        void SetControlPoints(const std::array<ImVec2, 4>& points) {
            p0 = points[0];
            p1 = points[1];
            p2 = points[2];
            p3 = points[3];
        }


        static BezierCurve Linear();
        static BezierCurve EaseInOut();
        static BezierCurve EaseOut();
        static BezierCurve EaseIn();
        static BezierCurve Sharp();
        static BezierCurve Gentle();
        static BezierCurve Snap();
        static BezierCurve Smooth();
    };


    struct CatmullRomSpline {
        std::vector<ImVec2> points;
        bool closed = false;
        float tension = 0.5f;


        void AddPoint(const ImVec2& point);
        void RemovePoint(size_t index);
        void Clear();


        float Evaluate(float t) const;


        size_t SegmentCount() const;


        static CatmullRomSpline SmoothAim();
        static CatmullRomSpline SharpAim();
        static CatmullRomSpline GentleAim();
    };


    struct SmoothCurveConfig {
        CurveType type = CurveType::EaseOutCubic;
        BezierCurve bezier;
        CatmullRomSpline spline;


        bool use_distance_curves = false;
        struct DistanceCurve {
            float max_distance;
            CurveType type;
            BezierCurve bezier;
        };
        std::array<DistanceCurve, 4> distance_curves{};


        bool dynamic_adjustment = false;
        float velocity_factor = 0.1f;
        float error_factor = 0.05f;
        float distance_factor = 0.02f;


        float overshoot = 0.0f;
        float amplitude = 1.0f;
        float period = 0.3f;
        float bounces = 3.0f;


        float input_min = 0.0f;
        float input_max = 1.0f;
        float output_min = 0.0f;
        float output_max = 1.0f;


        bool clamp_output = true;
        float min_output = 0.0f;
        float max_output = 1.0f;
    };


    class SmoothCurveEvaluator {
    public:
        SmoothCurveEvaluator();
        explicit SmoothCurveEvaluator(const SmoothCurveConfig& config);

        void SetConfig(const SmoothCurveConfig& config);
        const SmoothCurveConfig& GetConfig() const { return config_; }


        float Evaluate(float input) const;
        float Evaluate(const CurveContext& context) const;


        float EvaluateWithDistance(float input, float distance) const;


        const char* GetCurveName() const;


        static SmoothCurveConfig LegitConfig();
        static SmoothCurveConfig RageConfig();
        static SmoothCurveConfig SniperConfig();
        static SmoothCurveConfig CloseRangeConfig();
        static SmoothCurveConfig LongRangeConfig();


        std::string Serialize() const;
        bool Deserialize(const std::string& data);

    private:
        SmoothCurveConfig config_;
        mutable float last_output_ = 0.0f;
        mutable float last_input_ = 0.0f;

        float EvaluateCurveType(CurveType type, float t, float overshoot, float amplitude, float period) const;
        float EvaluateBezier(float t) const;
        float EvaluateSpline(float t) const;
    };


    namespace CurveEditor {

        void DrawCurveEditor(const char* label, SmoothCurveConfig& config,
                            const ImVec2& size = ImVec2(300, 200));


        void DrawBezierEditor(const char* label, BezierCurve& curve,
                             const ImVec2& size = ImVec2(200, 200));


        void DrawSplineEditor(const char* label, CatmullRomSpline& spline,
                             const ImVec2& size = ImVec2(300, 200));


        void DrawCurvePreview(const SmoothCurveConfig& config,
                             const ImVec2& size = ImVec2(200, 100));


        bool DrawCurvePresetSelector(const char* label, CurveType& type);
    }


    class DistanceCurveManager {
    public:
        struct CurveEntry {
            float min_distance;
            float max_distance;
            SmoothCurveConfig config;
            float blend_range = 10.0f;
        };

        void AddCurve(float min_dist, float max_dist, const SmoothCurveConfig& config);
        void RemoveCurve(size_t index);
        void Clear();


        SmoothCurveConfig GetCurveForDistance(float distance) const;


        float Evaluate(float input, float distance) const;


        std::string Serialize() const;
        bool Deserialize(const std::string& data);


        static DistanceCurveManager DefaultAimCurves();
        static DistanceCurveManager SniperCurves();
        static DistanceCurveManager AggressiveCurves();

    private:
        std::vector<CurveEntry> curves_;
    };


    class AimHumanizer {
    public:
        struct HumanizerConfig {
            bool enabled = true;
            float micro_jitter = 0.15f;
            float macro_drift = 0.05f;
            float reaction_variance = 0.02f;
            float overshoot_chance = 0.03f;
            float overshoot_amount = 0.1f;
            float pause_chance = 0.01f;
            float pause_duration = 0.05f;
            float curve_variance = 0.05f;


            float close_range_multiplier = 0.5f;
            float mid_range_multiplier = 1.0f;
            float far_range_multiplier = 1.5f;
        };

        explicit AimHumanizer(const HumanizerConfig& config = {});
        void SetConfig(const HumanizerConfig& config) { config_ = config; }


        ImVec2 Humanize(const ImVec2& raw_movement, float distance, float dt);


        void Reset();

    private:
        HumanizerConfig config_;
        float accumulated_drift_x_ = 0.0f;
        float accumulated_drift_y_ = 0.0f;
        float pause_timer_ = 0.0f;
        bool is_paused_ = false;
        uint64_t last_entity_id_ = 0;
        std::mt19937 rng_;
    };

}