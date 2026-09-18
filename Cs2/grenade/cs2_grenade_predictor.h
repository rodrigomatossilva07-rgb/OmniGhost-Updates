#pragma once
#include "cs2_grenade_types.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>

namespace CS2_Grenade {

// Forward declaration
class IGrenadeWorld;

class GrenadePredictor {
public:
    GrenadePredictor(const GrenadePhysicsConfig& config = GrenadePhysicsConfig());
    ~GrenadePredictor() = default;

    // Set world for collision tracing
    void SetWorld(const IGrenadeWorld* world) { world_ = world; }
    
    // Set physics config
    void SetConfig(const GrenadePhysicsConfig& config) { config_ = config; }
    const GrenadePhysicsConfig& GetConfig() const { return config_; }
    GrenadePhysicsConfig& GetConfig() { return config_; }

    // Main prediction function
    GrenadePrediction Predict(const GrenadeInputState& input);

    // Get cached prediction if available
    bool GetCachedPrediction(const GrenadeInputState& input, GrenadePrediction& out);

    // Clear cache
    void ClearCache();

    // Enable/disable debug mode
    void SetDebugMode(bool enabled) { debugMode_ = enabled; }
    bool IsDebugMode() const { return debugMode_; }

    // Get debug info
    struct DebugInfo {
        int simulationSteps = 0;
        float totalSimulationTime = 0.f;
        int bounces = 0;
        float finalVelocity = 0.f;
        float simulationTimeMs = 0.f;
        std::vector<Vec3> debugPoints;
        std::vector<Vec3> debugNormals;
        std::vector<float> debugVelocities;
    };
    const DebugInfo& GetLastDebugInfo() const { return lastDebugInfo_; }

private:
    // Physics simulation
    GrenadePrediction Simulate(const GrenadeInputState& input);
    
    // Calculate initial velocity based on throw mode and player state
    Vec3 CalculateInitialVelocity(const GrenadeInputState& input) const;
    
    // Determine throw mode from input
    ThrowMode DetermineThrowMode(const GrenadeInputState& input) const;
    
    // Get grenade type config
    const GrenadePhysicsConfig::TypeConfig& GetTypeConfig(GrenadeType type) const;
    
    // Resolve collision
    void ResolveCollision(const TraceResult& trace, Vec3& velocity, const GrenadePhysicsConfig::TypeConfig& typeConfig);
    
    // Check detonation condition
    bool CheckDetonation(const GrenadeInputState& input, float flightTime, const Vec3& velocity, const GrenadePhysicsConfig::TypeConfig& typeConfig) const;
    
    // Generate cache key
    GrenadePredictionKey GenerateKey(const GrenadeInputState& input) const;

    // Cache
    mutable std::mutex cacheMutex_;
    std::unordered_map<GrenadePredictionKey, GrenadePrediction, GrenadePredictionKeyHash> cache_;
    std::atomic<uint64_t> cacheHits_{0};
    std::atomic<uint64_t> cacheMisses_{0};
    uint64_t cacheTimestamp_ = 0;
    static constexpr uint64_t CACHE_TTL_MS = 100; // Cache valid for 100ms

    // World for collision
    const IGrenadeWorld* world_ = nullptr;
    
    // Config
    GrenadePhysicsConfig config_;
    
    // Debug
    mutable DebugInfo lastDebugInfo_;
    bool debugMode_ = false;
    
    // Statistics
    std::atomic<uint64_t> totalPredictions_{0};
    std::atomic<uint64_t> totalSimulationTimeMs_{0};
};

// Mock world implementation for testing
class MockGrenadeWorld : public IGrenadeWorld {
public:
    struct Box {
        Vec3 min, max;
    };
    
    void AddBox(const Vec3& min, const Vec3& max) {
        boxes_.push_back({min, max});
    }
    
    void AddPlane(const Vec3& point, const Vec3& normal) {
        planes_.push_back({point, normal});
    }
    
    void Clear() {
        boxes_.clear();
        planes_.clear();
    }
    
    TraceResult TraceGrenade(const Vec3& start, const Vec3& end, const Vec3& hullMins, const Vec3& hullMaxs) const override;
    bool IsPointInSolid(const Vec3& point) const override;

    std::vector<Box> boxes_;
    struct Plane { Vec3 point, normal; };
    std::vector<Plane> planes_;
};

} // namespace CS2_Grenade