#pragma once

#include "interfaces.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Feature Flags - Runtime + Compile-time feature toggles
// ============================================================

// Compile-time feature flags (set via CMake/MSBuild)
// These are evaluated at compile time for zero overhead
// Define in build: /D OMNIGHOST_FEATURE_RADAR_WEB /D OMNIGHOST_FEATURE_VEHICLE_ESP etc.

// Runtime feature flags (configurable at runtime)
class FeatureFlags {
public:
    // Flag definition
    struct Flag {
        std::string name;
        std::string description;
        bool defaultValue = false;
        bool value = false;
        bool requiresRestart = false;  // If true, change requires app restart
        std::vector<std::string> dependencies;  // Other flags that must be enabled
        std::string category;  // For UI grouping: "aim", "visuals", "radar", "experimental"
        bool isExperimental = false;
        bool isHidden = false;  // Hidden from UI, internal only
    };

    // Built-in flag categories
    static constexpr std::string_view CategoryCore = "core";
    static constexpr std::string_view CategoryAim = "aim";
    static constexpr std::string_view CategoryVisuals = "visuals";
    static constexpr std::string_view CategoryRadar = "radar";
    static constexpr std::string_view CategoryHardware = "hardware";
    static constexpr std::string_view CategoryExperimental = "experimental";
    static constexpr std::string_view CategoryPerformance = "performance";

    FeatureFlags() = default;
    ~FeatureFlags() = default;

    // Register a flag (idempotent)
    void Register(const Flag& flag) {
        std::unique_lock lock(mutex_);
        auto& existing = flags_[flag.name];
        existing = flag;
        existing.value = flag.defaultValue;
    }

    // Register multiple flags
    void RegisterAll(const std::vector<Flag>& flags) {
        for (const auto& flag : flags) Register(flag);
    }

    // Check if a flag is enabled (runtime)
    [[nodiscard]] bool IsEnabled(std::string_view name) const noexcept {
        std::shared_lock lock(mutex_);
        auto it = flags_.find(std::string(name));
        return it != flags_.end() && it->second.value;
    }

    // Get flag value (returns default if not registered)
    [[nodiscard]] bool Get(std::string_view name, bool defaultValue = false) const noexcept {
        std::shared_lock lock(mutex_);
        auto it = flags_.find(std::string(name));
        return it != flags_.end() ? it->second.value : defaultValue;
    }

    // Set flag value (returns true if changed)
    bool Set(std::string_view name, bool value) {
        std::unique_lock lock(mutex_);
        auto it = flags_.find(std::string(name));
        if (it == flags_.end()) return false;
        
        if (it->second.value == value) return false;
        
        // Check dependencies
        if (value) {
            for (const auto& dep : it->second.dependencies) {
                auto depIt = flags_.find(dep);
                if (depIt == flags_.end() || !depIt->second.value) {
                    return false;  // Dependency not satisfied
                }
            }
        }
        
        it->second.value = value;
        
        // Notify listeners
        if (onChange_) {
            lock.unlock();
            onChange_(std::string(name), value);
        }
        
        return true;
    }

    // Toggle flag
    bool Toggle(std::string_view name) {
        std::shared_lock lock(mutex_);
        auto it = flags_.find(std::string(name));
        if (it == flags_.end()) return false;
        const bool next = !it->second.value;
        lock.unlock();
        return Set(name, next);
    }

    // Get flag metadata
    [[nodiscard]] std::optional<Flag> GetFlag(std::string_view name) const noexcept {
        std::shared_lock lock(mutex_);
        auto it = flags_.find(std::string(name));
        if (it == flags_.end()) return std::nullopt;
        return it->second;
    }

    // Get all flags
    [[nodiscard]] std::vector<Flag> GetAllFlags() const noexcept {
        std::shared_lock lock(mutex_);
        std::vector<Flag> result;
        result.reserve(flags_.size());
        for (const auto& [_, flag] : flags_) {
            result.push_back(flag);
        }
        return result;
    }

    // Get flags by category
    [[nodiscard]] std::vector<Flag> GetFlagsByCategory(std::string_view category) const noexcept {
        std::shared_lock lock(mutex_);
        std::vector<Flag> result;
        for (const auto& [_, flag] : flags_) {
            if (flag.category == category) result.push_back(flag);
        }
        return result;
    }

    // Get experimental flags
    [[nodiscard]] std::vector<Flag> GetExperimentalFlags() const noexcept {
        std::shared_lock lock(mutex_);
        std::vector<Flag> result;
        for (const auto& [_, flag] : flags_) {
            if (flag.isExperimental) result.push_back(flag);
        }
        return result;
    }

    // Get visible flags (not hidden)
    [[nodiscard]] std::vector<Flag> GetVisibleFlags() const noexcept {
        std::shared_lock lock(mutex_);
        std::vector<Flag> result;
        for (const auto& [_, flag] : flags_) {
            if (!flag.isHidden) result.push_back(flag);
        }
        return result;
    }

    // Set change callback
    void SetChangeCallback(std::function<void(std::string_view, bool)> callback) {
        std::unique_lock lock(mutex_);
        onChange_ = std::move(callback);
    }

    // Reset all flags to defaults
    void ResetToDefaults() {
        std::unique_lock lock(mutex_);
        for (auto& [_, flag] : flags_) {
            flag.value = flag.defaultValue;
        }
    }

    // Export to JSON string
    [[nodiscard]] std::string ToJson() const noexcept {
        std::shared_lock lock(mutex_);
        std::string json = "{";
        bool first = true;
        for (const auto& [name, flag] : flags_) {
            if (!first) json += ",";
            first = false;
            json += "\"" + name + "\":" + (flag.value ? "true" : "false");
        }
        json += "}";
        return json;
    }

    // Import from JSON string
    bool FromJson(std::string_view json) {
        // Simple JSON parsing for flat object
        std::unique_lock lock(mutex_);
        std::string str(json);
        size_t pos = 0;
        while ((pos = str.find('"', pos)) != std::string::npos) {
            size_t end = str.find('"', pos + 1);
            if (end == std::string::npos) break;
            std::string name = str.substr(pos + 1, end - pos - 1);
            pos = end + 1;
            if (pos >= str.size()) break;
            if (str[pos] != ':') { pos++; continue; }
            pos++;
            while (pos < str.size() && std::isspace(str[pos])) pos++;
            bool value = false;
            if (pos + 4 <= str.size() && str.substr(pos, 4) == "true") {
                value = true;
                pos += 4;
            } else if (pos + 5 <= str.size() && str.substr(pos, 5) == "false") {
                value = false;
                pos += 5;
            } else {
                continue;
            }
            auto it = flags_.find(name);
            if (it != flags_.end()) {
                it->second.value = value;
            }
        }
        return true;
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Flag> flags_;
    std::function<void(std::string_view, bool)> onChange_;
};

// ============================================================
// Compile-time feature detection
// ============================================================

// Usage: if constexpr (Feature<FeatureFlags::RadarWeb>::enabled) { ... }
template <typename T>
struct Feature {
    static constexpr bool enabled = false;
};

// Helper to check compile-time features
#define OMNIGHOST_HAS_FEATURE(name) (defined(OMNIGHOST_FEATURE_##name) && OMNIGHOST_FEATURE_##name)

// Common compile-time features (defined in build system)
#ifndef OMNIGHOST_FEATURE_RADAR_WEB
#define OMNIGHOST_FEATURE_RADAR_WEB 0
#endif

#ifndef OMNIGHOST_FEATURE_VEHICLE_ESP
#define OMNIGHOST_FEATURE_VEHICLE_ESP 0
#endif

#ifndef OMNIGHOST_FEATURE_SOUND_ESP
#define OMNIGHOST_FEATURE_SOUND_ESP 0
#endif

#ifndef OMNIGHOST_FEATURE_RECOIL_CONTROL
#define OMNIGHOST_FEATURE_RECOIL_CONTROL 0
#endif

#ifndef OMNIGHOST_FEATURE_TRIGGERBOT
#define OMNIGHOST_FEATURE_TRIGGERBOT 0
#endif

#ifndef OMNIGHOST_FEATURE_LEGIT_MODE
#define OMNIGHOST_FEATURE_LEGIT_MODE 0
#endif

#ifndef OMNIGHOST_FEATURE_SCRIPTING
#define OMNIGHOST_FEATURE_SCRIPTING 0
#endif

#ifndef OMNIGHOST_FEATURE_CLOUD_SYNC
#define OMNIGHOST_FEATURE_CLOUD_SYNC 0
#endif

#ifndef OMNIGHOST_FEATURE_HOT_RELOAD_OFFSETS
#define OMNIGHOST_FEATURE_HOT_RELOAD_OFFSETS 0
#endif

// Compile-time feature specializations
template <> struct Feature<struct RadarWebTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_RADAR_WEB; };
template <> struct Feature<struct VehicleEspTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_VEHICLE_ESP; };
template <> struct Feature<struct SoundEspTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_SOUND_ESP; };
template <> struct Feature<struct RecoilControlTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_RECOIL_CONTROL; };
template <> struct Feature<struct TriggerbotTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_TRIGGERBOT; };
template <> struct Feature<struct LegitModeTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_LEGIT_MODE; };
template <> struct Feature<struct ScriptingTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_SCRIPTING; };
template <> struct Feature<struct CloudSyncTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_CLOUD_SYNC; };
template <> struct Feature<struct HotReloadOffsetsTag> { static constexpr bool enabled = OMNIGHOST_FEATURE_HOT_RELOAD_OFFSETS; };

// ============================================================
// Runtime feature flag definitions
// ============================================================

// Standard feature flag names (used as string keys)
namespace FeatureFlag {
    // Core
    inline constexpr std::string_view EnableDebugOverlay = "core.debug_overlay";
    inline constexpr std::string_view EnableSessionLog = "core.session_log";
    inline constexpr std::string_view EnableCrashReporting = "core.crash_reporting";
    inline constexpr std::string_view EnableTelemetry = "core.telemetry";

    // Aim
    inline constexpr std::string_view EnableAim = "aim.enabled";
    inline constexpr std::string_view EnableSmoothAim = "aim.smooth";
    inline constexpr std::string_view EnablePrediction = "aim.prediction";
    inline constexpr std::string_view EnableRecoilControl = "aim.recoil_control";
    inline constexpr std::string_view EnableTriggerbot = "aim.triggerbot";
    inline constexpr std::string_view EnableLegitMode = "aim.legit_mode";
    inline constexpr std::string_view EnableDynamicFov = "aim.dynamic_fov";

    // Visuals
    inline constexpr std::string_view EnableEsp = "visuals.esp";
    inline constexpr std::string_view EnableBoxEsp = "visuals.box";
    inline constexpr std::string_view EnableSkeletonEsp = "visuals.skeleton";
    inline constexpr std::string_view EnableHealthEsp = "visuals.health";
    inline constexpr std::string_view EnableWeaponEsp = "visuals.weapon";
    inline constexpr std::string_view EnableDistanceEsp = "visuals.distance";
    inline constexpr std::string_view EnableVehicleEsp = "visuals.vehicle";
    inline constexpr std::string_view EnableItemEsp = "visuals.item";
    inline constexpr std::string_view EnableSoundEsp = "visuals.sound_esp";
    inline constexpr std::string_view EnableCrosshair = "visuals.crosshair";

    // Radar
    inline constexpr std::string_view EnableRadar = "radar.enabled";
    inline constexpr std::string_view EnableRadarWeb = "radar.web";
    inline constexpr std::string_view EnableRadar3D = "radar.3d";

    // Hardware
    inline constexpr std::string_view EnableMakcu = "hardware.makcu";
    inline constexpr std::string_view EnableKMBox = "hardware.kmbox";
    inline constexpr std::string_view EnableFerrum = "hardware.ferrum";
    inline constexpr std::string_view EnableAutoDetect = "hardware.auto_detect";

    // Experimental
    inline constexpr std::string_view EnableHotReloadOffsets = "experimental.hot_reload_offsets";
    inline constexpr std::string_view EnableScripting = "experimental.scripting";
    inline constexpr std::string_view EnableCloudSync = "experimental.cloud_sync";
    inline constexpr std::string_view EnablePluginSystem = "experimental.plugin_system";

    // Performance
    inline constexpr std::string_view EnablePerformanceMode = "performance.mode";
    inline constexpr std::string_view EnableAdaptiveQuality = "performance.adaptive";
    inline constexpr std::string_view EnableFrameBudget = "performance.frame_budget";

    // Category aliases (FeatureFlags also defines these; keep both for call sites)
    inline constexpr std::string_view CategoryCore = FeatureFlags::CategoryCore;
    inline constexpr std::string_view CategoryAim = FeatureFlags::CategoryAim;
    inline constexpr std::string_view CategoryVisuals = FeatureFlags::CategoryVisuals;
    inline constexpr std::string_view CategoryRadar = FeatureFlags::CategoryRadar;
    inline constexpr std::string_view CategoryHardware = FeatureFlags::CategoryHardware;
    inline constexpr std::string_view CategoryExperimental = FeatureFlags::CategoryExperimental;
    inline constexpr std::string_view CategoryPerformance = FeatureFlags::CategoryPerformance;
}


inline FeatureFlags::Flag MakeFlag(
    std::string_view name,
    std::string description,
    bool defaultValue,
    bool requiresRestart,
    std::vector<std::string> dependencies,
    std::string_view category,
    bool isExperimental = false,
    bool isHidden = false) {
    FeatureFlags::Flag f;
    f.name = std::string(name);
    f.description = std::move(description);
    f.defaultValue = defaultValue;
    f.value = defaultValue;
    f.requiresRestart = requiresRestart;
    f.dependencies = std::move(dependencies);
    f.category = std::string(category);
    f.isExperimental = isExperimental;
    f.isHidden = isHidden;
    return f;
}

// ============================================================
// Default feature flag configuration
// ============================================================
inline std::vector<FeatureFlags::Flag> GetDefaultFeatureFlags() {
    using namespace FeatureFlag;
    std::vector<FeatureFlags::Flag> flags;
    flags.reserve(40);

    auto add = [&](std::string_view n, const char* d, bool def, bool restart,
                   std::vector<std::string> deps, std::string_view cat,
                   bool experimental = false) {
        flags.push_back(MakeFlag(n, d, def, restart, std::move(deps), cat, experimental));
    };

    add(EnableDebugOverlay, "Show debug overlay with FPS, entity count, etc.", false, false, {}, CategoryCore);
    add(EnableSessionLog, "Enable detailed session logging", true, true, {}, CategoryCore);
    add(EnableCrashReporting, "Enable automatic crash reporting", true, true, {}, CategoryCore);
    add(EnableTelemetry, "Enable anonymous telemetry (opt-in)", false, false, {}, CategoryCore);

    add(EnableAim, "Enable aim assist", true, true, {}, CategoryAim);
    add(EnableSmoothAim, "Enable smooth aim interpolation", true, true, {std::string(EnableAim)}, CategoryAim);
    add(EnablePrediction, "Enable lag compensation / prediction", true, true, {std::string(EnableAim)}, CategoryAim);
    add(EnableRecoilControl, "Enable recoil control system", false, false, {std::string(EnableAim)}, CategoryAim);
    add(EnableTriggerbot, "Enable triggerbot", false, false, {std::string(EnableAim)}, CategoryAim);
    add(EnableLegitMode, "Enable legit mode (restricts aggressive features)", false, false, {std::string(EnableAim)}, CategoryAim);
    add(EnableDynamicFov, "Enable dynamic FOV based on distance", true, true, {std::string(EnableAim)}, CategoryAim);

    add(EnableEsp, "Enable ESP", true, true, {}, CategoryVisuals);
    add(EnableBoxEsp, "Enable box ESP", true, true, {std::string(EnableEsp)}, CategoryVisuals);
    add(EnableSkeletonEsp, "Enable skeleton ESP", true, true, {std::string(EnableEsp)}, CategoryVisuals);
    add(EnableHealthEsp, "Enable health bar ESP", true, true, {std::string(EnableEsp)}, CategoryVisuals);
    add(EnableWeaponEsp, "Enable weapon ESP", true, true, {std::string(EnableEsp)}, CategoryVisuals);
    add(EnableDistanceEsp, "Enable distance ESP", true, true, {std::string(EnableEsp)}, CategoryVisuals);
    add(EnableVehicleEsp, "Enable vehicle ESP", true, true, {std::string(EnableEsp)}, CategoryVisuals);
    add(EnableItemEsp, "Enable item/resource ESP", false, false, {std::string(EnableEsp)}, CategoryVisuals);
    add(EnableSoundEsp, "Enable sound ESP (directional audio)", false, false, {std::string(EnableEsp)}, CategoryVisuals, true);
    add(EnableCrosshair, "Enable custom crosshair", true, true, {}, CategoryVisuals);

    add(EnableRadar, "Enable radar", true, true, {}, CategoryRadar);
    add(EnableRadarWeb, "Enable web radar (CS2)", false, true, {std::string(EnableRadar)}, CategoryRadar, true);
    add(EnableRadar3D, "Enable 3D radar", false, false, {std::string(EnableRadar)}, CategoryRadar, true);

    add(EnableMakcu, "Enable MAKCU support", true, true, {}, CategoryHardware);
    add(EnableKMBox, "Enable KMBox Net support", true, true, {}, CategoryHardware);
    add(EnableFerrum, "Enable Ferrum support", true, true, {}, CategoryHardware);
    add(EnableAutoDetect, "Auto-detect input devices", true, true, {}, CategoryHardware);

    add(EnableHotReloadOffsets, "Hot-reload offsets without restart", false, true, {}, CategoryExperimental, true);
    add(EnableScripting, "Enable Lua scripting", false, true, {}, CategoryExperimental, true);
    add(EnableCloudSync, "Enable cloud config sync", false, true, {}, CategoryExperimental, true);
    add(EnablePluginSystem, "Enable plugin system", false, true, {}, CategoryExperimental, true);

    add(EnablePerformanceMode, "Enable performance mode", true, true, {}, CategoryPerformance);
    add(EnableAdaptiveQuality, "Enable adaptive quality", true, true, {std::string(EnablePerformanceMode)}, CategoryPerformance);
    add(EnableFrameBudget, "Enable frame time budget", true, true, {std::string(EnablePerformanceMode)}, CategoryPerformance);

    return flags;
}

// ============================================================
// Global accessor
// ============================================================
inline FeatureFlags& GetFeatureFlags() noexcept {
    static FeatureFlags instance;
    static bool initialized = [] {
        instance.RegisterAll(GetDefaultFeatureFlags());
        return true;
    }();
    return instance;
}

// ============================================================
// Convenience macros
// ============================================================

// Runtime check
#define FF_ENABLED(name) (OmniGhost::Platform::GetFeatureFlags().IsEnabled(name))
#define FF_GET(name, defaultVal) (OmniGhost::Platform::GetFeatureFlags().Get(name, defaultVal))
#define FF_SET(name, value) (OmniGhost::Platform::GetFeatureFlags().Set(name, value))

// Compile-time check
#define FF_COMPILE_TIME(name) (OmniGhost::Platform::Feature<OmniGhost::Platform::name##Tag>::enabled)

} // namespace OmniGhost::Platform