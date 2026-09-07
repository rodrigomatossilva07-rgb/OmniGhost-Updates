#include "smooth_curves.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <sstream>

namespace Gameplay::SmoothCurves {

    // BezierCurve implementation
    float BezierCurve::Evaluate(float t) const {
        t = std::clamp(t, 0.0f, 1.0f);
        float u = 1.0f - t;
        float u2 = u * u;
        float u3 = u2 * u;
        float t2 = t * t;
        float t3 = t2 * t;
        
        // Y component of cubic bezier
        return p0.y * u3 + 
               3.0f * p1.y * u2 * t + 
               3.0f * p2.y * u * t2 + 
               p3.y * t3;
    }

    BezierCurve BezierCurve::Linear() {
        return BezierCurve{ImVec2(0,0), ImVec2(1.0f/3.0f, 1.0f/3.0f), ImVec2(2.0f/3.0f, 2.0f/3.0f), ImVec2(1,1)};
    }

    BezierCurve BezierCurve::EaseInOut() {
        return BezierCurve{ImVec2(0,0), ImVec2(0.42f, 0.0f), ImVec2(0.58f, 1.0f), ImVec2(1,1)};
    }

    BezierCurve BezierCurve::EaseOut() {
        return BezierCurve{ImVec2(0,0), ImVec2(0.25f, 0.1f), ImVec2(0.25f, 1.0f), ImVec2(1,1)};
    }

    BezierCurve BezierCurve::EaseIn() {
        return BezierCurve{ImVec2(0,0), ImVec2(0.42f, 0.0f), ImVec2(1.0f, 1.0f), ImVec2(1,1)};
    }

    BezierCurve BezierCurve::Sharp() {
        return BezierCurve{ImVec2(0,0), ImVec2(0.1f, 0.0f), ImVec2(0.9f, 1.0f), ImVec2(1,1)};
    }

    BezierCurve BezierCurve::Gentle() {
        return BezierCurve{ImVec2(0,0), ImVec2(0.4f, 0.0f), ImVec2(0.6f, 1.0f), ImVec2(1,1)};
    }

    BezierCurve BezierCurve::Snap() {
        return BezierCurve{ImVec2(0,0), ImVec2(0.05f, 0.0f), ImVec2(0.95f, 1.0f), ImVec2(1,1)};
    }

    BezierCurve BezierCurve::Smooth() {
        return BezierCurve{ImVec2(0,0), ImVec2(0.33f, 0.0f), ImVec2(0.67f, 1.0f), ImVec2(1,1)};
    }

    // CatmullRomSpline implementation
    void CatmullRomSpline::AddPoint(const ImVec2& point) {
        points.push_back(point);
    }

    void CatmullRomSpline::RemovePoint(size_t index) {
        if (index < points.size()) {
            points.erase(points.begin() + index);
        }
    }

    void CatmullRomSpline::Clear() {
        points.clear();
    }

    float CatmullRomSpline::Evaluate(float t) const {
        if (points.size() < 2) return 0.0f;
        if (points.size() == 2) {
            // Linear interpolation for 2 points
            return points[0].y + (points[1].y - points[0].y) * t;
        }
        
        t = std::clamp(t, 0.0f, 1.0f);
        
        // Map t to segment
        size_t num_segments = closed ? points.size() : points.size() - 1;
        float segment_t = t * num_segments;
        size_t segment = static_cast<size_t>(segment_t);
        
        if (!closed && segment >= num_segments) {
            segment = num_segments - 1;
            segment_t = 1.0f;
        } else {
            segment_t = segment_t - segment;
        }
        
        // Get 4 control points for Catmull-Rom
        size_t p0_idx = (segment + points.size() - 1) % points.size();
        size_t p1_idx = segment % points.size();
        size_t p2_idx = (segment + 1) % points.size();
        size_t p3_idx = (segment + 2) % points.size();
        
        if (!closed) {
            if (segment == 0) p0_idx = p1_idx;
            if (segment == num_segments - 1) p3_idx = p2_idx;
        }
        
        const ImVec2& p0 = points[p0_idx];
        const ImVec2& p1 = points[p1_idx];
        const ImVec2& p2 = points[p2_idx];
        const ImVec2& p3 = points[p3_idx];
        
        float t2 = segment_t * segment_t;
        float t3 = t2 * segment_t;
        
        // Catmull-Rom formula
        float result = p1.y * 2.0f +
                       (p2.y - p0.y) * segment_t +
                       (p0.y * 2.0f - p1.y * 5.0f + p2.y * 4.0f - p3.y) * t2 +
                       (p0.y * -1.0f + p1.y * 3.0f - p2.y * 3.0f + p3.y) * t3;
        
        return result * 0.5f * tension + p1.y * (1.0f - tension);
    }

    size_t CatmullRomSpline::SegmentCount() const {
        if (points.size() < 2) return 0;
        return closed ? points.size() : points.size() - 1;
    }

    CatmullRomSpline CatmullRomSpline::SmoothAim() {
        CatmullRomSpline spline;
        spline.points = {ImVec2(0, 0), ImVec2(0.25f, 0.05f), ImVec2(0.5f, 0.3f), ImVec2(0.75f, 0.7f), ImVec2(1.0f, 1.0f)};
        spline.tension = 0.5f;
        return spline;
    }

    CatmullRomSpline CatmullRomSpline::SharpAim() {
        CatmullRomSpline spline;
        spline.points = {ImVec2(0, 0), ImVec2(0.1f, 0.0f), ImVec2(0.3f, 0.1f), ImVec2(0.6f, 0.5f), ImVec2(0.9f, 0.95f), ImVec2(1.0f, 1.0f)};
        spline.tension = 0.5f;
        return spline;
    }

    CatmullRomSpline CatmullRomSpline::GentleAim() {
        CatmullRomSpline spline;
        spline.points = {ImVec2(0, 0), ImVec2(0.3f, 0.0f), ImVec2(0.5f, 0.15f), ImVec2(0.7f, 0.4f), ImVec2(0.9f, 0.8f), ImVec2(1.0f, 1.0f)};
        spline.tension = 0.5f;
        return spline;
    }

    // SmoothCurveEvaluator implementation
    SmoothCurveEvaluator::SmoothCurveEvaluator() {
        config_ = SmoothCurveConfig();
        config_.bezier = BezierCurve::EaseOutCubic();
    }

    SmoothCurveEvaluator::SmoothCurveEvaluator(const SmoothCurveConfig& config) : config_(config) {
        if (config_.type == CurveType::Bezier && (config_.bezier.p3.x == 0 && config_.bezier.p3.y == 0)) {
            config_.bezier = BezierCurve::EaseOutCubic();
        }
    }

    void SmoothCurveEvaluator::SetConfig(const SmoothCurveConfig& config) {
        config_ = config;
        last_output_ = 0.0f;
        last_input_ = 0.0f;
    }

    float SmoothCurveEvaluator::Evaluate(float input) const {
        input = std::clamp(input, config_.input_min, config_.input_max);
        
        // Normalize to 0-1
        float t = (input - config_.input_min) / (config_.input_max - config_.input_min);
        if (config_.input_max == config_.input_min) t = 0.0f;
        
        float result = EvaluateCurveType(config_.type, t, config_.overshoot, config_.amplitude, config_.period);
        
        // Denormalize
        result = result * (config_.output_max - config_.output_min) + config_.output_min;
        
        if (config_.clamp_output) {
            result = std::clamp(result, config_.min_output, config_.max_output);
        }
        
        last_input_ = input;
        last_output_ = result;
        return result;
    }

    float SmoothCurveEvaluator::Evaluate(const CurveContext& context) const {
        float input = context.input;
        
        // Apply dynamic adjustments
        if (config_.dynamic_adjustment) {
            // Adjust based on velocity
            if (context.velocity > 0) {
                input *= 1.0f + context.velocity * config_.velocity_factor;
            }
            
            // Adjust based on error
            if (context.error > 0) {
                input *= 1.0f + context.error * config_.error_factor;
            }
            
            // Adjust based on distance
            if (context.distance > 0) {
                input *= 1.0f + context.distance * config_.distance_factor;
            }
        }
        
        // Use distance-based curve if enabled
        if (config_.use_distance_curves && context.distance > 0) {
            for (const auto& dc : config_.distance_curves) {
                if (context.distance <= dc.max_distance) {
                    // Temporarily use distance curve
                    SmoothCurveConfig temp = config_;
                    temp.type = dc.type;
                    temp.bezier = dc.bezier;
                    SmoothCurveEvaluator temp_eval(temp);
                    return temp_eval.Evaluate(input);
                }
            }
        }
        
        return Evaluate(input);
    }

    float SmoothCurveEvaluator::EvaluateWithDistance(float input, float distance) const {
        CurveContext ctx;
        ctx.input = input;
        ctx.distance = distance;
        ctx.velocity = 0;
        ctx.error = 0;
        ctx.time_delta = 0.016f;
        return Evaluate(ctx);
    }

    float SmoothCurveEvaluator::EvaluateCurveType(CurveType type, float t, float overshoot, float amplitude, float period) const {
        t = std::clamp(t, 0.0f, 1.0f);
        
        switch (type) {
            case CurveType::Linear:
                return t;
                
            case CurveType::EaseInQuad:
                return t * t;
            case CurveType::EaseOutQuad:
                return 1.0f - (1.0f - t) * (1.0f - t);
            case CurveType::EaseInOutQuad:
                return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
                
            case CurveType::EaseInCubic:
                return t * t * t;
            case CurveType::EaseOutCubic:
                return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
            case CurveType::EaseInOutCubic:
                return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t);
                
            case CurveType::EaseInQuart:
                return t * t * t * t;
            case CurveType::EaseOutQuart:
                return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
            case CurveType::EaseInOutQuart:
                return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - 8.0f * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
                
            case CurveType::EaseInQuint:
                return t * t * t * t * t;
            case CurveType::EaseOutQuint:
                return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
            case CurveType::EaseInOutQuint:
                return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - 16.0f * (1.0f - t) * (1.0f - t) * (1.0f - t) * (1.0f - t);
                
            case CurveType::EaseInSine:
                return 1.0f - cosf(t * 1.5707963f);
            case CurveType::EaseOutSine:
                return sinf(t * 1.5707963f);
            case CurveType::EaseInOutSine:
                return 0.5f * (1.0f - cosf(t * 3.14159265f));
                
            case CurveType::EaseInExpo:
                return t == 0 ? 0 : powf(2.0f, 10.0f * (t - 1.0f));
            case CurveType::EaseOutExpo:
                return t == 1 ? 1 : 1.0f - powf(2.0f, -10.0f * t);
            case CurveType::EaseInOutExpo:
                return t == 0 ? 0 : t == 1 ? 1 : t < 0.5f ? 0.5f * powf(2.0f, 20.0f * t - 10.0f) : 1.0f - 0.5f * powf(2.0f, -20.0f * (t - 0.5f));
                
            case CurveType::EaseInCirc:
                return 1.0f - sqrtf(1.0f - t * t);
            case CurveType::EaseOutCirc:
                return sqrtf(1.0f - (1.0f - t) * (1.0f - t));
            case CurveType::EaseInOutCirc:
                return t < 0.5f ? 0.5f * (1.0f - sqrtf(1.0f - 4.0f * t * t)) : 0.5f * (sqrtf(1.0f - 4.0f * (1.0f - t) * (1.0f - t)) + 1.0f);
                
            case CurveType::EaseInBack: {
                float c1 = 1.70158f;
                float c3 = c1 + 1.0f;
                return c3 * t * t * t - c1 * t * t;
            }
            case CurveType::EaseOutBack: {
                float c1 = 1.70158f + overshoot;
                float c3 = c1 + 1.0f;
                return 1.0f + c3 * powf(t - 1.0f, 3.0f) + c1 * powf(t - 1.0f, 2.0f);
            }
            case CurveType::EaseInOutBack: {
                float c1 = 1.70158f + overshoot;
                float c2 = c1 * 1.525f;
                return t < 0.5f
                    ? (powf(2.0f * t, 2.0f) * ((c2 + 1.0f) * 2.0f * t - c2)) * 0.5f
                    : (powf(2.0f * t - 2.0f, 2.0f) * ((c2 + 1.0f) * (2.0f * t - 2.0f) + c2) + 2.0f) * 0.5f;
            }
                
            case CurveType::EaseInElastic: {
                if (t == 0) return 0;
                if (t == 1) return 1;
                float c4 = (2.0f * 3.14159265f) / period;
                return -powf(2.0f, 10.0f * (t - 1.0f)) * sinf((t * 10.0f - 10.75f) * c4) * amplitude;
            }
            case CurveType::EaseOutElastic: {
                if (t == 0) return 0;
                if (t == 1) return 1;
                float c4 = (2.0f * 3.14159265f) / period;
                return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) * amplitude + 1.0f;
            }
            case CurveType::EaseInOutElastic: {
                if (t == 0) return 0;
                if (t == 1) return 1;
                float c4 = (2.0f * 3.14159265f) / (period * 1.5f);
                return t < 0.5f
                    ? -0.5f * powf(2.0f, 20.0f * t - 10.0f) * sinf((20.0f * t - 11.125f) * c4) * amplitude
                    : 0.5f * powf(2.0f, -20.0f * t + 10.0f) * sinf((20.0f * t - 11.125f) * c4) * amplitude + 1.0f;
            }
                
            case CurveType::EaseInBounce:
                return 1.0f - EvaluateCurveType(CurveType::EaseOutBounce, 1.0f - t, 0, 0, 0);
            case CurveType::EaseOutBounce: {
                if (t < 1.0f / 2.75f) return 7.5625f * t * t;
                else if (t < 2.0f / 2.75f) return 7.5625f * (t -= 1.5f / 2.75f) * t + 0.75f;
                else if (t < 2.5f / 2.75f) return 7.5625f * (t -= 2.25f / 2.75f) * t + 0.9375f;
                else return 7.5625f * (t -= 2.625f / 2.75f) * t + 0.984375f;
            }
            case CurveType::EaseInOutBounce:
                return t < 0.5f
                    ? 0.5f * EvaluateCurveType(CurveType::EaseInBounce, 2.0f * t, 0, 0, 0)
                    : 0.5f * EvaluateCurveType(CurveType::EaseOutBounce, 2.0f * t - 1.0f, 0, 0, 0) + 0.5f;
                
            case CurveType::Bezier:
                return EvaluateBezier(t);
                
            case CurveType::CatmullRom:
                return EvaluateSpline(t);
                
            case CurveType::Custom:
                // User would provide custom function
                return t;
        }
        
        return t;
    }

    float SmoothCurveEvaluator::EvaluateBezier(float t) const {
        return config_.bezier.Evaluate(t);
    }

    float SmoothCurveEvaluator::EvaluateSpline(float t) const {
        return config_.spline.Evaluate(t);
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
            case CurveType::Bezier: return "Custom Bezier";
            case CurveType::CatmullRom: return "Catmull-Rom Spline";
            case CurveType::Custom: return "Custom Function";
        }
        return "Unknown";
    }

    SmoothCurveConfig SmoothCurveEvaluator::LegitConfig() {
        SmoothCurveConfig cfg;
        cfg.type = CurveType::EaseOutCubic;
        cfg.use_distance_curves = true;
        cfg.distance_curves[0] = {30.0f, CurveType::EaseOutCubic, BezierCurve::EaseOutCubic()};
        cfg.distance_curves[1] = {80.0f, CurveType::EaseOutQuart, BezierCurve::Gentle()};
        cfg.distance_curves[2] = {150.0f, CurveType::EaseOutQuint, BezierCurve::Smooth()};
        cfg.distance_curves[3] = {FLT_MAX, CurveType::EaseOutQuint, BezierCurve::Smooth()};
        cfg.dynamic_adjustment = true;
        cfg.velocity_factor = 0.05f;
        cfg.distance_factor = 0.01f;
        return cfg;
    }

    SmoothCurveConfig SmoothCurveEvaluator::RageConfig() {
        SmoothCurveConfig cfg;
        cfg.type = CurveType::Linear;
        cfg.dynamic_adjustment = false;
        return cfg;
    }

    SmoothCurveConfig SmoothCurveEvaluator::SniperConfig() {
        SmoothCurveConfig cfg;
        cfg.type = CurveType::EaseOutQuint;
        cfg.use_distance_curves = true;
        cfg.distance_curves[0] = {50.0f, CurveType::EaseOutQuart, BezierCurve::Gentle()};
        cfg.distance_curves[1] = {100.0f, CurveType::EaseOutQuint, BezierCurve::Smooth()};
        cfg.distance_curves[2] = {200.0f, CurveType::EaseOutQuint, BezierCurve::Smooth()};
        cfg.distance_curves[3] = {FLT_MAX, CurveType::EaseOutQuint, BezierCurve::Smooth()};
        cfg.dynamic_adjustment = true;
        cfg.velocity_factor = 0.02f;
        cfg.distance_factor = 0.005f;
        return cfg;
    }

    SmoothCurveConfig SmoothCurveEvaluator::CloseRangeConfig() {
        SmoothCurveConfig cfg;
        cfg.type = CurveType::EaseOutCubic;
        cfg.bezier = BezierCurve::Sharp();
        return cfg;
    }

    SmoothCurveConfig SmoothCurveEvaluator::LongRangeConfig() {
        SmoothCurveConfig cfg;
        cfg.type = CurveType::EaseOutQuint;
        cfg.bezier = BezierCurve::Smooth();
        return cfg;
    }

    std::string SmoothCurveEvaluator::Serialize() const {
        std::ostringstream out;
        out << static_cast<int>(config_.type) << '|'
            << config_.bezier.p0.x << ',' << config_.bezier.p0.y << '|'
            << config_.bezier.p1.x << ',' << config_.bezier.p1.y << '|'
            << config_.bezier.p2.x << ',' << config_.bezier.p2.y << '|'
            << config_.bezier.p3.x << ',' << config_.bezier.p3.y << '|'
            << config_.overshoot << '|'
            << config_.amplitude << '|'
            << config_.period << '|'
            << config_.bounces << '|'
            << (config_.use_distance_curves ? 1 : 0) << '|'
            << (config_.dynamic_adjustment ? 1 : 0) << '|'
            << config_.velocity_factor << '|'
            << config_.error_factor << '|'
            << config_.distance_factor << '|'
            << config_.input_min << '|'
            << config_.input_max << '|'
            << config_.output_min << '|'
            << config_.output_max << '|'
            << (config_.clamp_output ? 1 : 0) << '|'
            << config_.min_output << '|'
            << config_.max_output;
        return out.str();
    }

    bool SmoothCurveEvaluator::Deserialize(const std::string& data) {
        std::istringstream in(data);
        std::string token;
        
        if (!std::getline(in, token, '|')) return false;
        config_.type = static_cast<CurveType>(std::stoi(token));
        
        auto read_vec2 = [&](ImVec2& v) {
            if (!std::getline(in, token, '|')) return false;
            size_t comma = token.find(',');
            if (comma == std::string::npos) return false;
            v.x = std::stof(token.substr(0, comma));
            v.y = std::stof(token.substr(comma + 1));
            return true;
        };
        
        if (!read_vec2(config_.bezier.p0)) return false;
        if (!read_vec2(config_.bezier.p1)) return false;
        if (!read_vec2(config_.bezier.p2)) return false;
        if (!read_vec2(config_.bezier.p3)) return false;
        
        if (!std::getline(in, token, '|')) return false; config_.overshoot = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.amplitude = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.period = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.bounces = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.use_distance_curves = token == "1";
        if (!std::getline(in, token, '|')) return false; config_.dynamic_adjustment = token == "1";
        if (!std::getline(in, token, '|')) return false; config_.velocity_factor = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.error_factor = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.distance_factor = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.input_min = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.input_max = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.output_min = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.output_max = std::stof(token);
        if (!std::getline(in, token, '|')) return false; config_.clamp_output = token == "1";
        if (!std::getline(in, token)) return false; config_.min_output = std::stof(token);
        config_.max_output = 1.0f; // Would need another token
        
        return true;
    }

    // DistanceCurveManager implementation
    void DistanceCurveManager::AddCurve(float min_dist, float max_dist, const SmoothCurveConfig& config) {
        CurveEntry entry;
        entry.min_distance = min_dist;
        entry.max_distance = max_dist;
        entry.config = config;
        curves_.push_back(entry);
    }

    void DistanceCurveManager::RemoveCurve(size_t index) {
        if (index < curves_.size()) {
            curves_.erase(curves_.begin() + index);
        }
    }

    void DistanceCurveManager::Clear() {
        curves_.clear();
    }

    SmoothCurveConfig DistanceCurveManager::GetCurveForDistance(float distance) const {
        for (const auto& entry : curves_) {
            if (distance >= entry.min_distance && distance <= entry.max_distance) {
                return entry.config;
            }
        }
        if (!curves_.empty()) return curves_.back().config;
        return SmoothCurveConfig();
    }

    float DistanceCurveManager::Evaluate(float input, float distance) const {
        SmoothCurveConfig cfg = GetCurveForDistance(distance);
        SmoothCurveEvaluator evaluator(cfg);
        return evaluator.Evaluate(input);
    }

    std::string DistanceCurveManager::Serialize() const {
        std::ostringstream out;
        out << curves_.size() << '|';
        for (const auto& entry : curves_) {
            out << entry.min_distance << ',' << entry.max_distance << ',' << entry.blend_range << '|';
            SmoothCurveEvaluator eval(entry.config);
            out << eval.Serialize() << '|';
        }
        return out.str();
    }

    bool DistanceCurveManager::Deserialize(const std::string& data) {
        std::istringstream in(data);
        std::string token;
        
        if (!std::getline(in, token, '|')) return false;
        size_t count = std::stoul(token);
        
        curves_.clear();
        curves_.reserve(count);
        
        for (size_t i = 0; i < count; ++i) {
            CurveEntry entry;
            if (!std::getline(in, token, '|')) return false;
            size_t c1 = token.find(',');
            size_t c2 = token.find(',', c1 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos) return false;
            entry.min_distance = std::stof(token.substr(0, c1));
            entry.max_distance = std::stof(token.substr(c1 + 1, c2 - c1 - 1));
            entry.blend_range = std::stof(token.substr(c2 + 1));
            
            if (!std::getline(in, token, '|')) return false;
            SmoothCurveEvaluator eval;
            if (!eval.Deserialize(token)) return false;
            entry.config = eval.GetConfig();
            
            curves_.push_back(entry);
        }
        return true;
    }

    DistanceCurveManager DistanceCurveManager::DefaultAimCurves() {
        DistanceCurveManager mgr;
        mgr.AddCurve(0, 30, SmoothCurveEvaluator::CloseRangeConfig());
        mgr.AddCurve(30, 80, SmoothCurveEvaluator::LegitConfig());
        mgr.AddCurve(80, 150, SmoothCurveEvaluator::LongRangeConfig());
        mgr.AddCurve(150, FLT_MAX, SmoothCurveEvaluator::LongRangeConfig());
        return mgr;
    }

    DistanceCurveManager DistanceCurveManager::SniperCurves() {
        DistanceCurveManager mgr;
        mgr.AddCurve(0, 50, SmoothCurveEvaluator::SniperConfig());
        mgr.AddCurve(50, 150, SmoothCurveEvaluator::SniperConfig());
        mgr.AddCurve(150, FLT_MAX, SmoothCurveEvaluator::SniperConfig());
        return mgr;
    }

    DistanceCurveManager DistanceCurveManager::AggressiveCurves() {
        DistanceCurveManager mgr;
        mgr.AddCurve(0, 20, SmoothCurveEvaluator::CloseRangeConfig());
        mgr.AddCurve(20, 60, SmoothCurveEvaluator::RageConfig());
        mgr.AddCurve(60, FLT_MAX, SmoothCurveEvaluator::RageConfig());
        return mgr;
    }

    // AimHumanizer implementation
    AimHumanizer::AimHumanizer(const HumanizerConfig& config) : config_(config), rng_(std::random_device{}()) {}
    
    ImVec2 AimHumanizer::Humanize(const ImVec2& raw_movement, float distance, float dt) {
        if (!config_.enabled || (raw_movement.x == 0 && raw_movement.y == 0)) {
            return raw_movement;
        }
        
        // Determine distance multiplier
        float dist_mult = 1.0f;
        if (distance < 30) dist_mult = config_.close_range_multiplier;
        else if (distance < 80) dist_mult = config_.mid_range_multiplier;
        else dist_mult = config_.far_range_multiplier;
        
        ImVec2 result = raw_movement;
        
        // Micro jitter
        if (config_.micro_jitter > 0) {
            std::uniform_real_distribution<float> jitter(-config_.micro_jitter, config_.micro_jitter);
            result.x += jitter(rng_) * dist_mult;
            result.y += jitter(rng_) * dist_mult;
        }
        
        // Macro drift
        if (config_.macro_drift > 0) {
            std::uniform_real_distribution<float> drift(-config_.macro_drift, config_.macro_drift);
            accumulated_drift_x_ += drift(rng_) * dt * dist_mult;
            accumulated_drift_y_ += drift(rng_) * dt * dist_mult;
            accumulated_drift_x_ = std::clamp(accumulated_drift_x_, -1.0f, 1.0f);
            accumulated_drift_y_ = std::clamp(accumulated_drift_y_, -1.0f, 1.0f);
            result.x += accumulated_drift_x_;
            result.y += accumulated_drift_y_;
        }
        
        // Pause chance
        if (!is_paused_ && config_.pause_chance > 0) {
            std::uniform_real_distribution<float> pause_dist(0, 1);
            if (pause_dist(rng_) < config_.pause_chance * dt * 60.0f) {
                is_paused_ = true;
                pause_timer_ = config_.pause_duration;
            }
        }
        
        if (is_paused_) {
            pause_timer_ -= dt;
            if (pause_timer_ <= 0) {
                is_paused_ = false;
            } else {
                return ImVec2(0, 0); // No movement during pause
            }
        }
        
        // Overshoot chance
        if (config_.overshoot_chance > 0 && (raw_movement.x != 0 || raw_movement.y != 0)) {
            std::uniform_real_distribution<float> overshoot_dist(0, 1);
            if (overshoot_dist(rng_) < config_.overshoot_chance * dt * 60.0f) {
                float overshoot = config_.overshoot_amount * dist_mult;
                result.x += (raw_movement.x > 0 ? overshoot : -overshoot);
                result.y += (raw_movement.y > 0 ? overshoot : -overshoot);
            }
        }
        
        return result;
    }

    void AimHumanizer::Reset() {
        accumulated_drift_x_ = 0.0f;
        accumulated_drift_y_ = 0.0f;
        pause_timer_ = 0.0f;
        is_paused_ = false;
    }

} // namespace Gameplay::SmoothCurves