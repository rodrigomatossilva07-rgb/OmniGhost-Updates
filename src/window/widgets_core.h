#pragma once

#include "widgets.h"
#include "theme.h"
#include "animations.h"
#include "localization.h"
#include "ui_feedback.h"
#include "config/app_settings.h"
#include "src/project/third_party/ImGui/imgui.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace CyberWidgets {

// =============================================================================
// Core Widget Helpers
// =============================================================================

namespace {

// Animation state maps
inline std::unordered_map<ImGuiID, float> g_toggle_progress;
inline std::unordered_map<ImGuiID, float> g_status_progress;
inline std::unordered_map<ImGuiID, float> g_metric_progress;
inline float g_sidebar_indicator_y = -1.0f;

inline float WidgetWidth() {
    return std::max(40.0f,
        std::min(CyberTheme::Metrics::CardContentWidth(), ImGui::GetContentRegionAvail().x));
}

inline float SmoothMetric(ImGuiID id, float target) {
    auto [it, inserted] = g_metric_progress.try_emplace(id, target);
    if (inserted || !app_settings::MotionEnabled()) {
        it->second = target;
        return target;
    }
    const float amount = 1.0f - std::exp(-10.0f * std::min(ImGui::GetIO().DeltaTime, 0.05f));
    it->second += (target - it->second) * amount;
    return it->second;
}

inline std::string VisibleLabel(const char* label) {
    if (!label) return {};
    const char* marker = std::strstr(label, "###");
    if (!marker) marker = std::strstr(label, "##");
    return marker ? std::string(label, marker) : std::string(label);
}

inline const char* DisplayLabel(const char* label) {
    if (!label) return "";
    const char* triple = std::strstr(label, "###");
    if (triple) {
        static thread_local char buf[192];
        const size_t n = static_cast<size_t>(triple - label);
        if (n >= sizeof(buf)) return label;
        std::memcpy(buf, label, n);
        buf[n] = '\0';
        return buf;
    }
    const char* dbl = std::strstr(label, "##");
    if (dbl) {
        static thread_local char buf2[192];
        const size_t n = static_cast<size_t>(dbl - label);
        if (n >= sizeof(buf2)) return label;
        std::memcpy(buf2, label, n);
        buf2[n] = '\0';
        return buf2;
    }
    return label;
}

inline void DrawCheckers(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding) {
    dl->AddRectFilled(a, b, IM_COL32(44, 46, 54, 255), rounding);
    const float cell = 5.0f;
    for (float y = a.y; y < b.y; y += cell) {
        for (float x = a.x; x < b.x; x += cell) {
            const int ix = static_cast<int>((x - a.x) / cell);
            const int iy = static_cast<int>((y - a.y) / cell);
            if (((ix + iy) & 1) == 0) {
                dl->AddRectFilled(
                    ImVec2(x, y),
                    ImVec2(std::min(x + cell, b.x), std::min(y + cell, b.y)),
                    IM_COL32(72, 74, 84, 255));
            }
        }
    }
}

inline void ApplyCursorForItem(bool enabled = true, ImGuiMouseCursor cursor = ImGuiMouseCursor_Hand) {
    if (enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetMouseCursor(cursor);
}

inline void DrawFocusRing(float rounding = -1.0f) {
    if (!ImGui::IsItemFocused() && !ImGui::IsItemActive()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    dl->AddRect(
        ImVec2(a.x - 1.0f, a.y - 1.0f),
        ImVec2(b.x + 1.0f, b.y + 1.0f),
        WithAlpha(CyberTheme::Colors.Gold, 0.58f),
        rounding >= 0.0f ? rounding : CyberTheme::Metrics::ControlRounding,
        0, 1.2f);
}

inline ImVec4 SemanticColor(ButtonStyle style) {
    if (style == ButtonStyle::Destructive) return CyberTheme::Colors.Error;
    return CyberTheme::Colors.Gold;
}

inline ImU32 ToneColor(TextTone tone) {
    switch (tone) {
        case TextTone::Secondary: return CyberTheme::U32(CyberTheme::Colors.TextDisabled);
        case TextTone::Accent:    return CyberTheme::U32(CyberTheme::Colors.Gold);
        case TextTone::Success:   return CyberTheme::U32(CyberTheme::Colors.Success);
        case TextTone::Warning:   return CyberTheme::U32(CyberTheme::Colors.Warning);
        case TextTone::Error:     return CyberTheme::U32(CyberTheme::Colors.Error);
        case TextTone::Info:      return CyberTheme::U32(CyberTheme::Colors.Info);
        case TextTone::Primary:
        default:                  return CyberTheme::U32(CyberTheme::Colors.Text);
    }
}

} // anonymous namespace

// =============================================================================
// Button Widgets
// =============================================================================

bool StyledButton(const char* label, const ImVec2& size_arg, ButtonStyle style,
                  bool enabled, bool pending = false, const char* pending_label = nullptr);

bool ToggleSwitch(const char* label, bool* value);

// =============================================================================
// Input Widgets
// =============================================================================

bool InputText(const char* label, std::string* value, ImGuiInputTextFlags flags = 0);
bool InputFloat(const char* label, float* value, float step = 0.0f, float step_fast = 0.0f, const char* format = "%.3f");
bool InputInt(const char* label, int* value, int step = 1, int step_fast = 10);

// =============================================================================
// Select/Combo Widgets
// =============================================================================

bool Combo(const char* label, int* current, const std::vector<std::string>& items);
bool ListBox(const char* label, int* current, const std::vector<std::string>& items, int height_in_items = -1);

// =============================================================================
// Progress/Status Widgets
// =============================================================================

void ProgressBar(float fraction, const ImVec2& size_arg, const char* overlay = nullptr);
void StatusBadge(const char* label, StatusColor color);

// =============================================================================
// Card/Surface Widgets
// =============================================================================

void BeginCard(const char* title, bool collapsible = false);
void EndCard();

void BeginCardRow(int columns);
void NextCardColumn();
void EndCardRow();
void CardGap(float gap = 8.0f);

void BeginSurfaceList(const char* id, float height);
void EndSurfaceList();

// =============================================================================
// Layout Helpers
// =============================================================================

void DrawCheckers(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding);
void ApplyCursorForItem(bool enabled = true, ImGuiMouseCursor cursor = ImGuiMouseCursor_Hand);
void DrawFocusRing(float rounding = -1.0f);
void DrawSpinner(ImDrawList* dl, const ImVec2& center, float radius, float thickness, TextTone tone = TextTone::Accent);

// =============================================================================
// Color/Style Helpers
// =============================================================================

ImU32 ToneColor(TextTone tone);
ImVec4 SemanticColor(ButtonStyle style);
ImU32 WithAlpha(const ImVec4& color, float alpha);
ImVec4 Mix(const ImVec4& a, const ImVec4& b, float amount);

} // namespace CyberWidgets
