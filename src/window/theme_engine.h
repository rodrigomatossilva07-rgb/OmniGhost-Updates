#pragma once

#include "theme.h"
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <optional>
#include <mutex>

namespace OmniGhost::ThemeEngine {

// =============================================================================
// Theme Engine - Custom themes with JSON + ImGui style
// =============================================================================

struct ThemeColor {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    ThemeColor() = default;
    ThemeColor(float r_, float g_, float b_, float a_ = 1.0f) : r(r_), g(g_), b(b_), a(a_) {}

    // From ImVec4
    ThemeColor(const ImVec4& v) : r(v.x), g(v.y), b(v.z), a(v.w) {}

    // From ImU32
    ThemeColor(ImU32 col) {
        r = static_cast<float>((col >> 0) & 0xFF) / 255.0f;
        g = static_cast<float>((col >> 8) & 0xFF) / 255.0f;
        b = static_cast<float>((col >> 16) & 0xFF) / 255.0f;
        a = static_cast<float>((col >> 24) & 0xFF) / 255.0f;
    }

    [[nodiscard]] ImVec4 ToVec4() const { return ImVec4(r, g, b, a); }
    [[nodiscard]] ImU32 ToU32() const {
        return IM_COL32(static_cast<int>(r * 255),
                        static_cast<int>(g * 255),
                        static_cast<int>(b * 255),
                        static_cast<int>(a * 255));
    }

    // Serialization
    nlohmann::json ToJson() const {
        return nlohmann::json::array({r, g, b, a});
    }

    static ThemeColor FromJson(const nlohmann::json& j) {
        ThemeColor c;
        if (j.is_array() && j.size() >= 3) {
            c.r = j[0].get<float>();
            c.g = j[1].get<float>();
            c.b = j[2].get<float>();
            c.a = j.size() > 3 ? j[3].get<float>() : 1.0f;
        }
        return c;
    }
};

struct ThemeMetrics {
    // Window
    float windowWidth = 1180.0f;
    float windowHeight = 720.0f;
    float windowRounding = 14.0f;
    float headerHeight = 70.0f;
    float footerHeight = 40.0f;
    float sidebarWidth = 210.0f;
    float contentInset = 12.0f;
    float contentPaddingX = 14.0f;
    float contentPaddingY = 12.0f;
    float gridGap = 12.0f;

    // Cards
    float cardPadding = 14.0f;
    float cardHeaderHeight = 42.0f;
    float cardRounding = 10.0f;
    float cardMinHeight = 0.0f;

    // Controls
    float controlHeight = 36.0f;
    float controlRounding = 8.0f;
    float rowHeight = 34.0f;
    float toggleWidth = 48.0f;
    float toggleHeight = 25.0f;
    float searchHeight = 36.0f;

    // Sidebar
    float logoSize = 44.0f;
    float sidebarItemHeight = 40.0f;

    // Animation
    float pageTransitionSeconds = 0.20f;

    // Spacing
    float spacingXs = 4.0f;
    float spacingSm = 8.0f;
    float spacingMd = 12.0f;
    float spacingLg = 16.0f;
    float spacingXl = 24.0f;
    float spacingXxl = 32.0f;

    // Radius
    float radiusSm = 6.0f;
    float radiusMd = 10.0f;
    float radiusLg = 14.0f;

    // Typography
    float fontTitle = 20.0f;
    float fontHeading = 16.0f;
    float fontBody = 14.0f;
    float fontCaption = 11.5f;
    float fontMono = 11.0f;

    // Shadow
    float shadowOffsetY = 5.0f;
    float shadowRadius = 12.0f;
    int shadowAlpha = 46;

    // Serialization
    nlohmann::json ToJson() const;
    static ThemeMetrics FromJson(const nlohmann::json& j);
};

struct ThemeColors {
    // Base
    ThemeColor background;
    ThemeColor surface;
    ThemeColor card;
    ThemeColor cardHover;

    // Panels
    ThemeColor panel;
    ThemeColor panelHover;

    // Accent
    ThemeColor accent;
    ThemeColor accentHover;
    ThemeColor accentGlow;

    // Text
    ThemeColor text;
    ThemeColor textDisabled;

    // Border
    ThemeColor border;

    // Semantic
    ThemeColor success;
    ThemeColor warning;
    ThemeColor error;
    ThemeColor info;

    // Custom (for user-defined colors)
    std::unordered_map<std::string, ThemeColor> custom;

    // Serialization
    nlohmann::json ToJson() const;
    static ThemeColors FromJson(const nlohmann::json& j);
};

struct ThemeFonts {
    std::string titleFont = "default";
    std::string headingFont = "default";
    std::string bodyFont = "default";
    std::string monoFont = "default";
    std::string iconFont = "default";

    float titleSize = 20.0f;
    float headingSize = 16.0f;
    float bodySize = 14.0f;
    float captionSize = 11.5f;
    float monoSize = 11.0f;

    // Serialization
    nlohmann::json ToJson() const;
    static ThemeFonts FromJson(const nlohmann::json& j);
};

struct ThemeDefinition {
    std::string id;
    std::string name;
    std::string description;
    std::string author;
    std::string version = "1.0.0";
    std::string baseTheme = "Omni";  // Base theme to extend
    bool builtIn = false;

    ThemeColors colors;
    ThemeMetrics metrics;
    ThemeFonts fonts;

    // Serialization
    nlohmann::json ToJson() const;
    static ThemeDefinition FromJson(const nlohmann::json& j);
};

class ThemeManager {
public:
    static ThemeManager& Instance() noexcept {
        static ThemeManager instance;
        return instance;
    }

    // Initialize theme system
    bool Initialize();

    // Shutdown
    void Shutdown();

    // Register a theme
    [[nodiscard]] bool RegisterTheme(const ThemeDefinition& theme);

    // Unregister theme
    bool UnregisterTheme(std::string_view themeId);

    // Get theme by ID
    [[nodiscard]] std::optional<ThemeDefinition> GetTheme(std::string_view themeId) const;

    // Get all themes
    [[nodiscard]] std::vector<ThemeDefinition> GetAllThemes() const;

    // Get built-in themes
    [[nodiscard]] std::vector<ThemeDefinition> GetBuiltInThemes() const;

    // Get user themes
    [[nodiscard]] std::vector<ThemeDefinition> GetUserThemes() const;

    // Set active theme
    [[nodiscard]] bool SetActiveTheme(std::string_view themeId);

    // Get active theme
    [[nodiscard]] std::optional<ThemeDefinition> GetActiveTheme() const;

    // Apply theme to ImGui
    void ApplyTheme(const ThemeDefinition& theme);

    // Apply active theme
    void ApplyActiveTheme();

    // Create theme from current ImGui style
    [[nodiscard]] ThemeDefinition CreateThemeFromCurrent(std::string_view id,
                                                          std::string_view name,
                                                          std::string_view author = "User");

    // Export theme to file
    [[nodiscard]] bool ExportTheme(std::string_view themeId,
                                    const std::filesystem::path& outputPath) const;

    // Import theme from file
    [[nodiscard]] bool ImportTheme(const std::filesystem::path& inputPath);

    // Import all themes from directory
    [[nodiscard]] std::size_t ImportThemesFromDirectory(const std::filesystem::path& dirPath);

    // Delete user theme
    [[nodiscard]] bool DeleteTheme(std::string_view themeId);

    // Get theme directory
    [[nodiscard]] std::filesystem::path GetThemeDirectory() const;

    // Get built-in theme directory
    [[nodiscard]] std::filesystem::path GetBuiltInThemeDirectory() const;

    // Initialize built-in themes
    void InitializeBuiltInThemes();

    // Blend two themes
    [[nodiscard]] static ThemeDefinition BlendThemes(const ThemeDefinition& a,
                                                      const ThemeDefinition& b,
                                                      float factor);

    // Create theme variant (lighter/darker)
    [[nodiscard]] static ThemeDefinition CreateVariant(const ThemeDefinition& base,
                                                        std::string_view variantId,
                                                        std::string_view variantName,
                                                        float brightnessFactor);

private:
    ThemeManager() = default;
    ~ThemeManager() { Shutdown(); }
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    bool LoadThemesFromDisk();
    bool SaveThemesToDisk() const;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, ThemeDefinition> themes_;
    std::string activeThemeId_;
    bool initialized_ = false;
};

// =============================================================================
// Convenience Functions
// =============================================================================

inline ThemeManager& Themes() {
    return ThemeManager::Instance();
}

inline bool InitializeThemes() {
    return ThemeManager::Instance().Initialize();
}

inline void ShutdownThemes() {
    ThemeManager::Instance().Shutdown();
}

inline bool SetTheme(std::string_view themeId) {
    return ThemeManager::Instance().SetActiveTheme(themeId);
}

inline void ApplyCurrentTheme() {
    ThemeManager::Instance().ApplyActiveTheme();
}

// =============================================================================
// Built-in Theme Presets
// =============================================================================

// Create default Omni theme (gold accent)
ThemeDefinition CreateOmniTheme();

// Create Obsidian theme (dark blue/gray)
ThemeDefinition CreateObsidianTheme();

// Create Azure theme (blue accent)
ThemeDefinition CreateAzureTheme();

// Create Violet theme (purple accent)
ThemeDefinition CreateVioletTheme();

// Create Matrix theme (green accent)
ThemeDefinition CreateMatrixTheme();

// Create Crimson theme (red accent)
ThemeDefinition CreateCrimsonTheme();

// Create Arctic theme (light/blue-white)
ThemeDefinition CreateArcticTheme();

// Create High Contrast theme
ThemeDefinition CreateHighContrastTheme();

} // namespace OmniGhost::ThemeEngine