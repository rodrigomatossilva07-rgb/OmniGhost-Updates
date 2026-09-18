#include "cs2_grenade_predictor.h"
#include <algorithm>
#include <chrono>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace CS2_Grenade {

GrenadePredictor::GrenadePredictor(const GrenadePhysicsConfig& config)
    : config_(config), world_(nullptr), debugMode_(false) {
}

GrenadePrediction GrenadePredictor::Predict(const GrenadeInputState& input) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Check cache first
    GrenadePrediction cached;
    if (GetCachedPrediction(input, cached)) {
        cacheHits_++;
        return cached;
    }
    cacheMisses_++;
    
    GrenadePrediction prediction = Simulate(input);
    
    // Cache the result
    GrenadePredictionKey key = GenerateKey(input);
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_[key] = prediction;
        cacheTimestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    totalPredictions_++;
    auto endTime = std::chrono::high_resolution_clock::now();
    float simTimeMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    totalSimulationTimeMs_ += static_cast<uint64_t>(simTimeMs);
    
    return prediction;
}

bool GrenadePredictor::GetCachedPrediction(const GrenadeInputState& input, GrenadePrediction& out) {
    uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    GrenadePredictionKey key = GenerateKey(input);
    
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        if (now - cacheTimestamp_ < CACHE_TTL_MS) {
            out = it->second;
            cacheHits_++;
            return true;
        } else {
            cache_.erase(it);
        }
    }
    cacheMisses_++;
    return false;
}

void GrenadePredictor::ClearCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    cache_.clear();
    cacheTimestamp_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

GrenadePrediction GrenadePredictor::Simulate(const GrenadeInputState& input) {
    GrenadePrediction prediction;
    lastDebugInfo_ = DebugInfo{};
    
    if (!input.IsValid() || !world_) {
        return prediction;
    }
    
    auto simStart = std::chrono::high_resolution_clock::now();
    
    const GrenadePhysicsConfig::TypeConfig& typeConfig = GetTypeConfig(input.grenadeType);
    if (typeConfig.initialVelocity <= 0) {
        return prediction;
    }
    
    // Calculate initial velocity
    Vec3 velocity = CalculateInitialVelocity(input);
    prediction.initialVelocity = velocity;
    
    // Starting position (eye position + small forward offset)
    Vec3 position = input.eyePosition;
    
    // Add small forward offset from player
    float yaw = input.viewAngles.y * M_PI / 180.0f;
    float pitch = -input.viewAngles.x * M_PI / 180.0f;
    Vec3 forward{cosf(pitch) * cosf(yaw), cosf(pitch) * sinf(yaw), sinf(pitch)};
    position = position + forward * 16.0f; // Offset from eye to hand
    
    float flightTime = 0.0f;
    int steps = 0;
    int bounces = 0;
    const float dt = config_.timestep;
    const float gravity = config_.gravity;
    const int maxSteps = config_.maxSimulationSteps;
    
    prediction.points.push_back(position);
    
    for (int step = 0; step < maxSteps && flightTime < config_.maxSimulationTime; ++step) {
        // Apply gravity
        velocity.z -= gravity * dt;
        
        // Calculate next position
        Vec3 nextPos = position + velocity * dt;
        
        // Trace for collision
        TraceResult trace = world_->TraceGrenade(
            position, 
            nextPos, 
            config_.hullMins, 
            config_.hullMaxs
        );
        
        if (trace.hit) {
            // Record bounce point
            prediction.bouncePoints.push_back(trace.hitPosition);
            bounces++;
            lastDebugInfo_.debugPoints.push_back(trace.hitPosition);
            lastDebugInfo_.debugNormals.push_back(trace.hitNormal);
            
            // Reflect velocity
            float dot = velocity.x * trace.hitNormal.x + velocity.y * trace.hitNormal.y + velocity.z * trace.hitNormal.z;
            velocity.x = velocity.x - 2.0f * dot * trace.hitNormal.x;
            velocity.y = velocity.y - 2.0f * dot * trace.hitNormal.y;
            velocity.z = velocity.z - 2.0f * dot * trace.hitNormal.z;
            
            // Apply elasticity and friction
            velocity.x *= config_.elasticity * config_.friction;
            velocity.y *= config_.elasticity * config_.friction;
            velocity.z *= config_.elasticity * config_.friction;
            
            // Move to hit position with small offset
            position = trace.hitPosition + trace.hitNormal * 1.0f;
        } else {
            position = nextPos;
        }
        
        prediction.points.push_back(position);
        steps++;
        flightTime += dt;
        
        // Check detonation
        if (CheckDetonation(input, flightTime, velocity, typeConfig)) {
            prediction.landed = true;
            break;
        }
        
        // Check stop condition
        float velLen = sqrtf(velocity.x*velocity.x + velocity.y*velocity.y + velocity.z*velocity.z);
        if (velLen < config_.stopVelocity) {
            prediction.landed = true;
            break;
        }
    }
    
    prediction.flightTime = flightTime;
    prediction.bounceCount = bounces;
    prediction.landingPosition = prediction.points.empty() ? Vec3{} : prediction.points.back();
    prediction.valid = true;
    
    // Update debug info
    lastDebugInfo_.simulationSteps = steps;
    lastDebugInfo_.totalSimulationTime = flightTime;
    lastDebugInfo_.bounces = bounces;
    lastDebugInfo_.finalVelocity = sqrtf(velocity.x*velocity.x + velocity.y*velocity.y + velocity.z*velocity.z);
    
    return prediction;
}

Vec3 GrenadePredictor::CalculateInitialVelocity(const GrenadeInputState& input) const {
    const GrenadePhysicsConfig::TypeConfig& typeConfig = GetTypeConfig(input.grenadeType);
    if (typeConfig.initialVelocity <= 0) {
        return Vec3{};
    }
    
    // Calculate view direction
    float yaw = input.viewAngles.y * M_PI / 180.0f;
    float pitch = -input.viewAngles.x * M_PI / 180.0f;
    
    Vec3 forward{
        cosf(pitch) * cosf(yaw),
        cosf(pitch) * sinf(yaw),
        -sinf(pitch)
    };
    
    // Base velocity from throw
    float baseVelocity = GetTypeConfig(input.grenadeType).initialVelocity;
    
    // Apply throw strength
    float throwMult = input.throwStrength;
    if (input.throwMode == ThrowMode::Normal) throwMult = 1.0f;
    else if (input.throwMode == ThrowMode::JumpThrow) throwMult = 1.0f;
    else if (input.throwMode == ThrowMode::RunThrow) throwMult = 1.2f;
    else if (input.throwMode == ThrowMode::WalkThrow) throwMult = 0.7f;
    else if (input.throwMode == ThrowMode::CrouchThrow) throwMult = 0.5f;
    
    Vec3 throwVelocity = forward * (baseVelocity * throwMult);
    
    // Add player velocity contribution
    Vec3 playerVelContribution = input.playerVelocity * config_.playerVelocityContribution;
    
    return throwVelocity + playerVelContribution;
}

ThrowMode GrenadePredictor::DetermineThrowMode(const GrenadeInputState& input) const {
    return input.throwMode;
}

const GrenadePhysicsConfig::TypeConfig& GrenadePredictor::GetTypeConfig(GrenadeType type) const {
    static GrenadePhysicsConfig::TypeConfig defaultConfig;
    size_t idx = static_cast<size_t>(type);
    if (idx < 7) {
        return config_.typeConfigs[idx];
    }
    return defaultConfig;
}

void GrenadePredictor::ResolveCollision(const TraceResult& trace, Vec3& velocity, const GrenadePhysicsConfig::TypeConfig& /*typeConfig*/) {
    // Reflect velocity around normal
    float dot = velocity.x * trace.hitNormal.x + velocity.y * trace.hitNormal.y + velocity.z * trace.hitNormal.z;
    velocity.x = velocity.x - 2.0f * dot * trace.hitNormal.x;
    velocity.y = velocity.y - 2.0f * dot * trace.hitNormal.y;
    velocity.z = velocity.z - 2.0f * dot * trace.hitNormal.z;
    
    // Apply elasticity
    velocity.x *= config_.elasticity;
    velocity.y *= config_.elasticity;
    velocity.z *= config_.elasticity;
}

bool GrenadePredictor::CheckDetonation(const GrenadeInputState& /*input*/, float flightTime, const Vec3& velocity, const GrenadePhysicsConfig::TypeConfig& typeConfig) const {
    // Time-based detonation
    if (typeConfig.detonationTime > 0 && flightTime >= typeConfig.detonationTime) {
        return true;
    }
    
    // Impact detonation (for smoke, molotov, incendiary)
    if (typeConfig.detonationTime == 0.0f) {
        // Detonate on first impact (already handled by bounce logic)
        return false;
    }
    
    // Velocity-based (very slow = landed)
    float velLen = sqrtf(velocity.x*velocity.x + velocity.y*velocity.y + velocity.z*velocity.z);
    if (velLen < config_.stopVelocity) {
        return true;
    }
    
    return false;
}

GrenadePredictionKey GrenadePredictor::GenerateKey(const GrenadeInputState& input) const {
    GrenadePredictionKey key;
    key.eyePosition = input.eyePosition;
    key.viewAngles = input.viewAngles;
    key.playerVelocity = input.playerVelocity;
    key.grenadeType = input.grenadeType;
    key.onGround = input.onGround;
    key.throwStrength = input.throwStrength;
    key.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return key;
}

// Mock world implementation
TraceResult MockGrenadeWorld::TraceGrenade(const Vec3& start, const Vec3& end, const Vec3& hullMins, const Vec3& hullMaxs) const {
    TraceResult result;
    result.hit = false;
    result.fraction = 1.0f;
    result.hitPosition = start + (end - start);
    result.hitNormal = Vec3{0, 0, 1};
    
    Vec3 dir = end - start;
    float length = sqrtf(dir.x*dir.x + dir.y*dir.y + dir.z*dir.z);
    if (length < 0.001f) return result;
    dir = dir * (1.0f / length);
    
    float closestFraction = 1.0f;
    Vec3 hitNormal{0, 0, 1};
    bool hit = false;
    
    // Check boxes
    for (const auto& box : boxes_) {
        // Simple AABB vs ray with hull
        Vec3 boxMin = box.min - Vec3{hullMaxs.x - hullMins.x, hullMaxs.y - hullMins.y, hullMaxs.z - hullMins.z} * 0.5f;
        Vec3 boxMax = box.max + Vec3{hullMaxs.x - hullMins.x, hullMaxs.y - hullMins.y, hullMaxs.z - hullMins.z} * 0.5f;
        
        float tMin = -INFINITY, tMax = INFINITY;
        
        for (int i = 0; i < 3; i++) {
            float dirComp = (&dir.x)[i];
            float startComp = (&start.x)[i];
            float minComp = (&boxMin.x)[i];
            float maxComp = (&boxMax.x)[i];
            
            if (fabsf(dirComp) < 0.0001f) {
                if (startComp < minComp || startComp > maxComp) {
                    tMin = INFINITY;
                    break;
                }
            } else {
                float t1 = (minComp - startComp) / dirComp;
                float t2 = (maxComp - startComp) / dirComp;
                if (t1 > t2) std::swap(t1, t2);
                tMin = fmaxf(tMin, t1);
                tMax = fminf(tMax, t2);
                if (tMin > tMax) break;
            }
        }
        
        if (tMin <= tMax && tMin >= 0 && tMin < closestFraction) {
            closestFraction = tMin;
            hit = true;
        }
    }
    
    // Check planes
    for (const auto& plane : planes_) {
        float denom = dir.x * plane.normal.x + dir.y * plane.normal.y + dir.z * plane.normal.z;
        if (fabsf(denom) < 0.0001f) continue;
        
        float t = (plane.point.x * plane.normal.x + plane.point.y * plane.normal.y + plane.point.z * plane.normal.z
                 - start.x * plane.normal.x - start.y * plane.normal.y - start.z * plane.normal.z) / denom;
        
        if (t >= 0 && t < closestFraction) {
            closestFraction = t;
            hitNormal = plane.normal;
            hit = true;
        }
    }
    
    if (hit) {
        result.hit = true;
        result.fraction = closestFraction;
        result.hitPosition = start + (end - start) * closestFraction;
        result.hitNormal = hitNormal;
        result.isWorld = true;
    }
    
    return result;
}

bool MockGrenadeWorld::IsPointInSolid(const Vec3& point) const {
    for (const auto& box : boxes_) {
        if (point.x >= box.min.x && point.x <= box.max.x &&
            point.y >= box.min.y && point.y <= box.max.y &&
            point.z >= box.min.z && point.z <= box.max.z) {
            return true;
        }
    }
    return false;
}

} // namespace CS2_Grenade