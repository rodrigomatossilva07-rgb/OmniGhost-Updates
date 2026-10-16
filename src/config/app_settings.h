#pragma once
#include "third_party/ImGui/imgui.h"
#include "../window/theme.h"
#include <filesystem>
#include <string>
#include <Windows.h>

namespace app_settings {

    enum class Language : int {
        PT = 0,
        EN,
        DE,
        ES,
        FR,
        IT,
        RU
    };


    enum class EffectLevel : int {
        Off = 0,
        Subtle = 1,
        Full = 2
    };

    enum class StartBehavior : int {
        Home = 0,
        Library = 1,
        LastGame = 2
    };

    enum class MinimizeBehavior : int {
        Minimize = 0,
        KeepOpen = 1
    };

    enum class CloseBehavior : int {
        Exit = 0,
        Minimize = 1
    };

    enum class UpdateChannel : int {
        Stable = 0,
        Beta = 1,
        Development = 2
    };

    enum class SettingsPage : int {
        General = 0,
        Appearance,
        Overlay,
        Input,
        Devices,
        Licenses,
        Updates,
        Language,
        Diagnostics,
        About
    };

struct Config {
        int schema_version = 4;



        Language language = Language::EN;


        StartBehavior start_behavior = StartBehavior::Home;
        bool remember_last_game = true;
        MinimizeBehavior minimize_behavior = MinimizeBehavior::Minimize;
        CloseBehavior close_behavior = CloseBehavior::Exit;
        bool auto_save = true;


        int menu_bind = 0x2D;
        int hotkey_esp = 0;
        int hotkey_aim = 0;
        int hotkey_vehicle = 0;


        bool black_background = true;

        float black_level = 100.f;

        bool performance_mode = false;
        bool auto_performance = true;
        bool particles = true;
        bool snow_effect = false;
        bool matrix_rain = true;
        EffectLevel digital_rain_level = EffectLevel::Full;
        EffectLevel animation_intensity = EffectLevel::Subtle;
        bool reduce_motion = false;
        bool ui_sounds = true;
        float ui_sound_volume = 35.0f;
        bool notification_sounds = true;
        bool startup_sound = true;
        bool high_contrast = false;
        float ui_scale = 1.0f;
        int monitor_index = -1;
        bool show_fps = true;
        bool telemetry_log_enabled = false;
        bool show_hotkey_overlay = false;
        bool vsync = false;


        int overlay_position_mode = 0;
        RECT overlay_custom_rect{};
        float overlay_scale = 1.0f;
        bool overlay_lock_to_monitor = true;



        UpdateChannel update_channel = UpdateChannel::Stable;
        bool is_development_build = false;
        bool update_auto_check = true;
        bool update_auto_download = false;
        bool update_install_on_exit = false;



        bool show_advanced = false;


        ImU32 color_primary = IM_COL32(212, 175, 55, 255);
        ImU32 color_secondary = IM_COL32(40, 40, 50, 255);
        ImU32 color_fps = IM_COL32(0, 255, 120, 255);
        ImU32 color_particles = IM_COL32(212, 175, 55, 80);


        int theme_mode = 0;
        int accent_preset = 0;
        int visual_style = 0;


        bool aura_50 = false;
        bool aura_100 = false;
        bool onboarding_completed = false;
        bool license_accepted = false;
        int last_selected_game = -1;
        std::string changelog_last_viewed = "";
    };

    extern Config config;
    extern bool menu_open;

    void Initialize();


    bool LoadGlobal(std::string* error = nullptr);
    bool SaveGlobal(std::string* error = nullptr);
    bool ExportGlobal(const std::filesystem::path& path, std::string* error = nullptr);
    bool ImportGlobal(const std::filesystem::path& path, std::string* error = nullptr);
#if defined(OMNIGHOST_TESTING)
    bool ParseForTesting(const std::string& text, Config& output, std::string* error = nullptr);
#endif
    void ResetGlobal();
    void ResetPage(SettingsPage page);

    float UiScalePreference();
    float AnimationScale();
    float DigitalRainDensity();
    float DigitalRainOpacity();
    bool MotionEnabled();
    const char* EffectLevelName(EffectLevel level);
    const char* LangName(Language l);
    const char* T(const char* pt, const char* en);

}
