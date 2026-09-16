#pragma once

#include "../../ImGui/imgui.h"
#include <algorithm>
#include <cstdint>

namespace OmniGhost::Gameplay::EspCore {

enum class DataField : std::uint32_t {
    None       = 0,
    Position   = 1u << 0,
    Skeleton   = 1u << 1,
    Health     = 1u << 2,
    Armor      = 1u << 3,
    Name       = 1u << 4,
    Weapon     = 1u << 5,
    Visibility = 1u << 6,
    Velocity   = 1u << 7,
    Facing     = 1u << 8
};

constexpr DataField operator|(DataField a, DataField b) noexcept {
    return static_cast<DataField>(static_cast<std::uint32_t>(a) |
                                  static_cast<std::uint32_t>(b));
}

constexpr DataField& operator|=(DataField& a, DataField b) noexcept {
    a = a | b;
    return a;
}

constexpr bool Has(DataField fields, DataField value) noexcept {
    return (static_cast<std::uint32_t>(fields) & static_cast<std::uint32_t>(value)) != 0;
}

// Normalized feature declaration for existing and future game adapters. A game
// collector asks for RequiredFields() once per frame and reads only those fields.
struct FeatureSet {
    bool box = false;
    bool corner_box = false;
    bool skeleton = false;
    bool head = false;
    bool health = false;
    bool armor = false;
    bool snapline = false;
    bool name = false;
    bool weapon = false;
    bool distance = false;
    bool visibility = false;
    bool aim = false;
    bool prediction = false;
    bool trail = false;
    bool halo = false;
    bool look_direction = false;

    constexpr DataField RequiredFields() const noexcept {
        // Position is enough for box / HP / armor (origin + standing hull).
        // Skeleton is ONLY for features that truly need the joint buffer —
        // forcing bones for simple boxes caused massive DMA hitch spikes.
        DataField fields = DataField::None;
        if (box || corner_box || skeleton || head || health || armor || snapline ||
            distance || trail || halo || look_direction || aim || name || weapon)
            fields |= DataField::Position;
        if (skeleton || aim || halo || look_direction)
            fields |= DataField::Skeleton;
        if (health || aim) fields |= DataField::Health;
        if (armor) fields |= DataField::Armor;
        if (name) fields |= DataField::Name;
        if (weapon) fields |= DataField::Weapon;
        if (visibility) fields |= DataField::Visibility;
        if (prediction) fields |= DataField::Velocity;
        if (look_direction) fields |= DataField::Facing;
        return fields;
    }
};

inline void DrawCornerBox(ImDrawList* draw, ImVec2 min, ImVec2 max,
                          ImU32 color, float thickness = 1.5f) {
    if (!draw) return;
    const float boxWidth = (std::max)(0.f, max.x - min.x);
    const float boxHeight = (std::max)(0.f, max.y - min.y);
    const float x_len = boxWidth * 0.25f;
    const float y_len = boxHeight * 0.20f;

    draw->AddLine(min, ImVec2(min.x + x_len, min.y), color, thickness);
    draw->AddLine(min, ImVec2(min.x, min.y + y_len), color, thickness);
    draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x - x_len, min.y), color, thickness);
    draw->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + y_len), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + x_len, max.y), color, thickness);
    draw->AddLine(ImVec2(min.x, max.y), ImVec2(min.x, max.y - y_len), color, thickness);
    draw->AddLine(max, ImVec2(max.x - x_len, max.y), color, thickness);
    draw->AddLine(max, ImVec2(max.x, max.y - y_len), color, thickness);
}

inline ImU32 HealthColor(float fraction, int alpha = 255) {
    const float value = std::clamp(fraction, 0.f, 1.f);
    const float red = value < 0.5f ? 1.f : (1.f - value) * 2.f;
    const float green = value > 0.5f ? 1.f : value * 2.f;
    return ImGui::ColorConvertFloat4ToU32(ImVec4(red, green, 0.10f, alpha / 255.f));
}

// Constant draw-call bar: track + one GPU gradient + outline. Its cost does not
// grow with the height of the player box.
inline void DrawVerticalBar(ImDrawList* draw, ImVec2 min, ImVec2 max,
                            float fraction, ImU32 top_color, ImU32 bottom_color,
                            ImU32 track_color = IM_COL32(18, 18, 22, 225),
                            ImU32 outline_color = IM_COL32(0, 0, 0, 210)) {
    if (!draw || max.x <= min.x || max.y <= min.y) return;
    const float value = std::clamp(fraction, 0.f, 1.f);
    draw->AddRectFilled(ImVec2(min.x - 1.f, min.y - 1.f),
                        ImVec2(max.x + 1.f, max.y + 1.f), outline_color, 1.f);
    draw->AddRectFilled(min, max, track_color);
    const float fill_top = max.y - (max.y - min.y) * value;
    if (fill_top < max.y - 0.25f) {
        draw->AddRectFilledMultiColor(ImVec2(min.x, fill_top), max,
                                      top_color, top_color, bottom_color, bottom_color);
    }
}

inline void DrawHealthBar(ImDrawList* draw, ImVec2 min, ImVec2 max, float fraction) {
    const ImU32 top = HealthColor(fraction);
    const ImVec4 top_f = ImGui::ColorConvertU32ToFloat4(top);
    const ImU32 bottom = ImGui::ColorConvertFloat4ToU32(
        ImVec4(top_f.x * 0.52f, top_f.y * 0.52f, top_f.z * 0.52f, top_f.w));
    DrawVerticalBar(draw, min, max, fraction, top, bottom);
}

inline void DrawOutlinedText(ImDrawList* draw, ImVec2 position, ImU32 color,
                             const char* text) {
    if (!draw || !text || !*text) return;
    const ImU32 shadow = IM_COL32(0, 0, 0, 200);
    draw->AddText(ImVec2(position.x - 1.f, position.y), shadow, text);
    draw->AddText(ImVec2(position.x + 1.f, position.y), shadow, text);
    draw->AddText(ImVec2(position.x, position.y - 1.f), shadow, text);
    draw->AddText(ImVec2(position.x, position.y + 1.f), shadow, text);
    draw->AddText(position, color, text);
}

inline void DrawCenteredOutlinedText(ImDrawList* draw, float center_x, float y,
                                     ImU32 color, const char* text) {
    if (!text || !*text) return;
    const ImVec2 size = ImGui::CalcTextSize(text);
    DrawOutlinedText(draw, ImVec2(center_x - size.x * 0.5f, y), color, text);
}

} // namespace OmniGhost::Gameplay::EspCore
