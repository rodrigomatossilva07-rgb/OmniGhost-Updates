#pragma once
#include "../ImGui/imgui.h"
#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace CyberLayout {

enum class Breakpoint {
    Small,      // < 800px width
    Medium,     // 800px - 1200px
    Large,      // 1200px - 1600px
    XLarge,     // 1600px - 2560px
    XXLarge     // > 2560px (4K, ultrawide)
};

struct LayoutMetrics {
    Breakpoint current = Breakpoint::Medium;
    float uiScale = 1.0f;
    float dpiScale = 1.0f;
    ImVec2 windowSize = ImVec2(1280, 720);
    ImVec2 contentSize = ImVec2(1280, 720);
    float sidebarWidth = 280.0f;
    float headerHeight = 60.0f;
    float footerHeight = 40.0f;
    float cardSpacing = 12.0f;
    float sectionSpacing = 24.0f;
    int gridColumns = 2;
    float maxContentWidth = 1400.0f;
};

inline LayoutMetrics g_metrics;

[[nodiscard]] LayoutMetrics& GetMetrics() noexcept;

void UpdateMetrics(const ImVec2& window_size, float ui_scale, float dpi_scale) noexcept;

[[nodiscard]] Breakpoint GetBreakpoint(float width) noexcept;

[[nodiscard]] float ResponsiveValue(float small, float medium, float large, float xlarge, float xxlarge) noexcept;

[[nodiscard]] int ResponsiveColumns(int small, int medium, int large, int xlarge, int xxlarge) noexcept;

[[nodiscard]] ImVec2 ResponsiveSize(const ImVec2& small, const ImVec2& medium, const ImVec2& large, const ImVec2& xlarge, const ImVec2& xxlarge) noexcept;

void BeginResponsiveContainer(const char* id, float max_width = 0.0f, bool center = true);
void EndResponsiveContainer();

void BeginCardGrid(int columns = -1, float gap = -1.0f);
void EndCardGrid();
void NextCardGridItem();

void BeginSection(const char* title, bool collapsible = false, bool default_open = true);
void EndSection();

void PushKeyboardNavigationScope();
void PopKeyboardNavigationScope();
bool IsKeyboardNavActive() noexcept;

struct Focusable {
    virtual ~Focusable() = default;
    virtual void Render() = 0;
    virtual bool HandleKey([[maybe_unused]] ImGuiKey key) { return false; }
    virtual bool WantsFocus() const { return true; }
};

void RegisterFocusable(Focusable* widget);
void UnregisterFocusable(Focusable* widget);
void FocusNext();
void FocusPrevious();
void FocusFirst();
void FocusLast();

struct ScrollableRegion {
    const char* id;
    ImVec2 size = ImVec2(0, 0);
    ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar;
    bool autoHideScrollbar = true;
};

void BeginScrollable(const ScrollableRegion& region);
void EndScrollable();

struct HighContrastMode {
    static bool IsEnabled() noexcept;
    static void SetEnabled(bool enabled) noexcept;
    static void Toggle() noexcept;
    static ImU32 GetColor(ImU32 normal_color) noexcept;
};

struct ReducedMotion {
    static bool IsEnabled() noexcept;
    static float GetAnimationScale() noexcept;
    static void SetEnabled(bool enabled) noexcept;
};

struct LocalizedString {
    const char* key;
    const char* fallback;
    explicit operator const char*() const noexcept;
};

#define LOC(key, fallback) CyberLayout::LocalizedString{key, fallback}

[[nodiscard]] std::string GetLocalized(const char* key, const char* fallback) noexcept;

void AuditLocalizationCoverage();

} // namespace CyberLayout