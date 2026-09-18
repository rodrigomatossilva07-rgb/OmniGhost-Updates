#pragma once
#include "cs2_grenade_types.h"
#include "cs2_grenade_predictor.h"
#include "cs2_game.h"
#include <imgui.h>
#include <memory>
#include <atomic>
#include <chrono>
#include <mutex>

namespace CS2_Grenade {

// Configuration for grenade helper
struct GrenadeHelperConfig {
    bool enabled = true;
    bool showTrajectory = true;
    bool showBounceMarkers = true;
    bool showLandingMarker = true;
    bool showLandingPreview = true;
    bool showRadarMarker = true;
    bool showDebugPanel = false;
    
    // Visual settings
    float trajectoryThickness = 2.0f;
    float bounceMarkerSize = 6.0f;
    float landingMarkerSize = 10.0f;
    ImU32 trajectoryColor = IM_COL32(255, 255, 100, 255);
    ImU32 bounceColor = IM_COL32(255, 200, 50, 255);
    ImU32 landingColor = IM_COL32(100, 255, 100, 255);
    ImU32 previewBgColor = IM_COL32(20, 20, 30, 230);
    ImU32 previewTextColor = IM_COL32(255, 255, 255, 255);
    
    // Preview window
    float previewWindowWidth = 250.0f;
    float previewWindowHeight = 200.0f;
    
    // Physics config (will be synced with predictor)
    GrenadePhysicsConfig physicsConfig;
};

class GrenadeHelper {
public:
    GrenadeHelper();
    ~GrenadeHelper() = default;
    
    // Initialize
    bool Initialize();
    void Shutdown();
    
    // Main update - call every frame
    void Update();
    
    // Render trajectory and markers
    void Render(const CS2::Runtime& rt, const CS2::Config& cfg);
    
    // Render landing preview window
    void RenderLandingPreview();
    
    // Render debug panel
    void RenderDebugPanel();
    
    // Configuration
    GrenadeHelperConfig& GetConfig() { return config_; }
    const GrenadeHelperConfig& GetConfig() const { return config_; }
    
    // Get predictor for external use
    GrenadePredictor& GetPredictor() { return *predictor_; }
    const GrenadePredictor& GetPredictor() const { return *predictor_; }
    
    // Get snapshot for thread-safe access
    const GrenadeSnapshot* GetSnapshot() const { return &snapshot_; }
    
    // Check if grenade helper is active
    bool IsActive() const { return config_.enabled && active_; }
    
    // Set active state
    void SetActive(bool active) { active_ = active; }
    bool IsActiveState() const { return active_; }

private:
    // Read grenade input state from DMA
    bool ReadGrenadeInputState();
    
    // Update snapshot with latest data
    void UpdateSnapshot();
    
    // Check if local player is holding a grenade
    bool IsHoldingGrenade() const;
    
    // Get grenade type from weapon definition index
    GrenadeType GetGrenadeTypeFromDefIndex(int defIndex) const;
    
    // Determine throw mode from input state
    ThrowMode DetermineThrowMode() const;
    
    // Read player input for throw mode
    void UpdateThrowMode();
    
    // Render trajectory
    void RenderTrajectory(const GrenadePrediction& prediction, const float* viewMatrix);
    
    // Render bounce markers
    void RenderBounceMarkers(const GrenadePrediction& prediction, const float* viewMatrix);
    
    // Render landing marker
    void RenderLandingMarker(const GrenadePrediction& prediction, const float* viewMatrix);
    
    // Render radar marker
    void RenderRadarMarker(const GrenadePrediction& prediction, const CS2::Runtime& rt);

    // Predictor (must be pointer due to std::mutex)
    std::unique_ptr<GrenadePredictor> predictor_;
    
    // Config
    GrenadeHelperConfig config_;
    
    // State
    std::atomic<bool> active_{false};
    std::atomic<bool> lastPredictValid_{false};
    GrenadePrediction lastPrediction_;
    uint64_t lastPredictionSequence_{0};
    mutable std::mutex predictionMutex_;
    
    // Snapshot for thread-safe data exchange
    GrenadeSnapshot snapshot_;
    
    // Throw mode detection
    std::atomic<int> leftClickState_{0};
    std::atomic<int> rightClickState_{0};
    std::atomic<float> throwStrength_{1.0f};
    std::atomic<bool> jumpThrowDetected_{false};
    
    // Timing
    std::chrono::steady_clock::time_point lastUpdate_;
    std::chrono::steady_clock::time_point lastPredictionTime_;
};

} // namespace CS2_Grenade