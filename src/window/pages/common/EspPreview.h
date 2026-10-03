#pragma once

#include "imgui.h"
#include <array>
#include <algorithm>
#include <cmath>

// ============================================================
// UNIFIED ESP PREVIEW SYSTEM
// Shared across CS2, FiveM, Fortnite, Warzone, and future games
// ============================================================

namespace EspPreview {

// -----------------------------------------------------------------------------
// GEOMETRY: Single source of truth for all landmark positions
// -----------------------------------------------------------------------------

struct PlayerGeometry {
    // Player bounding box (full body: head to feet, arms width)
    ImVec2 boundsMin;
    ImVec2 boundsMax;
    ImVec2 boundsCenter;
    float boundsWidth;
    float boundsHeight;

    // Head landmarks
    ImVec2 headCenter;
    ImVec2 headTop;
    ImVec2 headBottom;
    float headRadius;

    // Spine landmarks
    ImVec2 neck;
    ImVec2 chest;
    ImVec2 pelvis;

    // Shoulders
    ImVec2 leftShoulder;
    ImVec2 rightShoulder;

    // Arms
    ImVec2 leftElbow;
    ImVec2 rightElbow;
    ImVec2 leftHand;
    ImVec2 rightHand;

    // Hips (leg connection points)
    ImVec2 leftHip;
    ImVec2 rightHip;

    // Legs
    ImVec2 leftKnee;
    ImVec2 rightKnee;
    ImVec2 leftFoot;
    ImVec2 rightFoot;

    // Feet center (for snaplines)
    ImVec2 feetCenter;

    // Text stacking origin (below bounds)
    float textStackY;
};

// Build geometry from preview panel dimensions
// All positions derived from normalized ratios - fully responsive
PlayerGeometry BuildGeometry(const ImVec2& origin, float width, float height);

// -----------------------------------------------------------------------------
// COLOR HELPERS
// -----------------------------------------------------------------------------

inline ImU32 ToImU32(const float c[4]) {
    return IM_COL32(
        static_cast<int>(c[0] * 255.f),
        static_cast<int>(c[1] * 255.f),
        static_cast<int>(c[2] * 255.f),
        static_cast<int>(c[3] * 255.f)
    );
}

inline ImU32 WithAlpha(ImU32 col, float alpha) {
    return IM_COL32(
        (col >> 0) & 0xFF,
        (col >> 8) & 0xFF,
        (col >> 16) & 0xFF,
        static_cast<int>(alpha * 255.f)
    );
}

// Dark outline colors (subtle)
static const ImU32 OUTLINE_DARK      = IM_COL32(0, 0, 0, 190);
static const ImU32 OUTLINE_DARK_STRONG = IM_COL32(0, 0, 0, 255);

// -----------------------------------------------------------------------------
// DRAWING UTILITIES: Outlined primitives for professional contrast
// -----------------------------------------------------------------------------

inline void DrawLineOutlined(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thick, float outlineExtra = 1.5f) {
    dl->AddLine(a, b, OUTLINE_DARK, thick + outlineExtra);
    dl->AddLine(a, b, col, thick);
}

inline void DrawRectOutlined(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col, float thick, float rounding = 0.f, float outlineExtra = 1.5f) {
    dl->AddRect(min, max, OUTLINE_DARK, rounding, 0, thick + outlineExtra);
    dl->AddRect(min, max, col, rounding, 0, thick);
}

inline void DrawCircleOutlined(ImDrawList* dl, ImVec2 center, float radius, ImU32 col, float thick, int segments = 32, float outlineExtra = 1.5f) {
    dl->AddCircle(center, radius, OUTLINE_DARK, segments, thick + outlineExtra);
    dl->AddCircle(center, radius, col, segments, thick);
}

inline void DrawCircleFilledOutlined(ImDrawList* dl, ImVec2 center, float radius, ImU32 fillCol, ImU32 outlineCol, float outlineThick = 1.f) {
    dl->AddCircleFilled(center, radius, fillCol);
    if (outlineThick > 0.f) dl->AddCircle(center, radius, outlineCol, 24, outlineThick);
}

inline void DrawBezierOutlined(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImVec2 p3, ImVec2 p4, ImU32 col, float thick, float outlineExtra = 1.5f) {
    dl->AddBezierCubic(p1, p2, p3, p4, OUTLINE_DARK, thick + outlineExtra);
    dl->AddBezierCubic(p1, p2, p3, p4, col, thick);
}

inline void DrawPolylineOutlined(ImDrawList* dl, const ImVec2* pts, int count, ImU32 col, float thick, bool closed, float outlineExtra = 1.5f) {
    dl->AddPolyline(pts, count, OUTLINE_DARK, closed, thick + outlineExtra);
    dl->AddPolyline(pts, count, col, closed, thick);
}

// -----------------------------------------------------------------------------
// TEXT STACKING: Auto-spacing for bottom-aligned text elements
// -----------------------------------------------------------------------------

struct TextStack {
    float y;
    float centerX;

    TextStack(float startY, float cx) : y(startY), centerX(cx) {}

    // Draw centered text with optional outline
    void DrawText(ImDrawList* dl, const char* text, ImU32 col, bool outline = false, ImU32 outlineCol = 0, float outlineThick = 1.f) {
        ImVec2 sz = ImGui::CalcTextSize(text);
        float x = centerX - sz.x * 0.5f;
        ImVec2 pos(x, y);

        if (outline) {
            dl->AddText(ImVec2(pos.x + outlineThick, pos.y), outlineCol, text);
            dl->AddText(ImVec2(pos.x - outlineThick, pos.y), outlineCol, text);
            dl->AddText(ImVec2(pos.x, pos.y + outlineThick), outlineCol, text);
            dl->AddText(ImVec2(pos.x, pos.y - outlineThick), outlineCol, text);
        }
        dl->AddText(pos, col, text);
        y += sz.y + 3.f;
    }

    // Draw horizontal bar with label above
    void DrawBar(ImDrawList* dl, const char* label, float fraction, ImU32 bgCol, ImU32 fillCol, float barW, float barH, bool outline = false, ImU32 outlineCol = 0, float outlineThick = 1.f) {
        ImVec2 labelSz = ImGui::CalcTextSize(label);
        float x = centerX - barW * 0.5f;

        // Reserve the label's own row; never paint it over a preceding line.
        const float labelY = y;
        y += labelSz.y + 3.f;

        // Background + outline
        dl->AddRectFilled(ImVec2(x - 1, y - 1), ImVec2(x + barW + 1, y + barH + 1), OUTLINE_DARK_STRONG);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + barW, y + barH), bgCol);

        // Fill
        float fw = barW * std::clamp(fraction, 0.f, 1.f);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + fw, y + barH), fillCol);

        // Label above bar
        float lx = centerX - labelSz.x * 0.5f;
        float ly = labelY;
        if (outline) {
            dl->AddText(ImVec2(lx + outlineThick, ly), outlineCol, label);
            dl->AddText(ImVec2(lx - outlineThick, ly), outlineCol, label);
            dl->AddText(ImVec2(lx, ly + outlineThick), outlineCol, label);
            dl->AddText(ImVec2(lx, ly - outlineThick), outlineCol, label);
        }
        dl->AddText(ImVec2(lx, ly), fillCol, label);

        y += barH + 6.f;
    }
};

// -----------------------------------------------------------------------------
// BACKGROUND: Panel background + character silhouette fallback
// -----------------------------------------------------------------------------

void DrawBackground(ImDrawList* dl, const ImVec2& origin, float width, float height);
void DrawCharacterTexture(ImDrawList* dl, const ImVec2& origin, float width, float height);

// -----------------------------------------------------------------------------
// INDIVIDUAL ELEMENT DRAW FUNCTIONS
// All use PlayerGeometry - NO magic numbers
// -----------------------------------------------------------------------------

// 1. Trails (behind everything)
void DrawTrails(ImDrawList* dl, const PlayerGeometry& g, ImU32 col);

// 2. Angel Wings (behind player)
void DrawAngelWings(ImDrawList* dl, const PlayerGeometry& g, ImU32 col);

// 3. Boxes
void DrawBox2D(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick, float rounding = 0.f, bool fill = false, ImU32 fillCol = 0);
void DrawCornerBox(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick);

// 4. Skeleton
void DrawSkeleton(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick);

// 5. Joints
void DrawJoints(ImDrawList* dl, const PlayerGeometry& g, ImU32 col);

// 6. Health Bar (left)
void DrawHealthBar(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float fraction, bool showValue = false);

// 7. Armor Bar (right)
void DrawArmorBar(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float fraction, bool showValue = false);

// 8. Head Circle
void DrawHeadCircle(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick);

// 9. Decorative Head Elements
void DrawHalo(ImDrawList* dl, const PlayerGeometry& g, ImU32 col);
void DrawChineseHat(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float scale = 1.f);
void DrawDemonHorns(ImDrawList* dl, const PlayerGeometry& g, ImU32 col);
void DrawFloatingCrown(ImDrawList* dl, const PlayerGeometry& g, ImU32 col);

// 10. Snaplines
void DrawSnaplines(ImDrawList* dl, const PlayerGeometry& g, const ImVec2& origin, float width, float height, ImU32 col, float thick, int snaplinePos = 2);

// 11. Eye Line
void DrawEyeLine(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick, float length);

// 12. Text Elements (use TextStack)
void DrawPlayerName(ImDrawList* dl, TextStack& stack, const PlayerGeometry& g, const char* name, ImU32 col, bool outline, ImU32 outlineCol, float outlineThick,
                    bool hasHalo, bool hasHat, bool hasHorns, bool hasCrown);
void DrawWeaponName(ImDrawList* dl, TextStack& stack, const char* name, ImU32 col, bool outline, ImU32 outlineCol, float outlineThick);
void DrawAmmo(ImDrawList* dl, TextStack& stack, const char* label, float fraction, ImU32 col, ImU32 bgCol, bool outline, ImU32 outlineCol, float outlineThick);
void DrawDistance(ImDrawList* dl, TextStack& stack, const char* text, ImU32 col, bool outline, ImU32 outlineCol, float outlineThick);

// -----------------------------------------------------------------------------
// CONFIG INTERFACE: Games implement this to provide their config to preview
// -----------------------------------------------------------------------------

struct PreviewConfig {
    // Core ESP
    bool esp_enabled = false;
    bool box = false;
    bool box_corner = false;
    bool box_fill = false;
    float box_thickness = 1.8f;
    float box_rounding = 0.f;

    // Skeleton
    bool skeleton = false;
    bool skeleton_joints = false;
    float skeleton_thickness = 1.85f;

    // Head
    bool head_dot = false;
    bool head_halo = false;
    float head_circle_thickness = 1.5f;

    // Bars
    bool health_bar = false;
    bool armor_bar = false;
    bool health_value = false;
    bool armor_value = false;

    // Text
    bool name = false;
    bool distance = false;
    bool weapon_name = false;
    bool weapon_ammo = false;
    bool text_outline = true;
    float text_outline_thickness = 1.f;

    // Snaplines
    bool snaplines = false;
    int snapline_position = 2; // 0=top, 1=center, 2=bottom
    float snapline_thickness = 1.5f;

    // Eye line
    bool look_direction = false;
    float eye_line_thickness = 1.6f;
    float look_direction_length = 90.f;

    // Fun effects
    bool chinese_hat = false;
    bool angel_wings = false;
    bool devil_horns = false;
    bool floating_crown = false;
    bool trails = false;
    float chinese_hat_scale = 1.f;
    float fun_effects_scale = 1.f;

    // Colors (RGBA 0-1)
    float col_box[4] = {0.9f, 0.25f, 0.25f, 1.f};
    float col_box_corner[4] = {0.f, 0.78f, 1.f, 1.f};
    float col_box_fill[4] = {0.9f, 0.25f, 0.25f, 0.16f};
    float col_skeleton[4] = {0.95f, 0.85f, 0.35f, 1.f};
    float col_joints[4] = {0.27f, 0.9f, 0.37f, 1.f};
    float col_head[4] = {0.95f, 0.85f, 0.35f, 1.f};
    float col_halo[4] = {1.f, 0.89f, 0.54f, 0.9f};
    float col_health[4] = {0.25f, 0.86f, 0.35f, 1.f};
    float col_armor[4] = {0.27f, 0.59f, 1.f, 1.f};
    float col_name[4] = {1.f, 1.f, 1.f, 1.f};
    float col_distance[4] = {0.85f, 0.85f, 0.9f, 1.f};
    float col_weapon[4] = {0.85f, 0.85f, 0.9f, 1.f};
    float col_snaplines[4] = {0.83f, 0.69f, 0.22f, 0.55f};
    float col_trail[4] = {0.83f, 0.69f, 0.22f, 0.82f};
    float col_fun_effects[4] = {1.f, 0.72f, 0.18f, 0.95f};
    float col_look[4] = {0.83f, 0.69f, 0.22f, 0.86f};
    float col_text_outline[4] = {0.f, 0.f, 0.f, 1.f};
    float col_visible[4] = {0.27f, 0.9f, 0.37f, 1.f};
    float col_occluded[4] = {0.9f, 0.25f, 0.25f, 1.f};
    float col_team[4] = {0.25f, 0.75f, 0.95f, 1.f};

    // RGB/Visibility modes
    bool rgb_mode = false;
    bool visibility_colors = false;
    bool visible_check = false;
    bool team_check = true;
};

// Game-specific config adapter - implement per game
struct IConfigAdapter {
    virtual ~IConfigAdapter() = default;
    virtual const PreviewConfig& GetPreviewConfig() const = 0;
    virtual ImU32 GetBoxColor(bool rgbMode, bool visibilityColors, bool visibleCheck) const = 0;
};

// -----------------------------------------------------------------------------
// MAIN DRAW FUNCTION: Call from game's DrawPreview()
// -----------------------------------------------------------------------------

void DrawPreview(ImDrawList* dl, const ImVec2& origin, float width, float height,
                 const IConfigAdapter& adapter,
                 const char* previewLabel = "PRÉ-VISUALIZAÇÃO ESP");

} // namespace EspPreview
