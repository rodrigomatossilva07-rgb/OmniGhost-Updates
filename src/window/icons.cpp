#include "icons.h"
#include "theme.h"
#include <cmath>

namespace CyberIcons {

    static ImVec2 C(ImVec2 pos, float size) {
        return ImVec2(pos.x + size * 0.5f, pos.y + size * 0.5f);
    }

    // Compact ESP eye with corner brackets. The silhouette stays readable at 20-26 px.
    void DrawESPIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        const float rx = size * 0.34f;
        const float ry = size * 0.20f;
        dl->AddBezierCubic(ImVec2(c.x - rx, c.y),
                           ImVec2(c.x - rx * 0.52f, c.y - ry * 1.45f),
                           ImVec2(c.x + rx * 0.52f, c.y - ry * 1.45f),
                           ImVec2(c.x + rx, c.y), color, 1.7f);
        dl->AddBezierCubic(ImVec2(c.x - rx, c.y),
                           ImVec2(c.x - rx * 0.52f, c.y + ry * 1.45f),
                           ImVec2(c.x + rx * 0.52f, c.y + ry * 1.45f),
                           ImVec2(c.x + rx, c.y), color, 1.7f);
        dl->AddCircle(c, size * 0.105f, color, 12, 1.5f);
        dl->AddCircleFilled(c, size * 0.035f, color, 8);

        const float k = size * 0.16f;
        const float inset = size * 0.08f;
        dl->AddLine(ImVec2(pos.x + inset, pos.y + k), ImVec2(pos.x + inset, pos.y + inset), color, 1.35f);
        dl->AddLine(ImVec2(pos.x + inset, pos.y + inset), ImVec2(pos.x + k, pos.y + inset), color, 1.35f);
        dl->AddLine(ImVec2(pos.x + size - k, pos.y + inset), ImVec2(pos.x + size - inset, pos.y + inset), color, 1.35f);
        dl->AddLine(ImVec2(pos.x + size - inset, pos.y + inset), ImVec2(pos.x + size - inset, pos.y + k), color, 1.35f);
        dl->AddLine(ImVec2(pos.x + inset, pos.y + size - k), ImVec2(pos.x + inset, pos.y + size - inset), color, 1.35f);
        dl->AddLine(ImVec2(pos.x + inset, pos.y + size - inset), ImVec2(pos.x + k, pos.y + size - inset), color, 1.35f);
        dl->AddLine(ImVec2(pos.x + size - k, pos.y + size - inset), ImVec2(pos.x + size - inset, pos.y + size - inset), color, 1.35f);
        dl->AddLine(ImVec2(pos.x + size - inset, pos.y + size - inset), ImVec2(pos.x + size - inset, pos.y + size - k), color, 1.35f);
    }

    void DrawWorldIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        const float r = size * 0.38f;
        dl->AddCircle(c, r, color, 20, 1.6f);
        dl->AddCircle(c, r * 0.55f, color, 16, 1.2f);
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), color, 1.3f);
        dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), color, 1.3f);
        dl->AddBezierCubic(
            ImVec2(c.x - r * 0.15f, c.y - r),
            ImVec2(c.x + r * 0.55f, c.y - r * 0.35f),
            ImVec2(c.x + r * 0.55f, c.y + r * 0.35f),
            ImVec2(c.x - r * 0.15f, c.y + r),
            color, 1.3f);
    }

    void DrawAimIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        const float r = size * 0.32f;
        const float gap = size * 0.11f;
        // Four broken scope arcs plus a precise center point.
        dl->PathArcTo(c, r, -2.82f, -1.90f, 7); dl->PathStroke(color, false, 1.8f);
        dl->PathArcTo(c, r, -1.24f, -0.32f, 7); dl->PathStroke(color, false, 1.8f);
        dl->PathArcTo(c, r,  0.32f,  1.24f, 7); dl->PathStroke(color, false, 1.8f);
        dl->PathArcTo(c, r,  1.90f,  2.82f, 7); dl->PathStroke(color, false, 1.8f);
        dl->AddLine(ImVec2(c.x, pos.y + size * 0.04f), ImVec2(c.x, c.y - r - gap * 0.15f), color, 1.5f);
        dl->AddLine(ImVec2(c.x, c.y + r + gap * 0.15f), ImVec2(c.x, pos.y + size * 0.96f), color, 1.5f);
        dl->AddLine(ImVec2(pos.x + size * 0.04f, c.y), ImVec2(c.x - r - gap * 0.15f, c.y), color, 1.5f);
        dl->AddLine(ImVec2(c.x + r + gap * 0.15f, c.y), ImVec2(pos.x + size * 0.96f, c.y), color, 1.5f);
        dl->AddCircleFilled(c, size * 0.055f, color, 10);
    }

    void DrawMouseIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const float x0 = pos.x + size * 0.28f;
        const float x1 = pos.x + size * 0.72f;
        const float y0 = pos.y + size * 0.10f;
        const float y1 = pos.y + size * 0.90f;
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), color, size * 0.18f, 0, 1.6f);
        const float mid = (y0 + y1) * 0.42f;
        dl->AddLine(ImVec2(x0 + 1.f, mid), ImVec2(x1 - 1.f, mid), color, 1.4f);
        dl->AddLine(ImVec2((x0 + x1) * 0.5f, y0 + 2.f),
                    ImVec2((x0 + x1) * 0.5f, mid), color, 1.4f);
        // Wheel
        dl->AddCircleFilled(ImVec2((x0 + x1) * 0.5f, (y0 + mid) * 0.55f), size * 0.05f, color, 8);
    }

    void DrawTriggerIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        const float r = size * 0.30f;
        // A pulse/click glyph: ring, center dot and three short activation rays.
        dl->AddCircle(c, r, color, 20, 1.7f);
        dl->AddCircleFilled(c, size * 0.07f, color, 10);
        dl->AddLine(ImVec2(c.x + r * 0.62f, c.y - r * 0.62f),
                    ImVec2(c.x + r * 1.08f, c.y - r * 1.08f), color, 1.6f);
        dl->AddLine(ImVec2(c.x + r * 0.95f, c.y),
                    ImVec2(c.x + r * 1.42f, c.y), color, 1.6f);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.95f),
                    ImVec2(c.x, c.y - r * 1.42f), color, 1.6f);
        // Lower stem differentiates it from the Aim icon.
        dl->AddLine(ImVec2(c.x, c.y + r * 0.70f),
                    ImVec2(c.x, pos.y + size * 0.94f), color, 1.7f);
    }

    void DrawWrenchIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        // Handle
        dl->AddLine(ImVec2(c.x - size * 0.28f, c.y + size * 0.28f),
                    ImVec2(c.x + size * 0.22f, c.y - size * 0.22f), color, 2.2f);
        // Head
        dl->AddCircle(ImVec2(c.x + size * 0.26f, c.y - size * 0.26f), size * 0.16f, color, 12, 1.6f);
        dl->AddLine(ImVec2(c.x + size * 0.14f, c.y - size * 0.36f),
                    ImVec2(c.x + size * 0.38f, c.y - size * 0.16f), color, 1.5f);
    }

    void DrawSettingsIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        dl->AddCircle(c, size * 0.18f, color, 12, 1.8f);
        for (int i = 0; i < 6; ++i) {
            const float a = i * (3.14159265f * 2.0f / 6.0f) + 0.3f;
            const float ca = cosf(a), sa = sinf(a);
            dl->AddLine(
                ImVec2(c.x + ca * size * 0.28f, c.y + sa * size * 0.28f),
                ImVec2(c.x + ca * size * 0.44f, c.y + sa * size * 0.44f),
                color, 2.4f);
        }
    }

    void DrawSaveIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        // Cloud
        dl->AddCircleFilled(ImVec2(c.x - size * 0.14f, c.y - size * 0.02f), size * 0.16f, color, 12);
        dl->AddCircleFilled(ImVec2(c.x + size * 0.10f, c.y - size * 0.06f), size * 0.20f, color, 12);
        dl->AddCircleFilled(ImVec2(c.x + size * 0.22f, c.y + size * 0.02f), size * 0.14f, color, 12);
        dl->AddRectFilled(
            ImVec2(c.x - size * 0.26f, c.y - size * 0.02f),
            ImVec2(c.x + size * 0.28f, c.y + size * 0.18f), color, 3.f);
        // Gear overlay (hollow)
        dl->AddCircle(ImVec2(c.x, c.y + size * 0.04f), size * 0.10f,
            IM_COL32(20, 20, 20, 220), 10, 1.6f);
    }

    void DrawUserIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        // Primary person
        dl->AddCircle(ImVec2(c.x - size * 0.08f, pos.y + size * 0.30f), size * 0.15f, color, 12, 1.6f);
        dl->AddBezierCubic(
            ImVec2(pos.x + size * 0.12f, pos.y + size * 0.88f),
            ImVec2(pos.x + size * 0.12f, pos.y + size * 0.55f),
            ImVec2(pos.x + size * 0.62f, pos.y + size * 0.55f),
            ImVec2(pos.x + size * 0.62f, pos.y + size * 0.88f),
            color, 1.6f);
        // Secondary smaller person
        dl->AddCircle(ImVec2(c.x + size * 0.22f, pos.y + size * 0.34f), size * 0.12f, color, 10, 1.4f);
        dl->AddBezierCubic(
            ImVec2(pos.x + size * 0.48f, pos.y + size * 0.88f),
            ImVec2(pos.x + size * 0.48f, pos.y + size * 0.60f),
            ImVec2(pos.x + size * 0.88f, pos.y + size * 0.60f),
            ImVec2(pos.x + size * 0.88f, pos.y + size * 0.88f),
            color, 1.4f);
    }

    void DrawVehicleIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const float y = pos.y + size * 0.42f;
        // Body
        dl->AddRectFilled(
            ImVec2(pos.x + size * 0.12f, y),
            ImVec2(pos.x + size * 0.88f, y + size * 0.22f), color, 2.f);
        // Cabin
        dl->AddQuadFilled(
            ImVec2(pos.x + size * 0.28f, y),
            ImVec2(pos.x + size * 0.38f, pos.y + size * 0.22f),
            ImVec2(pos.x + size * 0.62f, pos.y + size * 0.22f),
            ImVec2(pos.x + size * 0.72f, y),
            color);
        // Wheels
        dl->AddCircleFilled(ImVec2(pos.x + size * 0.28f, y + size * 0.28f), size * 0.10f, color, 10);
        dl->AddCircleFilled(ImVec2(pos.x + size * 0.72f, y + size * 0.28f), size * 0.10f, color, 10);
        dl->AddCircle(ImVec2(pos.x + size * 0.28f, y + size * 0.28f), size * 0.10f,
            IM_COL32(20, 20, 20, 200), 10, 1.2f);
        dl->AddCircle(ImVec2(pos.x + size * 0.72f, y + size * 0.28f), size * 0.10f,
            IM_COL32(20, 20, 20, 200), 10, 1.2f);
    }

    void DrawStatusIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        // Three rising signal bars
        const float base = pos.y + size * 0.88f;
        const float w = size * 0.16f;
        const float gap = size * 0.10f;
        float x = pos.x + size * 0.18f;
        dl->AddRectFilled(ImVec2(x, base - size * 0.28f), ImVec2(x + w, base), color, 1.5f);
        x += w + gap;
        dl->AddRectFilled(ImVec2(x, base - size * 0.48f), ImVec2(x + w, base), color, 1.5f);
        x += w + gap;
        dl->AddRectFilled(ImVec2(x, base - size * 0.72f), ImVec2(x + w, base), color, 1.5f);
    }

    void DrawRadarIcon(ImDrawList* dl, ImVec2 pos, float size, ImU32 color) {
        const ImVec2 c = C(pos, size);
        const float r = size * 0.37f;
        dl->AddCircle(c, r, color, 24, 1.5f);
        dl->AddCircle(c, r * 0.52f, color, 20, 1.1f);
        dl->AddLine(c, ImVec2(c.x + r * 0.70f, c.y - r * 0.55f), color, 1.6f);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.46f, c.y - r * 0.20f), size * 0.055f, color, 8);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.28f, c.y + r * 0.34f), size * 0.045f, color, 8);
        dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), color, 1.0f);
    }

} // namespace CyberIcons
