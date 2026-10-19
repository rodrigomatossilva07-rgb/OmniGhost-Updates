#include "app_settings.h"
#include "../platform/app_paths.h"
#include "../window/theme.h"
#include "../platform/session_log.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>

namespace app_settings {

    Config config;
    bool menu_open = false;

    namespace {
        constexpr int kGlobalSettingsSchema = 4;
        constexpr int kMinimumSupportedSchema = 1;
        constexpr std::uintmax_t kMaximumSettingsBytes = 128 * 1024;

        struct MigrationResult {
            bool success = false;
            std::string error;
            bool wasMigrated = false;
        };

        bool IsFinite(float value) noexcept {
            return std::isfinite(value);
        }

        template<typename T>
        T ClampValue(T value, T minVal, T maxVal, const char* fieldName, bool& wasClamped) {
            if (value < minVal || value > maxVal) {
                wasClamped = true;
                const T clamped = (value < minVal) ? minVal : maxVal;
                (void)fieldName;
                return clamped;
            }
            return value;
        }

        MigrationResult MigrateSchema(Config& cfg, int fromVersion) {
            MigrationResult result{};
            result.success = true;

            if (fromVersion < 2) {

                result.wasMigrated = true;
                cfg.schema_version = 2;



            }

            if (fromVersion < 3) {





                result.wasMigrated = true;
                cfg.schema_version = 3;
                cfg.language = Language::EN;
            }

            if (fromVersion < 4) {


                result.wasMigrated = true;
                cfg.schema_version = 4;
            }

            return result;
        }

        std::filesystem::path GlobalSettingsPath()
        {
            OmniGhost::Paths::EnsureUserDirectories();
            return OmniGhost::Paths::Configs() / L"settings.cfg";
        }

        bool ParseBool(const std::string& value, bool fallback)
        {
            if (value == "1" || value == "true" || value == "TRUE") return true;
            if (value == "0" || value == "false" || value == "FALSE") return false;
            return fallback;
        }

        int ParseInt(const std::string& value, int fallback)
        {
            try {
                std::size_t used = 0;
                const long long parsed = std::stoll(value, &used, 10);
                if (used != value.size() || parsed < INT_MIN || parsed > INT_MAX) {
                    return fallback;
                }
                return static_cast<int>(parsed);
            }
            catch (...) { return fallback; }
        }

        float ParseFloat(const std::string& value, float fallback)
        {
            try {
                std::size_t used = 0;
                const float parsed = std::stof(value, &used);
                if (used != value.size() || !IsFinite(parsed)) {
                    return fallback;
                }
                return parsed;
            }
            catch (...) { return fallback; }
        }

        ImU32 ParseU32(const std::string& value, ImU32 fallback)
        {
            try {
                std::size_t used = 0;
                const unsigned long long parsed = std::stoull(value, &used, 10);
                if (used != value.size() || parsed > UINT32_MAX) {
                    return fallback;
                }
                return static_cast<ImU32>(parsed);
            }
            catch (...) { return fallback; }
        }

        void Clamp(Config& value)
        {
            bool wasClamped = false;

            value.language = static_cast<Language>(ClampValue(static_cast<int>(value.language), 0, 5, "language", wasClamped));
            value.start_behavior = static_cast<StartBehavior>(ClampValue(static_cast<int>(value.start_behavior), 0, 2, "start_behavior", wasClamped));
            value.minimize_behavior = static_cast<MinimizeBehavior>(ClampValue(static_cast<int>(value.minimize_behavior), 0, 1, "minimize_behavior", wasClamped));
            value.close_behavior = static_cast<CloseBehavior>(ClampValue(static_cast<int>(value.close_behavior), 0, 1, "close_behavior", wasClamped));
            value.update_channel = static_cast<UpdateChannel>(ClampValue(static_cast<int>(value.update_channel), 0, 2, "update_channel", wasClamped));
            value.ui_scale = ClampValue(value.ui_scale, 0.75f, 2.50f, "ui_scale", wasClamped);
            value.monitor_index = ClampValue(value.monitor_index, -1, 15, "monitor_index", wasClamped);
            value.black_level = ClampValue(value.black_level, 0.0f, 100.0f, "black_level", wasClamped);




            value.digital_rain_level = static_cast<EffectLevel>(ClampValue(static_cast<int>(value.digital_rain_level), 0, 2, "digital_rain_level", wasClamped));
            value.animation_intensity = static_cast<EffectLevel>(ClampValue(static_cast<int>(value.animation_intensity), 0, 2, "animation_intensity", wasClamped));
            value.ui_sound_volume = ClampValue(value.ui_sound_volume, 0.0f, 100.0f, "ui_sound_volume", wasClamped);
            value.matrix_rain = value.digital_rain_level != EffectLevel::Off;
            value.black_background = value.black_level > 0.5f;
            value.menu_bind = ClampValue(value.menu_bind, 0, 255, "menu_bind", wasClamped);
            value.hotkey_esp = ClampValue(value.hotkey_esp, 0, 255, "hotkey_esp", wasClamped);
            value.hotkey_aim = ClampValue(value.hotkey_aim, 0, 255, "hotkey_aim", wasClamped);
            value.hotkey_vehicle = ClampValue(value.hotkey_vehicle, 0, 255, "hotkey_vehicle", wasClamped);
            value.theme_mode = ClampValue(value.theme_mode, 0, 2, "theme_mode", wasClamped);
            value.accent_preset = ClampValue(value.accent_preset, 0, 99, "accent_preset", wasClamped);
            value.visual_style = ClampValue(value.visual_style, 0, 6, "visual_style", wasClamped);
            value.overlay_position_mode = ClampValue(value.overlay_position_mode, 0, 3, "overlay_position_mode", wasClamped);
            value.overlay_scale = ClampValue(value.overlay_scale, 0.5f, 2.0f, "overlay_scale", wasClamped);
            value.overlay_custom_rect.left = ClampValue(value.overlay_custom_rect.left, static_cast<decltype(value.overlay_custom_rect.left)>(-10000), static_cast<decltype(value.overlay_custom_rect.left)>(10000), "overlay_custom_rect.left", wasClamped);
            value.overlay_custom_rect.top = ClampValue(value.overlay_custom_rect.top, static_cast<decltype(value.overlay_custom_rect.top)>(-10000), static_cast<decltype(value.overlay_custom_rect.top)>(10000), "overlay_custom_rect.top", wasClamped);
            value.overlay_custom_rect.right = ClampValue(value.overlay_custom_rect.right, static_cast<decltype(value.overlay_custom_rect.right)>(-10000), static_cast<decltype(value.overlay_custom_rect.right)>(10000), "overlay_custom_rect.right", wasClamped);
            value.overlay_custom_rect.bottom = ClampValue(value.overlay_custom_rect.bottom, static_cast<decltype(value.overlay_custom_rect.bottom)>(-10000), static_cast<decltype(value.overlay_custom_rect.bottom)>(10000), "overlay_custom_rect.bottom", wasClamped);

            if (wasClamped) {
                OmniGhost::SessionLog::Write(
                    OmniGhost::SessionLog::Severity::Info,
                    OmniGhost::SessionLog::Subsystem::Config,
                    "settings clamp completed",
                    {{"clamped", "true", false}});
            }
        }

        std::string Serialize(const Config& value)
        {
            std::ostringstream out;
            out << "# OmniGhost global settings\n";
            out << "schema=" << kGlobalSettingsSchema << '\n';
            out << "language=" << static_cast<int>(value.language) << '\n';
            out << "general.start_behavior=" << static_cast<int>(value.start_behavior) << '\n';
            out << "general.remember_last_game=" << (value.remember_last_game ? 1 : 0) << '\n';
            out << "general.minimize_behavior=" << static_cast<int>(value.minimize_behavior) << '\n';
            out << "general.close_behavior=" << static_cast<int>(value.close_behavior) << '\n';
            out << "general.auto_save=" << (value.auto_save ? 1 : 0) << '\n';
            out << "input.menu_bind=" << value.menu_bind << '\n';
            out << "input.hotkey_esp=" << value.hotkey_esp << '\n';
            out << "input.hotkey_aim=" << value.hotkey_aim << '\n';
            out << "input.hotkey_vehicle=" << value.hotkey_vehicle << '\n';
            out << "appearance.ui_scale=" << value.ui_scale << '\n';
            out << "appearance.digital_rain=" << static_cast<int>(value.digital_rain_level) << '\n';
            out << "appearance.animation=" << static_cast<int>(value.animation_intensity) << '\n';
            out << "appearance.reduce_motion=" << (value.reduce_motion ? 1 : 0) << '\n';
            out << "sound.ui=" << (value.ui_sounds ? 1 : 0) << '\n';
            out << "sound.volume=" << value.ui_sound_volume << '\n';
            out << "sound.notifications=" << (value.notification_sounds ? 1 : 0) << '\n';
            out << "sound.startup=" << (value.startup_sound ? 1 : 0) << '\n';
            out << "appearance.high_contrast=" << (value.high_contrast ? 1 : 0) << '\n';
            out << "appearance.theme_mode=" << value.theme_mode << '\n';
            out << "appearance.accent_preset=" << value.accent_preset << '\n';
            out << "appearance.visual_style=" << value.visual_style << '\n';
            out << "appearance.accent=" << value.color_primary << '\n';
            out << "appearance.secondary=" << value.color_secondary << '\n';
            out << "appearance.particles=" << (value.particles ? 1 : 0) << '\n';
            out << "overlay.black_level=" << value.black_level << '\n';
            out << "overlay.show_fps=" << (value.show_fps ? 1 : 0) << '\n';
            out << "overlay.telemetry_log_enabled=" << (value.telemetry_log_enabled ? 1 : 0) << '\n';
            out << "overlay.hotkey_overlay=" << (value.show_hotkey_overlay ? 1 : 0) << '\n';
            out << "overlay.vsync=" << (value.vsync ? 1 : 0) << '\n';
            out << "overlay.monitor=" << value.monitor_index << '\n';
            out << "overlay.position_mode=" << value.overlay_position_mode << '\n';
            out << "overlay.custom_rect=" << value.overlay_custom_rect.left << ',' << value.overlay_custom_rect.top << ',' << value.overlay_custom_rect.right << ',' << value.overlay_custom_rect.bottom << '\n';
            out << "overlay.scale=" << value.overlay_scale << '\n';
            out << "overlay.lock_to_monitor=" << (value.overlay_lock_to_monitor ? 1 : 0) << '\n';
            out << "updates.channel=" << static_cast<int>(value.update_channel) << '\n';
            out << "updates.auto_check=" << (value.update_auto_check ? 1 : 0) << '\n';
            out << "updates.auto_download=" << (value.update_auto_download ? 1 : 0) << '\n';
            out << "updates.install_on_exit=" << (value.update_install_on_exit ? 1 : 0) << '\n';

            out << "advanced.performance_mode=" << (value.performance_mode ? 1 : 0) << '\n';
            out << "advanced.auto_performance=" << (value.auto_performance ? 1 : 0) << '\n';
            out << "advanced.snow=" << (value.snow_effect ? 1 : 0) << '\n';
            out << "advanced.aura50=" << (value.aura_50 ? 1 : 0) << '\n';
            out << "advanced.aura100=" << (value.aura_100 ? 1 : 0) << '\n';
            out << "colors.fps=" << value.color_fps << '\n';
            out << "colors.particles=" << value.color_particles << '\n';
            out << "onboarding.completed=" << (value.onboarding_completed ? 1 : 0) << '\n';
            out << "onboarding.license_accepted=" << (value.license_accepted ? 1 : 0) << '\n';
            out << "onboarding.last_game=" << value.last_selected_game << '\n';
            out << "onboarding.changelog_viewed=" << value.changelog_last_viewed << '\n';
            return out.str();
        }

        bool Deserialize(const std::string& text, Config& output, std::string* error)
        {
            if (text.size() > kMaximumSettingsBytes) {
                if (error) *error = "O ficheiro de settings excede o limite de 128 KiB.";
                return false;
            }

            Config parsed = output;
            std::istringstream in(text);
            std::string line;
            int schema = 1;

            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                const std::size_t separator = line.find('=');
                if (separator == std::string::npos) continue;
                const std::string key = line.substr(0, separator);
                const std::string value = line.substr(separator + 1);

                if (key == "schema") schema = ParseInt(value, schema);
                else if (key == "language") parsed.language = static_cast<Language>(ParseInt(value, static_cast<int>(parsed.language)));
                else if (key == "general.start_behavior") parsed.start_behavior = static_cast<StartBehavior>(ParseInt(value, static_cast<int>(parsed.start_behavior)));
                else if (key == "general.remember_last_game") parsed.remember_last_game = ParseBool(value, parsed.remember_last_game);
                else if (key == "general.minimize_behavior") parsed.minimize_behavior = static_cast<MinimizeBehavior>(ParseInt(value, static_cast<int>(parsed.minimize_behavior)));
                else if (key == "general.close_behavior") parsed.close_behavior = static_cast<CloseBehavior>(ParseInt(value, static_cast<int>(parsed.close_behavior)));
                else if (key == "general.auto_save") parsed.auto_save = ParseBool(value, parsed.auto_save);
                else if (key == "input.menu_bind") parsed.menu_bind = ParseInt(value, parsed.menu_bind);
                else if (key == "input.hotkey_esp") parsed.hotkey_esp = ParseInt(value, parsed.hotkey_esp);
                else if (key == "input.hotkey_aim") parsed.hotkey_aim = ParseInt(value, parsed.hotkey_aim);
                else if (key == "input.hotkey_vehicle") parsed.hotkey_vehicle = ParseInt(value, parsed.hotkey_vehicle);
                else if (key == "appearance.ui_scale") parsed.ui_scale = ParseFloat(value, parsed.ui_scale);
                else if (key == "appearance.digital_rain") parsed.digital_rain_level = static_cast<EffectLevel>(ParseInt(value, static_cast<int>(parsed.digital_rain_level)));
                else if (key == "appearance.animation") parsed.animation_intensity = static_cast<EffectLevel>(ParseInt(value, static_cast<int>(parsed.animation_intensity)));
                else if (key == "appearance.reduce_motion") parsed.reduce_motion = ParseBool(value, parsed.reduce_motion);
                else if (key == "sound.ui") parsed.ui_sounds = ParseBool(value, parsed.ui_sounds);
                else if (key == "sound.volume") parsed.ui_sound_volume = ParseFloat(value, parsed.ui_sound_volume);
                else if (key == "sound.notifications") parsed.notification_sounds = ParseBool(value, parsed.notification_sounds);
                else if (key == "sound.startup") parsed.startup_sound = ParseBool(value, parsed.startup_sound);
                else if (key == "appearance.high_contrast") parsed.high_contrast = ParseBool(value, parsed.high_contrast);
                else if (key == "appearance.theme_mode") parsed.theme_mode = ParseInt(value, parsed.theme_mode);
                else if (key == "appearance.accent_preset") parsed.accent_preset = ParseInt(value, parsed.accent_preset);
                else if (key == "appearance.visual_style") parsed.visual_style = ParseInt(value, parsed.visual_style);
                else if (key == "appearance.accent") parsed.color_primary = ParseU32(value, parsed.color_primary);
                else if (key == "appearance.secondary") parsed.color_secondary = ParseU32(value, parsed.color_secondary);
                else if (key == "appearance.particles") parsed.particles = ParseBool(value, parsed.particles);
                else if (key == "overlay.black_level") parsed.black_level = ParseFloat(value, parsed.black_level);
                else if (key == "overlay.show_fps") parsed.show_fps = ParseBool(value, parsed.show_fps);
                else if (key == "overlay.telemetry_log_enabled") parsed.telemetry_log_enabled = ParseBool(value, parsed.telemetry_log_enabled);
                else if (key == "overlay.hotkey_overlay") parsed.show_hotkey_overlay = ParseBool(value, parsed.show_hotkey_overlay);
                else if (key == "overlay.vsync") parsed.vsync = ParseBool(value, parsed.vsync);
                else if (key == "overlay.monitor") parsed.monitor_index = ParseInt(value, parsed.monitor_index);
                else if (key == "overlay.position_mode") parsed.overlay_position_mode = ParseInt(value, parsed.overlay_position_mode);
                else if (key == "overlay.custom_rect") {
                    int l, t, r, b;
                    if (std::sscanf(value.c_str(), "%d,%d,%d,%d", &l, &t, &r, &b) == 4) {
                        parsed.overlay_custom_rect = RECT{l, t, r, b};
                    }
                }
                else if (key == "overlay.scale") parsed.overlay_scale = ParseFloat(value, parsed.overlay_scale);
                else if (key == "overlay.lock_to_monitor") parsed.overlay_lock_to_monitor = ParseBool(value, parsed.overlay_lock_to_monitor);
                else if (key == "updates.channel") parsed.update_channel = static_cast<UpdateChannel>(ParseInt(value, static_cast<int>(parsed.update_channel)));
                else if (key == "updates.auto_check") parsed.update_auto_check = ParseBool(value, parsed.update_auto_check);
                else if (key == "updates.auto_download") parsed.update_auto_download = ParseBool(value, parsed.update_auto_download);
                else if (key == "updates.install_on_exit") parsed.update_install_on_exit = ParseBool(value, parsed.update_install_on_exit);
                else if (key == "advanced.performance_mode") parsed.performance_mode = ParseBool(value, parsed.performance_mode);
                else if (key == "advanced.auto_performance") parsed.auto_performance = ParseBool(value, parsed.auto_performance);
                else if (key == "advanced.snow") parsed.snow_effect = ParseBool(value, parsed.snow_effect);
                else if (key == "advanced.aura50") parsed.aura_50 = ParseBool(value, parsed.aura_50);
                else if (key == "advanced.aura100") parsed.aura_100 = ParseBool(value, parsed.aura_100);
                else if (key == "colors.fps") parsed.color_fps = ParseU32(value, parsed.color_fps);
                else if (key == "colors.particles") parsed.color_particles = ParseU32(value, parsed.color_particles);
                else if (key == "onboarding.completed") parsed.onboarding_completed = ParseBool(value, parsed.onboarding_completed);
                else if (key == "onboarding.license_accepted") parsed.license_accepted = ParseBool(value, parsed.license_accepted);
                else if (key == "onboarding.last_game") parsed.last_selected_game = ParseInt(value, parsed.last_selected_game);
                else if (key == "onboarding.changelog_viewed") parsed.changelog_last_viewed = value;

            }

            if (schema < kMinimumSupportedSchema || schema > kGlobalSettingsSchema) {
                if (error) *error = "Versão do ficheiro de settings não suportada.";
                return false;
            }


            if (schema < kGlobalSettingsSchema) {
                auto migrationResult = MigrateSchema(parsed, schema);
                if (!migrationResult.success) {
                    if (error) *error = "Migração de schema falhou: " + migrationResult.error;
                    return false;
                }
                if (migrationResult.wasMigrated) {
                    OmniGhost::SessionLog::Write(
                        OmniGhost::SessionLog::Severity::Info,
                        OmniGhost::SessionLog::Subsystem::Config,
                        "settings schema migrated",
                        {{"from_version", std::to_string(schema), false},
                         {"to_version", std::to_string(kGlobalSettingsSchema), false}});
                }
            }

            Clamp(parsed);
            parsed.show_advanced = false;
            parsed.schema_version = kGlobalSettingsSchema;
            output = parsed;
            return true;
        }

        bool WriteAtomically(const std::filesystem::path& path, const std::string& bytes, std::string* error)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                if (error) *error = "Não foi possível criar a pasta de settings: " + ec.message();
                return false;
            }
            const std::filesystem::path temporary = path.wstring() + L".tmp";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                if (!file) {
                    if (error) *error = "Não foi possível abrir o ficheiro temporário.";
                    return false;
                }
                file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                file.flush();
                if (!file) {
                    if (error) *error = "Falha ao escrever settings.";
                    return false;
                }
            }
            std::filesystem::remove(path, ec);
            ec.clear();
            std::filesystem::rename(temporary, path, ec);
            if (ec) {
                std::filesystem::remove(temporary, ec);
                if (error) *error = "Não foi possível substituir o ficheiro de settings: " + ec.message();
                return false;
            }
            return true;
        }

    }

    bool LoadGlobal(std::string* error)
    {
        const std::filesystem::path path = GlobalSettingsPath();
        std::error_code ec;
        if (!std::filesystem::exists(path, ec))
            return true;
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec || size > kMaximumSettingsBytes) {
            if (error) *error = ec ? ec.message() : "O ficheiro de settings excede o limite de 128 KiB.";
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            if (error) *error = "Não foi possível abrir settings.cfg.";
            return false;
        }
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return Deserialize(bytes, config, error);
    }

#if defined(OMNIGHOST_TESTING)
    bool ParseForTesting(const std::string& text, Config& output, std::string* error)
    {
        return Deserialize(text, output, error);
    }
#endif

    bool SaveGlobal(std::string* error)
    {
        Clamp(config);
        return WriteAtomically(GlobalSettingsPath(), Serialize(config), error);
    }

    bool ExportGlobal(const std::filesystem::path& path, std::string* error)
    {
        if (path.empty()) {
            if (error) *error = "Destino de exportação vazio.";
            return false;
        }
        Config copy = config;
        Clamp(copy);
        return WriteAtomically(path, Serialize(copy), error);
    }

    bool ImportGlobal(const std::filesystem::path& path, std::string* error)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec)) {
            if (error) *error = "O ficheiro de importação não existe.";
            return false;
        }
        const std::uintmax_t size = std::filesystem::file_size(path, ec);
        if (ec || size > kMaximumSettingsBytes) {
            if (error) *error = ec ? ec.message() : "O ficheiro de settings excede o limite de 128 KiB.";
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            if (error) *error = "Não foi possível abrir o ficheiro de importação.";
            return false;
        }
        const std::string bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        Config imported = config;
        if (!Deserialize(bytes, imported, error))
            return false;
        config = imported;
        return SaveGlobal(error);
    }

    void ResetGlobal()
    {
        config = Config{};
        config.show_advanced = false;
        (void)SaveGlobal(nullptr);
    }

    void ResetPage(SettingsPage page)
    {
        const Config defaults{};
        switch (page) {
        case SettingsPage::General:
            config.start_behavior = defaults.start_behavior;
            config.remember_last_game = defaults.remember_last_game;
            config.minimize_behavior = defaults.minimize_behavior;
            config.close_behavior = defaults.close_behavior;
            config.auto_save = defaults.auto_save;
            break;
        case SettingsPage::Appearance:
            config.ui_scale = defaults.ui_scale;
            config.animation_intensity = defaults.animation_intensity;
            config.digital_rain_level = defaults.digital_rain_level;
            config.matrix_rain = defaults.matrix_rain;
            config.color_primary = defaults.color_primary;
            config.color_secondary = defaults.color_secondary;
            config.reduce_motion = defaults.reduce_motion;
            config.ui_sounds = defaults.ui_sounds;
            config.ui_sound_volume = defaults.ui_sound_volume;
            config.notification_sounds = defaults.notification_sounds;
            config.startup_sound = defaults.startup_sound;
            config.particles = defaults.particles;
            config.high_contrast = defaults.high_contrast;
            config.theme_mode = defaults.theme_mode;
            config.accent_preset = defaults.accent_preset;
            config.visual_style = defaults.visual_style;
            break;
        case SettingsPage::Overlay:
            config.black_background = defaults.black_background;
            config.black_level = defaults.black_level;
            config.show_fps = defaults.show_fps;
            config.show_hotkey_overlay = defaults.show_hotkey_overlay;
            config.vsync = defaults.vsync;
            config.monitor_index = defaults.monitor_index;
            break;
        case SettingsPage::Input:
            config.menu_bind = defaults.menu_bind;
            config.hotkey_esp = defaults.hotkey_esp;
            config.hotkey_aim = defaults.hotkey_aim;
            config.hotkey_vehicle = defaults.hotkey_vehicle;
            break;
        case SettingsPage::Updates:
            config.update_channel = defaults.update_channel;
            config.update_auto_check = defaults.update_auto_check;
            config.update_auto_download = defaults.update_auto_download;
            config.update_install_on_exit = defaults.update_install_on_exit;
            break;
        case SettingsPage::Language:
            config.language = defaults.language;
            break;
        case SettingsPage::Devices:
        case SettingsPage::Licenses:
        case SettingsPage::Diagnostics:
        case SettingsPage::About:
            break;
        }
        Clamp(config);
        (void)SaveGlobal(nullptr);
    }

    void Initialize() {
        menu_open = false;
        config = Config{};
        (void)LoadGlobal(nullptr);

        config.show_advanced = false;
#if defined(OMNIGHOST_PUBLISH) || defined(OMNIGHOST_PRIVATE_STATIC)
        config.is_development_build = true;
#endif
        Clamp(config);
    }

    float UiScalePreference() {
        return std::clamp(config.ui_scale, 0.75f, 2.50f);
    }

    float AnimationScale() {
        if (config.reduce_motion || config.animation_intensity == EffectLevel::Off)
            return 0.0f;
        return config.animation_intensity == EffectLevel::Full ? 1.0f : 0.55f;
    }

    float DigitalRainDensity() {

        switch (config.digital_rain_level) {
        case EffectLevel::Off: return 0.0f;
        case EffectLevel::Subtle: return 0.22f;
        case EffectLevel::Full:
        default: return 0.42f;
        }
    }

    float DigitalRainOpacity() {
        switch (config.digital_rain_level) {
        case EffectLevel::Off: return 0.0f;
        case EffectLevel::Subtle: return 0.45f;
        case EffectLevel::Full:
        default: return 0.70f;
        }
    }

    bool MotionEnabled() {
        return !config.reduce_motion && config.animation_intensity != EffectLevel::Off;
    }

    const char* EffectLevelName(EffectLevel level) {
        switch (level) {
        case EffectLevel::Off: return "Desligado";
        case EffectLevel::Subtle: return "Desempenho";
        case EffectLevel::Full: return "Completo";
        default: return "Desempenho";
        }
    }

    const char* LangName(Language l) {
        switch (l) {
        case Language::PT: return "Portugues";
        case Language::EN: return "English";
        case Language::DE: return "Deutsch";
        case Language::ES: return "Espanol";
        case Language::FR: return "Francais";
        case Language::IT: return "Italiano";
        case Language::RU: return "Russian";
        default: return "Portugues";
        }
    }

    const char* T(const char* pt, const char* en) {
        if (config.language == Language::EN) return en;
        if (config.language == Language::PT) return pt;
        return en;
    }

}
