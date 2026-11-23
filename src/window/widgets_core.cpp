#include "widgets.h"
#include "theme.h"
#include "animations.h"
#include "localization.h"
#include "ui_feedback.h"
#include "config/app_settings.h"
#include "src/project/third_party/ImGui/imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace CyberWidgets {

namespace {

// Animation state maps
std::unordered_map<ImGuiID, float> g_toggle_progress;
std::unordered_map<ImGuiID, float> g_status_progress;
std::unordered_map<ImGuiID, float> g_metric_progress;
float g_sidebar_indicator_y = -1.0f;

float WidgetWidth() {
    return std::max(40.0f,
        ImGui::GetContentRegionAvail().x);
}

float SmoothMetric(ImGuiID id, float target) {
    auto [it, inserted] = g_metric_progress.try_emplace(id, target);
    if (inserted || !app_settings::MotionEnabled()) {
        it->second = target;
        return target;
    }
    const float amount = 1.0f - std::exp(-10.0f * std::min(ImGui::GetIO().DeltaTime, 0.05f));
    it->second += (target - it->second) * amount;
    return it->second;
}

std::string VisibleLabel(const char* label) {
    if (!label) return {};
    const char* marker = std::strstr(label, "###");
    if (!marker) marker = std::strstr(label, "##");
    return marker ? std::string(label, marker) : std::string(label);
}

const char* DisplayLabel(const char* label) {
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

void DrawCheckers(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding) {
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

void ApplyCursorForItem(bool enabled = true, ImGuiMouseCursor cursor = ImGuiMouseCursor_Hand) {
    if (enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetMouseCursor(cursor);
}

void DrawFocusRing(float rounding) {
    if (!ImGui::IsItemFocused() && !ImGui::IsItemActive()) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();
    dl->AddRect(
        ImVec2(a.x - 1.0f, a.y - 1.0f),
        ImVec2(b.x + 1.0f, b.y + 1.0f),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.58f),
        rounding >= 0.0f ? rounding : CyberTheme::Metrics::ControlRounding,
        0, 1.2f);
}

ImVec4 SemanticColor(ButtonStyle style) {
    if (style == ButtonStyle::Destructive) return CyberTheme::Colors.Error;
    return CyberTheme::Colors.Gold;
}

ImU32 ToneColor(TextTone tone) {
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

void DrawSpinnerInternal(ImDrawList* draw, ImVec2 center, float radius, float thickness, TextTone tone) {
    draw->PathClear();
    constexpr int segments = 20;
    const float start = static_cast<float>(ImGui::GetTime()) * 5.0f;
    for (int i = 0; i <= segments; ++i) {
        const float angle = start + (static_cast<float>(i) / segments) * 4.7f;
        draw->PathLineTo(ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius));
    }
    draw->PathStroke(ToneColor(tone), ImDrawFlags_None, thickness);
}

} // anonymous namespace

// =============================================================================
// Button Widgets Implementation
// =============================================================================

bool StyledButton(const char* label, const ImVec2& size_arg, ButtonStyle style,
                  bool enabled, bool pending, const char* pending_label) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImGuiID id = window->GetID(label);
    const std::string visible = VisibleLabel(pending && pending_label ? pending_label : label);
    const ImVec2 text_size = ImGui::CalcTextSize(visible.c_str());
    ImVec2 size = size_arg;
    if (size.x < 0.0f) size.x = WidgetWidth();
    else if (size.x == 0.0f) size.x = text_size.x + (pending ? 50.0f : 28.0f);
    else if (WidgetWidth() < 300.0f && size.x > 120.0f) size.x = (WidgetWidth() - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if (size.y <= 0.0f) size.y = CyberTheme::Metrics::ControlHeight;

    const bool interactive = enabled && !pending;
    const ImVec2 item_pos = ImGui::GetCursorScreenPos();
    if (!interactive) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton(label, size);
    if (!interactive) ImGui::EndDisabled();
    const bool hovered = interactive && ImGui::IsItemHovered();
    const bool held = interactive && ImGui::IsItemActive();
    const bool clicked = interactive && pressed;
    ApplyCursorForItem(interactive);

    CyberAnimations::SetHover(id, hovered);
    const float hover = CyberAnimations::GetHover(id);
    const float motion = app_settings::AnimationScale();
    const float scale = motion <= 0.0f ? 1.0f
        : (held ? 0.988f : 1.0f + hover * (0.006f + motion * 0.006f));
    const ImVec2 pos(
        item_pos.x + size.x * (1.0f - scale) * 0.5f,
        item_pos.y + size.y * (1.0f - scale) * 0.5f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x * scale, pos.y + size.y * scale);
    const float disabledAlpha = interactive ? 1.0f : 0.48f;
    const ImVec4 semantic = SemanticColor(style);
    const bool fullMotion = app_settings::MotionEnabled() &&
        app_settings::config.animation_intensity == app_settings::EffectLevel::Full;

    if (style == ButtonStyle::Primary) {
        const ImVec4 bg = CyberTheme::Mix(semantic, CyberTheme::Colors.GoldHover, hover * 0.72f);
        if (hover > 0.01f) {
            dl->AddRectFilled(
                ImVec2(pos.x, pos.y + 2.0f),
                ImVec2(end.x, end.y + 3.0f),
                CyberTheme::WithAlpha(CyberTheme::Colors.GoldGlow, 0.24f * hover),
                CyberTheme::Metrics::ControlRounding);
        }
        dl->AddRectFilled(pos, end, CyberTheme::WithAlpha(bg, disabledAlpha), CyberTheme::Metrics::ControlRounding);
        dl->AddRect(pos, end, CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.28f * disabledAlpha), CyberTheme::Metrics::ControlRounding, 0, 1.0f);
        if (held) dl->AddRectFilled(pos, end, CyberTheme::SafeShadowU32(24), CyberTheme::Metrics::ControlRounding);
    } else if (style == ButtonStyle::Destructive) {
        const ImVec4 base = CyberTheme::Mix(CyberTheme::Colors.Surface, semantic, 0.12f + hover * 0.10f);
        dl->AddRectFilled(pos, end, CyberTheme::WithAlpha(base, disabledAlpha), CyberTheme::Metrics::ControlRounding);
        dl->AddRect(pos, end, CyberTheme::WithAlpha(semantic, (hovered ? 0.64f : 0.38f) * disabledAlpha), CyberTheme::Metrics::ControlRounding, 0, hovered ? 1.3f : 1.0f);
    } else if (style == ButtonStyle::Ghost) {
        if (hover > 0.01f)
            dl->AddRectFilled(pos, end, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.08f * hover * disabledAlpha), CyberTheme::Metrics::ControlRounding);
        dl->AddRect(pos, end, CyberTheme::WithAlpha(hovered ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border, (hovered ? 0.34f : 0.18f) * disabledAlpha), CyberTheme::Metrics::ControlRounding, 0, 1.0f);
    } else {
        const ImVec4 bg = CyberTheme::Mix(CyberTheme::Colors.Panel, CyberTheme::Colors.PanelHover, hover);
        dl->AddRectFilled(pos, end, CyberTheme::WithAlpha(bg, disabledAlpha), CyberTheme::Metrics::ControlRounding);
        dl->AddRect(pos, end, CyberTheme::WithAlpha(hovered ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border, (hovered ? 0.28f : 0.50f) * disabledAlpha), CyberTheme::Metrics::ControlRounding, 0, 1.0f);
    }

    if (fullMotion && hover > 0.01f) {
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.72f, 1.0f);
        const float sheenX = pos.x - CyberTheme::Px(38.0f) + (size.x + CyberTheme::Px(76.0f)) * phase;
        dl->PushClipRect(pos, end, true);
        dl->AddRectFilledMultiColor(
            ImVec2(sheenX - CyberTheme::Px(25.0f), pos.y),
            ImVec2(sheenX + CyberTheme::Px(25.0f), end.y),
            IM_COL32(255, 255, 255, 0),
            IM_COL32(255, 247, 205, static_cast<int>(18.0f * hover * disabledAlpha)),
            IM_COL32(255, 247, 205, static_cast<int>(18.0f * hover * disabledAlpha)),
            IM_COL32(255, 255, 255, 0));
        dl->PopClipRect();
    }

    const ImU32 textColor = style == ButtonStyle::Primary
        ? IM_COL32(24, 22, 18, static_cast<int>(255 * disabledAlpha))
        : (style == ButtonStyle::Destructive ? CyberTheme::WithAlpha(CyberTheme::Colors.Error, disabledAlpha)
            : CyberTheme::WithAlpha(CyberTheme::Colors.Text, disabledAlpha));

    const float spinnerSpace = pending ? 18.0f : 0.0f;
    const float totalTextWidth = text_size.x + spinnerSpace;
    const float nudge = CyberAnimations::HoverNudge(id, hovered);
    const float textX = item_pos.x + (size.x - totalTextWidth) * 0.5f + spinnerSpace + nudge;
    dl->AddText(ImVec2(textX, item_pos.y + (size.y - text_size.y) * 0.5f), textColor, visible.c_str());
    if (pending) {
        const ImVec2 center(textX - CyberTheme::Px(10.0f), item_pos.y + size.y * 0.5f);
        DrawSpinnerInternal(dl, center, CyberTheme::Px(6.0f), CyberTheme::Px(1.8f),
            style == ButtonStyle::Destructive ? TextTone::Error : TextTone::Accent);
    }

    if (ImGui::IsItemFocused())
        dl->AddRect(ImVec2(pos.x - 1.0f, pos.y - 1.0f), ImVec2(end.x + 1.0f, end.y + 1.0f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.52f), CyberTheme::Metrics::ControlRounding, 0, 1.2f);
    if (clicked) OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Click);
    return clicked;
}

bool ToggleSwitch(const char* label, bool* value) {
    if (!value) return false;
    const ImGuiID id = ImGui::GetID(label);
    const std::string visible = VisibleLabel(label);
    const ImVec2 text_size = ImGui::CalcTextSize(visible.c_str());

    const ImVec2 size(CyberTheme::Px(52.0f), CyberTheme::Px(28.0f));
    const ImVec2 item_pos = ImGui::GetCursorScreenPos();
    const ImVec2 end(item_pos.x + size.x, item_pos.y + size.y);

    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool pressed = ImGui::InvisibleButton(label, size);
    ApplyCursorForItem(true);

    CyberAnimations::SetHover(id, hovered);
    const float hover = CyberAnimations::GetHover(id);
    const float target = *value ? 1.0f : 0.0f;
    auto& progress = g_toggle_progress[id];
    progress += (target - progress) * (1.0f - std::exp(-15.0f * std::min(ImGui::GetIO().DeltaTime, 0.05f)));

    if (pressed) {
        *value = !*value;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 bg = CyberTheme::Mix(*value ? CyberTheme::Colors.Gold : CyberTheme::Colors.Panel,
                          *value ? CyberTheme::Colors.GoldHover : CyberTheme::Colors.PanelHover,
                          hover);
    dl->AddRectFilled(item_pos, end, CyberTheme::WithAlpha(bg, 1.0f), CyberTheme::Metrics::ControlRounding);
    dl->AddRect(item_pos, end, CyberTheme::WithAlpha(hovered ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border,
        hovered ? 0.34f : 0.18f), CyberTheme::Metrics::ControlRounding);

    const float knobRadius = size.y * 0.42f;
    const float knobX = item_pos.x + size.y * 0.28f + (size.x - size.y * 0.56f) * progress;
    const ImVec2 knobCenter(knobX, item_pos.y + size.y * 0.5f);
    dl->AddCircleFilled(knobCenter, knobRadius, IM_COL32(24, 22, 18, 255), 24);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (pressed) OmniGhost::UiFeedback::Play(*value ? OmniGhost::UiFeedback::SoundEvent::ToggleOn : OmniGhost::UiFeedback::SoundEvent::ToggleOff);

    return *value;
}

} // namespace CyberWidgets
