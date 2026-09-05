#include "layout.h"
#include "theme.h"
#include "localization.h"
#include "../config/app_settings.h"
#include "../platform/session_log.h"
#include <algorithm>
#include <mutex>
#include <vector>
#include <utility>
#include "../ImGui/imgui_internal.h"

namespace CyberLayout {

namespace {
std::vector<Focusable*> g_focusables;
std::mutex g_focusables_mutex;
int g_focus_index = -1;
bool g_high_contrast = false;
bool g_reduced_motion = false;
} // namespace

LayoutMetrics& GetMetrics() noexcept { return g_metrics; }

void UpdateMetrics(const ImVec2& window_size, float ui_scale, float dpi_scale) noexcept {
    auto& m = GetMetrics();
    m.windowSize = window_size;
    m.uiScale = ui_scale;
    m.dpiScale = dpi_scale;
    m.contentSize = ImVec2(window_size.x * ui_scale * dpi_scale, window_size.y * ui_scale * dpi_scale);
    m.current = GetBreakpoint(m.contentSize.x);

    switch (static_cast<int>(m.current)) {
    case static_cast<int>(Breakpoint::Small):
        m.sidebarWidth = 240.0f;
        m.headerHeight = 50.0f;
        m.footerHeight = 36.0f;
        m.cardSpacing = 8.0f;
        m.sectionSpacing = 16.0f;
        m.gridColumns = 1;
        m.maxContentWidth = 600.0f;
        break;
    case static_cast<int>(Breakpoint::Medium):
        m.sidebarWidth = 260.0f;
        m.headerHeight = 56.0f;
        m.footerHeight = 38.0f;
        m.cardSpacing = 10.0f;
        m.sectionSpacing = 20.0f;
        m.gridColumns = 2;
        m.maxContentWidth = 900.0f;
        break;
    case static_cast<int>(Breakpoint::Large):
        m.sidebarWidth = 280.0f;
        m.headerHeight = 60.0f;
        m.footerHeight = 40.0f;
        m.cardSpacing = 12.0f;
        m.sectionSpacing = 24.0f;
        m.gridColumns = 2;
        m.maxContentWidth = 1200.0f;
        break;
    case static_cast<int>(Breakpoint::XLarge):
        m.sidebarWidth = 300.0f;
        m.headerHeight = 64.0f;
        m.footerHeight = 44.0f;
        m.cardSpacing = 14.0f;
        m.sectionSpacing = 28.0f;
        m.gridColumns = 3;
        m.maxContentWidth = 1400.0f;
        break;
    case static_cast<int>(Breakpoint::XXLarge):
        m.sidebarWidth = 320.0f;
        m.headerHeight = 72.0f;
        m.footerHeight = 48.0f;
        m.cardSpacing = 16.0f;
        m.sectionSpacing = 32.0f;
        m.gridColumns = 4;
        m.maxContentWidth = 1800.0f;
        break;
    }

    m.sidebarWidth *= ui_scale * dpi_scale;
    m.headerHeight *= ui_scale * dpi_scale;
    m.footerHeight *= ui_scale * dpi_scale;
    m.cardSpacing *= ui_scale * dpi_scale;
    m.sectionSpacing *= ui_scale * dpi_scale;
    m.maxContentWidth *= ui_scale * dpi_scale;
}

Breakpoint GetBreakpoint(float width) noexcept {
    if (width < 800.0f) return Breakpoint::Small;
    if (width < 1200.0f) return Breakpoint::Medium;
    if (width < 1600.0f) return Breakpoint::Large;
    if (width < 2560.0f) return Breakpoint::XLarge;
    return Breakpoint::XXLarge;
}

float ResponsiveValue(float small, float medium, float large, float xlarge, float xxlarge) noexcept {
    switch (static_cast<int>(GetMetrics().current)) {
    case static_cast<int>(Breakpoint::Small): return small;
    case static_cast<int>(Breakpoint::Medium): return medium;
    case static_cast<int>(Breakpoint::Large): return large;
    case static_cast<int>(Breakpoint::XLarge): return xlarge;
    case static_cast<int>(Breakpoint::XXLarge): return xxlarge;
    }
    return medium;
}

int ResponsiveColumns(int small, int medium, int large, int xlarge, int xxlarge) noexcept {
    switch (static_cast<int>(GetMetrics().current)) {
    case static_cast<int>(Breakpoint::Small): return small;
    case static_cast<int>(Breakpoint::Medium): return medium;
    case static_cast<int>(Breakpoint::Large): return large;
    case static_cast<int>(Breakpoint::XLarge): return xlarge;
    case static_cast<int>(Breakpoint::XXLarge): return xxlarge;
    }
    return medium;
}

ImVec2 ResponsiveSize(const ImVec2& small, const ImVec2& medium, const ImVec2& large, const ImVec2& xlarge, const ImVec2& xxlarge) noexcept {
    switch (static_cast<int>(GetMetrics().current)) {
    case static_cast<int>(Breakpoint::Small): return small;
    case static_cast<int>(Breakpoint::Medium): return medium;
    case static_cast<int>(Breakpoint::Large): return large;
    case static_cast<int>(Breakpoint::XLarge): return xlarge;
    case static_cast<int>(Breakpoint::XXLarge): return xxlarge;
    }
    return medium;
}

void BeginResponsiveContainer(const char* id, float max_width, bool center) {
    ImGui::PushID(id);
    const auto& m = GetMetrics();
    float width = max_width > 0.0f ? max_width : m.maxContentWidth;
    width = (std::min)(width, m.contentSize.x - m.sidebarWidth - m.cardSpacing * 4);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    float x = center ? (avail.x - width) * 0.5f : 0.0f;
    if (x < m.cardSpacing) x = m.cardSpacing;

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    ImGui::BeginChild("##responsive_container", ImVec2(width, 0), false, ImGuiWindowFlags_None);
}

void EndResponsiveContainer() {
    ImGui::EndChild();
    ImGui::PopID();
}

void BeginCardGrid(int columns, float gap) {
    auto& m = GetMetrics();
    if (columns <= 0) columns = m.gridColumns;
    if (gap < 0.0f) gap = m.cardSpacing;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap, gap));
    ImGui::Columns(columns, "##card_grid", false);
}

void EndCardGrid() {
    ImGui::Columns(1);
    ImGui::PopStyleVar();
}

void NextCardGridItem() {
    ImGui::NextColumn();
}

void BeginSection(const char* title, bool collapsible, bool default_open) {
    ImGui::PushID(title);
    ImGui::Spacing();
    ImGui::SeparatorText(title);
    if (collapsible) {
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowItemOverlap;
        if (default_open) flags |= ImGuiTreeNodeFlags_DefaultOpen;
        if (!ImGui::CollapsingHeader("", flags)) {
            ImGui::PopID();
            return;
        }
        ImGui::Indent();
    }
}

void EndSection() {
    if (ImGui::GetCurrentContext() && ImGui::GetCurrentContext()->CurrentWindowStack.Size > 0) {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window && window->DC.TreeDepth > 0) {
            ImGui::Unindent();
        }
    }
    ImGui::PopID();
}

void PushKeyboardNavigationScope() {
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
}

void PopKeyboardNavigationScope() {
}

bool IsKeyboardNavActive() noexcept {
    return ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard;
}

void RegisterFocusable(Focusable* widget) {
    std::scoped_lock lock(g_focusables_mutex);
    if (widget && std::find(g_focusables.begin(), g_focusables.end(), widget) == g_focusables.end()) {
        g_focusables.push_back(widget);
    }
}

void UnregisterFocusable(Focusable* widget) {
    std::scoped_lock lock(g_focusables_mutex);
    auto it = std::find(g_focusables.begin(), g_focusables.end(), widget);
    if (it != g_focusables.end()) {
        g_focusables.erase(it);
        if (g_focus_index >= static_cast<int>(g_focusables.size())) {
            g_focus_index = -1;
        }
    }
}

void FocusNext() {
    std::scoped_lock lock(g_focusables_mutex);
    if (g_focusables.empty()) return;
    g_focus_index = (g_focus_index + 1) % static_cast<int>(g_focusables.size());
    if (auto* w = g_focusables[g_focus_index]; w && w->WantsFocus()) {
        ImGui::SetKeyboardFocusHere();
    }
}

void FocusPrevious() {
    std::scoped_lock lock(g_focusables_mutex);
    if (g_focusables.empty()) return;
    g_focus_index = (g_focus_index - 1 + static_cast<int>(g_focusables.size())) % static_cast<int>(g_focusables.size());
    if (auto* w = g_focusables[g_focus_index]; w && w->WantsFocus()) {
        ImGui::SetKeyboardFocusHere();
    }
}

void FocusFirst() {
    std::scoped_lock lock(g_focusables_mutex);
    if (g_focusables.empty()) return;
    g_focus_index = 0;
    if (auto* w = g_focusables[0]; w && w->WantsFocus()) {
        ImGui::SetKeyboardFocusHere();
    }
}

void FocusLast() {
    std::scoped_lock lock(g_focusables_mutex);
    if (g_focusables.empty()) return;
    g_focus_index = static_cast<int>(g_focusables.size()) - 1;
    if (auto* w = g_focusables[g_focus_index]; w && w->WantsFocus()) {
        ImGui::SetKeyboardFocusHere();
    }
}

void BeginScrollable(const ScrollableRegion& region) {
    ImGui::PushID(region.id);
    ImGui::BeginChild(region.id, region.size, true, region.flags);
    if (region.autoHideScrollbar) {
        ImGuiContext* ctx = ImGui::GetCurrentContext();
        if (ctx && ctx->CurrentWindowStack.Size > 0) {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window) {
                window->ScrollbarY = false;
                window->ScrollbarX = false;
            }
        }
    }
}

void EndScrollable() {
    ImGui::EndChild();
    ImGui::PopID();
}

bool HighContrastMode::IsEnabled() noexcept { return g_high_contrast; }
void HighContrastMode::SetEnabled(bool enabled) noexcept { g_high_contrast = enabled; }
void HighContrastMode::Toggle() noexcept { g_high_contrast = !g_high_contrast; }
ImU32 HighContrastMode::GetColor(ImU32 normal_color) noexcept {
    if (!g_high_contrast) return normal_color;
    unsigned char r = (normal_color >> 0) & 0xFF;
    unsigned char g = (normal_color >> 8) & 0xFF;
    unsigned char b = (normal_color >> 16) & 0xFF;
    unsigned char a = (normal_color >> 24) & 0xFF;
    int luminance = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);
    return luminance > 128 ? IM_COL32(0, 0, 0, a) : IM_COL32(255, 255, 255, a);
}

bool ReducedMotion::IsEnabled() noexcept { return g_reduced_motion; }
float ReducedMotion::GetAnimationScale() noexcept { return g_reduced_motion ? 0.0f : 1.0f; }
void ReducedMotion::SetEnabled(bool enabled) noexcept { g_reduced_motion = enabled; }

LocalizedString::operator const char*() const noexcept {
    return GetLocalized(key, fallback).c_str();
}

std::string GetLocalized(const char* key, const char* fallback) noexcept {
    if (!key) return fallback ? fallback : "";
    return Loc::Tr(key);
}

void AuditLocalizationCoverage() {
    // This will be called from the UI to report coverage
    // The actual audit is done in the localization system
}

} // namespace CyberLayout