#pragma once
#include "src/project/third_party/ImGui/imgui.h"
#include <array>
#include <vector>
#include <string>
#include <functional>
#include <random>

namespace Gameplay::SmoothCurves {


    enum class CurveType : int {
        // Easing functions
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

        // Advanced curve algorithms (from crisis-fortnite)
        Bezier = 31,           // Cubic Bezier com 4 control points
        BSpline = 32,          // B-Spline (uniform, grau 3)
        Chaikin = 33,          // Chaikin corner cutting (subdivision)
        Staggered = 34,        // Staggered steps (discrete, human-like)
        CatmullRom = 35,       // Catmull-Rom spline (interpolating)
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


    // ===== NOVOS ALGORITMOS (crisis-fortnite ports) =====

    struct BSplineCurve {
        std::vector<ImVec2> control_points;  // Mínimo 4 para grau 3
        int degree = 3;                       // Grau da spline (3 = cúbica)
        bool uniform_knots = true;            // Uniform vs non-uniform

        void AddControlPoint(const ImVec2& point);
        void RemoveControlPoint(size_t index);
        void Clear();

        float Evaluate(float t) const;        // t em [0, 1]

        // Presets
        static BSplineCurve SmoothAim();
        static BSplineCurve SharpAim();
        static BSplineCurve GentleAim();
        static BSplineCurve Default();
    };


    struct ChaikinCurve {
        std::vector<ImVec2> points;           // Pontos de controle (poligonal inicial)
        int subdivisions = 3;                 // Nível de subdivisão (2-5 recomendado)
        float corner_weight = 0.25f;          // Peso do corte de canto (0.25 = Chaikin clássico)

        void AddPoint(const ImVec2& point);
        void RemovePoint(size_t index);
        void Clear();

        float Evaluate(float t) const;        // t em [0, 1]
        std::vector<ImVec2> GetSubdividedPoints() const;

        // Presets
        static ChaikinCurve SmoothAim();
        static ChaikinCurve SharpAim();
        static ChaikinCurve GentleAim();
        static ChaikinCurve Default();
    };


    struct StaggeredCurve {
        std::vector<float> steps;             // Valores discretos de progresso (0.0 a 1.0)
        bool smooth_steps = true;             // Suaviza transições entre steps
        float step_smoothness = 0.15f;        // Quanto suavizar (0 = hard steps)

        void AddStep(float progress);
        void RemoveStep(size_t index);
        void Clear();
        void GenerateEvenSteps(int count);    // Gera steps uniformes

        float Evaluate(float t) const;        // t em [0, 1]

        // Presets
        static StaggeredCurve HumanLike(int steps = 8);
        static StaggeredCurve Robotic(int steps = 4);
        static StaggeredCurve Snappy(int steps = 6);
        static StaggeredCurve Default();
    };


    struct SmoothCurveConfig {
        CurveType type = CurveType::EaseOutCubic;
        BezierCurve bezier;
        CatmullRomSpline spline;
        BSplineCurve bspline;
        ChaikinCurve chaikin;
        StaggeredCurve staggered;


        bool use_distance_curves = false;
        struct DistanceCurve {
            float max_distance;
            CurveType type;
            BezierCurve bezier;
            BSplineCurve bspline;
            ChaikinCurve chaikin;
            StaggeredCurve staggered;
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


        // ===== Visual snapshot para preview em tempo real (crisis-fortnite style) =====
        struct VisualSnapshot {
            enum class Algorithm : uint8_t { None, Bezier, BSpline, Chaikin, Staggered, CatmullRom } algorithm = Algorithm::None;
            std::vector<ImVec2> control_points;     // Pontos de controle atuais
            std::vector<ImVec2> curve_points;       // Pontos da curva avaliada (para draw)
            float target_x = 0.0f;
            float target_y = 0.0f;
            float progress = 0.0f;                  // 0..1 progresso atual
            bool valid = false;
            uint64_t entity_id = 0;
        };
        mutable VisualSnapshot last_snapshot;       // Atualizado no Evaluate
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


        // Presets otimizados para diferentes estilos
        static SmoothCurveConfig LegitConfig();
        static SmoothCurveConfig RageConfig();
        static SmoothCurveConfig SniperConfig();
        static SmoothCurveConfig CloseRangeConfig();
        static SmoothCurveConfig LongRangeConfig();

        // Presets dos novos algoritmos (crisis-fortnite style)
        static SmoothCurveConfig BezierPreset();
        static SmoothCurveConfig BSplinePreset();
        static SmoothCurveConfig ChaikinPreset();
        static SmoothCurveConfig StaggeredPreset();


        std::string Serialize() const;
        bool Deserialize(const std::string& data);

        // Acesso ao snapshot visual para UI preview
        const SmoothCurveConfig::VisualSnapshot& GetLastSnapshot() const { return config_.last_snapshot; }

    private:
        SmoothCurveConfig config_;
        mutable float last_output_ = 0.0f;
        mutable float last_input_ = 0.0f;

        float EvaluateCurveType(CurveType type, float t, float overshoot, float amplitude, float period) const;
        float EvaluateBezier(float t) const;
        float EvaluateSpline(float t) const;
        float EvaluateBSpline(float t) const;
        float EvaluateChaikin(float t) const;
        float EvaluateStaggered(float t) const;

        void UpdateSnapshot(CurveType type, float input, float output) const;
    };


    namespace CurveEditor {

        void DrawCurveEditor(const char* label, SmoothCurveConfig& config,
                            const ImVec2& size = ImVec2(300, 200));


        void DrawBezierEditor(const char* label, BezierCurve& curve,
                             const ImVec2& size = ImVec2(200, 200));


        void DrawSplineEditor(const char* label, CatmullRomSpline& spline,
                             const ImVec2& size = ImVec2(300, 200));

        // Novos editors
        void DrawBSplineEditor(const char* label, BSplineCurve& curve,
                               const ImVec2& size = ImVec2(300, 200));
        void DrawChaikinEditor(const char* label, ChaikinCurve& curve,
                               const ImVec2& size = ImVec2(300, 200));
        void DrawStaggeredEditor(const char* label, StaggeredCurve& curve,
                                 const ImVec2& size = ImVec2(300, 150));

        void DrawCurvePreview(const SmoothCurveConfig& config,
                             const ImVec2& size = ImVec2(200, 100));

        // Preview com snapshot visual (curva animada)
        void DrawCurvePreviewAnimated(const SmoothCurveConfig& config,
                                      const ImVec2& size = ImVec2(300, 200),
                                      float anim_speed = 1.0f);


        bool DrawCurvePresetSelector(const char* label, CurveType& type);
        // Selector expandido com novos algoritmos
        bool DrawAdvancedCurveSelector(const char* label, SmoothCurveConfig& config);
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