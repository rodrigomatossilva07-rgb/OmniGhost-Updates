#include "cs2_game_module.h"

#include "cs2_game.h"
#include "platform/expected.h"
#include "platform/logging.h"
#include "platform/app_paths.h"

#include <chrono>

namespace OmniGhost::Games {

CS2GameModule::CS2GameModule(OmniGhost::Platform::ServiceContainer& container)
    : GameModuleBase(container) {
    // Initialize runtime references from CS2 namespace
    runtimeRef_.ready = &CS2::ready;
    runtimeRef_.offsetsLoaded = &offsetsLoaded_;
}

Expected<void> CS2GameModule::Initialize(OmniGhost::Platform::ServiceContainer& container) {
    OGS_LOG_INFO("[CS2GameModule] Initializing...");

    // Load configuration
    auto loadResult = LoadConfig();
    if (!loadResult) {
        OGS_LOG_WARN("[CS2GameModule] Failed to load config: {}", loadResult.error().message);
    }

    // Load offsets
    auto offsetResult = LoadOffsets();
    if (!offsetResult) {
        OGS_LOG_ERROR("[CS2GameModule] Failed to load offsets: {}", offsetResult.error().message);
        return offsetResult;
    }

    OGS_LOG_INFO("[CS2GameModule] Initialized successfully");
    return Success();
}

Expected<void> CS2GameModule::Shutdown() {
    OGS_LOG_INFO("[CS2GameModule] Shutting down...");

    DisableFeatures();
    DetachFromGame();

    auto saveResult = SaveConfig();
    if (!saveResult) {
        OGS_LOG_WARN("[CS2GameModule] Failed to save config: {}", saveResult.error().message);
    }

    OGS_LOG_INFO("[CS2GameModule] Shutdown complete");
    return Success();
}

Expected<void> CS2GameModule::Update(float deltaTime) {
    if (!attached_) return Success();

    // Probe game state periodically
    static float probeTimer = 0.0f;
    probeTimer += deltaTime;
    if (probeTimer > 1.0f) {
        probeTimer = 0.0f;
        ProbeGameState();
    }

    return Success();
}

bool CS2GameModule::IsGameRunning() const noexcept {
    return inGame_;
}

bool CS2GameModule::IsAttached() const noexcept {
    return attached_;
}

bool CS2GameModule::IsInGame() const noexcept {
    return inGame_;
}

Expected<void> CS2GameModule::LoadOffsets() {
    OGS_LOG_INFO("[CS2GameModule] Loading offsets...");

    if (CS2::LoadOffsetsFromJson(nullptr)) {
        offsetsLoaded_ = true;
        if (runtimeRef_.offsetsLoaded) *runtimeRef_.offsetsLoaded = true;
        OGS_LOG_INFO("[CS2GameModule] Offsets loaded successfully");
        return Success();
    }

    return Failure<void>(ErrorCode::OffsetsMissing, "Failed to load CS2 offsets");
}

Expected<void> CS2GameModule::ReloadOffsets() {
    OGS_LOG_INFO("[CS2GameModule] Reloading offsets...");
    offsetsLoaded_ = false;
    return LoadOffsets();
}

bool CS2GameModule::HasValidOffsets() const noexcept {
    return offsetsLoaded_;
}

Expected<void> CS2GameModule::EnableFeatures() {
    if (featuresEnabled_) return Success();

    OGS_LOG_INFO("[CS2GameModule] Enabling features...");

    // Enable CS2-specific features
    featuresEnabled_ = true;
    OGS_LOG_INFO("[CS2GameModule] Features enabled");
    return Success();
}

Expected<void> CS2GameModule::DisableFeatures() {
    if (!featuresEnabled_) return Success();

    OGS_LOG_INFO("[CS2GameModule] Disabling features...");

    featuresEnabled_ = false;
    OGS_LOG_INFO("[CS2GameModule] Features disabled");
    return Success();
}

void CS2GameModule::DrawSettings() {
    // Draw CS2-specific settings UI
    // This would be called from the main UI loop
}

void CS2GameModule::DrawDebugInfo() {
    // Draw debug information
}

Expected<void> CS2GameModule::SaveConfig() {
    // Save CS2-specific configuration
    return Success();
}

Expected<void> CS2GameModule::LoadConfig() {
    // Load CS2-specific configuration
    return Success();
}

std::string CS2GameModule::GetDiagnosticsInfo() const {
    std::string info = "[CS2GameModule] ";
    info += "Attached: " + std::string(attached_ ? "Yes" : "No") + " ";
    info += "InGame: " + std::string(inGame_ ? "Yes" : "No") + " ";
    info += "Offsets: " + std::string(offsetsLoaded_ ? "Loaded" : "Not Loaded") + " ";
    info += "Features: " + std::string(featuresEnabled_ ? "Enabled" : "Disabled");
    return info;
}

// Private helpers

Expected<void> CS2GameModule::AttachToGame() {
    if (attached_) return Success();

    OGS_LOG_INFO("[CS2GameModule] Attaching to game...");

    // Use existing CS2 attach logic
    if (CS2::Attach()) {
        attached_ = true;
        *runtimeRef_.ready = true;
        OGS_LOG_INFO("[CS2GameModule] Attached successfully");
        return Success();
    }

    return Failure<void>(ErrorCode::DmaError, "Failed to attach to CS2");
}

Expected<void> CS2GameModule::DetachFromGame() {
    if (!attached_) return Success();

    OGS_LOG_INFO("[CS2GameModule] Detaching from game...");

    // Use existing CS2 detach logic
    CS2::Shutdown();
    CS2::ready = false;
    attached_ = false;
    inGame_ = false;

    OGS_LOG_INFO("[CS2GameModule] Detached successfully");
    return Success();
}

Expected<void> CS2GameModule::ProbeGameState() {
    if (!attached_) return Success();

    // Check if we're in a match
    // This would use existing CS2 logic to detect game state
    // For now, just return success
    return Success();
}

} // namespace OmniGhost::Games
