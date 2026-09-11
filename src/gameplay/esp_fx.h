#pragma once

#include "../../ImGui/imgui.h"
#include <cmath>
#include <algorithm>
#include <functional>

namespace OmniGhost::Gameplay::EspFx {

inline ImU32 Hsv(float h, float s, float v, float a = 1.f) {
    h = h - std::floor(h);
    const float i = std::floor(h * 6.f);
    const float f = h * 6.f - i;
    const float p = v * (1.f - s);
    const float q = v * (1.f - f * s);
    const float t = v * (1.f - (1.f - f) * s);
    float r = 0, g = 0, b = 0;
    switch (static_cast<int>(i) % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
    return IM_COL32(
        static_cast<int>(r * 255.f),
        static_cast<int>(g * 255.f),
        static_cast<int>(b * 255.f),
        static_cast<int>(std::clamp(a, 0.f, 1.f) * 255.f));
}

// Project world -> screen. Returns false if behind camera / invalid.
using ProjectFn = std::function<bool(float wx, float wy, float wz, float& sx, float& sy)>;

// Animated rainbow Chinese hat (cone) + optional stacked discs, rotates over time.
inline void DrawChineseHat(ImDrawList* dl, float hx, float hy, float hz,
                           const ProjectFn& project, float timeSec,
                           float scale = 1.f, bool rainbow = true) {
    if (!dl || !project) return;
    const float spin = timeSec * 1.85f;
    const float baseR = 0.28f * scale;
    const float hatHeight = 0.32f * scale;
    constexpr int segs = 20;

    float apexX = 0.f, apexY = 0.f;
    if (!project(hx, hy, hz + hatHeight + 0.06f * scale, apexX, apexY))
        return;

    // Cone sides
    float prevSx = 0.f, prevSy = 0.f;
    bool prevOk = false;
    for (int i = 0; i <= segs; ++i) {
        const float a = spin + (6.28318530718f * i) / segs;
        const float wx = hx + std::cos(a) * baseR;
        const float wy = hy + std::sin(a) * baseR;
        const float wz = hz + 0.04f * scale;
        float sx = 0.f, sy = 0.f;
        const bool ok = project(wx, wy, wz, sx, sy);
        if (ok) {
            const ImU32 col = rainbow
                ? Hsv((a / 6.28318530718f) + timeSec * 0.15f, 0.95f, 1.f, 0.95f)
                : IM_COL32(255, 80, 180, 230);
            dl->AddLine(ImVec2(apexX, apexY), ImVec2(sx, sy), col, 1.5f);
            if (prevOk)
                dl->AddLine(ImVec2(prevSx, prevSy), ImVec2(sx, sy), col, 1.35f);
        }
        prevSx = sx; prevSy = sy; prevOk = ok;
    }

    // Floating rainbow discs (matches reference style)
    for (int ring = 0; ring < 3; ++ring) {
        const float z = hz + 0.02f * scale + ring * 0.09f * scale;
        const float rr = baseR * (0.95f - ring * 0.18f);
        const float ringSpin = spin * (ring % 2 == 0 ? 1.f : -1.15f);
        float p0x = 0.f, p0y = 0.f;
        bool p0ok = false;
        for (int i = 0; i <= segs; ++i) {
            const float a = ringSpin + (6.28318530718f * i) / segs;
            float sx = 0.f, sy = 0.f;
            const bool ok = project(hx + std::cos(a) * rr, hy + std::sin(a) * rr, z, sx, sy);
            if (ok && p0ok) {
                const ImU32 col = Hsv((a / 6.28318530718f) + timeSec * 0.2f + ring * 0.12f,
                                      0.95f, 1.f, 0.85f);
                dl->AddLine(ImVec2(p0x, p0y), ImVec2(sx, sy), col, 1.4f);
            }
            p0x = sx; p0y = sy; p0ok = ok;
        }
    }
}

inline ImU32 RainbowFade(float t, float ageNorm, float alphaScale = 1.f) {
    return Hsv(t + ageNorm * 0.35f, 0.95f, 1.f, (1.f - ageNorm) * (1.f - ageNorm) * alphaScale);
}

} // namespace OmniGhost::Gameplay::EspFx
