#pragma warning(disable: 4100 4189)
#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "animations.h"
#include "localization.h"
#include "hardware_monitor.h"
#include "config_history.h"
#include "../config/app_settings.h"
#include "../ImGui/imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Windows headers map GetMessage -> GetMessageW; ProgressDialog uses GetMessage().
#ifdef GetMessage
#undef GetMessage
#endif

using namespace CyberTheme;

namespace CyberWidgets {

    namespace {
        std::unordered_map<ImGuiID, float> g_toggle_progress;

        float WidgetWidth()
        {
            return std::max(40.0f, CardContentWidth());
        }

        // Strip ImGui ID suffixes: "Label###stable" or "Label##id" → "Label"
        std::string VisibleLabel(const char* label)
        {
            if (!label)
                return {};
            const char* marker = std::strstr(label, "###");
            if (!marker)
                marker = std::strstr(label, "##");
            return marker ? std::string(label, marker) : std::string(label);
        }

        // Display pointer without allocating when possible (for draw calls).
        // Returns a pointer into `label` or a static empty string.
        // For ### cases, uses a thread_local buffer.
        const char* DisplayLabel(const char* label)
        {
            if (!label)
                return "";
            const char* triple = std::strstr(label, "###");
            if (triple) {
                static thread_local char buf[192];
                const size_t n = static_cast<size_t>(triple - label);
                if (n >= sizeof(buf))
                    return label;
                std::memcpy(buf, label, n);
                buf[n] = '\0';
                return buf;
            }
            const char* dbl = std::strstr(label, "##");
            if (dbl) {
                static thread_local char buf2[192];
                const size_t n = static_cast<size_t>(dbl - label);
                if (n >= sizeof(buf2))
                    return label;
                std::memcpy(buf2, label, n);
                buf2[n] = '\0';
                return buf2;
            }
            return label;
        }

        void DrawCheckers(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding)
        {
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

        void ApplyCursorForItem(bool enabled = true, ImGuiMouseCursor cursor = ImGuiMouseCursor_Hand)
        {
            if (enabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetMouseCursor(cursor);
        }

        void DrawFocusRing(float rounding = -1.0f)
        {
            if (!ImGui::IsItemFocused() && !ImGui::IsItemActive())
                return;
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

        ImVec4 SemanticColor(ButtonStyle style)
        {
            if (style == ButtonStyle::Destructive)
                return CyberTheme::Colors.Error;
            return CyberTheme::Colors.Gold;
        }

        bool StyledButton(const char* label, const ImVec2& size_arg, ButtonStyle style,
                          bool enabled, bool pending = false, const char* pending_label = nullptr)
        {
            ImGuiWindow* window = ImGui::GetCurrentWindow();
            if (window->SkipItems)
                return false;

            const ImGuiID id = window->GetID(label);
            const std::string visible = VisibleLabel(pending && pending_label ? pending_label : label);
            const ImVec2 text_size = ImGui::CalcTextSize(visible.c_str());
            ImVec2 size = size_arg;
            if (size.x < 0.0f)
                size.x = WidgetWidth();
            else if (size.x == 0.0f)
                size.x = text_size.x + (pending ? 50.0f : 28.0f);
            else if (WidgetWidth() < 300.0f && size.x > 120.0f)
                size.x = (WidgetWidth() - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            if (size.y <= 0.0f)
                size.y = CyberTheme::Metrics::ControlHeight;

            const bool interactive = enabled && !pending;
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            if (!interactive) ImGui::BeginDisabled();
            const bool pressed = ImGui::InvisibleButton(label, size);
            if (!interactive) ImGui::EndDisabled();
            const bool hovered = interactive && ImGui::IsItemHovered();
            const bool held = interactive && ImGui::IsItemActive();
            const bool clicked = interactive && pressed;
            ApplyCursorForItem(interactive);

            CyberAnimations::SetHover(id, hovered);
            const float hover = CyberAnimations::GetHover(id);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 end(pos.x + size.x, pos.y + size.y);
            const float disabledAlpha = interactive ? 1.0f : 0.48f;
            const ImVec4 semantic = SemanticColor(style);

            if (style == ButtonStyle::Primary) {
                const ImVec4 bg = Mix(semantic, CyberTheme::Colors.GoldHover, hover * 0.72f);
                if (hover > 0.01f) {
                    dl->AddRectFilled(
                        ImVec2(pos.x, pos.y + 2.0f),
                        ImVec2(end.x, end.y + 3.0f),
                        WithAlpha(CyberTheme::Colors.GoldGlow, 0.24f * hover),
                        CyberTheme::Metrics::ControlRounding);
                }
                dl->AddRectFilled(pos, end, WithAlpha(bg, disabledAlpha),
                    CyberTheme::Metrics::ControlRounding);
                dl->AddRect(pos, end, WithAlpha(CyberTheme::Colors.GoldHover, 0.28f * disabledAlpha),
                    CyberTheme::Metrics::ControlRounding, 0, 1.0f);
                if (held)
                    dl->AddRectFilled(pos, end, CyberTheme::SafeShadowU32(24),
                        CyberTheme::Metrics::ControlRounding);
            } else if (style == ButtonStyle::Destructive) {
                const ImVec4 base = Mix(CyberTheme::Colors.Surface, semantic, 0.12f + hover * 0.10f);
                dl->AddRectFilled(pos, end, WithAlpha(base, disabledAlpha), CyberTheme::Metrics::ControlRounding);
                dl->AddRect(pos, end, WithAlpha(semantic, (hovered ? 0.64f : 0.38f) * disabledAlpha),
                    CyberTheme::Metrics::ControlRounding, 0, hovered ? 1.3f : 1.0f);
            } else if (style == ButtonStyle::Ghost) {
                if (hover > 0.01f)
                    dl->AddRectFilled(pos, end, WithAlpha(CyberTheme::Colors.Gold, 0.08f * hover * disabledAlpha),
                        CyberTheme::Metrics::ControlRounding);
                dl->AddRect(pos, end,
                    WithAlpha(hovered ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border,
                              (hovered ? 0.34f : 0.18f) * disabledAlpha),
                    CyberTheme::Metrics::ControlRounding, 0, 1.0f);
            } else {
                const ImVec4 bg = Mix(CyberTheme::Colors.Panel, CyberTheme::Colors.PanelHover, hover);
                dl->AddRectFilled(pos, end, WithAlpha(bg, disabledAlpha), CyberTheme::Metrics::ControlRounding);
                dl->AddRect(pos, end,
                    WithAlpha(hovered ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border,
                              (hovered ? 0.28f : 0.50f) * disabledAlpha),
                    CyberTheme::Metrics::ControlRounding, 0, 1.0f);
            }

            const ImU32 textColor = style == ButtonStyle::Primary
                ? IM_COL32(24, 22, 18, static_cast<int>(255 * disabledAlpha))
                : (style == ButtonStyle::Destructive
                    ? CyberTheme::WithAlpha(CyberTheme::Colors.Error, disabledAlpha)
                    : CyberTheme::WithAlpha(CyberTheme::Colors.Text, disabledAlpha));

            const float spinnerSpace = pending ? 18.0f : 0.0f;
            const float totalTextWidth = text_size.x + spinnerSpace;
            const float textX = pos.x + (size.x - totalTextWidth) * 0.5f + spinnerSpace;
            dl->AddText(ImVec2(textX, pos.y + (size.y - text_size.y) * 0.5f), textColor, visible.c_str());
            if (pending) {
                const ImVec2 center(textX - CyberTheme::Px(10.0f), pos.y + size.y * 0.5f);
                DrawSpinner(dl, center, CyberTheme::Px(6.0f), CyberTheme::Px(1.8f),
                            style == ButtonStyle::Destructive ? TextTone::Error : TextTone::Accent);
            }

            if (ImGui::IsItemFocused())
                dl->AddRect(ImVec2(pos.x - 1.0f, pos.y - 1.0f), ImVec2(end.x + 1.0f, end.y + 1.0f),
                    WithAlpha(CyberTheme::Colors.Gold, 0.52f), CyberTheme::Metrics::ControlRounding, 0, 1.2f);
            return clicked;
        }

        ImU32 ToneColor(TextTone tone)
        {
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

        struct SurfaceListState {
            ImVec2 position;
            float width;
            float height;
        };

        std::vector<SurfaceListState> g_surface_lists;
    }

    bool ToggleSwitch(const char* label, bool* value)
    {
        if (!value)
            return false;
        if (!PassSearch(label))
            return false;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        // Stable ID from the bound bool pointer — never from translated text.
        // This prevents one toggle from flipping its neighbour when labels share hashes.
        ImGui::PushID(static_cast<const void*>(value));

        const float width = WidgetWidth();
        const float row_height = CyberTheme::Metrics::RowHeight;
        const float track_width = CyberTheme::Metrics::ToggleWidth;
        const float track_height = CyberTheme::Metrics::ToggleHeight;
        const ImVec2 row_pos = ImGui::GetCursorScreenPos();
        const ImGuiID id = window->GetID("##toggle");

        const bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(width, row_height));
        const bool hovered = ImGui::IsItemHovered();
        ApplyCursorForItem(true);
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        if (clicked)
            *value = !*value;
        CyberAnimations::SetHover(id, hovered);

        float& progress = g_toggle_progress[id];
        const float target = *value ? 1.0f : 0.0f;
        const float motion = app_settings::AnimationScale();
        if (motion <= 0.0f) progress = target;
        else {
            const float amount = 1.0f - std::exp(-15.0f * ImGui::GetIO().DeltaTime * (0.65f + motion));
            progress += (target - progress) * amount;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* display = DisplayLabel(label);
        const float text_y = row_pos.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(row_pos.x, text_y),
            hovered ? IM_COL32(238, 239, 244, 255)
                    : ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text),
            display);

        const ImVec2 track_pos(
            row_pos.x + width - track_width,
            row_pos.y + (row_height - track_height) * 0.5f);
        const ImVec4 off_color = Mix(
            CyberTheme::Colors.Background,
            CyberTheme::Colors.TextDisabled, 0.24f);
        const ImVec4 track_color = Mix(off_color, CyberTheme::Colors.Gold, progress);
        dl->AddRectFilled(track_pos,
            ImVec2(track_pos.x + track_width, track_pos.y + track_height),
            ImGui::ColorConvertFloat4ToU32(track_color), track_height * 0.5f);

        const float knob_radius = 8.0f;
        const float knob_x = track_pos.x + 11.0f + progress * (track_width - 22.0f);
        const ImVec2 knob(knob_x, track_pos.y + track_height * 0.5f);
        dl->AddCircleFilled(ImVec2(knob.x, knob.y + 1.0f), knob_radius,
            CyberTheme::SafeShadowU32(25), 18);
        dl->AddCircleFilled(knob, knob_radius, IM_COL32(250, 250, 252, 255), 18);

        ImGui::PopID();
        return clicked;
    }

    bool Button(const char* label, ButtonStyle style, const ImVec2& size, bool enabled)
    {
        return StyledButton(label, size, style, enabled);
    }

    bool LoadingButton(const char* label, const char* pending_label, bool pending,
                       ButtonStyle style, const ImVec2& size, bool enabled)
    {
        return StyledButton(label, size, style, enabled, pending, pending_label);
    }

    bool CyberButton(const char* label, const ImVec2& size)
    {
        return Button(label, ButtonStyle::Secondary, size, true);
    }

    bool GoldButton(const char* label, const ImVec2& size)
    {
        return Button(label, ButtonStyle::Primary, size, true);
    }

    bool GhostButton(const char* label, const ImVec2& size)
    {
        return Button(label, ButtonStyle::Ghost, size, true);
    }

    bool DangerButton(const char* label, const ImVec2& size)
    {
        return Button(label, ButtonStyle::Destructive, size, true);
    }

    bool SidebarButton(const char* label, bool active,
                       void(*icon_fn)(ImDrawList*, ImVec2, float, ImU32),
                       float height)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID id = window->GetID(label);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 size(
            ImGui::GetContentRegionAvail().x,
            height > 0.0f ? height : CyberTheme::Metrics::SidebarItemHeight);

        const bool clicked = ImGui::InvisibleButton(label, size);
        const bool hovered = ImGui::IsItemHovered();
        ApplyCursorForItem(true);
        CyberAnimations::SetHover(id, hovered || active);

        const float transition = CyberAnimations::GetHover(id);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (transition > 0.01f) {
            const ImVec4 tint = active
                ? Mix(CyberTheme::Colors.Background,
                      CyberTheme::Colors.Gold, 0.11f)
                : CyberTheme::Colors.PanelHover;
            const float tint_alpha = active ? 0.96f : 0.35f * transition;
            dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                WithAlpha(tint, tint_alpha),
                CyberTheme::Metrics::ControlRounding);
        }

        if (active) {
            const float indicator_h = 24.0f + transition * 6.0f;
            const float indicator_y = pos.y + (size.y - indicator_h) * 0.5f;
            dl->AddRectFilled(
                ImVec2(pos.x, indicator_y),
                ImVec2(pos.x + 4.0f, indicator_y + indicator_h),
                ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), 2.0f);
        }

        const ImU32 icon_color = active
            ? ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold)
            : (hovered ? IM_COL32(218, 220, 228, 255)
                       : ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.TextDisabled));
        if (icon_fn)
            icon_fn(dl, ImVec2(pos.x + 12.0f, pos.y + 10.0f), 26.0f, icon_color);

        const ImU32 text_color = active
            ? IM_COL32(239, 240, 244, 255)
            : (hovered ? IM_COL32(220, 222, 230, 255)
                       : IM_COL32(151, 155, 169, 255));
        ImFont* body = CyberFonts::GetBodyFont();
        const char* side_display = DisplayLabel(label);
        if (body)
            dl->AddText(body, 16.0f, ImVec2(pos.x + 46.0f, pos.y + 11.0f),
                text_color, side_display);
        else
            dl->AddText(ImVec2(pos.x + 46.0f, pos.y + 12.0f), text_color, side_display);
        return clicked;
    }

    void SectionTitle(const char* title)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImFont* body = CyberFonts::GetBodyFont();
        if (body)
            dl->AddText(body, 11.5f, pos,
                ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), title);
        else
            dl->AddText(pos, ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), title);
        ImGui::Dummy(ImVec2(0.0f, 20.0f));
    }

    void Separator()
    {
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        dl->AddLine(pos, ImVec2(pos.x + WidgetWidth(), pos.y),
            WithAlpha(CyberTheme::Colors.Border, 0.38f), 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    bool SliderFloat(const char* label, float* value, float minimum, float maximum,
                     const char* format)
    {
        if (!value)
            return false;
        if (!PassSearch(label))
            return false;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::PushID(static_cast<const void*>(value));
        const ImGuiID id = window->GetID("##slider");
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const float height = 42.0f;

        ImGui::InvisibleButton("##slider", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ApplyCursorForItem(true);
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        CyberAnimations::SetHover(id, hovered || active);

        const float previous = *value;
        const float keyboardStep = std::max(0.0001f, (maximum - minimum) * 0.01f);
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
            *value = std::max(minimum, *value - keyboardStep);
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
            *value = std::min(maximum, *value + keyboardStep);
        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float amount = (ImGui::GetIO().MousePos.x - pos.x) / width;
            amount = std::clamp(amount, 0.0f, 1.0f);
            *value = minimum + amount * (maximum - minimum);
        }

        const float fraction = std::clamp(
            (*value - minimum) / std::max(0.0001f, maximum - minimum), 0.0f, 1.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        char value_text[64];
        snprintf(value_text, sizeof(value_text), format, *value);
        const ImVec2 value_size = ImGui::CalcTextSize(value_text);
        dl->AddText(pos, ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text), DisplayLabel(label));
        dl->AddText(ImVec2(pos.x + width - value_size.x, pos.y),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), value_text);

        const float track_y = pos.y + 28.0f;
        dl->AddRectFilled(ImVec2(pos.x, track_y),
            ImVec2(pos.x + width, track_y + 5.0f),
            CyberTheme::U32(Mix(
                CyberTheme::Colors.Background,
                CyberTheme::Colors.TextDisabled, 0.16f)), 2.5f);
        if (fraction > 0.0f)
            dl->AddRectFilled(ImVec2(pos.x, track_y),
                ImVec2(pos.x + width * fraction, track_y + 5.0f),
                ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), 2.5f);

        const ImVec2 knob(pos.x + width * fraction, track_y + 2.5f);
        if (hovered || active)
            dl->AddCircleFilled(knob, 9.0f,
                WithAlpha(CyberTheme::Colors.GoldGlow, 0.42f), 20);
        dl->AddCircleFilled(knob, 6.0f, IM_COL32(246, 246, 249, 255), 18);
        ImGui::PopID();
        return previous != *value;
    }

    bool Combo(const char* label, int* current_item,
               const char* const items[], int items_count)
    {
        if (!current_item)
            return false;
        if (!PassSearch(label))
            return false;

        ImGui::PushID(static_cast<const void*>(current_item));
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const float row_height = 36.0f;
        const float field_width = std::clamp(width * 0.48f, 96.0f, 168.0f);
        const ImVec2 field_pos(pos.x + width - field_width, pos.y + 2.0f);
        const ImVec2 field_end(pos.x + width, pos.y + row_height - 2.0f);

        const bool clicked = ImGui::InvisibleButton("##combo_row", ImVec2(width, row_height));
        const bool hovered = ImGui::IsItemHovered();
        ApplyCursorForItem(true);
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        if (clicked)
            ImGui::OpenPopup("##combo_popup");

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(pos, ImVec2(field_pos.x - 8.0f, pos.y + row_height), true);
        dl->AddText(ImVec2(pos.x, pos.y + 9.0f),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text), DisplayLabel(label));
        dl->PopClipRect();

        dl->AddRectFilled(field_pos, field_end,
            CyberTheme::U32(hovered
                ? CyberTheme::Colors.PanelHover
                : Mix(CyberTheme::Colors.Background,
                      CyberTheme::Colors.Panel, 0.68f)),
            CyberTheme::Metrics::ControlRounding);
        dl->AddRect(field_pos, field_end,
            hovered ? WithAlpha(CyberTheme::Colors.Gold, 0.34f)
                    : WithAlpha(CyberTheme::Colors.Border, 0.48f),
            CyberTheme::Metrics::ControlRounding, 0, 1.0f);

        const char* current = (*current_item >= 0 && *current_item < items_count)
            ? items[*current_item] : "--";
        dl->AddText(ImVec2(field_pos.x + 10.0f, field_pos.y + 7.0f),
            IM_COL32(207, 209, 218, 255), current);
        const ImVec2 arrow(field_end.x - 13.0f, field_pos.y + 14.0f);
        dl->AddTriangleFilled(
            ImVec2(arrow.x - 4.0f, arrow.y - 2.0f),
            ImVec2(arrow.x + 4.0f, arrow.y - 2.0f),
            ImVec2(arrow.x, arrow.y + 2.5f),
            IM_COL32(137, 140, 153, 255));

        bool changed = false;
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(field_width, 0.0f), ImVec2(field_width, 220.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5.0f, 5.0f));
        if (ImGui::BeginPopup("##combo_popup",
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
            for (int index = 0; index < items_count; ++index) {
                ImGui::PushID(index);
                const ImVec2 item_pos = ImGui::GetCursorScreenPos();
                const ImVec2 item_size(ImGui::GetContentRegionAvail().x, 28.0f);
                const bool item_clicked = ImGui::InvisibleButton("##item", item_size);
                const bool item_hovered = ImGui::IsItemHovered();
                ApplyCursorForItem(true);
                const bool selected = *current_item == index;
                if (item_clicked) {
                    *current_item = index;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }

                if (selected || item_hovered) {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        item_pos, ImVec2(item_pos.x + item_size.x, item_pos.y + item_size.y),
                        selected ? WithAlpha(CyberTheme::Colors.Gold, 0.14f)
                                 : IM_COL32(255, 255, 255, 8), 6.0f);
                }
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(item_pos.x + 8.0f, item_pos.y + 7.0f),
                    selected ? ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold)
                             : ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text),
                    items[index]);
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
        return changed;
    }

    bool Combo(const char* label, int* current_item,
               const char* const items[], int items_count, const std::string& history_id)
    {
        int prev = *current_item;
        bool changed = Combo(label, current_item, items, items_count);
        if (changed && !history_id.empty()) {
            ConfigHistory::RecordChange(history_id, label, ConfigHistory::ActionType::SetEnum,
                prev, *current_item);
        }
        return changed;
    }

    bool SliderFloat(const char* label, float* value, float minimum, float maximum,
                     const char* format, const std::string& history_id)
    {
        if (!value)
            return false;
        if (!PassSearch(label))
            return false;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImGui::PushID(static_cast<const void*>(value));
        const ImGuiID id = window->GetID("##slider");
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const float height = 42.0f;

        ImGui::InvisibleButton("##slider", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        ApplyCursorForItem(true);
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        CyberAnimations::SetHover(id, hovered || active);

        float previous = *value;
        const float keyboardStep = std::max(0.0001f, (maximum - minimum) * 0.01f);
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))
            *value = std::max(minimum, *value - keyboardStep);
        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_RightArrow, true))
            *value = std::min(maximum, *value + keyboardStep);
        if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float amount = (ImGui::GetIO().MousePos.x - pos.x) / width;
            amount = std::clamp(amount, 0.0f, 1.0f);
            *value = minimum + amount * (maximum - minimum);
        }

        const float fraction = std::clamp(
            (*value - minimum) / std::max(0.0001f, maximum - minimum), 0.0f, 1.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        char value_text[64];
        snprintf(value_text, sizeof(value_text), format, *value);
        const ImVec2 value_size = ImGui::CalcTextSize(value_text);
        dl->AddText(pos, ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text), DisplayLabel(label));
        dl->AddText(ImVec2(pos.x + width - value_size.x, pos.y),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), value_text);

        const float track_y = pos.y + 28.0f;
        dl->AddRectFilled(ImVec2(pos.x, track_y),
            ImVec2(pos.x + width, track_y + 5.0f),
            CyberTheme::U32(Mix(
                CyberTheme::Colors.Background,
                CyberTheme::Colors.TextDisabled, 0.16f)), 2.5f);
        if (fraction > 0.0f)
            dl->AddRectFilled(ImVec2(pos.x, track_y),
                ImVec2(pos.x + width * fraction, track_y + 5.0f),
                ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), 2.5f);

        const ImVec2 knob(pos.x + width * fraction, track_y + 2.5f);
        if (hovered || active)
            dl->AddCircleFilled(knob, 9.0f,
                WithAlpha(CyberTheme::Colors.GoldGlow, 0.42f), 20);
        dl->AddCircleFilled(knob, 6.0f, IM_COL32(246, 246, 249, 255), 18);
        ImGui::PopID();
        
        if (previous != *value && !history_id.empty()) {
            ConfigHistory::RecordChange(history_id, label, ConfigHistory::ActionType::SetFloat,
                previous, *value);
        }
        return previous != *value;
    }

    bool ToggleSwitch(const char* label, bool* value, const std::string& history_id)
    {
        if (!value)
            return false;
        if (!PassSearch(label))
            return false;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGui::PushID(static_cast<const void*>(value));

        const float width = WidgetWidth();
        const float row_height = CyberTheme::Metrics::RowHeight;
        const float track_width = CyberTheme::Metrics::ToggleWidth;
        const float track_height = CyberTheme::Metrics::ToggleHeight;
        const ImVec2 row_pos = ImGui::GetCursorScreenPos();
        const ImGuiID id = window->GetID("##toggle");

        const bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(width, row_height));
        const bool hovered = ImGui::IsItemHovered();
        ApplyCursorForItem(true);
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        if (clicked)
            *value = !*value;
        CyberAnimations::SetHover(id, hovered);

        float& progress = g_toggle_progress[id];
        const float target = *value ? 1.0f : 0.0f;
        const float motion = app_settings::AnimationScale();
        if (motion <= 0.0f) progress = target;
        else {
            const float amount = 1.0f - std::exp(-15.0f * ImGui::GetIO().DeltaTime * (0.65f + motion));
            progress += (target - progress) * amount;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* display = DisplayLabel(label);
        const float text_y = row_pos.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(row_pos.x, text_y),
            hovered ? IM_COL32(238, 239, 244, 255)
                    : ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text),
            display);

        const ImVec2 track_pos(
            row_pos.x + width - track_width,
            row_pos.y + (row_height - track_height) * 0.5f);
        const ImVec4 off_color = Mix(
            CyberTheme::Colors.Background,
            CyberTheme::Colors.TextDisabled, 0.24f);
        const ImVec4 track_color = Mix(off_color, CyberTheme::Colors.Gold, progress);
        dl->AddRectFilled(track_pos,
            ImVec2(track_pos.x + track_width, track_pos.y + track_height),
            ImGui::ColorConvertFloat4ToU32(track_color), track_height * 0.5f);

        const float knob_radius = 8.0f;
        const float knob_x = track_pos.x + 11.0f + progress * (track_width - 22.0f);
        const ImVec2 knob(knob_x, track_pos.y + track_height * 0.5f);
        dl->AddCircleFilled(ImVec2(knob.x, knob.y + 1.0f), knob_radius,
            CyberTheme::SafeShadowU32(25), 18);
        dl->AddCircleFilled(knob, knob_radius, IM_COL32(250, 250, 252, 255), 18);

        ImGui::PopID();
        
        if (clicked && !history_id.empty()) {
            ConfigHistory::RecordChange(history_id, label, ConfigHistory::ActionType::SetBool,
                !*value, *value);
        }
        return clicked;
    }

    bool InputField(const char* id, char* buffer, std::size_t buffer_size,
                    const char* hint, ImGuiInputTextFlags flags, float width,
                    bool default_focus)
    {
        if (!buffer || buffer_size == 0)
            return false;
        ImGui::PushID(id ? id : "input");
        ImGui::SetNextItemWidth(width);
        const bool changed = ImGui::InputTextWithHint(
            "##field", hint ? hint : "", buffer, buffer_size, flags);
        if (default_focus && !ImGui::IsAnyItemActive())
            ImGui::SetItemDefaultFocus();
        DrawFocusRing();
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        ImGui::PopID();
        return changed;
    }

    bool PasswordField(const char* id, char* buffer, std::size_t buffer_size,
                       bool* reveal, const char* hint, ImGuiInputTextFlags flags,
                       float width, bool default_focus)
    {
        if (!reveal)
            return InputField(id, buffer, buffer_size, hint,
                              flags | ImGuiInputTextFlags_Password, width, default_focus);

        const float totalWidth = width < 0.0f ? ImGui::GetContentRegionAvail().x : width;
        const float eyeWidth = CyberTheme::Px(34.0f);
        const float gap = CyberTheme::Spacing::Xs;
        ImGui::PushID(id ? id : "password");
        ImGui::SetNextItemWidth(std::max(CyberTheme::Px(80.0f), totalWidth - eyeWidth - gap));
        const ImGuiInputTextFlags inputFlags = *reveal ? flags : (flags | ImGuiInputTextFlags_Password);
        const bool changed = ImGui::InputTextWithHint(
            "##field", hint ? hint : "", buffer, buffer_size, inputFlags);
        if (default_focus && !ImGui::IsAnyItemActive())
            ImGui::SetItemDefaultFocus();
        DrawFocusRing();
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

        ImGui::SameLine(0.0f, gap);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        const bool clicked = ImGui::InvisibleButton("##eye", ImVec2(eyeWidth, h));
        ApplyCursorForItem(true);
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        if (clicked)
            *reveal = !*reveal;
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 center(pos.x + eyeWidth * 0.5f, pos.y + h * 0.5f);
        const ImU32 color = CyberTheme::U32(hovered ? CyberTheme::Colors.GoldHover : CyberTheme::Colors.TextDisabled);
        const ImVec2 eyeLeft(center.x - CyberTheme::Px(8.0f), center.y);
        const ImVec2 eyeRight(center.x + CyberTheme::Px(8.0f), center.y);
        dl->AddBezierQuadratic(eyeLeft, ImVec2(center.x, center.y - CyberTheme::Px(7.0f)), eyeRight, color, CyberTheme::Px(1.2f), 12);
        dl->AddBezierQuadratic(eyeRight, ImVec2(center.x, center.y + CyberTheme::Px(7.0f)), eyeLeft, color, CyberTheme::Px(1.2f), 12);
        dl->AddCircleFilled(center, CyberTheme::Px(2.2f), color, 14);
        if (!*reveal) {
            dl->AddLine(ImVec2(center.x - CyberTheme::Px(8.0f), center.y + CyberTheme::Px(6.0f)),
                        ImVec2(center.x + CyberTheme::Px(8.0f), center.y - CyberTheme::Px(6.0f)),
                        color, CyberTheme::Px(1.3f));
        }
        ImGui::PopID();
        return changed;
    }

    bool TextInput(const char* label, char* buffer, std::size_t buffer_size,
                   const char* hint)
    {
        ImGui::PushID(label);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const float row_height = 36.0f;
        const float field_width = std::clamp(width * 0.62f, 120.0f, 330.0f);
        const ImVec2 field_pos(pos.x + width - field_width, pos.y + 1.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(pos,
            ImVec2(field_pos.x - 8.0f, pos.y + row_height), true);
        dl->AddText(ImVec2(pos.x, pos.y + 9.0f),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text), DisplayLabel(label));
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(field_pos);
        const bool changed = InputField("value", buffer, buffer_size, hint, 0, field_width, false);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + row_height));
        return changed;
    }

    bool InputInt(const char* label, int* value)
    {
        ImGui::PushID(label);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const float row_height = 34.0f;
        const float field_width = std::clamp(width * 0.62f, 120.0f, 330.0f);
        const ImVec2 field_pos(pos.x + width - field_width, pos.y + 2.0f);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRect(pos,
            ImVec2(field_pos.x - 8.0f, pos.y + row_height), true);
        dl->AddText(ImVec2(pos.x, pos.y + 9.0f),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text), DisplayLabel(label));
        dl->PopClipRect();

        ImGui::SetCursorScreenPos(field_pos);
        ImGui::SetNextItemWidth(field_width);
        const bool changed = ImGui::InputInt(
            "##value", value, 0, 0, ImGuiInputTextFlags_CharsDecimal);
        DrawFocusRing();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + row_height));
        return changed;
    }

    void TextLine(const char* text, TextTone tone)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImFont* font = CyberFonts::GetBodyFont();
        if (font)
            ImGui::GetWindowDrawList()->AddText(font, 13.0f, pos, ToneColor(tone), text ? text : "");
        else
            ImGui::GetWindowDrawList()->AddText(pos, ToneColor(tone), text ? text : "");
        ImGui::Dummy(ImVec2(0.0f, 19.0f));
    }

    void TextLineF(TextTone tone, const char* format, ...)
    {
        char buffer[256];
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        TextLine(buffer, tone);
    }

    void KeyValueRow(const char* label, const char* value)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(pos.x, pos.y + 7.0f),
            ToneColor(TextTone::Secondary), label ? label : "");

        ImFont* mono = CyberFonts::GetMonoFont();
        const ImVec2 value_size = mono
            ? mono->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, value ? value : "")
            : ImGui::CalcTextSize(value ? value : "");
        if (mono)
            dl->AddText(mono, 12.0f,
                ImVec2(pos.x + width - value_size.x, pos.y + 7.0f),
                ToneColor(TextTone::Accent), value ? value : "");
        else
            dl->AddText(ImVec2(pos.x + width - value_size.x, pos.y + 7.0f),
                ToneColor(TextTone::Accent), value ? value : "");
        ImGui::Dummy(ImVec2(width, CyberTheme::Metrics::RowHeight));
    }

    void HealthRow(const char* label, const char* value, HealthStatus status)
    {
        const TextTone tone = status == HealthStatus::Ok ? TextTone::Success
            : (status == HealthStatus::Warning ? TextTone::Warning : TextTone::Error);
        const char* mark = status == HealthStatus::Ok ? "✓" : (status == HealthStatus::Warning ? "!" : "×");
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(pos.x, pos.y + 7.0f), ToneColor(tone), mark);
        dl->AddText(ImVec2(pos.x + 22.0f, pos.y + 7.0f), ToneColor(TextTone::Secondary), label ? label : "");
        const char* safeValue = value ? value : "—";
        const ImVec2 valueSize = ImGui::CalcTextSize(safeValue);
        dl->AddText(ImVec2((std::max)(pos.x + 160.0f, pos.x + width - valueSize.x), pos.y + 7.0f),
                    ToneColor(tone), safeValue);
        ImGui::Dummy(ImVec2(width, 29.0f));
    }

    void StatusBadge(const char* label, bool online)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        constexpr float badge_width = 72.0f;
        constexpr float badge_height = 22.0f;
        const ImVec2 badge_a(
            pos.x + width - badge_width,
            pos.y + (CyberTheme::Metrics::RowHeight - badge_height) * 0.5f);
        const ImVec2 badge_b(badge_a.x + badge_width, badge_a.y + badge_height);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(ImVec2(pos.x, pos.y + 7.0f),
            ToneColor(TextTone::Secondary), label ? label : "");

        const ImVec4& state_color = online
            ? CyberTheme::Colors.Success
            : CyberTheme::Colors.Error;
        dl->AddRectFilled(badge_a, badge_b, WithAlpha(state_color, 0.12f), 11.0f);
        dl->AddRect(badge_a, badge_b, WithAlpha(state_color, 0.28f), 11.0f);
        dl->AddCircleFilled(ImVec2(badge_a.x + 11.0f, badge_a.y + 11.0f),
            2.5f, CyberTheme::U32(state_color), 10);
        dl->AddText(ImVec2(badge_a.x + 19.0f, badge_a.y + 4.0f),
            CyberTheme::U32(state_color), online ? Loc::Tr("footer.connected") : Loc::Tr("footer.offline"));
        ImGui::Dummy(ImVec2(width, CyberTheme::Metrics::RowHeight));
    }

    void Badge(const char* label, TextTone tone)
    {
        const char* text = label ? label : "";
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        const float height = CyberTheme::Px(22.0f);
        const float padX = CyberTheme::Px(9.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 end(pos.x + textSize.x + padX * 2.0f, pos.y + height);
        const ImU32 color = ToneColor(tone);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, end, (color & 0x00FFFFFFu) | (32u << 24), CyberTheme::Radius::Sm);
        dl->AddRect(pos, end, (color & 0x00FFFFFFu) | (86u << 24), CyberTheme::Radius::Sm, 0, 1.0f);
        dl->AddText(ImVec2(pos.x + padX, pos.y + (height - textSize.y) * 0.5f), color, text);
        ImGui::Dummy(ImVec2(end.x - pos.x, height));
    }

    void HelpMarker(const char* description)
    {
        ImGui::PushID(description ? description : "help");
        ImGui::SameLine(0.0f, CyberTheme::Spacing::Xs);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float size = CyberTheme::Px(17.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool hovered = ImGui::IsMouseHoveringRect(pos, ImVec2(pos.x + size, pos.y + size));
        dl->AddCircleFilled(ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * 0.45f,
            CyberTheme::WithAlpha(hovered ? CyberTheme::Colors.Gold : CyberTheme::Colors.TextDisabled,
                                  hovered ? 0.18f : 0.09f), 18);
        dl->AddCircle(ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f), size * 0.45f,
            CyberTheme::WithAlpha(hovered ? CyberTheme::Colors.GoldHover : CyberTheme::Colors.Border,
                                  hovered ? 0.72f : 0.52f), 18, 1.0f);
        const char* mark = "?";
        const ImVec2 markSize = ImGui::CalcTextSize(mark);
        dl->AddText(ImVec2(pos.x + (size - markSize.x) * 0.5f,
                           pos.y + (size - markSize.y) * 0.5f - CyberTheme::Px(1.0f)),
                    hovered ? CyberTheme::U32(CyberTheme::Colors.GoldHover)
                            : CyberTheme::U32(CyberTheme::Colors.TextDisabled), mark);
        ImGui::InvisibleButton("##help_marker", ImVec2(size, size));
        ApplyCursorForItem(true);
        if (ImGui::IsItemHovered() && description && *description) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(CyberTheme::Px(360.0f));
            ImGui::TextUnformatted(description);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }

    void InlineMessage(const char* message, TextTone tone, const char* error_id)
    {
        // Compact muted helper text — no large tinted boxes (esp_disabled / aim_disabled).
        (void)error_id;
        const float width = WidgetWidth();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const char* text = message ? message : "";
        const ImU32 accent = (tone == TextTone::Secondary)
            ? CyberTheme::U32(CyberTheme::Colors.TextDisabled)
            : ToneColor(tone);
        const float wrapWidth = std::max(CyberTheme::Px(80.0f), width - CyberTheme::Px(8.0f));
        const ImVec2 textSize = ImGui::CalcTextSize(text, nullptr, false, wrapWidth);
        const float height = textSize.y + CyberTheme::Px(4.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddText(nullptr, 0.0f, ImVec2(pos.x + CyberTheme::Px(2.0f), pos.y + CyberTheme::Px(1.0f)),
                    accent, text, nullptr, wrapWidth);
        ImGui::Dummy(ImVec2(width, height));
    }

    bool EmptyState(const char* title, const char* description, const char* action_label)
    {
        const float width = WidgetWidth();
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const float height = CyberTheme::Px(action_label && *action_label ? 56.0f : 36.0f);
        const ImVec2 end(start.x + width, start.y + height);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(start, end, CyberTheme::U32(CyberTheme::Colors.Surface), CyberTheme::Radius::Md);
        dl->AddRect(start, end, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.42f), CyberTheme::Radius::Md);
        const ImVec2 center(start.x + width * 0.5f, start.y + CyberTheme::Px(31.0f));
        dl->AddCircle(center, CyberTheme::Px(12.0f), CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.55f), 24, 1.2f);
        dl->AddLine(ImVec2(center.x - CyberTheme::Px(5.0f), center.y),
                    ImVec2(center.x + CyberTheme::Px(5.0f), center.y),
                    CyberTheme::U32(CyberTheme::Colors.Gold), 1.2f);

        const char* heading = title ? title : "Sem conteúdo";
        const char* detail = description ? description : "";
        ImVec2 headingSize = ImGui::CalcTextSize(heading);
        dl->AddText(ImVec2(start.x + (width - headingSize.x) * 0.5f, start.y + CyberTheme::Px(54.0f)),
                    CyberTheme::U32(CyberTheme::Colors.Text), heading);
        ImVec2 detailSize = ImGui::CalcTextSize(detail);
        dl->AddText(ImVec2(start.x + (width - detailSize.x) * 0.5f, start.y + CyberTheme::Px(78.0f)),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), detail);

        bool clicked = false;
        if (action_label && *action_label) {
            const ImVec2 buttonSize(CyberTheme::Px(150.0f), CyberTheme::Px(32.0f));
            ImGui::SetCursorScreenPos(ImVec2(start.x + (width - buttonSize.x) * 0.5f,
                                             start.y + CyberTheme::Px(108.0f)));
            clicked = CyberButton(action_label, buttonSize);
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, end.y + CyberTheme::Spacing::Sm));
        ImGui::Dummy(ImVec2(width, 1.0f));
        return clicked;
    }

    void SkeletonLine(float width, float height)
    {
        if (width <= 0.0f) width = WidgetWidth();
        height = std::max(CyberTheme::Px(8.0f), height);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 end(pos.x + width, pos.y + height);
        const float motion = app_settings::AnimationScale();
        const float t = motion > 0.0f ? static_cast<float>(ImGui::GetTime()) * (0.8f + motion * 0.7f) : 0.0f;
        const float pulse = motion > 0.0f ? 0.5f + 0.5f * std::sin(t) : 0.35f;
        const ImVec4 base = CyberTheme::Mix(CyberTheme::Colors.Surface, CyberTheme::Colors.Card, 0.58f);
        const ImVec4 hi = CyberTheme::Mix(base, CyberTheme::Colors.TextDisabled, 0.07f + pulse * 0.05f);
        ImGui::GetWindowDrawList()->AddRectFilled(pos, end, CyberTheme::U32(hi), CyberTheme::Radius::Sm);
        ImGui::Dummy(ImVec2(width, height));
    }

    void BeginSurfaceList(const char* id, float height)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const ImVec2 end(pos.x + width, pos.y + height);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, end,
            CyberTheme::U32(CyberTheme::Mix(
                CyberTheme::Colors.Background, CyberTheme::Colors.Panel, 0.55f)),
            CyberTheme::Metrics::ControlRounding);
        dl->AddRect(pos, end, WithAlpha(CyberTheme::Colors.Border, 0.36f),
            CyberTheme::Metrics::ControlRounding, 0, 1.0f);

        g_surface_lists.push_back({ pos, width, height });
        ImGui::PushID(id);
        ImGui::SetCursorScreenPos(ImVec2(pos.x + 5.0f, pos.y + 5.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(7.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(5.0f, 2.0f));
        ImGui::BeginChild("##surface_list",
            ImVec2(width - 10.0f, height - 10.0f), false,
            ImGuiWindowFlags_NoBackground);
    }

    void EndSurfaceList()
    {
        if (g_surface_lists.empty())
            return;

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopID();

        const SurfaceListState state = g_surface_lists.back();
        g_surface_lists.pop_back();
        ImGui::SetCursorScreenPos(
            ImVec2(state.position.x, state.position.y + state.height));
    }

    static char g_search[64] = {};

    bool SearchBar(const char* hint)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const float height = CyberTheme::Metrics::SearchHeight;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
            CyberTheme::U32(Mix(
                CyberTheme::Colors.Background,
                CyberTheme::Colors.Panel, 0.62f)),
            CyberTheme::Metrics::ControlRounding);
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height),
            WithAlpha(CyberTheme::Colors.Border, 0.42f),
            CyberTheme::Metrics::ControlRounding, 0, 1.0f);

        const ImVec2 center(pos.x + 16.0f, pos.y + height * 0.5f);
        dl->AddCircle(center, 6.0f, IM_COL32(120, 124, 140, 255), 18, 1.6f);
        dl->AddLine(ImVec2(center.x + 4.2f, center.y + 4.2f),
            ImVec2(center.x + 8.5f, center.y + 8.5f),
            IM_COL32(120, 124, 140, 255), 1.6f);

        ImGui::SetCursorScreenPos(ImVec2(pos.x + 30.0f, pos.y + (height - ImGui::GetFrameHeight()) * 0.5f));
        ImGui::SetNextItemWidth(width - 40.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
        const bool changed = ImGui::InputTextWithHint(
            "##search", hint, g_search, sizeof(g_search));
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        ImGui::PopStyleColor(3);
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + height + 1.0f));
        return changed;
    }

    bool PassSearch(const char* label)
    {
        if (!g_search[0])
            return true;
        std::string source(label ? label : "");
        std::string query(g_search);
        std::transform(source.begin(), source.end(), source.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(query.begin(), query.end(), query.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return source.find(query) != std::string::npos;
    }

    void DrawSpinner(ImDrawList* draw, ImVec2 center, float radius, float thickness, TextTone tone)
    {
        if (!draw) return;
        const float start = static_cast<float>(ImGui::GetTime()) * 5.6f;
        draw->PathClear();
        draw->PathArcTo(center, radius, start, start + 4.65f, 28);
        draw->PathStroke(ToneColor(tone), 0, thickness);
    }

    void Spinner(const char* id, float radius, float thickness, TextTone tone)
    {
        ImGui::PushID(id ? id : "spinner");
        const float diameter = radius * 2.0f + thickness * 2.0f;
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 center(pos.x + diameter * 0.5f, pos.y + diameter * 0.5f);
        DrawSpinner(ImGui::GetWindowDrawList(), center, radius, thickness, tone);
        ImGui::Dummy(ImVec2(diameter, diameter));
        ImGui::PopID();
    }

    struct Toast {
        char msg[192]{};
        char action[64]{};
        float life{};
        float maximum{};
        ToastType type{ToastType::Info};
        ToastActionCallback callback{};
    };
    static std::vector<Toast> g_toasts;

    static ImVec4 ToastColor(ToastType type)
    {
        switch (type) {
        case ToastType::Success: return CyberTheme::Colors.Success;
        case ToastType::Error: return CyberTheme::Colors.Error;
        case ToastType::Warning: return CyberTheme::Colors.Warning;
        case ToastType::Info:
        default: return CyberTheme::Colors.Info;
        }
    }

    void Notify(const char* msg, ToastType type)
    {
        NotifyAction(msg, type, nullptr, nullptr);
    }

    void NotifyAction(const char* msg, ToastType type, const char* action_label,
                      ToastActionCallback callback)
    {
        Toast toast{};
        std::snprintf(toast.msg, sizeof(toast.msg), "%s", msg ? msg : "");
        if (action_label)
            std::snprintf(toast.action, sizeof(toast.action), "%s", action_label);
        toast.maximum = callback && toast.action[0] ? 4.6f : 3.2f;
        toast.life = toast.maximum;
        toast.type = type;
        toast.callback = callback;
        g_toasts.push_back(toast);
        if (g_toasts.size() > 5)
            g_toasts.erase(g_toasts.begin());
    }

    void CopyToClipboard(const char* text, const char* feedback)
    {
        if (!text)
            return;
        ImGui::SetClipboardText(text);
        Notify(feedback && *feedback ? feedback : "Copiado", ToastType::Info);
    }

    void DrawToasts()
    {
        if (g_toasts.empty())
            return;

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float delta = std::min(ImGui::GetIO().DeltaTime, 0.05f);
        float y = display.y - CyberTheme::Px(20.0f);

        for (int index = static_cast<int>(g_toasts.size()) - 1; index >= 0; --index) {
            Toast& toast = g_toasts[static_cast<std::size_t>(index)];
            toast.life -= delta;
            if (toast.life <= 0.0f) {
                g_toasts.erase(g_toasts.begin() + index);
                continue;
            }

            const bool hasAction = toast.callback && toast.action[0];
            const float width = std::min(CyberTheme::Px(430.0f),
                std::max(CyberTheme::Px(290.0f), ImGui::CalcTextSize(toast.msg).x + CyberTheme::Px(hasAction ? 145.0f : 54.0f)));
            const float height = CyberTheme::Px(50.0f);
            const float alphaIn = std::clamp((toast.maximum - toast.life) / 0.18f, 0.0f, 1.0f);
            const float alphaOut = std::clamp(toast.life / 0.28f, 0.0f, 1.0f);
            const float alpha = std::min(alphaIn, alphaOut);
            const float slide = app_settings::MotionEnabled() ? (1.0f - alpha) * CyberTheme::Px(14.0f) : 0.0f;
            const ImVec2 pos(display.x - width - CyberTheme::Px(20.0f) + slide, y - height);

            char windowId[48]{};
            std::snprintf(windowId, sizeof(windowId), "##omnighost_toast_%d", index);
            ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ImGui::Begin(windowId, nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoFocusOnAppearing);

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
                toast.life += delta;

            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 a = ImGui::GetWindowPos();
            const ImVec2 b(a.x + width, a.y + height);
            const ImVec4 semantic = ToastColor(toast.type);
            dl->AddRectFilled(ImVec2(a.x + 1.0f, a.y + CyberTheme::Px(4.0f)),
                ImVec2(b.x + 1.0f, b.y + CyberTheme::Px(4.0f)),
                CyberTheme::SafeShadowU32(42), CyberTheme::Radius::Sm);
            dl->AddRectFilled(a, b, CyberTheme::U32(CyberTheme::Colors.Surface), CyberTheme::Radius::Sm);
            dl->AddRect(a, b, WithAlpha(CyberTheme::Colors.Border, 0.54f), CyberTheme::Radius::Sm);
            dl->AddRectFilled(a, ImVec2(a.x + CyberTheme::Px(3.0f), b.y),
                WithAlpha(semantic, 0.92f), CyberTheme::Radius::Sm);
            const float actionWidth = hasAction ? CyberTheme::Px(104.0f) : 0.0f;
            dl->AddText(nullptr, 0.0f,
                ImVec2(a.x + CyberTheme::Px(15.0f), a.y + CyberTheme::Px(16.0f)),
                CyberTheme::U32(CyberTheme::Colors.Text), toast.msg, nullptr,
                width - CyberTheme::Px(30.0f) - actionWidth);

            if (hasAction) {
                ImGui::SetCursorScreenPos(ImVec2(b.x - actionWidth - CyberTheme::Px(10.0f), a.y + CyberTheme::Px(8.0f)));
                ImGui::PushID(index);
                if (Button(toast.action, ButtonStyle::Ghost,
                           ImVec2(actionWidth, CyberTheme::Px(34.0f)), true)) {
                    ToastActionCallback callback = toast.callback;
                    toast.life = 0.0f;
                    if (callback)
                        callback();
                }
                ImGui::PopID();
            }

            ImGui::End();
            ImGui::PopStyleVar(2);
            y -= height + CyberTheme::Spacing::Sm;
        }
    }

    void OpenModal(const char* id)
    {
        if (id && *id)
            ImGui::OpenPopup(id);
    }

    bool BeginModal(const char* id, const char* title, float width)
    {
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(CyberTheme::Px(std::max(320.0f, width)), 0.0f),
            ImVec2(CyberTheme::Px(std::max(320.0f, width)), ImGui::GetIO().DisplaySize.y * 0.85f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
            ImVec2(CyberTheme::Spacing::Xl, CyberTheme::Spacing::Lg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, CyberTheme::Colors.Surface);
        ImGui::PushStyleColor(ImGuiCol_Border, WithAlpha(CyberTheme::Colors.Border, 0.72f));
        const bool opened = ImGui::BeginPopupModal(id, nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);
        if (!opened) {
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            return false;
        }
        if (title && *title) {
            ImGui::TextColored(CyberTheme::Colors.Text, "%s", title);
            ImGui::Dummy(ImVec2(0.0f, CyberTheme::Spacing::Xs));
            Separator();
        }
        return true;
    }

    void EndModal()
    {
        ImGui::EndPopup();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    ModalResult ConfirmModal(const char* id, const char* title, const char* message,
                             const char* confirm_label, const char* cancel_label,
                             ButtonStyle confirm_style, float width)
    {
        if (!BeginModal(id, title, width))
            return ModalResult::None;

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
        ImGui::TextColored(CyberTheme::Colors.TextDisabled, "%s", message ? message : "");
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, CyberTheme::Spacing::Md));

        ModalResult result = ModalResult::None;
        const float buttonWidth = CyberTheme::Px(128.0f);
        const bool cancel = Button(cancel_label && *cancel_label ? cancel_label : "Cancelar",
                                   ButtonStyle::Secondary, ImVec2(buttonWidth, CyberTheme::Px(36.0f)), true);
        ImGui::SameLine();
        const bool confirm = Button(confirm_label && *confirm_label ? confirm_label : "Confirmar",
                                    confirm_style, ImVec2(buttonWidth, CyberTheme::Px(36.0f)), true);
        const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !ImGui::IsAnyItemActive();
        if (cancel || escape) {
            result = ModalResult::Cancelled;
            ImGui::CloseCurrentPopup();
        } else if (confirm || enter) {
            result = ModalResult::Confirmed;
            ImGui::CloseCurrentPopup();
        }
        EndModal();
        return result;
    }

    static ThemeId g_theme = ThemeId::Cyber;

    void SetTheme(ThemeId id)
    {
        g_theme = id;
        auto& colors = CyberTheme::Colors;

        // Theme changes only affect accents and text; dark surfaces remain pure black.
        colors.Text = ImVec4(0.925f, 0.933f, 0.956f, 1.0f);
        colors.TextDisabled = ImVec4(0.475f, 0.500f, 0.565f, 1.0f);

        switch (id) {
        case ThemeId::Dark:
            colors.Gold = ImVec4(0.70f, 0.72f, 0.77f, 1.0f);
            break;
        case ThemeId::Cyber:
            colors.Gold = ImVec4(0.851f, 0.714f, 0.361f, 1.0f);
            break;
        case ThemeId::Purple:
            colors.Gold = ImVec4(0.666f, 0.455f, 0.965f, 1.0f);
            break;
        case ThemeId::Gold:
            colors.Gold = ImVec4(0.941f, 0.741f, 0.247f, 1.0f);
            break;
        case ThemeId::Matrix:
            colors.Gold = ImVec4(0.235f, 0.875f, 0.467f, 1.0f);
            break;
        case ThemeId::Red:
            colors.Gold = ImVec4(0.941f, 0.349f, 0.365f, 1.0f);
            break;
        case ThemeId::Blue:
            colors.Gold = ImVec4(0.318f, 0.616f, 0.961f, 1.0f);
            break;
        case ThemeId::White:
            colors.Gold = ImVec4(0.88f, 0.89f, 0.93f, 1.0f);
            break;
        }

        colors.Border = Mix(colors.Background, ImVec4(1, 1, 1, 1), 0.18f);
        colors.Border.w = 0.52f;
        colors.GoldHover = Mix(colors.Gold, ImVec4(1, 1, 1, 1), 0.22f);
        colors.GoldGlow = colors.Gold;
        colors.GoldGlow.w = 0.28f;
        colors.Success = ImVec4(0.286f, 0.820f, 0.537f, 1.0f);
        colors.Warning = ImVec4(0.945f, 0.671f, 0.275f, 1.0f);
        colors.Error = ImVec4(0.941f, 0.349f, 0.365f, 1.0f);
        colors.Info = ImVec4(0.353f, 0.671f, 0.965f, 1.0f);
        CyberTheme::ApplyTheme();
    }

    bool ThemeCombo(const char* label)
    {
        int current = static_cast<int>(g_theme);
        const char* names[] = {
            "Dark", "Cyber", "Purple", "Gold",
            "Matrix", "Red", "Blue", "White"
        };
        if (Combo(label, &current, names, 8)) {
            SetTheme(static_cast<ThemeId>(current));
            Notify("Theme applied", ToastType::Success);
            return true;
        }
        return false;
    }

    void ColorEditU32(const char* label, ImU32* color)
    {
        if (!color)
            return;
        if (!PassSearch(label))
            return;

        ImGui::PushID(static_cast<const void*>(color));
        ImVec4 value = ImGui::ColorConvertU32ToFloat4(*color);

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = WidgetWidth();
        const float row_height = 30.0f;

        // Fixed zones: [label flexible] [HEX fixed] [swatch fixed]
        const float swatch_w = 42.0f;
        const float gap = 8.0f;
        char hex[16];
        const int red = static_cast<int>(value.x * 255.0f + 0.5f);
        const int green = static_cast<int>(value.y * 255.0f + 0.5f);
        const int blue = static_cast<int>(value.z * 255.0f + 0.5f);
        const int alpha = static_cast<int>(value.w * 255.0f + 0.5f);
        snprintf(hex, sizeof(hex), "#%02X%02X%02X", red, green, blue);

        ImFont* font = ImGui::GetFont();
        const float base_size = ImGui::GetFontSize();
        const ImVec2 hex_size = font->CalcTextSizeA(base_size * 0.92f, FLT_MAX, 0.0f, hex);
        const float hex_w = hex_size.x;
        const float label_max = std::max(40.0f, width - swatch_w - hex_w - gap * 2.0f);

        const bool color_clicked = ImGui::InvisibleButton("##color_row", ImVec2(width, row_height));
        const bool hovered = ImGui::IsItemHovered();
        ApplyCursorForItem(true);
        DrawFocusRing(CyberTheme::Metrics::ControlRounding);
        if (color_clicked)
            ImGui::OpenPopup("##color_picker_popup");

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Label — scale down slightly only if needed so the full text stays visible
        const char* color_display = DisplayLabel(label);
        ImVec2 label_size = font->CalcTextSizeA(base_size, FLT_MAX, 0.0f, color_display);
        float label_size_px = base_size;
        if (label_size.x > label_max && label_size.x > 1.0f) {
            label_size_px = base_size * (label_max / label_size.x);
            if (label_size_px < base_size * 0.78f)
                label_size_px = base_size * 0.78f;
            label_size = font->CalcTextSizeA(label_size_px, FLT_MAX, 0.0f, color_display);
        }
        const float label_y = pos.y + (row_height - label_size.y) * 0.5f;
        dl->AddText(font, label_size_px, ImVec2(pos.x, label_y),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text), DisplayLabel(label));

        // Full label always available on hover (fallback)
        if (hovered)
            ImGui::SetTooltip("%s  %s", DisplayLabel(label), hex);

        // HEX
        const float hex_x = pos.x + width - swatch_w - gap - hex_w;
        const float hex_y = pos.y + (row_height - hex_size.y) * 0.5f;
        dl->AddText(font, base_size * 0.92f, ImVec2(hex_x, hex_y),
            IM_COL32(140, 144, 158, 255), hex);

        // Swatch
        const ImVec2 swatch_a(pos.x + width - swatch_w, pos.y + 5.0f);
        const ImVec2 swatch_b(pos.x + width, pos.y + row_height - 5.0f);
        DrawCheckers(dl, swatch_a, swatch_b, 6.0f);
        dl->AddRectFilled(swatch_a, swatch_b,
            ImGui::ColorConvertFloat4ToU32(value), 5.0f);
        dl->AddRect(swatch_a, swatch_b,
            hovered ? WithAlpha(CyberTheme::Colors.Gold, 0.60f)
                    : IM_COL32(255, 255, 255, 25),
            5.0f, 0, 1.0f);

        ImGui::SetNextWindowSize(ImVec2(330.0f, 0.0f), ImGuiCond_Appearing);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 12.0f));
        if (ImGui::BeginPopup("##color_picker_popup",
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar)) {
            ImGui::TextColored(CyberTheme::Colors.Text, "%s", DisplayLabel(label));
            ImGui::TextColored(CyberTheme::Colors.TextDisabled,
                "HUE  /  SATURATION  /  RGB  /  ALPHA");
            ImGui::Dummy(ImVec2(0.0f, 3.0f));

            const ImVec2 preview_a = ImGui::GetCursorScreenPos();
            const ImVec2 preview_b(preview_a.x + 64.0f, preview_a.y + 44.0f);
            DrawCheckers(ImGui::GetWindowDrawList(), preview_a, preview_b, 7.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(preview_a, preview_b,
                ImGui::ColorConvertFloat4ToU32(value), 7.0f);
            ImGui::GetWindowDrawList()->AddRect(preview_a, preview_b,
                IM_COL32(255, 255, 255, 28), 7.0f);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(preview_b.x + 12.0f, preview_a.y + 4.0f),
                ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Gold), hex);
            char rgba[64];
            snprintf(rgba, sizeof(rgba), "R %d   G %d   B %d   A %d",
                red, green, blue, alpha);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2(preview_b.x + 12.0f, preview_a.y + 23.0f),
                ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.TextDisabled), rgba);
            ImGui::Dummy(ImVec2(0.0f, 50.0f));

            ImGui::TextColored(CyberTheme::Colors.TextDisabled,
                "SATURATION / VALUE");
            ImGui::SetNextItemWidth(306.0f);
            const ImGuiColorEditFlags flags =
                ImGuiColorEditFlags_NoLabel |
                ImGuiColorEditFlags_NoSidePreview |
                ImGuiColorEditFlags_NoSmallPreview |
                ImGuiColorEditFlags_AlphaBar |
                ImGuiColorEditFlags_AlphaPreviewHalf |
                ImGuiColorEditFlags_DisplayRGB |
                ImGuiColorEditFlags_PickerHueBar |
                ImGuiColorEditFlags_InputRGB;
            if (ImGui::ColorPicker4("##photoshop_picker", &value.x, flags))
                *color = ImGui::ColorConvertFloat4ToU32(value);

            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
    }

} // namespace CyberWidgets

namespace CyberWidgets {

bool DrawVisualState(
    VisualState state,
    const char* message,
    const char* actionLabel,
    bool (*actionCallback)(),
    float width)
{
    const float w = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = 80.0f;
    const ImVec2 size(w, height);
    
    ImU32 bgColor, borderColor, textColor, accentColor;
    const char* icon = "";
    bool showAction = false;
    
    switch (state) {
        case VisualState::Loading:
            bgColor = IM_COL32(15, 15, 20, 230);
            borderColor = IM_COL32(80, 80, 100, 180);
            textColor = IM_COL32(200, 200, 220, 255);
            accentColor = U32(CyberTheme::Colors.Gold);
            icon = "⏳";
            break;
        case VisualState::Unavailable:
            bgColor = IM_COL32(20, 18, 18, 230);
            borderColor = IM_COL32(100, 60, 60, 180);
            textColor = IM_COL32(220, 180, 180, 255);
            accentColor = IM_COL32(220, 80, 80, 255);
            icon = "⚠";
            showAction = true;
            break;
        case VisualState::Error:
            bgColor = IM_COL32(25, 15, 15, 230);
            borderColor = IM_COL32(150, 50, 50, 180);
            textColor = IM_COL32(255, 150, 150, 255);
            accentColor = IM_COL32(255, 80, 80, 255);
            icon = "✕";
            showAction = true;
            break;
        case VisualState::Empty:
            bgColor = IM_COL32(15, 18, 22, 230);
            borderColor = IM_COL32(60, 70, 90, 180);
            textColor = IM_COL32(160, 170, 190, 255);
            accentColor = IM_COL32(100, 150, 220, 255);
            icon = "○";
            showAction = actionLabel != nullptr;
            break;
        case VisualState::Retry:
            bgColor = IM_COL32(25, 20, 15, 230);
            borderColor = IM_COL32(120, 90, 50, 180);
            textColor = IM_COL32(230, 200, 140, 255);
            accentColor = IM_COL32(255, 200, 80, 255);
            icon = "↻";
            showAction = true;
            break;
        default:
            bgColor = IM_COL32(15, 15, 20, 230);
            borderColor = IM_COL32(60, 60, 80, 180);
            textColor = IM_COL32(180, 180, 200, 255);
            accentColor = U32(CyberTheme::Colors.Gold);
            break;
    }
    
    // Background
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bgColor, CyberTheme::Metrics::ControlRounding);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), borderColor, CyberTheme::Metrics::ControlRounding, 0, CyberTheme::Px(1.0f));
    
    // Icon
    const float iconSize = 24.0f;
    const ImVec2 iconPos(pos.x + (w - iconSize) * 0.5f, pos.y + 10.0f);
    ImFont* font = CyberFonts::GetMonoFont();
    if (font) {
        dl->AddText(font, iconSize, iconPos, accentColor, icon);
    } else {
        dl->AddText(iconPos, accentColor, icon);
    }
    
    // Message
    if (message) {
        ImFont* msgFont = ImGui::GetFont();
        const float textWidth = w - 32.0f;
        const ImVec2 textSize = msgFont->CalcTextSizeA(14.0f, textWidth, 0.0f, message);
        const ImVec2 textPos(pos.x + (w - textSize.x) * 0.5f, pos.y + 38.0f);
        dl->AddText(msgFont, 14.0f, textPos, textColor, message);
    }
    
    // Action button
    [[maybe_unused]] bool clicked = false;
    if (showAction && actionLabel && actionCallback) {
        const float btnWidth = 140.0f;
        const float btnHeight = 30.0f;
        const ImVec2 btnPos(pos.x + (w - btnWidth) * 0.5f, pos.y + height - btnHeight - 10.0f);
        
        const bool hovered = ImGui::IsMouseHoveringRect(btnPos, ImVec2(btnPos.x + btnWidth, btnPos.y + btnHeight));
        clicked = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        
        const ImU32 btnBg = hovered ? 
            IM_COL32(accentColor & 0xFF, (accentColor >> 8) & 0xFF, (accentColor >> 16) & 0xFF, 200) :
            IM_COL32(accentColor & 0xFF, (accentColor >> 8) & 0xFF, (accentColor >> 16) & 0xFF, 150);
        const ImU32 btnBorder = IM_COL32(255, 255, 255, hovered ? 180 : 100);
        
        dl->AddRectFilled(btnPos, ImVec2(btnPos.x + btnWidth, btnPos.y + btnHeight), btnBg, 6.0f);
        dl->AddRect(btnPos, ImVec2(btnPos.x + btnWidth, btnPos.y + btnHeight), btnBorder, 6.0f, 0, 1.5f);
        
        ImFont* btnFont = ImGui::GetFont();
        const ImVec2 btnTextSize = btnFont->CalcTextSizeA(13.0f, btnWidth - 16.0f, 0.0f, actionLabel);
        dl->AddText(btnFont, 13.0f, 
            ImVec2(btnPos.x + (btnWidth - btnTextSize.x) * 0.5f, btnPos.y + (btnHeight - btnTextSize.y) * 0.5f),
            IM_COL32(0, 0, 0, 255), actionLabel);
        
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        
        if (clicked) {
            return actionCallback();
        }
    }
    
    ImGui::Dummy(size);
    return false;
}

bool DrawLoadingState(const char* message, float width) {
    return DrawVisualState(VisualState::Loading, message, nullptr, nullptr, width);
}

bool DrawUnavailableState(const char* message, const char* actionLabel, bool (*actionCallback)(), float width) {
    return DrawVisualState(VisualState::Unavailable, message, actionLabel, actionCallback, width);
}

bool DrawErrorState(const char* message, const char* actionLabel, bool (*actionCallback)(), float width) {
    return DrawVisualState(VisualState::Error, message, actionLabel, actionCallback, width);
}

bool DrawEmptyState(const char* title, const char* description, const char* actionLabel, bool (*actionCallback)(), float width) {
    const char* msg = description ? description : title;
    return DrawVisualState(VisualState::Empty, msg, actionLabel, actionCallback, width);
}

bool DrawRetryState(const char* message, const char* actionLabel, bool (*actionCallback)(), float width) {
    return DrawVisualState(VisualState::Retry, message, actionLabel, actionCallback, width);
}

void DrawHardwareStatusWidget() {
    HardwareMonitor::Initialize();
    HardwareMonitor::Update();

    const auto& metrics = HardwareMonitor::GetSystemMetrics();

    CyberWidgets::BeginCard("Hardware Status · Real-time");

    CyberWidgets::BeginCardRow(4);

    CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
    char fps_str[32];
    std::snprintf(fps_str, sizeof(fps_str), "%.0f FPS", metrics.fps);
    CyberWidgets::TextLine(fps_str, CyberWidgets::TextTone::Accent);
    CyberWidgets::TextLine("Frame Rate", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
    char frame_str[32];
    std::snprintf(frame_str, sizeof(frame_str), "%.2f ms", metrics.frame_time_ms);
    CyberWidgets::TextLine(frame_str, CyberWidgets::TextTone::Accent);
    CyberWidgets::TextLine("Frame Time", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
    char ent_str[32];
    std::snprintf(ent_str, sizeof(ent_str), "%d", metrics.entity_count);
    CyberWidgets::TextLine(ent_str, CyberWidgets::TextTone::Accent);
    CyberWidgets::TextLine("Entities", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
    char lat_str[32];
    std::snprintf(lat_str, sizeof(lat_str), "%.1f ms", metrics.dma_read_latency_ms);
    CyberWidgets::TextLine(lat_str, CyberWidgets::TextTone::Accent);
    CyberWidgets::TextLine("DMA Latency", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::EndCardRow();
    CyberWidgets::CardGap(12.0f);

    CyberWidgets::SectionTitle("Devices");
    CyberWidgets::BeginCardRow(2);

    for (int i = 0; i < static_cast<int>(HardwareMonitor::DeviceType::Count); ++i) {
        const auto dtype = static_cast<HardwareMonitor::DeviceType>(i);
        const auto& status = HardwareMonitor::GetDeviceStatus(dtype);
        if (!status.enabled && !status.connected) continue;

        CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
        CyberWidgets::TextLine(status.name.c_str(),
            status.connected ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Error);

        const std::string health = HardwareMonitor::GetDeviceHealthString(dtype);
        CyberWidgets::TextTone tone = CyberWidgets::TextTone::Success;
        if (health == "Disconnected" || health == "Stale") tone = CyberWidgets::TextTone::Error;
        else if (health == "High Latency" || health == "Elevated Latency") tone = CyberWidgets::TextTone::Warning;
        else if (health == "Disabled") tone = CyberWidgets::TextTone::Secondary;
        CyberWidgets::Badge(health.c_str(), tone);

        if (status.connected) {
            char lat[64];
            std::snprintf(lat, sizeof(lat), "Avg: %.1fms  Max: %.1fms",
                HardwareMonitor::GetAverageLatency(dtype, 5),
                HardwareMonitor::GetMaxLatency(dtype, 5));
            CyberWidgets::KeyValueRow("Latency", lat);
        }
        if (!status.port.empty()) {
            CyberWidgets::KeyValueRow("Port", status.port.c_str());
        }
        if (!status.version.empty()) {
            CyberWidgets::KeyValueRow("Version", status.version.c_str());
        }
        if (!status.last_error.empty()) {
            CyberWidgets::KeyValueRow("Error", status.last_error.c_str());
        }

        CyberWidgets::EndCard();

        if (i % 2 == 0 && i < static_cast<int>(HardwareMonitor::DeviceType::Count) - 1) {
            CyberWidgets::NextCardColumn();
        } else if (i % 2 == 1 && i < static_cast<int>(HardwareMonitor::DeviceType::Count) - 1) {
            CyberWidgets::EndCardRow();
            CyberWidgets::BeginCardRow(2);
        }
    }

    CyberWidgets::EndCardRow();
    CyberWidgets::CardGap(12.0f);

    CyberWidgets::SectionTitle("DMA Latency (last 60s)");
    const auto& latency_history = HardwareMonitor::GetLatencyHistory();
    if (!latency_history.empty()) {
        std::vector<float> lat_data;
        lat_data.reserve(latency_history.size());
        for (const auto& s : latency_history) lat_data.push_back(s.latency_ms);
        DrawMetricGraph("Latency", lat_data, ImGui::GetContentRegionAvail().x, 100.0f, CyberTheme::U32(CyberTheme::Colors.Gold));
    } else {
        CyberWidgets::TextLine("No latency data yet", CyberWidgets::TextTone::Secondary);
    }

    CyberWidgets::CardGap(8.0f);

    CyberWidgets::SectionTitle("Entity Count (last 60s)");
    const auto& entity_history = HardwareMonitor::GetEntityHistory();
    if (!entity_history.empty()) {
        DrawMetricGraphInt("Entities", entity_history, ImGui::GetContentRegionAvail().x, 100.0f, CyberTheme::U32(CyberTheme::Colors.Info));
    } else {
        CyberWidgets::TextLine("No entity data yet", CyberWidgets::TextTone::Secondary);
    }

    CyberWidgets::EndCard();
}

void DrawMetricGraph(const char* label, const std::vector<float>& data, float width, float height, ImU32 color) {
    if (data.empty() || width <= 0 || height <= 0) return;
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 end(pos.x + width, pos.y + height);
    
    // Background
    dl->AddRectFilled(pos, end, CyberTheme::U32(CyberTheme::Colors.Surface), CyberTheme::Radius::Sm);
    dl->AddRect(pos, end, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.5f), CyberTheme::Radius::Sm);
    
    // Grid lines
    for (int i = 1; i < 4; ++i) {
        float y = pos.y + height * i / 4.0f;
        dl->AddLine(ImVec2(pos.x, y), ImVec2(end.x, y), CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.15f));
    }
    
    // Find min/max for scaling
    float min_val = data[0], max_val = data[0];
    for (float v : data) {
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
    }
    float range = std::max(1.0f, max_val - min_val);
    
    // Draw line graph
    ImVec2 prev(pos.x, pos.y + height - (data[0] - min_val) / range * height);
    for (size_t i = 1; i < data.size(); ++i) {
        float x = pos.x + (width * i) / (data.size() - 1);
        float y = pos.y + height - (data[i] - min_val) / range * height;
        dl->AddLine(prev, ImVec2(x, y), color, 1.5f);
        prev = ImVec2(x, y);
    }
    
    // Fill area under curve
    if (data.size() > 1) {
        std::vector<ImVec2> poly;
        poly.reserve(data.size() + 2);
        poly.push_back(ImVec2(pos.x, end.y));
        for (size_t i = 0; i < data.size(); ++i) {
            float x = pos.x + (width * i) / (data.size() - 1);
            float y = pos.y + height - (data[i] - min_val) / range * height;
            poly.push_back(ImVec2(x, y));
        }
        poly.push_back(ImVec2(end.x, end.y));
        dl->AddConvexPolyFilled(poly.data(), static_cast<int>(poly.size()), color & 0x33FFFFFF);
    }
    
    // Current value label
    char val_str[32];
    std::snprintf(val_str, sizeof(val_str), "%.1f", data.back());
    ImVec2 text_size = ImGui::CalcTextSize(val_str);
    dl->AddText(ImVec2(end.x - text_size.x - 8, pos.y + 4), color, val_str);
    
    ImGui::Dummy(ImVec2(width, height + 4));
}

void DrawMetricGraphInt(const char* label, const std::vector<int>& data, float width, float height, ImU32 color) {
    if (data.empty() || width <= 0 || height <= 0) return;
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 end(pos.x + width, pos.y + height);
    
    // Background
    dl->AddRectFilled(pos, end, CyberTheme::U32(CyberTheme::Colors.Surface), CyberTheme::Radius::Sm);
    dl->AddRect(pos, end, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.5f), CyberTheme::Radius::Sm);
    
    // Grid lines
    for (int i = 1; i < 4; ++i) {
        float y = pos.y + height * i / 4.0f;
        dl->AddLine(ImVec2(pos.x, y), ImVec2(end.x, y), CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.15f));
    }
    
    // Find min/max for scaling
    int min_val = data[0], max_val = data[0];
    for (int v : data) {
        min_val = std::min(min_val, v);
        max_val = std::max(max_val, v);
    }
    float range = std::max(1.0f, float(max_val - min_val));
    
    // Draw line graph
    ImVec2 prev(pos.x, pos.y + height - (data[0] - min_val) / range * height);
    for (size_t i = 1; i < data.size(); ++i) {
        float x = pos.x + (width * i) / (data.size() - 1);
        float y = pos.y + height - (data[i] - min_val) / range * height;
        dl->AddLine(prev, ImVec2(x, y), color, 1.5f);
        prev = ImVec2(x, y);
    }
    
    // Fill area under curve
    if (data.size() > 1) {
        std::vector<ImVec2> poly;
        poly.reserve(data.size() + 2);
        poly.push_back(ImVec2(pos.x, end.y));
        for (size_t i = 0; i < data.size(); ++i) {
            float x = pos.x + (width * i) / (data.size() - 1);
            float y = pos.y + height - (data[i] - min_val) / range * height;
            poly.push_back(ImVec2(x, y));
        }
        poly.push_back(ImVec2(end.x, end.y));
        dl->AddConvexPolyFilled(poly.data(), static_cast<int>(poly.size()), color & 0x33FFFFFF);
    }
    
    // Current value label
    char val_str[32];
    std::snprintf(val_str, sizeof(val_str), "%d", data.back());
    ImVec2 text_size = ImGui::CalcTextSize(val_str);
    dl->AddText(ImVec2(end.x - text_size.x - 8, pos.y + 4), color, val_str);
    
    ImGui::Dummy(ImVec2(width, height + 4));
}

} // namespace CyberWidgets

// ProgressDialog implementation

void ProgressDialog::Open(const Config& config) noexcept {
    config_ = config;
    open_ = true;
    cancelled_ = false;
    progress_ = 0.0f;
    message_.clear();
    startTime_ = std::chrono::steady_clock::now();
    minDurationMet_ = false;
}

void ProgressDialog::SetProgress(float progress, const char* message) noexcept {
    if (!open_) return;
    progress_ = std::clamp(progress, 0.0f, 1.0f);
    if (message) message_ = message;
}

void ProgressDialog::Close() noexcept {
    open_ = false;
}

float ProgressDialog::GetElapsedSeconds() const noexcept {
    if (!open_) return 0.0f;
    return std::chrono::duration<float>(std::chrono::steady_clock::now() - startTime_).count();
}

// Helper function to draw a progress dialog (call from your render loop)
namespace {
bool DrawProgressDialogInternal(ProgressDialog& dialog) {
    if (!dialog.IsOpen()) return false;

    const auto& config = dialog.GetConfig();
    [[maybe_unused]] const float elapsed = dialog.GetElapsedSeconds();
    [[maybe_unused]] const float progress = dialog.IsCancelled() ? 0.0f : dialog.GetProgress();
    
    // Check minimum duration
    if (elapsed < config.minDuration) {
        return true;  // Keep dialog open
    }

    ImGui::OpenPopup(config.title);
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(config.width, 0), ImGuiCond_Appearing);

    [[maybe_unused]] ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | 
                            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar |
                            ImGuiWindowFlags_AlwaysAutoResize;

    bool open = true;
    if (ImGui::BeginPopupModal(config.title, &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Message
        if (config.message) {
            ImGui::TextWrapped("%s", config.message);
        }
        // Parentheses defeat the Windows GetMessage -> GetMessageW macro.
        if (!(dialog.GetMessage)().empty()) {
            ImGui::TextWrapped("%s", (dialog.GetMessage)().c_str());
        }

        ImGui::Spacing();

        // Progress bar
        if (config.showProgressBar) {
            ImGui::ProgressBar(dialog.IsCancelled() ? 0.0f : dialog.GetProgress(), ImVec2(-1, 0));
        }

        // Time elapsed
        if (config.showTimeElapsed) {
            const float elapsed2 = dialog.GetElapsedSeconds();
            int minutes = static_cast<int>(elapsed2 / 60.0f);
            int seconds = static_cast<int>(elapsed2) % 60;
            ImGui::Text("Tempo: %d:%02d", minutes, seconds);
        }

        ImGui::Spacing();

        // Cancel button
        if (config.cancellable) {
            if (ImGui::Button(config.cancelLabel, ImVec2(120, 0))) {
                const_cast<ProgressDialog&>(dialog).Cancel();
            }
        }

        ImGui::EndPopup();
    }

    if (!open) {
        const_cast<ProgressDialog&>(dialog).Close();
        return false;
    }

    return true;
}
} // anonymous namespace
