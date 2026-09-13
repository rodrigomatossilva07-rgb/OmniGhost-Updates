#pragma once

#include "feature_registry.h"
#include "feature_flags.h"
#include "event_bus.h"
#include "service_container.h"

namespace OmniGhost::Platform {

// ============================================================
// Example Features
// ============================================================

// Radar Web Feature (CS2)
class RadarWebFeature final : public GameFeatureBase<RadarWebFeature> {
public:
    static constexpr std::string_view Id = "radar_web";
    static constexpr std::string_view Name = "Web Radar";
    static constexpr std::string_view Description = "CS2 web-based radar overlay";
    static constexpr std::string_view Category = FeatureFlag::CategoryRadar;
    static constexpr std::string_view GameId = "CS2";
    static constexpr bool IsExperimental = true;
    static constexpr bool IsEnabledByDefault = false;
    static std::vector<std::string_view> Dependencies() { return {  FeatureFlag::EnableRadar, FeatureFlag::EnableRadarWeb  }; }
    static constexpr std::string_view FeatureFlag = FeatureFlag::EnableRadarWeb;

    explicit RadarWebFeature(const ServiceContainer& services) : GameFeatureBase(services) {}

    Result<void> OnInitialize() noexcept override {
        if (FF_COMPILE_TIME(RadarWeb)) {
            // CS2_Radar::Initialize();
        }
        return Ok();
    }

    void OnTick() noexcept override {
        if (FF_ENABLED(FeatureFlag::EnableRadarWeb) && FF_COMPILE_TIME(RadarWeb)) {
            // CS2_Radar::RunFrame();
        }
    }

    void OnConfigChange(std::string_view key, std::string_view value) noexcept override { (void)key; (void)value; }
};

// Vehicle ESP Feature (FiveM)
class VehicleEspFeature final : public GameFeatureBase<VehicleEspFeature> {
public:
    static constexpr std::string_view Id = "vehicle_esp";
    static constexpr std::string_view Name = "Vehicle ESP";
    static constexpr std::string_view Description = "Vehicle and transport ESP";
    static constexpr std::string_view Category = FeatureFlag::CategoryVisuals;
    static constexpr std::string_view GameId = ""; // Universal
    static constexpr bool IsExperimental = true;
    static constexpr bool IsEnabledByDefault = false;
    static std::vector<std::string_view> Dependencies() { return {  FeatureFlag::EnableEsp, FeatureFlag::EnableVehicleEsp  }; }
    static constexpr std::string_view FeatureFlag = FeatureFlag::EnableVehicleEsp;

    explicit VehicleEspFeature(const ServiceContainer& services) : GameFeatureBase(services) {}

    void OnTick() noexcept override {
        if (FF_ENABLED(FeatureFlag::EnableVehicleEsp)) {
            // Game-specific vehicle ESP drawing
        }
    }
};

// Sound ESP Feature
class SoundEspFeature final : public GameFeatureBase<SoundEspFeature> {
public:
    static constexpr std::string_view Id = "sound_esp";
    static constexpr std::string_view Name = "Sound ESP";
    static constexpr std::string_view Description = "Directional audio visualization";
    static constexpr std::string_view Category = FeatureFlag::CategoryVisuals;
    static constexpr std::string_view GameId = "";
    static constexpr bool IsExperimental = true;
    static constexpr bool IsEnabledByDefault = false;
    static std::vector<std::string_view> Dependencies() { return {  FeatureFlag::EnableEsp, FeatureFlag::EnableSoundEsp  }; }
    static constexpr std::string_view FeatureFlag = FeatureFlag::EnableSoundEsp;

    explicit SoundEspFeature(const ServiceContainer& services) : GameFeatureBase(services) {}

    void OnTick() noexcept override {
        if (FF_ENABLED(FeatureFlag::EnableSoundEsp)) {
            // Sound ESP logic
        }
    }
};

// Recoil Control Feature
class RecoilControlFeature final : public GameFeatureBase<RecoilControlFeature> {
public:
    static constexpr std::string_view Id = "recoil_control";
    static constexpr std::string_view Name = "Recoil Control";
    static constexpr std::string_view Description = "Advanced recoil compensation";
    static constexpr std::string_view Category = FeatureFlag::CategoryAim;
    static constexpr std::string_view GameId = "";
    static constexpr bool IsExperimental = true;
    static constexpr bool IsEnabledByDefault = false;
    static std::vector<std::string_view> Dependencies() { return {  FeatureFlag::EnableAim, FeatureFlag::EnableRecoilControl  }; }
    static constexpr std::string_view FeatureFlag = FeatureFlag::EnableRecoilControl;

    explicit RecoilControlFeature(const ServiceContainer& services) : GameFeatureBase(services) {}

    void OnTick() noexcept override {
        if (FF_ENABLED(FeatureFlag::EnableRecoilControl)) {
            // Recoil control logic
        }
    }
};

// Triggerbot Feature
class TriggerbotFeature final : public GameFeatureBase<TriggerbotFeature> {
public:
    static constexpr std::string_view Id = "triggerbot";
    static constexpr std::string_view Name = "Triggerbot";
    static constexpr std::string_view Description = "Automatic trigger on target";
    static constexpr std::string_view Category = FeatureFlag::CategoryAim;
    static constexpr std::string_view GameId = "";
    static constexpr bool IsExperimental = true;
    static constexpr bool IsEnabledByDefault = false;
    static std::vector<std::string_view> Dependencies() { return {  FeatureFlag::EnableAim, FeatureFlag::EnableTriggerbot  }; }
    static constexpr std::string_view FeatureFlag = FeatureFlag::EnableTriggerbot;

    explicit TriggerbotFeature(const ServiceContainer& services) : GameFeatureBase(services) {}

    void OnTick() noexcept override {
        if (FF_ENABLED(FeatureFlag::EnableTriggerbot)) {
            // Triggerbot logic
        }
    }
};

// ============================================================
// Feature Registration
// ============================================================
inline void RegisterAllFeatures(GameFeatureRegistry& registry) {
    registry.Register<RadarWebFeature>();
    registry.Register<VehicleEspFeature>();
    registry.Register<SoundEspFeature>();
    registry.Register<RecoilControlFeature>();
    registry.Register<TriggerbotFeature>();
}

} // namespace OmniGhost::Platform
