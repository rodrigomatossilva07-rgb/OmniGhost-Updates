#include "prediction.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Gameplay::Prediction {

    // EntityPredictor implementation
    void EntityPredictor::Reset() {
        history_.clear();
        smoothed_velocity_ = Vec3{};
        smoothed_acceleration_ = Vec3{};
        last_position_ = Vec3{};
        last_update_ = {};
        has_last_position_ = false;
        frames_since_valid_ = 0;
    }

    void EntityPredictor::Update(const Vec3& position, const Vec3* velocity) {
        auto now = std::chrono::steady_clock::now();
        
        PositionSample sample;
        sample.position = position;
        sample.timestamp = now;
        sample.valid = true;
        sample.entity_id = entity_id_;
        
        if (velocity) {
            sample.velocity = *velocity;
        } else if (has_last_position_) {
            float dt = std::chrono::duration<float>(now - last_update_).count();
            if (dt > 0.001f && dt < 0.1f) {
                sample.velocity = (position - last_position_) / dt;
            }
        }
        
        history_.push_back(sample);
        CleanHistory();
        
        // Update smoothed velocity
        if (sample.velocity.Length() > 0) {
            float alpha = config_.velocity_smoothing;
            smoothed_velocity_ = smoothed_velocity_ * (1.0f - alpha) + sample.velocity * alpha;
        }
        
        // Update smoothed acceleration
        if (history_.size() >= 2) {
            UpdateAcceleration();
        }
        
        last_position_ = position;
        last_update_ = now;
        has_last_position_ = true;
        frames_since_valid_ = 0;
    }

    void EntityPredictor::UpdateVelocity(const PositionSample& current, const PositionSample& previous) {
        float dt = std::chrono::duration<float>(current.timestamp - previous.timestamp).count();
        if (dt > 0.001f && dt < 0.1f) {
            Vec3 vel = (current.position - previous.position) / dt;
            if (vel.Length() < config_.max_velocity) {
                float alpha = config_.velocity_smoothing;
                smoothed_velocity_ = smoothed_velocity_ * (1.0f - alpha) + vel * alpha;
            }
        }
    }

    void EntityPredictor::UpdateAcceleration() {
        if (history_.size() < 3) return;
        
        auto it = history_.rbegin();
        const PositionSample& s0 = *it;     // Current
        const PositionSample& s1 = *(it + 1); // Previous
        const PositionSample& s2 = *(it + 2); // Before previous
        
        float dt1 = std::chrono::duration<float>(s0.timestamp - s1.timestamp).count();
        float dt2 = std::chrono::duration<float>(s1.timestamp - s2.timestamp).count();
        
        if (dt1 > 0.001f && dt1 < 0.1f && dt2 > 0.001f && dt2 < 0.1f) {
            Vec3 vel1 = (s0.position - s1.position) / dt1;
            Vec3 vel2 = (s1.position - s2.position) / dt2;
            float dt_avg = (dt1 + dt2) * 0.5f;
            
            Vec3 accel = (vel1 - vel2) / dt_avg;
            if (accel.Length() < config_.max_acceleration) {
                float alpha = config_.acceleration_smoothing;
                smoothed_acceleration_ = smoothed_acceleration_ * (1.0f - alpha) + accel * alpha;
            }
        }
    }

    void EntityPredictor::CleanHistory() {
        auto now = std::chrono::steady_clock::now();
        float max_age = config_.max_history_age;
        
        while (!history_.empty()) {
            float age = std::chrono::duration<float>(now - history_.front().timestamp).count();
            if (age > max_age || history_.size() > static_cast<size_t>(config_.history_size)) {
                history_.pop_front();
            } else {
                break;
            }
        }
    }

    float EntityPredictor::GetDistanceScale(float distance) const {
        if (!config_.distance_scaling) return 1.0f;
        
        if (distance <= config_.close_range) return config_.close_scale;
        if (distance <= config_.mid_range) return config_.mid_scale;
        if (distance <= config_.far_range) return config_.far_scale;
        return config_.far_scale * 0.5f; // Extra reduction for very far
    }

    PredictionResult EntityPredictor::Predict(float time_ahead) const {
        PredictionResult result;
        result.prediction_timestamp = std::chrono::steady_clock::now();
        
        if (!HasValidPrediction()) {
            result.confidence = 0.0f;
            return result;
        }
        
        const auto& latest = history_.back();
        if (!latest.valid) {
            result.confidence = 0.0f;
            return result;
        }
        
        // Determine prediction time
        float pred_time = time_ahead >= 0 ? time_ahead : config_.prediction_time;
        pred_time = std::min(pred_time, config_.max_prediction_time);
        result.prediction_time_used = pred_time;
        
        // Apply distance scaling
        float distance = latest.position.Length();
        float scale = GetDistanceScale(distance);
        pred_time *= scale;
        
        // Start with current position
        Vec3 predicted = latest.position;
        result.used_velocity = false;
        result.used_acceleration = false;
        
        // Velocity prediction
        if (config_.velocity_prediction && smoothed_velocity_.Length() > config_.min_velocity_for_prediction) {
            predicted = predicted + smoothed_velocity_ * pred_time;
            result.used_velocity = true;
        }
        
        // Acceleration prediction
        if (config_.acceleration_prediction && smoothed_acceleration_.Length() > 0.1f) {
            predicted = predicted + smoothed_acceleration_ * (pred_time * pred_time * 0.5f);
            result.used_acceleration = true;
        }
        
        // Vertical prediction (gravity)
        if (config_.predict_vertical && smoothed_velocity_.y < 0) {
            float vertical_drop = 0.5f * config_.gravity * pred_time * pred_time;
            vertical_drop = std::min(vertical_drop, config_.max_vertical_prediction);
            predicted.y -= vertical_drop;
        }
        
        // Latency compensation
        if (config_.latency_compensation) {
            float latency_time = (config_.local_latency_ms + config_.server_latency_ms) / 1000.0f;
            if (smoothed_velocity_.Length() > 0) {
                predicted = predicted + smoothed_velocity_ * latency_time;
            }
        }
        
        result.predicted_position = predicted;
        result.predicted_velocity = smoothed_velocity_;
        
        // Calculate confidence based on history quality
        float confidence = 1.0f;
        if (frames_since_valid_ > 0) confidence *= 0.5f;
        if (history_.size() < 3) confidence *= 0.7f;
        if (smoothed_velocity_.Length() < config_.min_velocity_for_prediction) confidence *= 0.5f;
        if (pred_time > config_.prediction_time) confidence *= 0.8f;
        
        result.confidence = std::clamp(confidence, 0.0f, 1.0f);
        return result;
    }

    bool EntityPredictor::HasValidPrediction() const {
        return has_last_position_ && frames_since_valid_ < 5 && !history_.empty();
    }

    // PredictionManager implementation
    PredictionManager& PredictionManager::Instance() {
        static PredictionManager instance;
        return instance;
    }

    EntityPredictor* PredictionManager::GetPredictor(uint64_t entity_id) {
        auto& predictor = predictors_[entity_id];
        if (predictor.GetConfig().enabled != global_config_.enabled) {
            predictor.SetConfig(global_config_);
        }
        return &predictor;
    }

    void PredictionManager::RemovePredictor(uint64_t entity_id) {
        predictors_.erase(entity_id);
    }

    void PredictionManager::ClearAll() {
        predictors_.clear();
    }

    void PredictionManager::UpdateAll(float dt) {
        auto now = std::chrono::steady_clock::now();
        
        // Remove stale predictors
        auto it = predictors_.begin();
        while (it != predictors_.end()) {
            if (!it->second.HasValidPrediction()) {
                int frames_since = 0;
                if (it->second.GetHistory().size() > 0) {
                    float age = std::chrono::duration<float>(now - it->second.GetHistory().back().timestamp).count();
                    frames_since = static_cast<int>(age / dt);
                }
                if (frames_since > 60) { // Remove after ~1 second at 60fps
                    it = predictors_.erase(it);
                    continue;
                }
            }
            ++it;
        }
    }

    PredictionResult PredictionManager::Predict(uint64_t entity_id, float time_ahead) {
        auto it = predictors_.find(entity_id);
        if (it == predictors_.end()) {
            return PredictionResult{};
        }
        return it->second.Predict(time_ahead);
    }

    void PredictionManager::UpdateLatencyEstimate(float ping_ms) {
        if (global_config_.adaptive_latency && ping_ms > 0) {
            // Smooth latency estimate
            estimated_latency_ms_ = estimated_latency_ms_ * 0.9f + ping_ms * 0.1f;
        }
    }

    // ProjectilePrediction implementation
    std::vector<Vec3> ProjectilePrediction::Simulate(int max_steps) const {
        std::vector<Vec3> trajectory;
        trajectory.reserve(max_steps);
        
        Vec3 pos = start_position;
        Vec3 vel = start_velocity;
        trajectory.push_back(pos);
        
        for (int i = 0; i < max_steps; ++i) {
            float t = i * time_step;
            if (t > max_time) break;
            
            // Apply gravity
            vel.y -= gravity * time_step;
            
            // Apply drag
            if (drag > 0) {
                float speed = vel.Length();
                if (speed > 0) {
                    Vec3 drag_force = vel * (-drag * speed * time_step);
                    vel = vel + drag_force;
                }
            }
            
            pos = pos + vel * time_step;
            trajectory.push_back(pos);
            
            // Stop if underground
            if (pos.y < -1000) break;
        }
        
        return trajectory;
    }

    bool ProjectilePrediction::FindPlaneImpact(const Vec3& plane_point, const Vec3& plane_normal, Vec3& impact) const {
        Vec3 pos = start_position;
        Vec3 vel = start_velocity;
        
        for (int i = 0; i < 1000; ++i) {
            float t = i * time_step;
            if (t > max_time) break;
            
            Vec3 next_pos = pos + vel * time_step;
            vel.y -= gravity * time_step;
            
            if (drag > 0) {
                float speed = vel.Length();
                if (speed > 0) {
                    vel = vel + vel * (-drag * speed * time_step);
                }
            }
            
            // Check plane intersection
            float d1 = (pos - plane_point).x * plane_normal.x + 
                       (pos - plane_point).y * plane_normal.y + 
                       (pos - plane_point).z * plane_normal.z;
            float d2 = (next_pos - plane_point).x * plane_normal.x + 
                       (next_pos - plane_point).y * plane_normal.y + 
                       (next_pos - plane_point).z * plane_normal.z;
            
            if (d1 * d2 <= 0) {
                // Intersection found
                float ratio = d1 / (d1 - d2);
                impact = pos + (next_pos - pos) * ratio;
                return true;
            }
            
            pos = next_pos;
        }
        return false;
    }

    bool ProjectilePrediction::FindSphereImpact(const Vec3& center, float radius, Vec3& impact, float& time) const {
        Vec3 pos = start_position;
        Vec3 vel = start_velocity;
        
        for (int i = 0; i < 1000; ++i) {
            float t = i * time_step;
            if (t > max_time) break;
            
            Vec3 next_pos = pos + vel * time_step;
            vel.y -= gravity * time_step;
            
            if (drag > 0) {
                float speed = vel.Length();
                if (speed > 0) {
                    vel = vel + vel * (-drag * speed * time_step);
                }
            }
            
            // Check sphere intersection
            Vec3 to_center = pos - center;
            float a = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
            float b = 2.0f * (to_center.x * vel.x + to_center.y * vel.y + to_center.z * vel.z);
            float c = to_center.x * to_center.x + to_center.y * to_center.y + to_center.z * to_center.z - radius * radius;
            
            float discriminant = b * b - 4 * a * c;
            if (discriminant >= 0) {
                float sqrt_d = sqrtf(discriminant);
                float t1 = (-b - sqrt_d) / (2 * a);
                float t2 = (-b + sqrt_d) / (2 * a);
                
                float hit_t = (t1 >= 0 && t1 <= time_step) ? t1 : 
                              (t2 >= 0 && t2 <= time_step) ? t2 : -1;
                
                if (hit_t >= 0) {
                    impact = pos + vel * hit_t;
                    time = t + hit_t;
                    return true;
                }
            }
            
            pos = next_pos;
        }
        return false;
    }

    // Interpolation implementation
    namespace Interpolation {
        Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return a + (b - a) * t;
        }

        Vec3 CatmullRom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            float t2 = t * t;
            float t3 = t2 * t;
            
            return p1 * 2.0f + 
                   (p2 - p0) * t + 
                   (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + 
                   (p0 * -1.0f + p1 * 3.0f - p2 * 3.0f + p3) * t3;
        }

        Vec3 BezierQuadratic(const Vec3& p0, const Vec3& p1, const Vec3& p2, float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            float u = 1.0f - t;
            return p0 * u * u + p1 * 2.0f * u * t + p2 * t * t;
        }

        Vec3 BezierCubic(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            float u = 1.0f - t;
            float u2 = u * u;
            float u3 = u2 * u;
            float t2 = t * t;
            float t3 = t2 * t;
            return p0 * u3 + p1 * 3.0f * u2 * t + p2 * 3.0f * u * t2 + p3 * t3;
        }

        float SmoothStep(float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        float SmootherStep(float t) {
            t = std::clamp(t, 0.0f, 1.0f);
            return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
        }
    }

} // namespace Gameplay::Prediction