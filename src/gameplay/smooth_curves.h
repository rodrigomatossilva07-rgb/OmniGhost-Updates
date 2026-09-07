#pragma once
#include "../../ImGui/imgui.h"
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <random>

namespace Gameplay::SmoothCurves {

    // Curve types for aim smoothing
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
        Bezier = 31,          // Custom bezier curve
        CatmullRom = 32,      // Catmull-Rom spline
        Custom = 99           // User-defined function
    };

    // Curve evaluation context
    struct CurveContext {
        float input;           // Input value (0.0 - 1.0 typically)
        float distance;        // Target distance
        float velocity;        // Current velocity
        float error;           // Current error
        float time_delta;      // Frame time
        uint64_t entity_id;    // Target entity ID
    };

    // Bezier curve definition
    struct BezierCurve {
        ImVec2 p0 = ImVec2(0.0f, 0.0f);  // Start (0, 0)
        ImVec2 p1 = ImVec2(0.25f, 0.1f); // Control 1
        ImVec2 p2 = ImVec2(0.75f, 0.9f); // Control 2
        ImVec2 p3 = ImVec2(1.0f, 1.0f);  // End (1, 1)
        
        // Evaluate cubic bezier at t
        float Evaluate(float t) const;
        
        // Get control points for UI editor
        std::array<ImVec2, 4> GetControlPoints() const {
            return {p0, p1, p2, p3};
        }
        
        void SetControlPoints(const std::array<ImVec2, 4>& points) {
            p0 = points[0];
            p1 = points[1];
            p2 = points[2];
            p3 = points[3];
        }
        
        // Preset curves
        static BezierCurve Linear();
        static BezierCurve EaseInOut();
        static BezierCurve EaseOut();
        static BezierCurve EaseIn();
        static BezierCurve Sharp();
        static BezierCurve Gentle();
        static BezierCurve Snap();
        static BezierCurve Smooth();
    };

    // Catmull-Rom spline for multi-point curves
    struct CatmullRomSpline {
        std::vector<ImVec2> points;  // Control points
        bool closed = false;
        float tension = 0.5f;        // 0.0 = linear, 0.5 = standard, 1.0 = tight
        
        // Add control point
        void AddPoint(const ImVec2& point);
        void RemovePoint(size_t index);
        void Clear();
        
        // Evaluate spline at t (0.0 - 1.0 across entire spline)
        float Evaluate(float t) const;
        
        // Get segment count
        size_t SegmentCount() const;
        
        // Preset splines
        static CatmullRomSpline SmoothAim();
        static CatmullRomSpline SharpAim();
        static CatmullRomSpline GentleAim();
    };

    // Smooth curve configuration for aim
    struct SmoothCurveConfig {
        CurveType type = CurveType::EaseOutCubic;
        BezierCurve bezier;
        CatmullRomSpline spline;
        
        // Distance-based curve selection
        bool use_distance_curves = false;
        struct DistanceCurve {
            float max_distance;
            CurveType type;
            BezierCurve bezier;
        };
        std::array<DistanceCurve, 4> distance_curves{};
        
        // Dynamic curve adjustment
        bool dynamic_adjustment = false;
        float velocity_factor = 0.1f;      // Adjust curve based on velocity
        float error_factor = 0.05f;        // Adjust curve based on error
        float distance_factor = 0.02f;     // Adjust curve based on distance
        
        // Curve parameters
        float overshoot = 0.0f;            // For Back/Elastic curves
        float amplitude = 1.0f;            // For Elastic curves
        float period = 0.3f;               // For Elastic curves
        float bounces = 3.0f;              // For Bounce curves
        
        // Input/output range
        float input_min = 0.0f;
        float input_max = 1.0f;
        float output_min = 0.0f;
        float output_max = 1.0f;
        
        // Clamping
        bool clamp_output = true;
        float min_output = 0.0f;
        float max_output = 1.0f;
    };

    // Smooth curve evaluator
    class SmoothCurveEvaluator {
    public:
        SmoothCurveEvaluator();
        explicit SmoothCurveEvaluator(const SmoothCurveConfig& config);
        
        void SetConfig(const SmoothCurveConfig& config);
        const SmoothCurveConfig& GetConfig() const { return config_; }
        
        // Evaluate curve for given input
        float Evaluate(float input) const;
        float Evaluate(const CurveContext& context) const;
        
        // Evaluate with distance scaling
        float EvaluateWithDistance(float input, float distance) const;
        
        // Get curve name
        const char* GetCurveName() const;
        
        // Preset configurations
        static SmoothCurveConfig LegitConfig();
        static SmoothCurveConfig RageConfig();
        static SmoothCurveConfig SniperConfig();
        static SmoothCurveConfig CloseRangeConfig();
        static SmoothCurveConfig LongRangeConfig();
        
        // Serialize/deserialize
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
    
    // Curve editor UI
    namespace CurveEditor {
        // Draw curve editor widget
        void DrawCurveEditor(const char* label, SmoothCurveConfig& config, 
                            const ImVec2& size = ImVec2(300, 200));
        
        // Draw bezier control points
        void DrawBezierEditor(const char* label, BezierCurve& curve,
                             const ImVec2& size = ImVec2(200, 200));
        
        // Draw catmull-rom spline editor
        void DrawSplineEditor(const char* label, CatmullRomSpline& spline,
                             const ImVec2& size = ImVec2(300, 200));
        
        // Draw curve preview
        void DrawCurvePreview(const SmoothCurveConfig& config,
                             const ImVec2& size = ImVec2(200, 100));
        
        // Curve preset selector
        bool DrawCurvePresetSelector(const char* label, CurveType& type);
    }

    // Distance-based curve manager
    class DistanceCurveManager {
    public:
        struct CurveEntry {
            float min_distance;
            float max_distance;
            SmoothCurveConfig config;
            float blend_range = 10.0f;  // Distance range for blending
        };
        
        void AddCurve(float min_dist, float max_dist, const SmoothCurveConfig& config);
        void RemoveCurve(size_t index);
        void Clear();
        
        // Get interpolated curve for distance
        SmoothCurveConfig GetCurveForDistance(float distance) const;
        
        // Evaluate with automatic distance selection
        float Evaluate(float input, float distance) const;
        
        // Serialize/deserialize
        std::string Serialize() const;
        bool Deserialize(const std::string& data);
        
        // Presets
        static DistanceCurveManager DefaultAimCurves();
        static DistanceCurveManager SniperCurves();
        static DistanceCurveManager AggressiveCurves();
        
    private:
        std::vector<CurveEntry> curves_;
    };

    // Humanizer - adds subtle imperfections to aim
    class AimHumanizer {
    public:
        struct HumanizerConfig {
            bool enabled = true;
            float micro_jitter = 0.15f;       // Tiny random movements
            float macro_drift = 0.05f;        // Slow drift over time
            float reaction_variance = 0.02f;  // Reaction time variance
            float overshoot_chance = 0.03f;   // Chance to slightly overshoot
            float overshoot_amount = 0.1f;    // Overshoot magnitude
            float pause_chance = 0.01f;       // Chance to pause briefly
            float pause_duration = 0.05f;     // Pause duration
            float curve_variance = 0.05f;     // Curve parameter variance
            
            // Per-distance scaling
            float close_range_multiplier = 0.5f;
            float mid_range_multiplier = 1.0f;
            float far_range_multiplier = 1.5f;
        };
        
        explicit AimHumanizer(const HumanizerConfig& config = {});
        void SetConfig(const HumanizerConfig& config) { config_ = config; }
        
        // Apply humanization to aim output
        ImVec2 Humanize(const ImVec2& raw_movement, float distance, float dt);
        
        // Reset internal state
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

} // namespace Gameplay::SmoothCurves