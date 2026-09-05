#pragma once
#include "../ImGui/imgui.h"

namespace CyberIcons {

    // Distinct OmniGhost-style icons (inspired by common menu glyphs, not copies)
    void DrawESPIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawWorldIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawAimIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawMouseIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawTriggerIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawWrenchIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawSettingsIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawSaveIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawUserIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawVehicleIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawStatusIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);
    void DrawRadarIcon(ImDrawList* draw_list, ImVec2 pos, float size, ImU32 color);

    // Alias kept for older call sites
    inline void DrawBuildIcon(ImDrawList* d, ImVec2 p, float s, ImU32 c) { DrawStatusIcon(d, p, s, c); }

} // namespace CyberIcons
