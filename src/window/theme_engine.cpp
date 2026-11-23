#include "theme_engine.h"
#include "theme.h"
#include "../platform/app_paths.h"
#include "../platform/logging.h"

#include <fstream>
#include <algorithm>
#include <filesystem>

namespace OmniGhost::ThemeEngine {





nlohmann::json ThemeMetrics::ToJson() const {
    nlohmann::json j;
    j["windowWidth"] = windowWidth;
    j["windowHeight"] = windowHeight;
    j["windowRounding"] = windowRounding;
    j["headerHeight"] = headerHeight;
    j["footerHeight"] = footerHeight;
    j["sidebarWidth"] = sidebarWidth;
    j["contentInset"] = contentInset;
    j["contentPaddingX"] = contentPaddingX;
    j["contentPaddingY"] = contentPaddingY;
    j["gridGap"] = gridGap;
    j["cardPadding"] = cardPadding;
    j["cardHeaderHeight"] = cardHeaderHeight;
    j["cardRounding"] = cardRounding;
    j["cardMinHeight"] = cardMinHeight;
    j["controlHeight"] = controlHeight;
    j["controlRounding"] = controlRounding;
    j["rowHeight"] = rowHeight;
    j["toggleWidth"] = toggleWidth;
    j["toggleHeight"] = toggleHeight;
    j["searchHeight"] = searchHeight;
    j["logoSize"] = logoSize;
    j["sidebarItemHeight"] = sidebarItemHeight;
    j["pageTransitionSeconds"] = pageTransitionSeconds;
    j["spacingXs"] = spacingXs;
    j["spacingSm"] = spacingSm;
    j["spacingMd"] = spacingMd;
    j["spacingLg"] = spacingLg;
    j["spacingXl"] = spacingXl;
    j["spacingXxl"] = spacingXxl;
    j["radiusSm"] = radiusSm;
    j["radiusMd"] = radiusMd;
    j["radiusLg"] = radiusLg;
    j["fontTitle"] = fontTitle;
    j["fontHeading"] = fontHeading;
    j["fontBody"] = fontBody;
    j["fontCaption"] = fontCaption;
    j["fontMono"] = fontMono;
    j["shadowOffsetY"] = shadowOffsetY;
    j["shadowRadius"] = shadowRadius;
    j["shadowAlpha"] = shadowAlpha;
    return j;
}

ThemeMetrics ThemeMetrics::FromJson(const nlohmann::json& j) {
    ThemeMetrics m;
    m.windowWidth = j.value("windowWidth", 1180.0f);
    m.windowHeight = j.value("windowHeight", 720.0f);
    m.windowRounding = j.value("windowRounding", 14.0f);
    m.headerHeight = j.value("headerHeight", 70.0f);
    m.footerHeight = j.value("footerHeight", 40.0f);
    m.sidebarWidth = j.value("sidebarWidth", 210.0f);
    m.contentInset = j.value("contentInset", 12.0f);
    m.contentPaddingX = j.value("contentPaddingX", 14.0f);
    m.contentPaddingY = j.value("contentPaddingY", 12.0f);
    m.gridGap = j.value("gridGap", 12.0f);
    m.cardPadding = j.value("cardPadding", 14.0f);
    m.cardHeaderHeight = j.value("cardHeaderHeight", 42.0f);
    m.cardRounding = j.value("cardRounding", 10.0f);
    m.cardMinHeight = j.value("cardMinHeight", 0.0f);
    m.controlHeight = j.value("controlHeight", 36.0f);
    m.controlRounding = j.value("controlRounding", 8.0f);
    m.rowHeight = j.value("rowHeight", 34.0f);
    m.toggleWidth = j.value("toggleWidth", 48.0f);
    m.toggleHeight = j.value("toggleHeight", 25.0f);
    m.searchHeight = j.value("searchHeight", 36.0f);
    m.logoSize = j.value("logoSize", 44.0f);
    m.sidebarItemHeight = j.value("sidebarItemHeight", 40.0f);
    m.pageTransitionSeconds = j.value("pageTransitionSeconds", 0.20f);
    m.spacingXs = j.value("spacingXs", 4.0f);
    m.spacingSm = j.value("spacingSm", 8.0f);
    m.spacingMd = j.value("spacingMd", 12.0f);
    m.spacingLg = j.value("spacingLg", 16.0f);
    m.spacingXl = j.value("spacingXl", 24.0f);
    m.spacingXxl = j.value("spacingXxl", 32.0f);
    m.radiusSm = j.value("radiusSm", 6.0f);
    m.radiusMd = j.value("radiusMd", 10.0f);
    m.radiusLg = j.value("radiusLg", 14.0f);
    m.fontTitle = j.value("fontTitle", 20.0f);
    m.fontHeading = j.value("fontHeading", 16.0f);
    m.fontBody = j.value("fontBody", 14.0f);
    m.fontCaption = j.value("fontCaption", 11.5f);
    m.fontMono = j.value("fontMono", 11.0f);
    m.shadowOffsetY = j.value("shadowOffsetY", 5.0f);
    m.shadowRadius = j.value("shadowRadius", 12.0f);
    m.shadowAlpha = j.value("shadowAlpha", 46);
    return m;
}





nlohmann::json ThemeColors::ToJson() const {
    nlohmann::json j;
    j["background"] = background.ToJson();
    j["surface"] = surface.ToJson();
    j["card"] = card.ToJson();
    j["cardHover"] = cardHover.ToJson();
    j["panel"] = panel.ToJson();
    j["panelHover"] = panelHover.ToJson();
    j["accent"] = accent.ToJson();
    j["accentHover"] = accentHover.ToJson();
    j["accentGlow"] = accentGlow.ToJson();
    j["text"] = text.ToJson();
    j["textDisabled"] = textDisabled.ToJson();
    j["border"] = border.ToJson();
    j["success"] = success.ToJson();
    j["warning"] = warning.ToJson();
    j["error"] = error.ToJson();
    j["info"] = info.ToJson();

    nlohmann::json customJson;
    for (const auto& [key, color] : custom) {
        customJson[key] = color.ToJson();
    }
    j["custom"] = customJson;

    return j;
}

ThemeColors ThemeColors::FromJson(const nlohmann::json& j) {
    ThemeColors c;
    c.background = ThemeColor::FromJson(j.value("background", nlohmann::json::array({0.03f, 0.03f, 0.04f, 1.0f})));
    c.surface = ThemeColor::FromJson(j.value("surface", nlohmann::json::array({0.06f, 0.06f, 0.08f, 1.0f})));
    c.card = ThemeColor::FromJson(j.value("card", nlohmann::json::array({0.08f, 0.08f, 0.10f, 1.0f})));
    c.cardHover = ThemeColor::FromJson(j.value("cardHover", nlohmann::json::array({0.10f, 0.10f, 0.12f, 1.0f})));
    c.panel = ThemeColor::FromJson(j.value("panel", nlohmann::json::array({0.07f, 0.07f, 0.09f, 1.0f})));
    c.panelHover = ThemeColor::FromJson(j.value("panelHover", nlohmann::json::array({0.09f, 0.09f, 0.11f, 1.0f})));
    c.accent = ThemeColor::FromJson(j.value("accent", nlohmann::json::array({0.83f, 0.69f, 0.22f, 1.0f})));
    c.accentHover = ThemeColor::FromJson(j.value("accentHover", nlohmann::json::array({0.90f, 0.75f, 0.25f, 1.0f})));
    c.accentGlow = ThemeColor::FromJson(j.value("accentGlow", nlohmann::json::array({0.83f, 0.69f, 0.22f, 0.3f})));
    c.text = ThemeColor::FromJson(j.value("text", nlohmann::json::array({0.95f, 0.95f, 0.96f, 1.0f})));
    c.textDisabled = ThemeColor::FromJson(j.value("textDisabled", nlohmann::json::array({0.50f, 0.50f, 0.55f, 1.0f})));
    c.border = ThemeColor::FromJson(j.value("border", nlohmann::json::array({0.15f, 0.15f, 0.18f, 1.0f})));
    c.success = ThemeColor::FromJson(j.value("success", nlohmann::json::array({0.20f, 0.80f, 0.40f, 1.0f})));
    c.warning = ThemeColor::FromJson(j.value("warning", nlohmann::json::array({1.0f, 0.75f, 0.10f, 1.0f})));
    c.error = ThemeColor::FromJson(j.value("error", nlohmann::json::array({1.0f, 0.30f, 0.30f, 1.0f})));
    c.info = ThemeColor::FromJson(j.value("info", nlohmann::json::array({0.20f, 0.60f, 1.0f, 1.0f})));

    if (j.contains("custom") && j["custom"].is_object()) {
        for (auto it = j["custom"].begin(); it != j["custom"].end(); ++it) {
            c.custom[it.key()] = ThemeColor::FromJson(it.value());
        }
    }

    return c;
}





nlohmann::json ThemeFonts::ToJson() const {
    nlohmann::json j;
    j["titleFont"] = titleFont;
    j["headingFont"] = headingFont;
    j["bodyFont"] = bodyFont;
    j["monoFont"] = monoFont;
    j["iconFont"] = iconFont;
    j["titleSize"] = titleSize;
    j["headingSize"] = headingSize;
    j["bodySize"] = bodySize;
    j["captionSize"] = captionSize;
    j["monoSize"] = monoSize;
    return j;
}

ThemeFonts ThemeFonts::FromJson(const nlohmann::json& j) {
    ThemeFonts f;
    f.titleFont = j.value("titleFont", "default");
    f.headingFont = j.value("headingFont", "default");
    f.bodyFont = j.value("bodyFont", "default");
    f.monoFont = j.value("monoFont", "default");
    f.iconFont = j.value("iconFont", "default");
    f.titleSize = j.value("titleSize", 20.0f);
    f.headingSize = j.value("headingSize", 16.0f);
    f.bodySize = j.value("bodySize", 14.0f);
    f.captionSize = j.value("captionSize", 11.5f);
    f.monoSize = j.value("monoSize", 11.0f);
    return f;
}





nlohmann::json ThemeDefinition::ToJson() const {
    nlohmann::json j;
    j["id"] = id;
    j["name"] = name;
    j["description"] = description;
    j["author"] = author;
    j["version"] = version;
    j["baseTheme"] = baseTheme;
    j["builtIn"] = builtIn;
    j["colors"] = colors.ToJson();
    j["metrics"] = metrics.ToJson();
    j["fonts"] = fonts.ToJson();
    return j;
}

ThemeDefinition ThemeDefinition::FromJson(const nlohmann::json& j) {
    ThemeDefinition t;
    t.id = j.value("id", "");
    t.name = j.value("name", "");
    t.description = j.value("description", "");
    t.author = j.value("author", "");
    t.version = j.value("version", "1.0.0");
    t.baseTheme = j.value("baseTheme", "Omni");
    t.builtIn = j.value("builtIn", false);
    t.colors = ThemeColors::FromJson(j.value("colors", nlohmann::json::object()));
    t.metrics = ThemeMetrics::FromJson(j.value("metrics", nlohmann::json::object()));
    t.fonts = ThemeFonts::FromJson(j.value("fonts", nlohmann::json::object()));
    return t;
}





bool ThemeManager::Initialize() {
    if (initialized_) {
        return true;
    }


    std::error_code ec;
    std::filesystem::create_directories(GetThemeDirectory(), ec);
    std::filesystem::create_directories(GetBuiltInThemeDirectory(), ec);


    LoadThemesFromDisk();


    InitializeBuiltInThemes();


    if (!activeThemeId_.empty()) {
        ApplyActiveTheme();
    }

    initialized_ = true;
    OGS_LOG_INFO("[ThemeEngine] Initialized with {} theme(s)", themes_.size());
    return true;
}

void ThemeManager::Shutdown() {
    if (!initialized_) {
        return;
    }
    SaveThemesToDisk();
    initialized_ = false;
    OGS_LOG_INFO("[ThemeEngine] Shutdown");
}

bool ThemeManager::RegisterTheme(const ThemeDefinition& theme) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (theme.id.empty() || theme.name.empty()) {
        OGS_LOG_ERROR("[ThemeEngine] Invalid theme: empty id or name");
        return false;
    }

    if (themes_.contains(theme.id) && !theme.builtIn) {
        OGS_LOG_WARN("[ThemeEngine] Theme already exists: {}", theme.id);
        return false;
    }

    themes_[theme.id] = theme;


    if (!theme.builtIn) {
        std::filesystem::path filePath = GetThemeDirectory() / (theme.id + ".json");
        nlohmann::json j = theme.ToJson();
        std::ofstream file(filePath);
        if (file) {
            file << j.dump(4);
        }
    }

    OGS_LOG_INFO("[ThemeEngine] Registered theme: {} ({})", theme.name, theme.id);
    return true;
}

bool ThemeManager::UnregisterTheme(std::string_view themeId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = themes_.find(std::string(themeId));
    if (it == themes_.end()) {
        return false;
    }


    if (it->second.builtIn) {
        OGS_LOG_WARN("[ThemeEngine] Cannot unregister built-in theme: {}", themeId);
        return false;
    }


    std::filesystem::path filePath = GetThemeDirectory() / (std::string(themeId) + ".json");
    std::error_code ec;
    std::filesystem::remove(filePath, ec);


    if (activeThemeId_ == themeId) {
        activeThemeId_.clear();
    }

    themes_.erase(it);
    OGS_LOG_INFO("[ThemeEngine] Unregistered theme: {}", themeId);
    return true;
}

std::optional<ThemeDefinition> ThemeManager::GetTheme(std::string_view themeId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = themes_.find(std::string(themeId));
    if (it != themes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<ThemeDefinition> ThemeManager::GetAllThemes() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ThemeDefinition> result;
    result.reserve(themes_.size());
    for (const auto& [id, theme] : themes_) {
        result.push_back(theme);
    }


    std::sort(result.begin(), result.end(),
        [](const ThemeDefinition& a, const ThemeDefinition& b) {
            if (a.builtIn != b.builtIn) {
                return a.builtIn > b.builtIn;
            }
            return a.name < b.name;
        });

    return result;
}

std::vector<ThemeDefinition> ThemeManager::GetBuiltInThemes() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ThemeDefinition> result;
    for (const auto& [id, theme] : themes_) {
        if (theme.builtIn) {
            result.push_back(theme);
        }
    }
    return result;
}

std::vector<ThemeDefinition> ThemeManager::GetUserThemes() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ThemeDefinition> result;
    for (const auto& [id, theme] : themes_) {
        if (!theme.builtIn) {
            result.push_back(theme);
        }
    }
    return result;
}

bool ThemeManager::SetActiveTheme(std::string_view themeId) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::string themeKey(themeId);
    if (!themes_.contains(themeKey)) {
        OGS_LOG_ERROR("[ThemeEngine] Theme not found: {}", themeId);
        return false;
    }

    activeThemeId_ = themeKey;
    ApplyTheme(themes_[themeKey]);

    OGS_LOG_INFO("[ThemeEngine] Active theme set to: {}", themeId);
    return true;
}

std::optional<ThemeDefinition> ThemeManager::GetActiveTheme() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (activeThemeId_.empty()) {
        return std::nullopt;
    }

    auto it = themes_.find(activeThemeId_);
    if (it != themes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void ThemeManager::ApplyTheme(const ThemeDefinition& theme) {

    auto& colors = CyberTheme::Colors;

    colors.Background = theme.colors.background.ToVec4();
    colors.Surface = theme.colors.surface.ToVec4();
    colors.Card = theme.colors.card.ToVec4();
    colors.CardHover = theme.colors.cardHover.ToVec4();
    colors.Panel = theme.colors.panel.ToVec4();
    colors.PanelHover = theme.colors.panelHover.ToVec4();
    colors.Gold = theme.colors.accent.ToVec4();
    colors.GoldHover = theme.colors.accentHover.ToVec4();
    colors.GoldGlow = theme.colors.accentGlow.ToVec4();
    colors.Text = theme.colors.text.ToVec4();
    colors.TextDisabled = theme.colors.textDisabled.ToVec4();
    colors.Border = theme.colors.border.ToVec4();
    colors.Success = theme.colors.success.ToVec4();
    colors.Warning = theme.colors.warning.ToVec4();
    colors.Error = theme.colors.error.ToVec4();
    colors.Info = theme.colors.info.ToVec4();


    using namespace CyberTheme::Metrics;
    WindowWidth = theme.metrics.windowWidth;
    WindowHeight = theme.metrics.windowHeight;
    WindowRounding = theme.metrics.windowRounding;
    HeaderHeight = theme.metrics.headerHeight;
    FooterHeight = theme.metrics.footerHeight;
    SidebarWidth = theme.metrics.sidebarWidth;
    ContentInset = theme.metrics.contentInset;
    ContentPaddingX = theme.metrics.contentPaddingX;
    ContentPaddingY = theme.metrics.contentPaddingY;
    GridGap = theme.metrics.gridGap;
    CardPadding = theme.metrics.cardPadding;
    CardHeaderHeight = theme.metrics.cardHeaderHeight;
    CardRounding = theme.metrics.cardRounding;
    CardMinHeight = theme.metrics.cardMinHeight;
    ControlHeight = theme.metrics.controlHeight;
    ControlRounding = theme.metrics.controlRounding;
    RowHeight = theme.metrics.rowHeight;
    ToggleWidth = theme.metrics.toggleWidth;
    ToggleHeight = theme.metrics.toggleHeight;
    SearchHeight = theme.metrics.searchHeight;
    LogoSize = theme.metrics.logoSize;
    SidebarItemHeight = theme.metrics.sidebarItemHeight;
    PageTransitionSeconds = theme.metrics.pageTransitionSeconds;


    using namespace CyberTheme::Spacing;
    Xs = theme.metrics.spacingXs;
    Sm = theme.metrics.spacingSm;
    Md = theme.metrics.spacingMd;
    Lg = theme.metrics.spacingLg;
    Xl = theme.metrics.spacingXl;
    Xxl = theme.metrics.spacingXxl;

    CyberTheme::Radius::Sm = theme.metrics.radiusSm;
    CyberTheme::Radius::Md = theme.metrics.radiusMd;
    CyberTheme::Radius::Lg = theme.metrics.radiusLg;

    using namespace CyberTheme::Typography;
    Title = theme.metrics.fontTitle;
    Heading = theme.metrics.fontHeading;
    Body = theme.metrics.fontBody;
    Caption = theme.metrics.fontCaption;
    Mono = theme.metrics.fontMono;

    using namespace CyberTheme::Shadow;
    SoftOffsetY = theme.metrics.shadowOffsetY;
    SoftRadius = theme.metrics.shadowRadius;
    SoftAlpha = theme.metrics.shadowAlpha;



    OGS_LOG_DEBUG("[ThemeEngine] Applied theme: {} (v{})", theme.name, theme.version);
}

void ThemeManager::ApplyActiveTheme() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!activeThemeId_.empty() && themes_.contains(activeThemeId_)) {
        ApplyTheme(themes_.at(activeThemeId_));
    }
}

ThemeDefinition ThemeManager::CreateThemeFromCurrent(std::string_view id,
                                                      std::string_view name,
                                                      std::string_view author) {
    ThemeDefinition theme;
    theme.id = id;
    theme.name = name;
    theme.description = "Theme created from current ImGui style";
    theme.author = author;
    theme.version = "1.0.0";
    theme.baseTheme = activeThemeId_.empty() ? "Omni" : activeThemeId_;
    theme.builtIn = false;


    theme.colors.background = CyberTheme::Colors.Background;
    theme.colors.surface = CyberTheme::Colors.Surface;
    theme.colors.card = CyberTheme::Colors.Card;
    theme.colors.cardHover = CyberTheme::Colors.CardHover;
    theme.colors.panel = CyberTheme::Colors.Panel;
    theme.colors.panelHover = CyberTheme::Colors.PanelHover;
    theme.colors.accent = CyberTheme::Colors.Gold;
    theme.colors.accentHover = CyberTheme::Colors.GoldHover;
    theme.colors.accentGlow = CyberTheme::Colors.GoldGlow;
    theme.colors.text = CyberTheme::Colors.Text;
    theme.colors.textDisabled = CyberTheme::Colors.TextDisabled;
    theme.colors.border = CyberTheme::Colors.Border;
    theme.colors.success = CyberTheme::Colors.Success;
    theme.colors.warning = CyberTheme::Colors.Warning;
    theme.colors.error = CyberTheme::Colors.Error;
    theme.colors.info = CyberTheme::Colors.Info;


    using namespace CyberTheme::Metrics;
    theme.metrics.windowWidth = WindowWidth;
    theme.metrics.windowHeight = WindowHeight;
    theme.metrics.windowRounding = WindowRounding;
    theme.metrics.headerHeight = HeaderHeight;
    theme.metrics.footerHeight = FooterHeight;
    theme.metrics.sidebarWidth = SidebarWidth;
    theme.metrics.contentInset = ContentInset;
    theme.metrics.contentPaddingX = ContentPaddingX;
    theme.metrics.contentPaddingY = ContentPaddingY;
    theme.metrics.gridGap = GridGap;
    theme.metrics.cardPadding = CardPadding;
    theme.metrics.cardHeaderHeight = CardHeaderHeight;
    theme.metrics.cardRounding = CardRounding;
    theme.metrics.cardMinHeight = CardMinHeight;
    theme.metrics.controlHeight = ControlHeight;
    theme.metrics.controlRounding = ControlRounding;
    theme.metrics.rowHeight = RowHeight;
    theme.metrics.toggleWidth = ToggleWidth;
    theme.metrics.toggleHeight = ToggleHeight;
    theme.metrics.searchHeight = SearchHeight;
    theme.metrics.logoSize = LogoSize;
    theme.metrics.sidebarItemHeight = SidebarItemHeight;
    theme.metrics.pageTransitionSeconds = PageTransitionSeconds;

    return theme;
}

bool ThemeManager::ExportTheme(std::string_view themeId,
                                const std::filesystem::path& outputPath) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = themes_.find(std::string(themeId));
    if (it == themes_.end()) {
        return false;
    }

    nlohmann::json j = it->second.ToJson();
    std::ofstream file(outputPath);
    if (!file) {
        return false;
    }
    file << j.dump(4);
    return true;
}

bool ThemeManager::ImportTheme(const std::filesystem::path& inputPath) {
    std::ifstream file(inputPath);
    if (!file) {
        return false;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (...) {
        return false;
    }

    ThemeDefinition theme = ThemeDefinition::FromJson(j);


    std::string baseId = theme.id;
    int counter = 1;
    while (themes_.contains(theme.id)) {
        theme.id = baseId + "_" + std::to_string(counter++);
    }
    theme.builtIn = false;

    return RegisterTheme(theme);
}

std::size_t ThemeManager::ImportThemesFromDirectory(const std::filesystem::path& dirPath) {
    std::size_t count = 0;
    std::error_code ec;

    for (const auto& entry : std::filesystem::directory_iterator(dirPath, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            if (ImportTheme(entry.path())) {
                count++;
            }
        }
    }

    return count;
}

bool ThemeManager::DeleteTheme(std::string_view themeId) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = themes_.find(std::string(themeId));
    if (it == themes_.end()) {
        return false;
    }

    if (it->second.builtIn) {
        OGS_LOG_WARN("[ThemeEngine] Cannot delete built-in theme: {}", themeId);
        return false;
    }


    std::filesystem::path filePath = GetThemeDirectory() / (std::string(themeId) + ".json");
    std::error_code ec;
    std::filesystem::remove(filePath, ec);


    if (activeThemeId_ == themeId) {
        activeThemeId_.clear();
    }

    themes_.erase(it);
    OGS_LOG_INFO("[ThemeEngine] Deleted theme: {}", themeId);
    return true;
}

std::filesystem::path ThemeManager::GetThemeDirectory() const {
    return OmniGhost::Paths::Configs() / "themes";
}

std::filesystem::path ThemeManager::GetBuiltInThemeDirectory() const {
    return OmniGhost::Paths::InstallDirectory() / "data" / "themes";
}

void ThemeManager::InitializeBuiltInThemes() {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::vector<ThemeDefinition> builtInThemes = {
        CreateOmniTheme(),
        CreateObsidianTheme(),
        CreateAzureTheme(),
        CreateVioletTheme(),
        CreateMatrixTheme(),
        CreateCrimsonTheme(),
        CreateArcticTheme(),
        CreateHighContrastTheme()
    };

    for (const auto& theme : builtInThemes) {
        if (!themes_.contains(theme.id)) {
            themes_[theme.id] = theme;
        }
    }


    if (activeThemeId_.empty() && themes_.contains("omni")) {
        activeThemeId_ = "omni";
    }
}

ThemeDefinition ThemeManager::BlendThemes(const ThemeDefinition& a,
                                           const ThemeDefinition& b,
                                           float factor) {
    ThemeDefinition result = a;
    result.id = a.id + "_blended_" + b.id;
    result.name = a.name + " + " + b.name;
    result.description = "Blended theme";
    factor = std::clamp(factor, 0.0f, 1.0f);


    auto blendColor = [factor](const ThemeColor& ca, const ThemeColor& cb) {
        return ThemeColor(
            ca.r + (cb.r - ca.r) * factor,
            ca.g + (cb.g - ca.g) * factor,
            ca.b + (cb.b - ca.b) * factor,
            ca.a + (cb.a - ca.a) * factor
        );
    };

    result.colors.background = blendColor(a.colors.background, b.colors.background);
    result.colors.surface = blendColor(a.colors.surface, b.colors.surface);
    result.colors.card = blendColor(a.colors.card, b.colors.card);
    result.colors.cardHover = blendColor(a.colors.cardHover, b.colors.cardHover);
    result.colors.panel = blendColor(a.colors.panel, b.colors.panel);
    result.colors.panelHover = blendColor(a.colors.panelHover, b.colors.panelHover);
    result.colors.accent = blendColor(a.colors.accent, b.colors.accent);
    result.colors.accentHover = blendColor(a.colors.accentHover, b.colors.accentHover);
    result.colors.accentGlow = blendColor(a.colors.accentGlow, b.colors.accentGlow);
    result.colors.text = blendColor(a.colors.text, b.colors.text);
    result.colors.textDisabled = blendColor(a.colors.textDisabled, b.colors.textDisabled);
    result.colors.border = blendColor(a.colors.border, b.colors.border);
    result.colors.success = blendColor(a.colors.success, b.colors.success);
    result.colors.warning = blendColor(a.colors.warning, b.colors.warning);
    result.colors.error = blendColor(a.colors.error, b.colors.error);
    result.colors.info = blendColor(a.colors.info, b.colors.info);

    return result;
}

ThemeDefinition ThemeManager::CreateVariant(const ThemeDefinition& base,
                                             std::string_view variantId,
                                             std::string_view variantName,
                                             float brightnessFactor) {
    ThemeDefinition variant = base;
    variant.id = variantId;
    variant.name = variantName;
    variant.description = "Variant of " + base.name;
    variant.version = "1.0.0";
    variant.builtIn = false;


    auto adjustColor = [brightnessFactor](const ThemeColor& c) {
        return ThemeColor(
            std::clamp(c.r * brightnessFactor, 0.0f, 1.0f),
            std::clamp(c.g * brightnessFactor, 0.0f, 1.0f),
            std::clamp(c.b * brightnessFactor, 0.0f, 1.0f),
            c.a
        );
    };

    variant.colors.background = adjustColor(base.colors.background);
    variant.colors.surface = adjustColor(base.colors.surface);
    variant.colors.card = adjustColor(base.colors.card);
    variant.colors.cardHover = adjustColor(base.colors.cardHover);
    variant.colors.panel = adjustColor(base.colors.panel);
    variant.colors.panelHover = adjustColor(base.colors.panelHover);
    variant.colors.accent = adjustColor(base.colors.accent);
    variant.colors.accentHover = adjustColor(base.colors.accentHover);
    variant.colors.accentGlow = adjustColor(base.colors.accentGlow);
    variant.colors.text = adjustColor(base.colors.text);
    variant.colors.textDisabled = adjustColor(base.colors.textDisabled);
    variant.colors.border = adjustColor(base.colors.border);
    variant.colors.success = adjustColor(base.colors.success);
    variant.colors.warning = adjustColor(base.colors.warning);
    variant.colors.error = adjustColor(base.colors.error);
    variant.colors.info = adjustColor(base.colors.info);

    return variant;
}

bool ThemeManager::LoadThemesFromDisk() {
    std::filesystem::path themeDir = GetThemeDirectory();
    if (!std::filesystem::exists(themeDir)) {
        return true;
    }

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(themeDir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                std::ifstream file(entry.path());
                nlohmann::json j;
                file >> j;

                ThemeDefinition theme = ThemeDefinition::FromJson(j);
                if (!theme.id.empty()) {
                    themes_[theme.id] = theme;
                }
            } catch (...) {
                OGS_LOG_WARN("[ThemeEngine] Failed to load theme: {}", entry.path().string());
            }
        }
    }


    std::filesystem::path builtInDir = GetBuiltInThemeDirectory();
    if (std::filesystem::exists(builtInDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(builtInDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") {
                try {
                    std::ifstream file(entry.path());
                    nlohmann::json j;
                    file >> j;

                    ThemeDefinition theme = ThemeDefinition::FromJson(j);
                    if (!theme.id.empty()) {
                        themes_[theme.id] = theme;
                    }
                } catch (...) {
                    OGS_LOG_WARN("[ThemeEngine] Failed to load built-in theme: {}", entry.path().string());
                }
            }
        }
    }

    return true;
}

bool ThemeManager::SaveThemesToDisk() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::error_code ec;
    std::filesystem::create_directories(GetThemeDirectory(), ec);

    for (const auto& [id, theme] : themes_) {
        if (theme.builtIn) {
            continue;
        }

        std::filesystem::path filePath = GetThemeDirectory() / (id + ".json");
        nlohmann::json j = theme.ToJson();

        std::ofstream file(filePath);
        if (!file) {
            OGS_LOG_ERROR("[ThemeEngine] Failed to save theme: {}", filePath.string());
            continue;
        }
        file << j.dump(4);
    }

    return true;
}





ThemeDefinition CreateOmniTheme() {
    ThemeDefinition t;
    t.id = "omni";
    t.name = "Omni";
    t.description = "Tema padrão dourado/cyberpunk";
    t.author = "OmniGhost Team";
    t.version = "1.0.0";
    t.builtIn = true;


    t.colors.background = ThemeColor(0.03f, 0.03f, 0.04f);
    t.colors.surface = ThemeColor(0.06f, 0.06f, 0.08f);
    t.colors.card = ThemeColor(0.08f, 0.08f, 0.10f);
    t.colors.cardHover = ThemeColor(0.10f, 0.10f, 0.12f);
    t.colors.panel = ThemeColor(0.07f, 0.07f, 0.09f);
    t.colors.panelHover = ThemeColor(0.09f, 0.09f, 0.11f);
    t.colors.accent = ThemeColor(0.83f, 0.69f, 0.22f);
    t.colors.accentHover = ThemeColor(0.90f, 0.75f, 0.25f);
    t.colors.accentGlow = ThemeColor(0.83f, 0.69f, 0.22f, 0.3f);
    t.colors.text = ThemeColor(0.95f, 0.95f, 0.96f);
    t.colors.textDisabled = ThemeColor(0.50f, 0.50f, 0.55f);
    t.colors.border = ThemeColor(0.15f, 0.15f, 0.18f);
    t.colors.success = ThemeColor(0.20f, 0.80f, 0.40f);
    t.colors.warning = ThemeColor(1.0f, 0.75f, 0.10f);
    t.colors.error = ThemeColor(1.0f, 0.30f, 0.30f);
    t.colors.info = ThemeColor(0.20f, 0.60f, 1.0f);

    return t;
}

ThemeDefinition CreateObsidianTheme() {
    ThemeDefinition t = CreateOmniTheme();
    t.id = "obsidian";
    t.name = "Obsidian";
    t.description = "Tema escuro azul/cinza elegante";
    t.colors.accent = ThemeColor(0.30f, 0.55f, 0.85f);
    t.colors.accentHover = ThemeColor(0.40f, 0.65f, 0.90f);
    t.colors.accentGlow = ThemeColor(0.30f, 0.55f, 0.85f, 0.3f);
    t.colors.background = ThemeColor(0.02f, 0.02f, 0.03f);
    t.colors.surface = ThemeColor(0.05f, 0.05f, 0.07f);
    t.colors.border = ThemeColor(0.12f, 0.15f, 0.20f);
    return t;
}

ThemeDefinition CreateAzureTheme() {
    ThemeDefinition t = CreateOmniTheme();
    t.id = "azure";
    t.name = "Azure";
    t.description = "Tema azul brilhante";
    t.colors.accent = ThemeColor(0.0f, 0.65f, 1.0f);
    t.colors.accentHover = ThemeColor(0.2f, 0.75f, 1.0f);
    t.colors.accentGlow = ThemeColor(0.0f, 0.65f, 1.0f, 0.3f);
    t.colors.success = ThemeColor(0.0f, 1.0f, 0.5f);
    return t;
}

ThemeDefinition CreateVioletTheme() {
    ThemeDefinition t = CreateOmniTheme();
    t.id = "violet";
    t.name = "Violet";
    t.description = "Tema roxo místico";
    t.colors.accent = ThemeColor(0.65f, 0.30f, 0.95f);
    t.colors.accentHover = ThemeColor(0.75f, 0.40f, 1.0f);
    t.colors.accentGlow = ThemeColor(0.65f, 0.30f, 0.95f, 0.3f);
    t.colors.border = ThemeColor(0.20f, 0.12f, 0.25f);
    return t;
}

ThemeDefinition CreateMatrixTheme() {
    ThemeDefinition t = CreateOmniTheme();
    t.id = "matrix";
    t.name = "Matrix";
    t.description = "Tema verde terminal/hacker";
    t.colors.accent = ThemeColor(0.0f, 1.0f, 0.2f);
    t.colors.accentHover = ThemeColor(0.2f, 1.0f, 0.4f);
    t.colors.accentGlow = ThemeColor(0.0f, 1.0f, 0.2f, 0.3f);
    t.colors.background = ThemeColor(0.0f, 0.05f, 0.0f);
    t.colors.surface = ThemeColor(0.0f, 0.10f, 0.0f);
    t.colors.card = ThemeColor(0.0f, 0.15f, 0.0f);
    t.colors.text = ThemeColor(0.0f, 0.95f, 0.1f);
    t.colors.textDisabled = ThemeColor(0.0f, 0.50f, 0.1f);
    t.colors.border = ThemeColor(0.0f, 0.30f, 0.0f);
    t.colors.success = ThemeColor(0.0f, 1.0f, 0.0f);
    return t;
}

ThemeDefinition CreateCrimsonTheme() {
    ThemeDefinition t = CreateOmniTheme();
    t.id = "crimson";
    t.name = "Crimson";
    t.description = "Tema vermelho sangue";
    t.colors.accent = ThemeColor(0.95f, 0.15f, 0.25f);
    t.colors.accentHover = ThemeColor(1.0f, 0.25f, 0.35f);
    t.colors.accentGlow = ThemeColor(0.95f, 0.15f, 0.25f, 0.3f);
    t.colors.background = ThemeColor(0.08f, 0.02f, 0.02f);
    t.colors.surface = ThemeColor(0.12f, 0.04f, 0.04f);
    t.colors.border = ThemeColor(0.30f, 0.10f, 0.10f);
    t.colors.error = ThemeColor(1.0f, 0.2f, 0.2f);
    return t;
}

ThemeDefinition CreateArcticTheme() {
    ThemeDefinition t = CreateOmniTheme();
    t.id = "arctic";
    t.name = "Arctic";
    t.description = "Tema claro gelado (light mode)";
    t.colors.background = ThemeColor(0.97f, 0.97f, 0.98f);
    t.colors.surface = ThemeColor(0.93f, 0.93f, 0.95f);
    t.colors.card = ThemeColor(0.90f, 0.90f, 0.92f);
    t.colors.cardHover = ThemeColor(0.85f, 0.85f, 0.88f);
    t.colors.panel = ThemeColor(0.92f, 0.92f, 0.94f);
    t.colors.panelHover = ThemeColor(0.88f, 0.88f, 0.90f);
    t.colors.accent = ThemeColor(0.20f, 0.50f, 0.90f);
    t.colors.accentHover = ThemeColor(0.30f, 0.60f, 1.0f);
    t.colors.accentGlow = ThemeColor(0.20f, 0.50f, 0.90f, 0.2f);
    t.colors.text = ThemeColor(0.10f, 0.10f, 0.12f);
    t.colors.textDisabled = ThemeColor(0.45f, 0.45f, 0.50f);
    t.colors.border = ThemeColor(0.75f, 0.75f, 0.80f);
    t.colors.success = ThemeColor(0.15f, 0.70f, 0.30f);
    t.colors.warning = ThemeColor(0.90f, 0.60f, 0.05f);
    t.colors.error = ThemeColor(0.90f, 0.20f, 0.20f);
    return t;
}

ThemeDefinition CreateHighContrastTheme() {
    ThemeDefinition t = CreateOmniTheme();
    t.id = "high_contrast";
    t.name = "High Contrast";
    t.description = "Tema alto contraste para acessibilidade";
    t.colors.background = ThemeColor(0.0f, 0.0f, 0.0f);
    t.colors.surface = ThemeColor(0.05f, 0.05f, 0.05f);
    t.colors.card = ThemeColor(0.10f, 0.10f, 0.10f);
    t.colors.accent = ThemeColor(1.0f, 1.0f, 0.0f);
    t.colors.accentHover = ThemeColor(1.0f, 1.0f, 0.2f);
    t.colors.text = ThemeColor(1.0f, 1.0f, 1.0f);
    t.colors.textDisabled = ThemeColor(0.7f, 0.7f, 0.7f);
    t.colors.border = ThemeColor(1.0f, 1.0f, 1.0f);
    t.metrics.windowRounding = 0.0f;
    t.metrics.cardRounding = 0.0f;
    t.metrics.controlRounding = 0.0f;
    return t;
}

}
