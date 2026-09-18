#include "cs2_grenade_helper.h"
#include "cs2_game.h"
#include "Memory/Memory.h"
#include <imgui.h>
#include <cmath>

extern Memory mem;

namespace CS2_Grenade {

// Local WorldToScreen function
static bool W2S(const Vec3& world, const float* vm, Vec2& screen) {
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float clipX = world.x * vm[0]  + world.y * vm[1]  + world.z * vm[2]  + vm[3];
    const float clipY = world.x * vm[4]  + world.y * vm[5]  + world.z * vm[6]  + vm[7];
    const float clipW = world.x * vm[12] + world.y * vm[13] + world.z * vm[14] + vm[15];
    if (!std::isfinite(clipW) || clipW < 0.01f) return false;
    float inv = 1.f / clipW;
    screen.x = (ds.x * 0.5f) + (0.5f * clipX * inv * ds.x);
    screen.y = (ds.y * 0.5f) - (0.5f * clipY * inv * ds.y);
    return std::isfinite(screen.x) && std::isfinite(screen.y);
}

GrenadeHelper::GrenadeHelper() {
    predictor_ = std::make_unique<GrenadePredictor>();
    config_.physicsConfig = predictor_->GetConfig();
    lastUpdate_ = std::chrono::steady_clock::now();
    lastPredictionTime_ = std::chrono::steady_clock::now();
}

bool GrenadeHelper::Initialize() {
    config_.physicsConfig = predictor_->GetConfig();
    predictor_->SetConfig(config_.physicsConfig);
    active_ = true;
    return true;
}

void GrenadeHelper::Shutdown() {
    active_ = false;
    predictor_->ClearCache();
}

void GrenadeHelper::Update() {
    if (!active_ || !config_.enabled) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    lastUpdate_ = now;
    
    UpdateThrowMode();
    
    if (!IsHoldingGrenade()) {
        lastPredictValid_ = false;
        return;
    }
    
    if (!ReadGrenadeInputState()) {
        return;
    }
    
    UpdateSnapshot();
    
    auto& offsets = CS2::offsets;
    uint64_t clientBase = CS2::runtime.client_base;
    
    uint64_t localPlayerPawn = mem.Read<uint64_t>(clientBase + offsets.dwLocalPlayerPawn);
    if (!localPlayerPawn) {
        lastPredictValid_ = false;
        return;
    }
    
    Vec3 eyePos = mem.Read<Vec3>(localPlayerPawn + offsets.m_vecAbsOrigin);
    eyePos.z += 64.0f;
    
    Vec3 viewAngles = mem.Read<Vec3>(clientBase + offsets.dwViewAngles);
    
    ThrowMode mode = DetermineThrowMode();
    float strength = throwStrength_.load();
    
    uint64_t activeWeapon = mem.Read<uint64_t>(localPlayerPawn + offsets.m_pClippingWeapon);
    if (!activeWeapon) {
        activeWeapon = mem.Read<uint64_t>(localPlayerPawn + offsets.m_hActiveWeapon);
        activeWeapon = mem.Read<uint64_t>(clientBase + offsets.dwEntityList + ((activeWeapon & 0x7FFF) >> 9) * 0x78);
    }
    
    int defIndex = 0;
    if (activeWeapon) {
        defIndex = mem.Read<int>(activeWeapon + offsets.m_iItemDefinitionIndex);
    }
    
    GrenadeType type = GetGrenadeTypeFromDefIndex(defIndex);
    if (type == GrenadeType::Unknown) {
        lastPredictValid_ = false;
        return;
    }
    
    GrenadeInputState inputState;
    inputState.eyePosition = eyePos;
    inputState.viewAngles = viewAngles;
    inputState.throwMode = mode;
    inputState.throwStrength = strength;
    inputState.grenadeType = type;
    inputState.jumpThrow = jumpThrowDetected_.load();
    inputState.runThrow = false;
    inputState.tickCount = mem.Read<int>(clientBase + offsets.dwGlobalVars + 0x10);
    inputState.sequence = ++lastPredictionSequence_;
    
    GrenadePrediction prediction = predictor_->Predict(inputState);
    if (prediction.valid) {
        std::lock_guard<std::mutex> lock(predictionMutex_);
        lastPrediction_ = prediction;
        lastPredictValid_ = true;
        lastPredictionTime_ = now;
    }
}

void GrenadeHelper::Render(const CS2::Runtime& rt, const CS2::Config& /*cfg*/) {
    if (!IsActive()) return;
    if (!lastPredictValid_) return;
    
    GrenadePrediction prediction;
    {
        std::lock_guard<std::mutex> lock(predictionMutex_);
        if (!lastPredictValid_) return;
        prediction = lastPrediction_;
    }
    
    const float* viewMatrix = rt.view_matrix;
    
    if (config_.showTrajectory) {
        RenderTrajectory(prediction, viewMatrix);
    }
    
    if (config_.showBounceMarkers) {
        RenderBounceMarkers(prediction, viewMatrix);
    }
    
    if (config_.showLandingMarker) {
        RenderLandingMarker(prediction, viewMatrix);
    }
    
    if (config_.showRadarMarker) {
        RenderRadarMarker(prediction, rt);
    }
    
    if (config_.showLandingPreview) {
        RenderLandingPreview();
    }
    
    if (config_.showDebugPanel) {
        RenderDebugPanel();
    }
}

void GrenadeHelper::RenderLandingPreview() {
    if (!lastPredictValid_) return;
    
    GrenadePrediction prediction;
    {
        std::lock_guard<std::mutex> lock(predictionMutex_);
        prediction = lastPrediction_;
    }
    
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - config_.previewWindowWidth - 20, 20), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(config_.previewWindowWidth, config_.previewWindowHeight), ImGuiCond_Once);
    
    if (ImGui::Begin("Grenade Landing Preview", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        if (prediction.landed) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 1.0f, 0.4f, 1.0f));
            ImGui::Text("LANDING POSITION");
            ImGui::PopStyleColor();
            
            ImGui::Separator();
            
            ImGui::Text("X: %.1f", prediction.landingPosition.x);
            ImGui::Text("Y: %.1f", prediction.landingPosition.y);
            ImGui::Text("Z: %.1f", prediction.landingPosition.z);
            
            ImGui::Separator();
            
            ImGui::Text("Distance: %.1f m", prediction.flightTime > 0 ? prediction.flightTime * 100 : 0);
            ImGui::Text("Flight Time: %.2f s", prediction.flightTime);
            ImGui::Text("Bounces: %d", prediction.bounceCount);
            
            const char* typeNames[] = {"Unknown", "HE", "Flash", "Smoke", "Molotov", "Decoy", "Incendiary", "TA"};
            ImGui::Text("Type: %s", typeNames[static_cast<int>(prediction.type)]);
            
            const char* modeNames[] = {"Normal", "Jump Throw", "Run Throw", "Walk Throw", "Crouch Throw"};
            ImGui::Text("Mode: %s", modeNames[static_cast<int>(prediction.throwMode)]);
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
            ImGui::Text("IN FLIGHT...");
            ImGui::PopStyleColor();
            
            ImGui::Separator();
            
            ImGui::Text("Time: %.2f s", prediction.flightTime);
            ImGui::Text("Pos: %.1f, %.1f, %.1f", 
                prediction.points.empty() ? 0 : prediction.points.back().x,
                prediction.points.empty() ? 0 : prediction.points.back().y,
                prediction.points.empty() ? 0 : prediction.points.back().z);
        }
    }
    ImGui::End();
}

void GrenadeHelper::RenderDebugPanel() {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(300, 400), ImGuiCond_Once);
    
    if (ImGui::Begin("Grenade Debug", &config_.showDebugPanel)) {
        ImGui::Checkbox("Enabled", &config_.enabled);
        ImGui::Checkbox("Show Trajectory", &config_.showTrajectory);
        ImGui::Checkbox("Show Bounces", &config_.showBounceMarkers);
        ImGui::Checkbox("Show Landing", &config_.showLandingMarker);
        ImGui::Checkbox("Show Preview", &config_.showLandingPreview);
        ImGui::Checkbox("Show Radar", &config_.showRadarMarker);
        
        ImGui::Separator();
        
        if (ImGui::CollapsingHeader("Physics Config")) {
            auto& physics = config_.physicsConfig;
            ImGui::DragFloat("Gravity", &physics.gravity, 1.0f, 500.0f, 1000.0f);
            ImGui::DragFloat("Timestep", &physics.timestep, 0.001f, 0.005f, 0.03f);
            ImGui::DragFloat("Elasticity", &physics.elasticity, 0.01f, 0.1f, 1.0f);
            ImGui::DragFloat("Friction", &physics.friction, 0.01f, 0.1f, 1.0f);
            ImGui::DragFloat("Stop Velocity", &physics.stopVelocity, 1.0f, 50.0f);
            ImGui::DragFloat("Player Vel Contrib", &physics.playerVelocityContribution, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Max Time", &physics.maxSimulationTime, 0.1f, 5.0f, 30.0f);
            ImGui::DragInt("Max Bounces", &physics.maxBounces, 1, 5, 50);
            ImGui::DragInt("Max Steps", &physics.maxSimulationSteps, 1, 100, 5000);
            
            if (ImGui::Button("Apply Physics Config")) {
                predictor_->SetConfig(config_.physicsConfig);
            }
        }
        
        ImGui::Separator();
        
        if (lastPredictValid_) {
            GrenadePrediction prediction;
            {
                std::lock_guard<std::mutex> lock(predictionMutex_);
                prediction = lastPrediction_;
            }
            
            ImGui::Text("Last Prediction:");
            ImGui::Text("  Valid: Yes");
            ImGui::Text("  Landed: %s", prediction.landed ? "Yes" : "No");
            ImGui::Text("  Flight Time: %.2f s", prediction.flightTime);
            ImGui::Text("  Trajectory Points: %zu", prediction.points.size());
            ImGui::Text("  Bounces: %d", prediction.bounceCount);
            ImGui::Text("  Landing: %.1f, %.1f, %.1f", 
                prediction.landingPosition.x, prediction.landingPosition.y, prediction.landingPosition.z);
        } else {
            ImGui::Text("Last Prediction: None");
        }
        
        ImGui::Separator();
        
        ImGui::Text("Throw Mode: %d", static_cast<int>(DetermineThrowMode()));
        ImGui::Text("Throw Strength: %.2f", throwStrength_.load());
        ImGui::Text("Jump Throw: %s", jumpThrowDetected_.load() ? "Yes" : "No");
    }
    ImGui::End();
}

bool GrenadeHelper::ReadGrenadeInputState() {
    auto& offsets = CS2::offsets;
    uint64_t clientBase = CS2::runtime.client_base;
    
    uint64_t localPlayerPawn = mem.Read<uint64_t>(clientBase + offsets.dwLocalPlayerPawn);
    if (!localPlayerPawn) return false;
    
    Vec3 viewAngles = mem.Read<Vec3>(clientBase + offsets.dwViewAngles);
    
    UpdateThrowMode();
    
    return true;
}

void GrenadeHelper::UpdateSnapshot() {
}

bool GrenadeHelper::IsHoldingGrenade() const {
    auto& offsets = CS2::offsets;
    uint64_t clientBase = CS2::runtime.client_base;
    
    uint64_t localPlayerPawn = mem.Read<uint64_t>(clientBase + offsets.dwLocalPlayerPawn);
    if (!localPlayerPawn) return false;
    
    uint64_t activeWeapon = mem.Read<uint64_t>(localPlayerPawn + offsets.m_pClippingWeapon);
    if (!activeWeapon) {
        int weaponHandle = mem.Read<int>(localPlayerPawn + offsets.m_hActiveWeapon);
        if (weaponHandle) {
            activeWeapon = mem.Read<uint64_t>(clientBase + offsets.dwEntityList + ((weaponHandle & 0x7FFF) >> 9) * 0x78);
        }
    }
    
    if (!activeWeapon) return false;
    
    int defIndex = mem.Read<int>(activeWeapon + offsets.m_iItemDefinitionIndex);
    GrenadeType type = GetGrenadeTypeFromDefIndex(defIndex);
    
    return type != GrenadeType::Unknown;
}

GrenadeType GrenadeHelper::GetGrenadeTypeFromDefIndex(int defIndex) const {
    switch (defIndex) {
        case 43: return GrenadeType::HEGrenade;
        case 44: return GrenadeType::Flashbang;
        case 45: return GrenadeType::SmokeGrenade;
        case 46: return GrenadeType::Molotov;
        case 47: return GrenadeType::Decoy;
        case 48: return GrenadeType::Incendiary;
        case 202: return GrenadeType::TAGrenade;
        default: return GrenadeType::Unknown;
    }
}

ThrowMode GrenadeHelper::DetermineThrowMode() const {
    int left = leftClickState_.load();
    int right = rightClickState_.load();
    
    if (jumpThrowDetected_.load()) {
        return ThrowMode::JumpThrow;
    }
    
    if (left && right) {
        return ThrowMode::Normal;
    }
    
    if (left && !right) {
        return ThrowMode::Normal;
    }
    
    if (!left && right) {
        return ThrowMode::Normal;
    }
    
    return ThrowMode::Normal;
}

void GrenadeHelper::UpdateThrowMode() {
    auto& offsets = CS2::offsets;
    uint64_t clientBase = CS2::runtime.client_base;
    
    uint64_t localPlayerPawn = mem.Read<uint64_t>(clientBase + offsets.dwLocalPlayerPawn);
    if (localPlayerPawn) {
        uint64_t activeWeapon = mem.Read<uint64_t>(localPlayerPawn + offsets.m_pClippingWeapon);
        if (activeWeapon) {
            float throwTime = mem.Read<float>(activeWeapon + offsets.m_flThrowTime);
            float throwStrength = mem.Read<float>(activeWeapon + offsets.m_flThrowStrength);
            
            if (throwTime > 0) {
                throwStrength_.store(throwStrength);
            }
        }
    }
}

void GrenadeHelper::RenderTrajectory(const GrenadePrediction& prediction, const float* viewMatrix) {
    if (prediction.points.size() < 2) return;
    
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    
    for (size_t i = 1; i < prediction.points.size(); ++i) {
        Vec2 s1, s2;
        if (W2S(prediction.points[i - 1], viewMatrix, s1) && W2S(prediction.points[i], viewMatrix, s2)) {
            draw->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), config_.trajectoryColor, config_.trajectoryThickness);
        }
    }
}

void GrenadeHelper::RenderBounceMarkers(const GrenadePrediction& prediction, const float* viewMatrix) {
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    
    for (size_t i = 0; i < prediction.bouncePoints.size(); ++i) {
        Vec2 screen;
        if (W2S(prediction.bouncePoints[i], viewMatrix, screen)) {
            float size = config_.bounceMarkerSize;
            draw->AddCircleFilled(ImVec2(screen.x, screen.y), size, config_.bounceColor);
            draw->AddCircle(ImVec2(screen.x, screen.y), size + 1, IM_COL32(0, 0, 0, 255), 0, 1.0f);
            
            char buf[16];
            sprintf_s(buf, "%zu", i + 1);
            draw->AddText(ImVec2(screen.x - 4, screen.y - 8), IM_COL32(255, 255, 255, 255), buf);
        }
    }
}

void GrenadeHelper::RenderLandingMarker(const GrenadePrediction& prediction, const float* viewMatrix) {
    if (!prediction.landed) return;
    
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    
    Vec2 screen;
    if (W2S(prediction.landingPosition, viewMatrix, screen)) {
        float size = config_.landingMarkerSize;
        
        draw->AddLine(
            ImVec2(screen.x - size, screen.y),
            ImVec2(screen.x + size, screen.y),
            config_.landingColor, 2.0f
        );
        draw->AddLine(
            ImVec2(screen.x, screen.y - size),
            ImVec2(screen.x, screen.y + size),
            config_.landingColor, 2.0f
        );
        
        draw->AddCircle(ImVec2(screen.x, screen.y), size + 2, IM_COL32(0, 0, 0, 200), 0, 2.0f);
        draw->AddCircle(ImVec2(screen.x, screen.y), size + 4, config_.landingColor, 0, 1.0f);
        
        char buf[64];
        sprintf_s(buf, "%.1fm", prediction.flightTime * 100);
        draw->AddText(ImVec2(screen.x + size + 5, screen.y - 8), config_.previewTextColor, buf);
    }
}

void GrenadeHelper::RenderRadarMarker(const GrenadePrediction& prediction, const CS2::Runtime& /*rt*/) {
    if (!prediction.landed) return;
}

} // namespace CS2_Grenade