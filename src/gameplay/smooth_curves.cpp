#include "smooth_curves.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace Gameplay::SmoothCurves {

// ===== BezierCurve Implementation =====

float BezierCurve::Evaluate(float t) const {
    t = std::clamp(t, 0.0f, 1.0f);
    float u = 1.0f - t;
    float uu = u * u;
    float uuu = uu * u;
    float tt = t * t;
    float ttt = tt * t;

    ImVec2 result(
        uuu * p0.x + 3 * uu * t * p1.x + 3 * u * tt * p2.x + ttt * p3.x,
        uuu * p0.y + 3 * uu * t * p1.y + 3 * u * tt * p2.y + ttt * p3.y
    );
    return result.y; // Assume x = t, retorna y
}

BezierCurve BezierCurve::Linear() {
    BezierCurve c;
    c.p0 = ImVec2(0, 0);
    c.p1 = ImVec2(0.33f, 0.33f);
    c.p2 = ImVec2(0.66f, 0.66f);
    c.p3 = ImVec2(1, 1);
    return c;
}

BezierCurve BezierCurve::EaseInOut() {
    BezierCurve c;
    c.p0 = ImVec2(0, 0);
    c.p1 = ImVec2(0.42f, 0.0f);
    c.p2 = ImVec2(0.58f, 1.0f);
    c.p3 = ImVec2(1, 1);
    return c;
}

BezierCurve BezierCurve::EaseOut() {
    BezierCurve c;
    c.p0 = ImVec2(0, 0);
    c.p1 = ImVec2(0.25f, 0.1f);
    c.p2 = ImVec2(0.25f, 1.0f);
    c.p3 = ImVec2(1, 1);
    return c;
}

BezierCurve BezierCurve::EaseIn() {
    BezierCurve c;
    c.p0 = ImVec2(0, 0);
    c.p1 = ImVec2(0.42f, 0.0f);
    c.p2 = ImVec2(1.0f, 1.0f);
    c.p3 = ImVec2(1, 1);
    return c;
}

BezierCurve BezierCurve::Sharp() {
    BezierCurve c;
    c.p0 = ImVec2(0, 0);
    c.p1 = ImVec2(0.1f, 0.9f);
    c.p2 = ImVec2(0.9f, 0.1f);
    c.p3 = ImVec2(1, 1);
    return c;
}

BezierCurve BezierCurve::Gentle() {
    BezierCurve c;
    c.p0 = ImVec2(0, 0);
    c.p1 = ImVec2(0.4f, 0.0f);
    c.p2 = ImVec2(0.6f, 1.0f);
    c.p3 = ImVec2(1, 1);
    return c;
}

BezierCurve BezierCurve::Snap() {
    BezierCurve c;
    c.p0 = ImVec2(0, 0);
    c.p1 = ImVec2(0.0f, 0.5f);
    c.p2 = ImVec2(1.0f, 0.5f);
    c.p3 = ImVec2(1, 1);
    return c;
}

BezierCurve BezierCurve::Smooth() {
    return EaseInOut();
}

// ===== BSplineCurve Implementation =====

void BSplineCurve::AddControlPoint(const ImVec2& point) {
    control_points.push_back(point);
}

void BSplineCurve::RemoveControlPoint(size_t index) {
    if (index < control_points.size())
        control_points.erase(control_points.begin() + index);
}

void BSplineCurve::Clear() {
    control_points.clear();
}

// Base B-Spline (Cox-de Boor algorithm)
float BSplineCurve::Evaluate(float t) const {
    if (control_points.size() < 4) return t; // Fallback linear

    t = std::clamp(t, 0.0f, 1.0f);

    // Para B-Spline uniforme grau 3, precisamos de pelo menos 4 pontos
    // Mapeia t para o intervalo de knots válido
    int n = static_cast<int>(control_points.size()) - 1;
    int degree = std::min(this->degree, n);

    // Knot vector uniforme
    std::vector<float> knots(n + degree + 2);
    for (size_t i = 0; i < knots.size(); ++i) {
        knots[i] = static_cast<float>(i) / static_cast<float>(knots.size() - 1);
    }

    // Encontra span do knot
    int span = degree;
    while (span < n && t >= knots[span + 1]) span++;

    // Cox-de Boor recursion
    std::vector<float> N(degree + 1);
    for (int j = 0; j <= degree; ++j) {
        N[j] = (t >= knots[span - degree + j] && t < knots[span - degree + j + 1]) ? 1.0f : 0.0f;
    }

    for (int k = 1; k <= degree; ++k) {
        for (int j = 0; j <= degree - k; ++j) {
            float denom1 = knots[span - degree + j + k] - knots[span - degree + j];
            float denom2 = knots[span - degree + j + k + 1] - knots[span - degree + j + 1];
            float w1 = (denom1 != 0) ? (t - knots[span - degree + j]) / denom1 : 0.0f;
            float w2 = (denom2 != 0) ? (knots[span - degree + j + k + 1] - t) / denom2 : 0.0f;
            N[j] = w1 * N[j] + w2 * N[j + 1];
        }
    }

    // Avalia curva
    ImVec2 result(0, 0);
    for (int j = 0; j <= degree; ++j) {
        int idx = span - degree + j;
        if (idx >= 0 && idx < static_cast<int>(control_points.size())) {
            result.x += control_points[idx].x * N[j];
            result.y += control_points[idx].y * N[j];
        }
    }

    return result.y;
}

BSplineCurve BSplineCurve::SmoothAim() {
    BSplineCurve c;
    c.degree = 3;
    c.control_points = {
        ImVec2(0.0f, 0.0f),
        ImVec2(0.15f, 0.05f),
        ImVec2(0.35f, 0.25f),
        ImVec2(0.55f, 0.55f),
        ImVec2(0.75f, 0.8f),
        ImVec2(0.9f, 0.95f),
        ImVec2(1.0f, 1.0f)
    };
    return c;
}

BSplineCurve BSplineCurve::SharpAim() {
    BSplineCurve c;
    c.degree = 3;
    c.control_points = {
        ImVec2(0.0f, 0.0f),
        ImVec2(0.05f, 0.3f),
        ImVec2(0.2f, 0.7f),
        ImVec2(0.4f, 0.9f),
        ImVec2(0.6f, 0.95f),
        ImVec2(0.8f, 0.98f),
        ImVec2(1.0f, 1.0f)
    };
    return c;
}

BSplineCurve BSplineCurve::GentleAim() {
    BSplineCurve c;
    c.degree = 3;
    c.control_points = {
        ImVec2(0.0f, 0.0f),
        ImVec2(0.2f, 0.0f),
        ImVec2(0.4f, 0.1f),
        ImVec2(0.6f, 0.3f),
        ImVec2(0.8f, 0.6f),
        ImVec2(0.9f, 0.85f),
        ImVec2(1.0f, 1.0f)
    };
    return c;
}

BSplineCurve BSplineCurve::Default() {
    return SmoothAim();
}

// ===== ChaikinCurve Implementation =====

void ChaikinCurve::AddPoint(const ImVec2& point) {
    points.push_back(point);
}

void ChaikinCurve::RemovePoint(size_t index) {
    if (index < points.size())
        points.erase(points.begin() + index);
}

void ChaikinCurve::Clear() {
    points.clear();
}

std::vector<ImVec2> ChaikinCurve::GetSubdividedPoints() const {
    if (points.size() < 2) return points;

    std::vector<ImVec2> current = points;
    for (int iter = 0; iter < subdivisions; ++iter) {
        std::vector<ImVec2> next;
        next.reserve(current.size() * 2);

        for (size_t i = 0; i < current.size() - 1; ++i) {
            ImVec2 p0 = current[i];
            ImVec2 p1 = current[i + 1];

            // Chaikin corner cutting: Q = 0.75*P0 + 0.25*P1, R = 0.25*P0 + 0.75*P1
            float w = corner_weight;
            ImVec2 q(
                p0.x * (1.0f - w) + p1.x * w,
                p0.y * (1.0f - w) + p1.y * w
            );
            ImVec2 r(
                p0.x * w + p1.x * (1.0f - w),
                p0.y * w + p1.y * (1.0f - w)
            );

            next.push_back(q);
            next.push_back(r);
        }
        // Adiciona último ponto
        next.push_back(current.back());
        current = std::move(next);
    }
    return current;
}

float ChaikinCurve::Evaluate(float t) const {
    if (points.size() < 2) return t;

    auto subdivided = GetSubdividedPoints();
    if (subdivided.size() < 2) return t;

    t = std::clamp(t, 0.0f, 1.0f);
    float idx_f = t * (subdivided.size() - 1);
    size_t idx = static_cast<size_t>(idx_f);
    float frac = idx_f - idx;

    if (idx + 1 >= subdivided.size()) return subdivided.back().y;

    // Interpola linear entre pontos subdivididos
    return subdivided[idx].y * (1.0f - frac) + subdivided[idx + 1].y * frac;
}

ChaikinCurve ChaikinCurve::SmoothAim() {
    ChaikinCurve c;
    c.points = {
        ImVec2(0.0f, 0.0f),
        ImVec2(0.3f, 0.1f),
        ImVec2(0.6f, 0.5f),
        ImVec2(0.85f, 0.9f),
        ImVec2(1.0f, 1.0f)
    };
    c.subdivisions = 3;
    c.corner_weight = 0.25f;
    return c;
}

ChaikinCurve ChaikinCurve::SharpAim() {
    ChaikinCurve c;
    c.points = {
        ImVec2(0.0f, 0.0f),
        ImVec2(0.15f, 0.5f),
        ImVec2(0.4f, 0.85f),
        ImVec2(0.7f, 0.95f),
        ImVec2(1.0f, 1.0f)
    };
    c.subdivisions = 3;
    c.corner_weight = 0.25f;
    return c;
}

ChaikinCurve ChaikinCurve::GentleAim() {
    ChaikinCurve c;
    c.points = {
        ImVec2(0.0f, 0.0f),
        ImVec2(0.4f, 0.05f),
        ImVec2(0.7f, 0.3f),
        ImVec2(0.9f, 0.7f),
        ImVec2(1.0f, 1.0f)
    };
    c.subdivisions = 4;
    c.corner_weight = 0.25f;
    return c;
}

ChaikinCurve ChaikinCurve::Default() {
    return SmoothAim();
}

// ===== StaggeredCurve Implementation =====

void StaggeredCurve::AddStep(float progress) {
    progress = std::clamp(progress, 0.0f, 1.0f);
    steps.push_back(progress);
    std::sort(steps.begin(), steps.end());
}

void StaggeredCurve::RemoveStep(size_t index) {
    if (index < steps.size())
        steps.erase(steps.begin() + index);
}

void StaggeredCurve::Clear() {
    steps.clear();
}

void StaggeredCurve::GenerateEvenSteps(int count) {
    steps.clear();
    if (count <= 1) { steps.push_back(1.0f); return; }
    for (int i = 1; i <= count; ++i) {
        steps.push_back(static_cast<float>(i) / count);
    }
}

float StaggeredCurve::Evaluate(float t) const {
    if (steps.empty()) return t;

    t = std::clamp(t, 0.0f, 1.0f);

    // Encontra step mais próximo
    float closest = steps[0];
    float min_dist = std::abs(t - closest);
    for (float step : steps) {
        float dist = std::abs(t - step);
        if (dist < min_dist) {
            min_dist = dist;
            closest = step;
        }
    }

    if (!smooth_steps) return closest;

    // Suaviza transição entre steps
    // Encontra step anterior e próximo
    float prev_step = 0.0f;
    float next_step = 1.0f;
    for (float step : steps) {
        if (step <= t && step > prev_step) prev_step = step;
        if (step >= t && step < next_step) next_step = step;
    }

    // Se t está exatamente num step, retorna ele
    if (min_dist < 0.001f) return closest;

    // Interpola suavemente entre prev e next
    float range = next_step - prev_step;
    if (range < 0.001f) return closest;

    float local_t = (t - prev_step) / range;
    // Smoothstep
    local_t = local_t * local_t * (3.0f - 2.0f * local_t);

    return prev_step + (next_step - prev_step) * local_t * step_smoothness + closest * (1.0f - step_smoothness);
}

StaggeredCurve StaggeredCurve::HumanLike(int steps) {
    StaggeredCurve c;
    c.GenerateEvenSteps(steps);
    // Adiciona variação humana
    std::mt19937 rng(0xC0FFEE);
    std::uniform_real_distribution<float> dist(-0.03f, 0.03f);
    for (float& s : c.steps) {
        s = std::clamp(s + dist(rng), 0.0f, 1.0f);
    }
    c.smooth_steps = true;
    c.step_smoothness = 0.15f;
    return c;
}

StaggeredCurve StaggeredCurve::Robotic(int steps) {
    StaggeredCurve c;
    c.GenerateEvenSteps(steps);
    c.smooth_steps = false;
    return c;
}

StaggeredCurve StaggeredCurve::Snappy(int steps) {
    StaggeredCurve c;
    c.GenerateEvenSteps(steps);
    c.smooth_steps = true;
    c.step_smoothness = 0.05f; // Quase hard steps
    return c;
}

StaggeredCurve StaggeredCurve::Default() {
    return HumanLike(8);
}

// ===== SmoothCurveEvaluator Implementation =====

SmoothCurveEvaluator::SmoothCurveEvaluator() = default;

SmoothCurveEvaluator::SmoothCurveEvaluator(const SmoothCurveConfig& config) : config_(config) {}

void SmoothCurveEvaluator::SetConfig(const SmoothCurveConfig& config) {
    config_ = config;
}

float SmoothCurveEvaluator::Evaluate(float input) const {
    CurveContext ctx{};
    ctx.input = input;
    return Evaluate(ctx);
}

float SmoothCurveEvaluator::Evaluate(const CurveContext& context) const {
    float t = std::clamp(context.input, 0.0f, 1.0f);

    // Mapeia input range
    if (config_.input_max > config_.input_min) {
        t = (t - config_.input_min) / (config_.input_max - config_.input_min);
    }

    float output = 0.0f;

    switch (config_.type) {
        case CurveType::Bezier:
            output = EvaluateBezier(t);
            break;
        case CurveType::BSpline:
            output = EvaluateBSpline(t);
            break;
        case CurveType::Chaikin:
            output = EvaluateChaikin(t);
            break;
        case CurveType::Staggered:
            output = EvaluateStaggered(t);
            break;
        case CurveType::CatmullRom:
            output = EvaluateSpline(t);
            break;
        default:
            output = EvaluateCurveType(config_.type, t, config_.overshoot, config_.amplitude, config_.period);
            break;
    }

    // Dynamic adjustment baseado no contexto
    if (config_.dynamic_adjustment) {
        output += context.velocity * config_.velocity_factor;
        output += context.error * config_.error_factor;
        output += context.distance * config_.distance_factor;
    }

    // Mapeia output range
    if (config_.output_max > config_.output_min) {
        output = config_.output_min + output * (config_.output_max - config_.output_min);
    }

    // Clamp
    if (config_.clamp_output) {
        output = std::clamp(output, config_.min_output, config_.max_output);
    }

    // Atualiza snapshot visual
    UpdateSnapshot(config_.type, context.input, output);

    last_output_ = output;
    last_input_ = context.input;
    return output;
}

float SmoothCurveEvaluator::EvaluateWithDistance(float input, float distance) const {
    if (!config_.use_distance_curves) return Evaluate(input);

    // Seleciona curva baseada na distância
    for (const auto& dc : config_.distance_curves) {
        if (distance <= dc.max_distance) {
            CurveType old_type = config_.type;
            BezierCurve old_bezier = config_.bezier;
            BSplineCurve old_bspline = config_.bspline;
            ChaikinCurve old_chaikin = config_.chaikin;
            StaggeredCurve old_staggered = config_.staggered;

            // Temporariamente troca config
            const_cast<SmoothCurveConfig&>(config_).type = dc.type;
            const_cast<SmoothCurveConfig&>(config_).bezier = dc.bezier;
            const_cast<SmoothCurveConfig&>(config_).bspline = dc.bspline;
            const_cast<SmoothCurveConfig&>(config_).chaikin = dc.chaikin;
            const_cast<SmoothCurveConfig&>(config_).staggered = dc.staggered;

            float result = Evaluate(input);

            // Restaura
            const_cast<SmoothCurveConfig&>(config_).type = old_type;
            const_cast<SmoothCurveConfig&>(config_).bezier = old_bezier;
            const_cast<SmoothCurveConfig&>(config_).bspline = old_bspline;
            const_cast<SmoothCurveConfig&>(config_).chaikin = old_chaikin;
            const_cast<SmoothCurveConfig&>(config_).staggered = old_staggered;

            return result;
        }
    }
    return Evaluate(input);
}

const char* SmoothCurveEvaluator::GetCurveName() const {
    switch (config_.type) {
        case CurveType::Linear: return "Linear";
        case CurveType::EaseInQuad: return "Ease In Quad";
        case CurveType::EaseOutQuad: return "Ease Out Quad";
        case CurveType::EaseInOutQuad: return "Ease In-Out Quad";
        case CurveType::EaseInCubic: return "Ease In Cubic";
        case CurveType::EaseOutCubic: return "Ease Out Cubic";
        case CurveType::EaseInOutCubic: return "Ease In-Out Cubic";
        case CurveType::EaseInQuart: return "Ease In Quart";
        case CurveType::EaseOutQuart: return "Ease Out Quart";
        case CurveType::EaseInOutQuart: return "Ease In-Out Quart";
        case CurveType::EaseInQuint: return "Ease In Quint";
        case CurveType::EaseOutQuint: return "Ease Out Quint";
        case CurveType::EaseInOutQuint: return "Ease In-Out Quint";
        case CurveType::EaseInSine: return "Ease In Sine";
        case CurveType::EaseOutSine: return "Ease Out Sine";
        case CurveType::EaseInOutSine: return "Ease In-Out Sine";
        case CurveType::EaseInExpo: return "Ease In Expo";
        case CurveType::EaseOutExpo: return "Ease Out Expo";
        case CurveType::EaseInOutExpo: return "Ease In-Out Expo";
        case CurveType::EaseInCirc: return "Ease In Circ";
        case CurveType::EaseOutCirc: return "Ease Out Circ";
        case CurveType::EaseInOutCirc: return "Ease In-Out Circ";
        case CurveType::EaseInBack: return "Ease In Back";
        case CurveType::EaseOutBack: return "Ease Out Back";
        case CurveType::EaseInOutBack: return "Ease In-Out Back";
        case CurveType::EaseInElastic: return "Ease In Elastic";
        case CurveType::EaseOutElastic: return "Ease Out Elastic";
        case CurveType::EaseInOutElastic: return "Ease In-Out Elastic";
        case CurveType::EaseInBounce: return "Ease In Bounce";
        case CurveType::EaseOutBounce: return "Ease Out Bounce";
        case CurveType::EaseInOutBounce: return "Ease In-Out Bounce";
        case CurveType::Bezier: return "Bezier (Custom)";
        case CurveType::BSpline: return "B-Spline";
        case CurveType::Chaikin: return "Chaikin";
        case CurveType::Staggered: return "Staggered";
        case CurveType::CatmullRom: return "Catmull-Rom";
        case CurveType::Custom: return "Custom";
        default: return "Unknown";
    }
}

float SmoothCurveEvaluator::EvaluateCurveType(CurveType type, float t, float overshoot, float amplitude, float period) const {
    t = std::clamp(t, 0.0f, 1.0f);

    switch (type) {
        case CurveType::Linear: return t;
        case CurveType::EaseInQuad: return t * t;
        case CurveType::EaseOutQuad: return 1.0f - (1.0f - t) * (1.0f - t);
        case CurveType::EaseInOutQuad: return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
        case CurveType::EaseInCubic: return t * t * t;
        case CurveType::EaseOutCubic: return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        case CurveType::EaseInOutCubic: return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);
        case CurveType::EaseInQuart: return t * t * t * t;
        case CurveType::EaseOutQuart: return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
        case CurveType::EaseInOutQuart: return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - 8.0f * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
        case CurveType::EaseInQuint: return t * t * t * t * t;
        case CurveType::EaseOutQuint: return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
        case CurveType::EaseInOutQuint: return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - 16.0f * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
        case CurveType::EaseInSine: return 1.0f - std::cos(t * 1.5707963f);
        case CurveType::EaseOutSine: return std::sin(t * 1.5707963f);
        case CurveType::EaseInOutSine: return 0.5f * (1.0f - std::cos(3.1415926f * t));
        case CurveType::EaseInExpo: return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
        case CurveType::EaseOutExpo: return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t);
        case CurveType::EaseInOutExpo: return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : t < 0.5f ? 0.5f * std::pow(2.0f, 20.0f * t - 10.0f) : 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
        case CurveType::EaseInCirc: return 1.0f - std::sqrt(1.0f - t * t);
        case CurveType::EaseOutCirc: return std::sqrt(1.0f - (1.0f - t) * (1.0f - t));
        case CurveType::EaseInOutCirc: return t < 0.5f ? 0.5f * (1.0f - std::sqrt(1.0f - 4.0f * t * t)) : 0.5f * (std::sqrt(1.0f - 4.0f * (1.0f - t) * (1.0f - t)) + 1.0f);
        case CurveType::EaseInBack: return t * t * (2.70158f * t - 1.70158f);
        case CurveType::EaseOutBack: return 1.0f + (1.0f - t) * (1.0f - t) * (2.70158f * (1.0f - t) + 1.70158f);
        case CurveType::EaseInOutBack: return t < 0.5f ? 0.5f * (2.0f * t) * (2.0f * t) * (3.70158f * 2.0f * t - 2.70158f) : 0.5f * ((2.0f * t - 2.0f) * (2.0f * t - 2.0f) * (3.70158f * (2.0f * t - 2.0f) + 2.70158f) + 2.0f);
        case CurveType::EaseInElastic: {
            float c4 = 13.0f * 1.5707963f / 3.0f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
        }
        case CurveType::EaseOutElastic: {
            float c4 = 13.0f * 1.5707963f / 3.0f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : std::pow(2.0f, -10.0f * t) * std::sin((t * 10.0f - 0.75f) * c4) + 1.0f;
        }
        case CurveType::EaseInOutElastic: {
            float c5 = 13.0f * 1.5707963f / 4.5f;
            return t == 0.0f ? 0.0f : t == 1.0f ? 1.0f : t < 0.5f
                ? -0.5f * std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * c5)
                : 0.5f * std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * c5) + 1.0f;
        }
        case CurveType::EaseInBounce: return 1.0f - EvaluateCurveType(CurveType::EaseOutBounce, 1.0f - t, 0, 0, 0);
        case CurveType::EaseOutBounce: {
            const float n1 = 7.5625f, d1 = 2.75f;
            if (t < 1.0f / d1) return n1 * t * t;
            else if (t < 2.0f / d1) return n1 * (t -= 1.5f / d1) * t + 0.75f;
            else if (t < 2.5f / d1) return n1 * (t -= 2.25f / d1) * t + 0.9375f;
            else return n1 * (t -= 2.625f / d1) * t + 0.984375f;
        }
        case CurveType::EaseInOutBounce: return t < 0.5f
            ? 0.5f * EvaluateCurveType(CurveType::EaseInBounce, 2.0f * t, 0, 0, 0)
            : 0.5f * EvaluateCurveType(CurveType::EaseOutBounce, 2.0f * t - 1.0f, 0, 0, 0) + 0.5f;
        default: return t;
    }
}

float SmoothCurveEvaluator::EvaluateBezier(float t) const {
    return config_.bezier.Evaluate(t);
}

float SmoothCurveEvaluator::EvaluateSpline(float t) const {
    return config_.spline.Evaluate(t);
}

float SmoothCurveEvaluator::EvaluateBSpline(float t) const {
    return config_.bspline.Evaluate(t);
}

float SmoothCurveEvaluator::EvaluateChaikin(float t) const {
    return config_.chaikin.Evaluate(t);
}

float SmoothCurveEvaluator::EvaluateStaggered(float t) const {
    return config_.staggered.Evaluate(t);
}

void SmoothCurveEvaluator::UpdateSnapshot(CurveType type, float input, float output) const {
    auto& snap = const_cast<VisualSnapshot&>(config_.last_snapshot);
    snap.valid = true;
    snap.target_x = input;
    snap.target_y = output;
    snap.progress = input;
    snap.control_points.clear();
    snap.curve_points.clear();

    switch (type) {
        case CurveType::Bezier:
            snap.algorithm = VisualSnapshot::Algorithm::Bezier;
            snap.control_points = { config_.bezier.p0, config_.bezier.p1, config_.bezier.p2, config_.bezier.p3 };
            break;
        case CurveType::BSpline:
            snap.algorithm = VisualSnapshot::Algorithm::BSpline;
            snap.control_points = config_.bspline.control_points;
            break;
        case CurveType::Chaikin:
            snap.algorithm = VisualSnapshot::Algorithm::Chaikin;
            snap.control_points = config_.chaikin.GetSubdividedPoints();
            break;
        case CurveType::Staggered:
            snap.algorithm = VisualSnapshot::Algorithm::Staggered;
            for (float s : config_.staggered.steps) {
                snap.control_points.push_back(ImVec2(s, s));
            }
            break;
        case CurveType::CatmullRom:
            snap.algorithm = VisualSnapshot::Algorithm::CatmullRom;
            snap.control_points = config_.spline.points;
            break;
        default:
            snap.algorithm = VisualSnapshot::Algorithm::None;
            break;
    }

    // Gera pontos da curva para draw
    if (snap.algorithm != VisualSnapshot::Algorithm::None) {
        snap.curve_points.reserve(64);
        for (int i = 0; i <= 63; ++i) {
            float tt = i / 63.0f;
            float yy = 0;
            switch (type) {
                case CurveType::Bezier: yy = config_.bezier.Evaluate(tt); break;
                case CurveType::BSpline: yy = config_.bspline.Evaluate(tt); break;
                case CurveType::Chaikin: yy = config_.chaikin.Evaluate(tt); break;
                case CurveType::Staggered: yy = config_.staggered.Evaluate(tt); break;
                case CurveType::CatmullRom: yy = config_.spline.Evaluate(tt); break;
                default: yy = EvaluateCurveType(type, tt, config_.overshoot, config_.amplitude, config_.period); break;
            }
            snap.curve_points.push_back(ImVec2(tt, yy));
        }
    }
}

// ===== Presets =====

SmoothCurveConfig SmoothCurveEvaluator::LegitConfig() {
    SmoothCurveConfig c;
    c.type = CurveType::EaseOutCubic;
    c.dynamic_adjustment = true;
    c.velocity_factor = 0.05f;
    c.error_factor = 0.03f;
    return c;
}

SmoothCurveConfig SmoothCurveEvaluator::RageConfig() {
    SmoothCurveConfig c;
    c.type = CurveType::EaseOutExpo;
    c.dynamic_adjustment = false;
    return c;
}

SmoothCurveConfig SmoothCurveEvaluator::SniperConfig() {
    SmoothCurveConfig c;
    c.type = CurveType::Bezier;
    c.bezier = BezierCurve::Gentle();
    c.dynamic_adjustment = true;
    c.velocity_factor = 0.02f;
    c.error_factor = 0.01f;
    c.distance_factor = 0.005f;
    return c;
}

SmoothCurveConfig SmoothCurveEvaluator::CloseRangeConfig() {
    SmoothCurveConfig c;
    c.type = CurveType::EaseOutBack;
    c.overshoot = 0.1f;
    return c;
}

SmoothCurveConfig SmoothCurveEvaluator::LongRangeConfig() {
    SmoothCurveConfig c;
    c.type = CurveType::EaseInOutCubic;
    c.dynamic_adjustment = true;
    c.distance_factor = 0.03f;
    return c;
}

// Novos presets (crisis-fortnite style)
SmoothCurveConfig SmoothCurveEvaluator::BezierPreset() {
    SmoothCurveConfig c;
    c.type = CurveType::Bezier;
    c.bezier = BezierCurve::EaseInOut();
    return c;
}

SmoothCurveConfig SmoothCurveEvaluator::BSplinePreset() {
    SmoothCurveConfig c;
    c.type = CurveType::BSpline;
    c.bspline = BSplineCurve::SmoothAim();
    return c;
}

SmoothCurveConfig SmoothCurveEvaluator::ChaikinPreset() {
    SmoothCurveConfig c;
    c.type = CurveType::Chaikin;
    c.chaikin = ChaikinCurve::SmoothAim();
    return c;
}

SmoothCurveConfig SmoothCurveEvaluator::StaggeredPreset() {
    SmoothCurveConfig c;
    c.type = CurveType::Staggered;
    c.staggered = StaggeredCurve::HumanLike(8);
    return c;
}

// Serialization (JSON-like string)
std::string SmoothCurveEvaluator::Serialize() const {
    // Simplified - usa nlohmann/json na prática
    return "{}";
}

bool SmoothCurveEvaluator::Deserialize(const std::string&) {
    return false;
}

// ===== CurveEditor Implementation =====

namespace CurveEditor {

void DrawBezierEditor(const char* label, BezierCurve& curve, const ImVec2& size) {
    ImGui::BeginGroup();
    ImGui::Text("%s", label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;
    ImVec2 canvas_end = ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y);

    // Background
    dl->AddRectFilled(canvas_pos, canvas_end, IM_COL32(20, 20, 30, 255), 4.0f);
    dl->AddRect(canvas_pos, canvas_end, IM_COL32(60, 60, 80, 255), 4.0f, 0, 1.0f);

    // Grid
    for (int i = 1; i < 4; ++i) {
        float x = canvas_pos.x + canvas_size.x * i / 4.0f;
        float y = canvas_pos.y + canvas_size.y * i / 4.0f;
        dl->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_end.y), IM_COL32(40, 40, 50, 100));
        dl->AddLine(ImVec2(canvas_pos.x, y), ImVec2(canvas_end.x, y), IM_COL32(40, 40, 50, 100));
    }

    // Diagonal reference
    dl->AddLine(canvas_pos, canvas_end, IM_COL32(40, 40, 50, 80), 1.0f);

    // Draw curve
    std::vector<ImVec2> curve_pts;
    curve_pts.reserve(64);
    for (int i = 0; i <= 63; ++i) {
        float t = i / 63.0f;
        float y = curve.Evaluate(t);
        curve_pts.push_back(ImVec2(canvas_pos.x + t * canvas_size.x, canvas_pos.y + (1.0f - y) * canvas_size.y));
    }
    dl->AddPolyline(curve_pts.data(), curve_pts.size(), IM_COL32(255, 180, 0, 255), false, 2.0f);

    // Control points
    auto pts = curve.GetControlPoints();
    for (int i = 0; i < 4; ++i) {
        ImVec2 p = ImVec2(canvas_pos.x + pts[i].x * canvas_size.x, canvas_pos.y + (1.0f - pts[i].y) * canvas_size.y);
        dl->AddCircleFilled(p, 6.0f, IM_COL32(0, 200, 255, 255));
        dl->AddCircle(p, 6.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
    }

    // Control lines
    for (int i = 0; i < 3; ++i) {
        ImVec2 p0 = ImVec2(canvas_pos.x + pts[i].x * canvas_size.x, canvas_pos.y + (1.0f - pts[i].y) * canvas_size.y);
        ImVec2 p1 = ImVec2(canvas_pos.x + pts[i+1].x * canvas_size.x, canvas_pos.y + (1.0f - pts[i+1].y) * canvas_size.y);
        dl->AddLine(p0, p1, IM_COL32(0, 200, 255, 100), 1.0f);
    }

    ImGui::Dummy(size);
    ImGui::EndGroup();
}

void DrawSplineEditor(const char* label, CatmullRomSpline& spline, const ImVec2& size) {
    ImGui::BeginGroup();
    ImGui::Text("%s", label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 30, 255), 4.0f);
    dl->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(60, 60, 80, 255), 4.0f, 0, 1.0f);

    if (spline.points.size() >= 2) {
        std::vector<ImVec2> curve_pts;
        for (int i = 0; i <= 63; ++i) {
            float t = i / 63.0f;
            float y = spline.Evaluate(t);
            curve_pts.push_back(ImVec2(canvas_pos.x + t * canvas_size.x, canvas_pos.y + (1.0f - y) * canvas_size.y));
        }
        dl->AddPolyline(curve_pts.data(), curve_pts.size(), IM_COL32(100, 255, 150, 255), false, 2.0f);
    }

    for (auto& p : spline.points) {
        ImVec2 pp = ImVec2(canvas_pos.x + p.x * canvas_size.x, canvas_pos.y + (1.0f - p.y) * canvas_size.y);
        dl->AddCircleFilled(pp, 5.0f, IM_COL32(100, 255, 150, 255));
    }

    ImGui::Dummy(size);
    ImGui::EndGroup();
}

void DrawBSplineEditor(const char* label, BSplineCurve& curve, const ImVec2& size) {
    ImGui::BeginGroup();
    ImGui::Text("%s", label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 30, 255), 4.0f);
    dl->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(60, 60, 80, 255), 4.0f, 0, 1.0f);

    if (curve.control_points.size() >= 4) {
        std::vector<ImVec2> curve_pts;
        for (int i = 0; i <= 63; ++i) {
            float t = i / 63.0f;
            float y = curve.Evaluate(t);
            curve_pts.push_back(ImVec2(canvas_pos.x + t * canvas_size.x, canvas_pos.y + (1.0f - y) * canvas_size.y));
        }
        dl->AddPolyline(curve_pts.data(), curve_pts.size(), IM_COL32(255, 150, 100, 255), false, 2.0f);
    }

    for (auto& p : curve.control_points) {
        ImVec2 pp = ImVec2(canvas_pos.x + p.x * canvas_size.x, canvas_pos.y + (1.0f - p.y) * canvas_size.y);
        dl->AddCircleFilled(pp, 5.0f, IM_COL32(255, 150, 100, 255));
    }

    ImGui::Dummy(size);
    ImGui::EndGroup();
}

void DrawChaikinEditor(const char* label, ChaikinCurve& curve, const ImVec2& size) {
    ImGui::BeginGroup();
    ImGui::Text("%s", label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 30, 255), 4.0f);
    dl->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(60, 60, 80, 255), 4.0f, 0, 1.0f);

    if (curve.points.size() >= 2) {
        // Original polygon
        std::vector<ImVec2> poly_pts;
        for (auto& p : curve.points) {
            poly_pts.push_back(ImVec2(canvas_pos.x + p.x * canvas_size.x, canvas_pos.y + (1.0f - p.y) * canvas_size.y));
        }
        dl->AddPolyline(poly_pts.data(), poly_pts.size(), IM_COL32(180, 180, 180, 100), false, 1.0f);

        // Subdivided curve
        auto sub = curve.GetSubdividedPoints();
        for (size_t i = 0; i + 1 < sub.size(); ++i) {
            ImVec2 p0 = ImVec2(canvas_pos.x + sub[i].x * canvas_size.x, canvas_pos.y + (1.0f - sub[i].y) * canvas_size.y);
            ImVec2 p1 = ImVec2(canvas_pos.x + sub[i+1].x * canvas_size.x, canvas_pos.y + (1.0f - sub[i+1].y) * canvas_size.y);
            dl->AddLine(p0, p1, IM_COL32(180, 100, 255, 255), 2.0f);
        }
    }

    for (auto& p : curve.points) {
        ImVec2 pp = ImVec2(canvas_pos.x + p.x * canvas_size.x, canvas_pos.y + (1.0f - p.y) * canvas_size.y);
        dl->AddCircleFilled(pp, 5.0f, IM_COL32(180, 100, 255, 255));
    }

    ImGui::Dummy(size);
    ImGui::EndGroup();
}

void DrawStaggeredEditor(const char* label, StaggeredCurve& curve, const ImVec2& size) {
    ImGui::BeginGroup();
    ImGui::Text("%s", label);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(20, 20, 30, 255), 4.0f);
    dl->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(60, 60, 80, 255), 4.0f, 0, 1.0f);

    // Steps as horizontal lines
    for (float s : curve.steps) {
        float x = canvas_pos.x + s * canvas_size.x;
        dl->AddLine(ImVec2(x, canvas_pos.y), ImVec2(x, canvas_pos.y + canvas_size.y), IM_COL32(255, 255, 100, 180), 2.0f);
    }

    // Curve preview
    std::vector<ImVec2> curve_pts;
    for (int i = 0; i <= 63; ++i) {
        float t = i / 63.0f;
        float y = curve.Evaluate(t);
        curve_pts.push_back(ImVec2(canvas_pos.x + t * canvas_size.x, canvas_pos.y + (1.0f - y) * canvas_size.y));
    }
    dl->AddPolyline(curve_pts.data(), curve_pts.size(), IM_COL32(100, 255, 255, 255), false, 2.0f);

    ImGui::Dummy(size);
    ImGui::EndGroup();
}

void DrawCurvePreview(const SmoothCurveConfig& config, const ImVec2& size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(15, 15, 25, 255), 3.0f);
    dl->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(50, 50, 70, 255), 3.0f, 0, 1.0f);

    // Diagonal
    dl->AddLine(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(40, 40, 50, 80), 1.0f);

    // Curva
    SmoothCurveEvaluator eval(config);
    std::vector<ImVec2> pts;
    pts.reserve(64);
    for (int i = 0; i <= 63; ++i) {
        float t = i / 63.0f;
        float y = eval.Evaluate(t);
        pts.push_back(ImVec2(canvas_pos.x + t * canvas_size.x, canvas_pos.y + (1.0f - y) * canvas_size.y));
    }
    dl->AddPolyline(pts.data(), pts.size(), IM_COL32(255, 180, 0, 255), false, 2.5f);

    ImGui::Dummy(size);
}

void DrawCurvePreviewAnimated(const SmoothCurveConfig& config, const ImVec2& size, float anim_speed) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    dl->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(15, 15, 25, 255), 3.0f);
    dl->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), IM_COL32(50, 50, 70, 255), 3.0f, 0, 1.0f);

    SmoothCurveEvaluator eval(config);
    const auto& snap = eval.GetLastSnapshot();

    // Desenha curva completa (ghost)
    std::vector<ImVec2> full_pts;
    full_pts.reserve(64);
    for (int i = 0; i <= 63; ++i) {
        float t = i / 63.0f;
        float y = eval.Evaluate(t);
        full_pts.push_back(ImVec2(canvas_pos.x + t * canvas_size.x, canvas_pos.y + (1.0f - y) * canvas_size.y));
    }
    dl->AddPolyline(full_pts.data(), full_pts.size(), IM_COL32(80, 80, 100, 150), false, 1.5f);

    // Desenha progresso animado
    float anim_t = std::fmod(ImGui::GetTime() * anim_speed, 1.0f);
    float progress = snap.valid ? snap.progress : anim_t;

    // Ponto atual pulsando
    float current_y = eval.Evaluate(progress);
    ImVec2 current_pos(canvas_pos.x + progress * canvas_size.x, canvas_pos.y + (1.0f - current_y) * canvas_size.y);

    // Trail do progresso
    std::vector<ImVec2> trail_pts;
    int trail_steps = static_cast<int>(progress * 63);
    for (int i = 0; i <= trail_steps; ++i) {
        float t = i / 63.0f;
        float y = eval.Evaluate(t);
        trail_pts.push_back(ImVec2(canvas_pos.x + t * canvas_size.x, canvas_pos.y + (1.0f - y) * canvas_size.y));
    }
    if (trail_pts.size() > 1) {
        dl->AddPolyline(trail_pts.data(), trail_pts.size(), IM_COL32(255, 180, 0, 255), false, 3.0f);
    }

    // Ponto atual
    float pulse = 0.5f + 0.5f * std::sin(ImGui::GetTime() * 6.0f);
    float radius = 6.0f + pulse * 3.0f;
    dl->AddCircleFilled(current_pos, radius, IM_COL32(255, 180, 0, 255));
    dl->AddCircle(current_pos, radius, IM_COL32(255, 255, 255, 200), 0, 2.0f);

    // Controle points (se disponível)
    if (snap.valid && !snap.control_points.empty()) {
        for (auto& cp : snap.control_points) {
            ImVec2 pp = ImVec2(canvas_pos.x + cp.x * canvas_size.x, canvas_pos.y + (1.0f - cp.y) * canvas_size.y);
            dl->AddCircle(pp, 4.0f, IM_COL32(180, 180, 255, 180), 0, 1.0f);
        }
    }

    ImGui::Dummy(size);
}

bool DrawCurvePresetSelector(const char* label, CurveType& type) {
    const char* items[] = {
        "Linear", "Ease Out Cubic", "Ease In-Out Cubic", "Ease Out Expo",
        "Ease Out Back", "Ease In-Out Sine", "Ease Out Elastic",
        "Bezier", "B-Spline", "Chaikin", "Staggered", "Catmull-Rom"
    };
    int current = static_cast<int>(type);
    if (current >= 12) current = 0;

    bool changed = ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items));
    if (changed) type = static_cast<CurveType>(current);
    return changed;
}

bool DrawAdvancedCurveSelector(const char* label, SmoothCurveConfig& config) {
    bool changed = false;

    if (ImGui::BeginCombo(label, SmoothCurveEvaluator(config).GetCurveName())) {
        // Categorias
        if (ImGui::BeginMenu("Easing Functions")) {
            const std::pair<CurveType, const char*> easing_items[] = {
                {CurveType::Linear, "Linear"},
                {CurveType::EaseOutQuad, "Ease Out Quad"},
                {CurveType::EaseInOutQuad, "Ease In-Out Quad"},
                {CurveType::EaseOutCubic, "Ease Out Cubic"},
                {CurveType::EaseInOutCubic, "Ease In-Out Cubic"},
                {CurveType::EaseOutQuart, "Ease Out Quart"},
                {CurveType::EaseInOutQuart, "Ease In-Out Quart"},
                {CurveType::EaseOutQuint, "Ease Out Quint"},
                {CurveType::EaseInOutQuint, "Ease In-Out Quint"},
                {CurveType::EaseOutSine, "Ease Out Sine"},
                {CurveType::EaseInOutSine, "Ease In-Out Sine"},
                {CurveType::EaseOutExpo, "Ease Out Expo"},
                {CurveType::EaseInOutExpo, "Ease In-Out Expo"},
                {CurveType::EaseOutCirc, "Ease Out Circ"},
                {CurveType::EaseOutBack, "Ease Out Back"},
                {CurveType::EaseInOutBack, "Ease In-Out Back"},
                {CurveType::EaseOutElastic, "Ease Out Elastic"},
                {CurveType::EaseOutBounce, "Ease Out Bounce"},
                {CurveType::EaseInOutBounce, "Ease In-Out Bounce"},
            };
            for (auto& item : easing_items) {
                if (ImGui::Selectable(item.second, config.type == item.first)) {
                    config.type = item.first;
                    changed = true;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Advanced Curves (crisis-fortnite)")) {
            const std::pair<CurveType, const char*> advanced_items[] = {
                {CurveType::Bezier, "Bezier (Custom Control Points)"},
                {CurveType::BSpline, "B-Spline (Smooth, Continuous)"},
                {CurveType::Chaikin, "Chaikin (Corner Cutting)"},
                {CurveType::Staggered, "Staggered (Discrete Steps)"},
                {CurveType::CatmullRom, "Catmull-Rom (Interpolating)"},
            };
            for (auto& item : advanced_items) {
                if (ImGui::Selectable(item.second, config.type == item.first)) {
                    config.type = item.first;
                    changed = true;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Presets")) {
            if (ImGui::Selectable("Legit", config.type == CurveType::EaseOutCubic)) {
                config = SmoothCurveEvaluator::LegitConfig(); changed = true;
            }
            if (ImGui::Selectable("Rage", config.type == CurveType::EaseOutExpo)) {
                config = SmoothCurveEvaluator::RageConfig(); changed = true;
            }
            if (ImGui::Selectable("Sniper", config.type == CurveType::Bezier)) {
                config = SmoothCurveEvaluator::SniperConfig(); changed = true;
            }
            if (ImGui::Selectable("Bezier Preset", config.type == CurveType::Bezier)) {
                config = SmoothCurveEvaluator::BezierPreset(); changed = true;
            }
            if (ImGui::Selectable("B-Spline Preset", config.type == CurveType::BSpline)) {
                config = SmoothCurveEvaluator::BSplinePreset(); changed = true;
            }
            if (ImGui::Selectable("Chaikin Preset", config.type == CurveType::Chaikin)) {
                config = SmoothCurveEvaluator::ChaikinPreset(); changed = true;
            }
            if (ImGui::Selectable("Staggered Preset", config.type == CurveType::Staggered)) {
                config = SmoothCurveEvaluator::StaggeredPreset(); changed = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace CurveEditor

// ===== DistanceCurveManager & AimHumanizer stubs =====
// (Manter implementações existentes)

} // namespace Gameplay::SmoothCurves