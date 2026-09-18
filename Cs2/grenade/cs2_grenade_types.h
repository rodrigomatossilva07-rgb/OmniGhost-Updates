#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <cstddef>
#include <cmath>
#include <unordered_map>
#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include "../../../src/math/math.h"

namespace CS2_Grenade {

// Grenade types matching CS2 item definition indexes
enum class GrenadeType : uint32_t {
    Unknown = 0,
    HEGrenade = 43,      // weapon_hegrenade
    Flashbang = 44,      // weapon_flashbang
    SmokeGrenade = 45,   // weapon_smokegrenade
    Molotov = 46,        // weapon_molotov
    Decoy = 47,          // weapon_decoy
    Incendiary = 48,     // weapon_incgrenade
    TAGrenade = 202      // weapon_tagrenade
};

// Throw modes in CS2
enum class ThrowMode : uint8_t {
    Normal = 0,
    JumpThrow,
    RunThrow,
    WalkThrow,
    CrouchThrow
};

inline std::string_view GrenadeTypeToString(GrenadeType type) {
    switch (type) {
        case GrenadeType::HEGrenade: return "HE";
        case GrenadeType::Flashbang: return "Flashbang";
        case GrenadeType::SmokeGrenade: return "Smoke";
        case GrenadeType::Molotov: return "Molotov";
        case GrenadeType::Incendiary: return "Incendiary";
        case GrenadeType::Decoy: return "Decoy";
        case GrenadeType::TAGrenade: return "TA";
        default: return "Unknown";
    }
}

inline std::string_view ThrowModeToString(ThrowMode mode) {
    switch (mode) {
        case ThrowMode::Normal: return "Normal";
        case ThrowMode::JumpThrow: return "Jump Throw";
        case ThrowMode::RunThrow: return "Run Throw";
        case ThrowMode::WalkThrow: return "Walk Throw";
        case ThrowMode::CrouchThrow: return "Crouch Throw";
        default: return "None";
    }
}

// Input state for grenade prediction - filled by DMA reader
struct GrenadeInputState {
    Vec3 eyePosition;
    Vec3 viewAngles;
    Vec3 playerVelocity;
    Vec3 playerPosition;

    GrenadeType grenadeType = GrenadeType::Unknown;
    ThrowMode throwMode = ThrowMode::Normal;

    bool onGround = false;
    bool preparingThrow = false;
    bool pinPulled = false;
    bool jumpThrow = false;
    bool runThrow = false;
    float throwStrength = 1.0f;
    int tickCount = 0;
    uint64_t sequence = 0;

    bool IsValid() const {
        return grenadeType != GrenadeType::Unknown && 
               !eyePosition.IsZero() && 
               !viewAngles.IsZero();
    }
};

// Prediction result
struct GrenadePrediction {
    std::vector<Vec3> points;           // Full trajectory points
    std::vector<Vec3> bouncePoints;     // Bounce collision points
    Vec3 landingPosition;               // Final landing position
    Vec3 initialVelocity;               // Initial velocity vector
    float flightTime = 0.f;             // Total flight time in seconds
    int bounceCount = 0;                // Number of bounces
    bool valid = false;                 // Whether prediction is valid
    bool landed = false;                // Whether grenade landed/detonated
    GrenadeType type = GrenadeType::Unknown;
    ThrowMode throwMode = ThrowMode::Normal;

    void Clear() {
        points.clear();
        bouncePoints.clear();
        landingPosition = Vec3{};
        initialVelocity = Vec3{};
        flightTime = 0.f;
        bounceCount = 0;
        valid = false;
        landed = false;
        type = GrenadeType::Unknown;
        throwMode = ThrowMode::Normal;
    }
};

// Physics configuration - all values configurable
struct GrenadePhysicsConfig {
    float gravity = 800.0f;              // Gravity in units/s^2 (CS2 default ~800)
    float timestep = 0.015625f;          // Simulation timestep (1/64 = 0.015625)
    float elasticity = 0.45f;            // Bounce elasticity (0.0 - 1.0)
    float friction = 0.8f;               // Surface friction
    float stopVelocity = 15.0f;          // Minimum velocity to continue simulation
    float playerVelocityContribution = 1.0f; // Player velocity contribution factor
    float maxSimulationTime = 20.0f;     // Maximum simulation time in seconds
    int maxBounces = 20;                 // Maximum number of bounces
    int maxSimulationSteps = 1280;       // Max simulation steps
    
    // Optional: air drag (not used by default predictor)
    float airDensity = 0.015f;           // Air density for drag
    float dragCoefficient = 0.3f;        // Drag coefficient
    
    // Grenade hull for collision
    Vec3 hullMins = Vec3{-2.0f, -2.0f, -2.0f};
    Vec3 hullMaxs = Vec3{2.0f, 2.0f, 2.0f};
    
    // Per-grenade type physics overrides
    struct TypeConfig {
        float initialVelocity = 0.0f;    // Base throw velocity
        float mass = 1.0f;               // Mass for physics
        float detonationTime = 0.0f;     // Auto-detonation time (0 = impact)
        bool isIncendiary = false;       // Creates fire pool
        float fireDuration = 0.0f;       // Fire duration for molotov/incendiary
        float fireRadius = 0.0f;         // Fire spread radius
    };
    
    TypeConfig typeConfigs[7]; // Indexed by GrenadeType
    
    GrenadePhysicsConfig() {
        // HEGrenade (index 43)
        typeConfigs[static_cast<size_t>(GrenadeType::HEGrenade)] = {
            .initialVelocity = 1500.0f,
            .mass = 1.0f,
            .detonationTime = 1.5f,
            .isIncendiary = false
        };
        
        // Flashbang (index 44)
        typeConfigs[static_cast<size_t>(GrenadeType::Flashbang)] = {
            .initialVelocity = 1500.0f,
            .mass = 1.0f,
            .detonationTime = 1.5f,
            .isIncendiary = false
        };
        
        // SmokeGrenade (index 45)
        typeConfigs[static_cast<size_t>(GrenadeType::SmokeGrenade)] = {
            .initialVelocity = 1350.0f,
            .mass = 1.0f,
            .detonationTime = 0.0f,
            .isIncendiary = false
        };
        
        // Molotov (index 46)
        typeConfigs[static_cast<size_t>(GrenadeType::Molotov)] = {
            .initialVelocity = 1000.0f,
            .mass = 1.0f,
            .detonationTime = 0.0f,
            .isIncendiary = true,
            .fireDuration = 7.0f,
            .fireRadius = 180.0f
        };
        
        // Decoy (index 47)
        typeConfigs[static_cast<size_t>(GrenadeType::Decoy)] = {
            .initialVelocity = 1200.0f,
            .mass = 1.0f,
            .detonationTime = 0.0f,
            .isIncendiary = false
        };
        
        // Incendiary (index 48)
        typeConfigs[static_cast<size_t>(GrenadeType::Incendiary)] = {
            .initialVelocity = 1000.0f,
            .mass = 1.0f,
            .detonationTime = 0.0f,
            .isIncendiary = true,
            .fireDuration = 7.0f,
            .fireRadius = 180.0f
        };
        
        // TAGrenade (index 202)
        typeConfigs[static_cast<size_t>(GrenadeType::TAGrenade)] = {
            .initialVelocity = 1200.0f,
            .mass = 1.0f,
            .detonationTime = 0.0f,
            .isIncendiary = false
        };
    }
};

// Trace result for collision detection
struct TraceResult {
    bool hit = false;
    Vec3 hitPosition;
    Vec3 hitNormal;
    float fraction = 1.0f;
    bool isWorld = false; // True if hit world geometry
};

// World collision interface
class IGrenadeWorld {
public:
    virtual ~IGrenadeWorld() = default;
    
    // Trace grenade hull from start to end
    virtual TraceResult TraceGrenade(const Vec3& start, const Vec3& end, const Vec3& hullMins, const Vec3& hullMaxs) const = 0;
    
    // Check if point is in solid
    virtual bool IsPointInSolid(const Vec3& point) const = 0;
};

// Trace result for cache key
struct GrenadePredictionKey {
    Vec3 eyePosition;
    Vec3 viewAngles;
    Vec3 playerVelocity;
    GrenadeType grenadeType = GrenadeType::Unknown;
    bool onGround = false;
    float throwStrength = 1.0f;
    uint64_t timestamp = 0; // For time-based invalidation

    bool operator==(const GrenadePredictionKey& other) const {
        constexpr float EPS = 0.1f;
        constexpr float ANGLE_EPS = 0.5f;
        
        return std::abs(eyePosition.x - other.eyePosition.x) < EPS &&
               std::abs(eyePosition.y - other.eyePosition.y) < EPS &&
               std::abs(eyePosition.z - other.eyePosition.z) < EPS &&
               std::abs(viewAngles.x - other.viewAngles.x) < ANGLE_EPS &&
               std::abs(viewAngles.y - other.viewAngles.y) < ANGLE_EPS &&
               std::abs(playerVelocity.x - other.playerVelocity.x) < EPS &&
               std::abs(playerVelocity.y - other.playerVelocity.y) < EPS &&
               std::abs(playerVelocity.z - other.playerVelocity.z) < EPS &&
               grenadeType == other.grenadeType &&
               onGround == other.onGround &&
               std::abs(throwStrength - other.throwStrength) < 0.01f;
    }
};

// Hash for unordered_map
struct GrenadePredictionKeyHash {
    size_t operator()(const GrenadePredictionKey& key) const noexcept {
        size_t h = 0;
        h ^= std::hash<float>{}(key.eyePosition.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(key.eyePosition.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(key.eyePosition.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(key.viewAngles.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(key.viewAngles.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(static_cast<int>(key.grenadeType)) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>{}(key.onGround) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Thread-safe snapshot for data exchange
struct GrenadeSnapshot {
    std::atomic<uint64_t> sequence{0};
    std::atomic<uint64_t> timestamp{0};
    
    struct InputState {
        Vec3 eyePosition;
        Vec3 viewAngles;
        Vec3 playerVelocity;
        Vec3 playerPosition;
        GrenadeType grenadeType = GrenadeType::Unknown;
        ThrowMode throwMode = ThrowMode::Normal;
        bool onGround = false;
        bool preparingThrow = false;
        bool pinPulled = false;
        float throwStrength = 1.0f;
    } input;
};

} // namespace CS2_Grenade