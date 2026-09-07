#pragma once
#include "../../ImGui/imgui.h"
#include <array>
#include <deque>
#include <chrono>
#include <cstdint>

namespace Gameplay::Prediction {

    struct Vec3 {
        float x = 0, y = 0, z = 0;
        Vec3() = default;
        Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
        
        Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
        Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
        Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
        Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
        
        float Length() const { return sqrtf(x*x + y*y + z*z); }
        float Length2D() const { return sqrtf(x*x + y*y); }
        float DistTo(const Vec3& o) const { return (*this - o).Length(); }
        bool IsZero() const { return x == 0 && y == 0 && z == 0; }
    };

    struct Vec2 {
        float x = 0, y = 0;
        Vec2() = default;
        Vec2(float x_, float y_) : x(x_), y(y_) {}
        float Length() const { return sqrtf(x*x + y*y); }
    };

    // Historical position sample
    struct PositionSample {
        Vec3 position;
        Vec3 velocity;
        std::chrono::steady_clock::time_point timestamp;
        bool valid = false;
        uint64_t entity_id = 0;
    };

    // Prediction configuration
    struct PredictionConfig {
        // General
        bool enabled = true;
        bool velocity_prediction = true;
        bool acceleration_prediction = false;
        
        // Timing
        float prediction_time = 0.03f;        // Seconds to predict ahead
        float max_prediction_time = 0.15f;    // Cap for safety
        float min_velocity_for_prediction = 1.0f;  // Minimum speed to predict
        
        // Velocity smoothing
        float velocity_smoothing = 0.3f;       // EMA factor for velocity
        float acceleration_smoothing = 0.2f;   // EMA factor for acceleration
        
        // Distance-based scaling
        bool distance_scaling = true;
        float close_range = 30.0f;             // Full prediction
        float mid_range = 80.0f;               // Reduced prediction
        float far_range = 150.0f;              // Minimal prediction
        float close_scale = 1.0f;
        float mid_scale = 0.5f;
        float far_scale = 0.15f;
        
        // Falling/jumping handling
        bool predict_vertical = true;
        float gravity = 9.81f;
        float max_vertical_prediction = 2.0f;
        
        // Network/latency compensation
        bool latency_compensation = true;
        float local_latency_ms = 0.0f;         // Local input latency
        float server_latency_ms = 0.0f;        // Estimated server latency
        bool adaptive_latency = true;          // Auto-estimate from ping
        
        // History
        int history_size = 30;                 // Number of samples to keep
        float max_history_age = 1.0f;          // Max age in seconds
        
        // Validation
        bool validate_predictions = true;
        float max_position_jump = 5.0f;        // Max valid position change per frame
        float max_velocity = 120.0f;           // Max valid velocity
        float max_acceleration = 50.0f;        // Max valid acceleration
        
        // Per-weapon overrides (indexed by weapon class)
        struct WeaponOverride {
            float prediction_time_multiplier = 1.0f;
            bool enabled = true;
        };
        std::array<WeaponOverride, 16> weapon_overrides{};
    };

    // Prediction result
    struct PredictionResult {
        Vec3 predicted_position;
        Vec3 predicted_velocity;
        float confidence = 0.0f;        // 0.0 - 1.0
        bool used_velocity = false;
        bool used_acceleration = false;
        float prediction_time_used = 0.0f;
        std::chrono::steady_clock::time_point prediction_timestamp;
    };

    // Entity tracker for prediction
    class EntityPredictor {
    public:
        explicit EntityPredictor(uint64_t entity_id = 0) : entity_id_(entity_id) {}
        
        void Reset();
        void Update(const Vec3& position, const Vec3* velocity = nullptr);
        void SetConfig(const PredictionConfig& config) { config_ = config; }
        const PredictionConfig& GetConfig() const { return config_; }
        
        // Get prediction for a specific time ahead
        PredictionResult Predict(float time_ahead = -1.0f) const;
        
        // Get current smoothed velocity
        Vec3 GetSmoothedVelocity() const { return smoothed_velocity_; }
        Vec3 GetSmoothedAcceleration() const { return smoothed_acceleration_; }
        
        // Check if we have enough data for prediction
        bool HasValidPrediction() const;
        
        // Get history for debugging
        const std::deque<PositionSample>& GetHistory() const { return history_; }
        
    private:
        uint64_t entity_id_ = 0;
        PredictionConfig config_;
        std::deque<PositionSample> history_;
        Vec3 smoothed_velocity_{};
        Vec3 smoothed_acceleration_{};
        Vec3 last_position_{};
        std::chrono::steady_clock::time_point last_update_;
        bool has_last_position_ = false;
        int frames_since_valid_ = 0;
        
        void UpdateVelocity(const PositionSample& current, const PositionSample& previous);
        void UpdateAcceleration();
        void CleanHistory();
        float GetDistanceScale(float distance) const;
    };
    
    // Global prediction manager
    class PredictionManager {
    public:
        static PredictionManager& Instance();
        
        void SetConfig(const PredictionConfig& config) { global_config_ = config; }
        const PredictionConfig& GetConfig() const { return global_config_; }
        
        // Get or create predictor for entity
        EntityPredictor* GetPredictor(uint64_t entity_id);
        void RemovePredictor(uint64_t entity_id);
        void ClearAll();
        
        // Update all predictors (call once per frame)
        void UpdateAll(float dt);
        
        // Get prediction for entity
        PredictionResult Predict(uint64_t entity_id, float time_ahead = -1.0f);
        
        // Latency estimation
        void UpdateLatencyEstimate(float ping_ms);
        float GetEstimatedLatency() const { return estimated_latency_ms_; }
        
    private:
        PredictionConfig global_config_;
        float estimated_latency_ms_ = 0.0f;
        std::unordered_map<uint64_t, EntityPredictor> predictors_;
        std::chrono::steady_clock::time_point last_update_;
    };
    
    // Latency compensation for aim
    struct LatencyCompensation {
        float total_latency_ms = 0.0f;        // Total estimated latency
        float input_latency_ms = 5.0f;        // Local input latency
        float render_latency_ms = 8.0f;       // Render pipeline latency
        float network_latency_ms = 0.0f;      // Network ping
        float processing_latency_ms = 2.0f;   // DMA/processing latency
        
        // Calculate total latency
        float GetTotalLatency() const {
            return input_latency_ms + render_latency_ms + network_latency_ms + processing_latency_ms;
        }
        
        // Convert to prediction time
        float GetPredictionTime() const {
            return GetTotalLatency() / 1000.0f;
        }
    };
    
    // Helper: calculate prediction for projectile
    struct ProjectilePrediction {
        Vec3 start_position;
        Vec3 start_velocity;
        float gravity = 9.81f;
        float drag = 0.0f;
        float max_time = 10.0f;
        float time_step = 0.01f;
        
        // Simulate projectile trajectory
        std::vector<Vec3> Simulate(int max_steps = 1000) const;
        
        // Find impact point with plane
        bool FindPlaneImpact(const Vec3& plane_point, const Vec3& plane_normal, Vec3& impact) const;
        
        // Find impact with sphere
        bool FindSphereImpact(const Vec3& center, float radius, Vec3& impact, float& time) const;
    };
    
    // Interpolation utilities
    namespace Interpolation {
        // Linear interpolation
        Vec3 Lerp(const Vec3& a, const Vec3& b, float t);
        
        // Cubic Hermite spline
        Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t);
        
        // Bezier curve
        Vec3 BezierQuadratic(const Vec3& p0, const Vec3& p1, const Vec3& p2, float t);
        Vec3 BezierCubic(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t);
        
        // Smooth step
        float SmoothStep(float t);
        float SmootherStep(float t);
    }

} // namespace Gameplay::Prediction