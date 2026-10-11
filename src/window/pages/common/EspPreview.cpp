#include "EspPreview.h"
#include "../../../launcher/launcher_assets.h"
#include "../../../config/app_settings.h"
#include "third_party/ImGui/imgui_internal.h"

#include <cmath>
#include <unordered_map>

namespace EspPreview {

namespace {
    float SmoothFeature(ImGuiID base, const char* name, bool enabled) {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        const ImGuiID id = ImHashStr(name, 0, base);
        const float target = enabled ? 1.0f : 0.0f;
        float value = storage->GetFloat(id, target);
        if (!app_settings::MotionEnabled()) value = target;
        else value += (target - value) *
            (1.0f - std::exp(-13.0f * std::min(ImGui::GetIO().DeltaTime, 0.05f)));
        storage->SetFloat(id, value);
        return value;
    }

    ImU32 SmoothColor(ImGuiID base, const char* name, ImU32 target, float alpha) {
        static std::unordered_map<ImGuiID, ImVec4> colors;
        const ImGuiID id = ImHashStr(name, 0, base);
        const ImVec4 targetColor = ImGui::ColorConvertU32ToFloat4(target);
        auto [it, inserted] = colors.try_emplace(id, targetColor);
        if (!inserted && app_settings::MotionEnabled()) {
            const float amount = 1.0f - std::exp(-11.0f * std::min(ImGui::GetIO().DeltaTime, 0.05f));
            it->second.x += (targetColor.x - it->second.x) * amount;
            it->second.y += (targetColor.y - it->second.y) * amount;
            it->second.z += (targetColor.z - it->second.z) * amount;
            it->second.w += (targetColor.w - it->second.w) * amount;
        } else if (!app_settings::MotionEnabled()) {
            it->second = targetColor;
        }
        ImVec4 result = it->second;
        result.w *= std::clamp(alpha, 0.0f, 1.0f);
        return ImGui::ColorConvertFloat4ToU32(result);
    }
}

// ============================================================
// GEOMETRY BUILDER
// ============================================================

PlayerGeometry BuildGeometry(const ImVec2& origin, float width, float height) {
    PlayerGeometry g{};
    const LauncherAssets::Texture portrait = LauncherAssets::FiveMEspPreview();
    const float aspect = portrait ? static_cast<float>(portrait.width) / portrait.height : 1000.f / 1490.f;
    const float imageH = (std::min)(height * 0.70f, width * 0.60f / aspect);
    const float imageW = imageH * aspect;
    const float imageX = origin.x + (width - imageW) * 0.5f;
    const float imageY = origin.y + (height - imageH) * 0.5f;
    const auto point = [&](float x, float y) { return ImVec2(imageX + imageW * x, imageY + imageH * y); };

    // Landmarks measured on the shared transparent operator portrait. Every overlay
    // and the portrait itself use this same rectangle, including when resized.
    g.boundsMin = point(.18f, .025f);
    g.boundsMax = point(.82f, .975f);
    g.boundsCenter = point(.50f, .50f);
    g.boundsWidth = g.boundsMax.x - g.boundsMin.x;
    g.boundsHeight = g.boundsMax.y - g.boundsMin.y;
    g.headCenter = point(.505f, .103f);
    g.headRadius = imageW * .085f;
    g.headTop = point(.505f, .025f);
    g.headBottom = point(.505f, .177f);
    g.neck = point(.505f, .194f);
    g.chest = point(.505f, .340f);
    g.pelvis = point(.505f, .530f);
    g.leftShoulder = point(.36f, .225f);
    g.rightShoulder = point(.65f, .225f);
    g.leftElbow = point(.235f, .365f);
    g.rightElbow = point(.775f, .365f);
    g.leftHand = point(.215f, .520f);
    g.rightHand = point(.795f, .520f);
    g.leftHip = point(.405f, .550f);
    g.rightHip = point(.615f, .550f);
    g.leftKnee = point(.395f, .735f);
    g.rightKnee = point(.625f, .735f);
    g.leftFoot = point(.325f, .970f);
    g.rightFoot = point(.715f, .970f);
    g.feetCenter = point(.52f, .970f);
    g.textStackY = g.boundsMax.y + 6.f;

    return g;
}

// ============================================================
// BACKGROUND
// ============================================================

void DrawBackground(ImDrawList* dl, const ImVec2& origin, float width, float height) {
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(8, 10, 12, 255), 8.f);
    dl->AddRect(origin, ImVec2(origin.x + width, origin.y + height), IM_COL32(199, 165, 43, 105), 8.f);

}

void DrawCharacterTexture(ImDrawList* dl, const ImVec2& origin, float width, float height) {
    const LauncherAssets::Texture portrait = LauncherAssets::FiveMEspPreview();
    if (!portrait) return;

    const float aspect = static_cast<float>(portrait.width) / static_cast<float>(portrait.height);
    const float portraitHeight = (std::min)(height * 0.70f, width * 0.60f / aspect);
    const float portraitWidth = portraitHeight * aspect;
    const float top = origin.y + (height - portraitHeight) * 0.5f;
    const float centerX = origin.x + width * 0.5f;
    dl->AddImage(portrait.id,
        ImVec2(centerX - portraitWidth * 0.5f, top),
        ImVec2(centerX + portraitWidth * 0.5f, top + portraitHeight),
        ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
        IM_COL32(255, 255, 255, 235));
}

// ============================================================
// TRAILS
// ============================================================

void DrawTrails(ImDrawList* dl, const PlayerGeometry& g, ImU32 col) {
    const int segments = 4;
    const float baseOpacity = 0.85f;

    // Hand trails
    for (int side = 0; side < 2; ++side) {
        ImVec2 hand = side == 0 ? g.leftHand : g.rightHand;
        float dirX = side == 0 ? -1.f : 1.f;

        for (int i = 0; i < segments; ++i) {
            float t = (i + 1) / float(segments + 1);
            float opacity = baseOpacity * (1.f - t * 0.75f);
            ImVec2 p(hand.x + dirX * g.boundsWidth * 0.12f * t, hand.y + g.boundsHeight * 0.04f * t);
            float radius = 2.5f * (1.f - t * 0.5f);
            dl->AddCircleFilled(p, radius, WithAlpha(col, opacity), 12);
        }
    }

    // Foot trails
    for (int side = 0; side < 2; ++side) {
        ImVec2 foot = side == 0 ? g.leftFoot : g.rightFoot;
        float dirX = side == 0 ? -1.f : 1.f;

        for (int i = 0; i < segments; ++i) {
            float t = (i + 1) / float(segments + 1);
            float opacity = baseOpacity * (1.f - t * 0.75f);
            ImVec2 p(foot.x + dirX * g.boundsWidth * 0.08f * t, foot.y - g.boundsHeight * 0.03f * t);
            float radius = 2.f * (1.f - t * 0.5f);
            dl->AddCircleFilled(p, radius, WithAlpha(col, opacity), 12);
        }
    }
}

// ============================================================
// ANGEL WINGS
// ============================================================

void DrawAngelWings(ImDrawList* dl, const PlayerGeometry& g, ImU32 col) {
    const float thick = 1.6f;

    // Left wing - from left shoulder blade
    ImVec2 originL(g.leftShoulder.x - g.boundsWidth * 0.08f, g.leftShoulder.y + g.boundsHeight * 0.05f);

    // Upper curve
    DrawBezierOutlined(dl,
        originL,
        ImVec2(originL.x - g.boundsWidth * 0.55f, originL.y - g.boundsHeight * 0.15f),
        ImVec2(originL.x - g.boundsWidth * 0.65f, originL.y + g.boundsHeight * 0.15f),
        ImVec2(originL.x - g.boundsWidth * 0.35f, originL.y + g.boundsHeight * 0.35f),
        col, thick);

    // Middle curve
    DrawBezierOutlined(dl,
        ImVec2(originL.x - g.boundsWidth * 0.35f, originL.y + g.boundsHeight * 0.35f),
        ImVec2(originL.x - g.boundsWidth * 0.60f, originL.y + g.boundsHeight * 0.45f),
        ImVec2(originL.x - g.boundsWidth * 0.55f, originL.y + g.boundsHeight * 0.65f),
        ImVec2(originL.x - g.boundsWidth * 0.25f, originL.y + g.boundsHeight * 0.55f),
        col, thick);

    // Lower curve
    DrawBezierOutlined(dl,
        ImVec2(originL.x - g.boundsWidth * 0.25f, originL.y + g.boundsHeight * 0.55f),
        ImVec2(originL.x - g.boundsWidth * 0.45f, originL.y + g.boundsHeight * 0.70f),
        ImVec2(originL.x - g.boundsWidth * 0.30f, originL.y + g.boundsHeight * 0.85f),
        ImVec2(g.leftHip.x - g.boundsWidth * 0.1f, g.leftHip.y + g.boundsHeight * 0.1f),
        col, thick);

    // Right wing - mirror
    ImVec2 originR(g.rightShoulder.x + g.boundsWidth * 0.08f, g.rightShoulder.y + g.boundsHeight * 0.05f);

    DrawBezierOutlined(dl,
        originR,
        ImVec2(originR.x + g.boundsWidth * 0.55f, originR.y - g.boundsHeight * 0.15f),
        ImVec2(originR.x + g.boundsWidth * 0.65f, originR.y + g.boundsHeight * 0.15f),
        ImVec2(originR.x + g.boundsWidth * 0.35f, originR.y + g.boundsHeight * 0.35f),
        col, thick);

    DrawBezierOutlined(dl,
        ImVec2(originR.x + g.boundsWidth * 0.35f, originR.y + g.boundsHeight * 0.35f),
        ImVec2(originR.x + g.boundsWidth * 0.60f, originR.y + g.boundsHeight * 0.45f),
        ImVec2(originR.x + g.boundsWidth * 0.55f, originR.y + g.boundsHeight * 0.65f),
        ImVec2(originR.x + g.boundsWidth * 0.25f, originR.y + g.boundsHeight * 0.55f),
        col, thick);

    DrawBezierOutlined(dl,
        ImVec2(originR.x + g.boundsWidth * 0.25f, originR.y + g.boundsHeight * 0.55f),
        ImVec2(originR.x + g.boundsWidth * 0.45f, originR.y + g.boundsHeight * 0.70f),
        ImVec2(originR.x + g.boundsWidth * 0.30f, originR.y + g.boundsHeight * 0.85f),
        ImVec2(g.rightHip.x + g.boundsWidth * 0.1f, g.rightHip.y + g.boundsHeight * 0.1f),
        col, thick);
}

// ============================================================
// BOXES
// ============================================================

void DrawBox2D(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick, float rounding, bool fill, ImU32 fillCol) {
    // Padding: 6% horizontal, 3% vertical
    const float hPad = g.boundsWidth * 0.06f;
    const float vPad = g.boundsHeight * 0.03f;

    ImVec2 min(g.boundsMin.x - hPad, g.boundsMin.y - vPad);
    ImVec2 max(g.boundsMax.x + hPad, g.boundsMax.y + vPad);

    if (fill && fillCol != 0) {
        dl->AddRectFilled(min, max, fillCol, rounding);
    }
    DrawRectOutlined(dl, min, max, col, thick, rounding);
}

void DrawCornerBox(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick) {
    // Same bounds as 2D box
    const float hPad = g.boundsWidth * 0.06f;
    const float vPad = g.boundsHeight * 0.03f;

    ImVec2 min(g.boundsMin.x - hPad, g.boundsMin.y - vPad);
    ImVec2 max(g.boundsMax.x + hPad, g.boundsMax.y + vPad);

    float boxW = max.x - min.x;
    float boxH = max.y - min.y;

    // Corner lengths: 22% width, 18% height
    float hLen = boxW * 0.22f;
    float vLen = boxH * 0.18f;

    // Top-left
    DrawLineOutlined(dl, min, ImVec2(min.x + hLen, min.y), col, thick);
    DrawLineOutlined(dl, min, ImVec2(min.x, min.y + vLen), col, thick);

    // Top-right
    DrawLineOutlined(dl, ImVec2(max.x - hLen, min.y), ImVec2(max.x, min.y), col, thick);
    DrawLineOutlined(dl, ImVec2(max.x, min.y), ImVec2(max.x, min.y + vLen), col, thick);

    // Bottom-left
    DrawLineOutlined(dl, ImVec2(min.x, max.y - vLen), ImVec2(min.x, max.y), col, thick);
    DrawLineOutlined(dl, ImVec2(min.x, max.y), ImVec2(min.x + hLen, max.y), col, thick);

    // Bottom-right
    DrawLineOutlined(dl, ImVec2(max.x - hLen, max.y), ImVec2(max.x, max.y), col, thick);
    DrawLineOutlined(dl, ImVec2(max.x, max.y - vLen), ImVec2(max.x, max.y), col, thick);
}

// ============================================================
// SKELETON
// ============================================================

void DrawSkeleton(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick) {
    auto bone = [&](ImVec2 a, ImVec2 b) {
        DrawLineOutlined(dl, a, b, col, thick);
    };

    // Spine
    bone(g.headCenter, g.neck);
    bone(g.neck, g.chest);
    bone(g.chest, g.pelvis);

    // Arms
    bone(g.neck, g.leftShoulder);
    bone(g.leftShoulder, g.leftElbow);
    bone(g.leftElbow, g.leftHand);

    bone(g.neck, g.rightShoulder);
    bone(g.rightShoulder, g.rightElbow);
    bone(g.rightElbow, g.rightHand);

    // Legs
    bone(g.pelvis, g.leftHip);
    bone(g.leftHip, g.leftKnee);
    bone(g.leftKnee, g.leftFoot);

    bone(g.pelvis, g.rightHip);
    bone(g.rightHip, g.rightKnee);
    bone(g.rightKnee, g.rightFoot);
}

// ============================================================
// JOINTS
// ============================================================

void DrawJoints(ImDrawList* dl, const PlayerGeometry& g, ImU32 col) {
    // Scale with preview: 1.4% of player height, clamped 2.5-4.5px
    float radius = std::clamp(g.boundsHeight * 0.014f, 2.5f, 4.5f);

    std::array<ImVec2, 15> joints = {
        g.neck, g.chest, g.pelvis,
        g.leftShoulder, g.leftElbow, g.leftHand,
        g.rightShoulder, g.rightElbow, g.rightHand,
        g.leftHip, g.leftKnee, g.leftFoot,
        g.rightHip, g.rightKnee, g.rightFoot
    };

    for (const ImVec2& p : joints) {
        DrawCircleFilledOutlined(dl, p, radius, col, OUTLINE_DARK_STRONG, 1.f);
    }
}

// ============================================================
// HEALTH BAR (LEFT)
// ============================================================

void DrawHealthBar(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float fraction, bool showValue) {
    const ImU32 bgCol = IM_COL32(18, 18, 18, 235);
    float barW = 4.f;
    float barH = g.boundsHeight;
    float barX = g.boundsMin.x - 7.f - barW; // 7px gap

    ImVec2 bmin(barX, g.boundsMin.y);
    ImVec2 bmax(barX + barW, g.boundsMax.y);

    // Background + outline
    dl->AddRectFilled(ImVec2(bmin.x - 1, bmin.y - 1), ImVec2(bmax.x + 1, bmax.y + 1), OUTLINE_DARK_STRONG);
    dl->AddRectFilled(bmin, bmax, bgCol);

    // Fill (bottom to top)
    float fillTop = bmax.y - barH * std::clamp(fraction, 0.f, 1.f);
    dl->AddRectFilled(ImVec2(bmin.x, fillTop), bmax, col);

    // Value text at fill level
    if (showValue) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d HP", static_cast<int>(fraction * 100));
        ImVec2 sz = ImGui::CalcTextSize(buf);
        float tx = bmin.x - sz.x - 4.f;
        float ty = fillTop - sz.y * 0.5f;
        dl->AddText(ImVec2(tx, ty), col, buf);
    }
}

// ============================================================
// ARMOR BAR (RIGHT)
// ============================================================

void DrawArmorBar(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float fraction, bool showValue) {
    const ImU32 bgCol = IM_COL32(18, 18, 18, 235);
    float barW = 4.f;
    float barH = g.boundsHeight;
    float barX = g.boundsMax.x + 7.f; // 7px gap

    ImVec2 bmin(barX, g.boundsMin.y);
    ImVec2 bmax(barX + barW, g.boundsMax.y);

    dl->AddRectFilled(ImVec2(bmin.x - 1, bmin.y - 1), ImVec2(bmax.x + 1, bmax.y + 1), OUTLINE_DARK_STRONG);
    dl->AddRectFilled(bmin, bmax, bgCol);

    float fillTop = bmax.y - barH * std::clamp(fraction, 0.f, 1.f);
    dl->AddRectFilled(ImVec2(bmin.x, fillTop), bmax, col);

    if (showValue) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d AP", static_cast<int>(fraction * 100));
        ImVec2 sz = ImGui::CalcTextSize(buf);
        float tx = bmax.x + 4.f;
        float ty = fillTop - sz.y * 0.5f;
        dl->AddText(ImVec2(tx, ty), col, buf);
    }
}

// ============================================================
// HEAD CIRCLE
// ============================================================

void DrawHeadCircle(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick) {
    // Radius = head radius + 15% margin
    float radius = g.headRadius * 1.15f;
    DrawCircleOutlined(dl, g.headCenter, radius, col, thick, 32);
}

// ============================================================
// HALO (Horizontal ellipse above head)
// ============================================================

void DrawHalo(ImDrawList* dl, const PlayerGeometry& g, ImU32 col) {
    const float thick = 1.7f;

    // Center above head top
    ImVec2 center = g.headCenter;
    center.y = g.headTop.y - g.headRadius * 0.6f;

    float rx = g.headRadius * 1.35f;
    float ry = g.headRadius * 0.35f;

    // Ellipse as 2 bezier curves (top + bottom)
    ImVec2 p[7];
    p[0] = ImVec2(center.x - rx, center.y);
    p[1] = ImVec2(center.x - rx * 0.55f, center.y - ry);
    p[2] = ImVec2(center.x + rx * 0.55f, center.y - ry);
    p[3] = ImVec2(center.x + rx, center.y);
    p[4] = ImVec2(center.x + rx * 0.55f, center.y + ry);
    p[5] = ImVec2(center.x - rx * 0.55f, center.y + ry);
    p[6] = ImVec2(center.x - rx, center.y);

    DrawBezierOutlined(dl, p[0], p[1], p[2], p[3], col, thick);
    DrawBezierOutlined(dl, p[3], p[4], p[5], p[6], col, thick);

    // Subtle inner line for depth
    ImU32 inner = WithAlpha(col, 0.4f);
    float irx = rx * 0.7f, iry = ry * 0.7f;
    ImVec2 q[7];
    q[0] = ImVec2(center.x - irx, center.y);
    q[1] = ImVec2(center.x - irx * 0.55f, center.y - iry);
    q[2] = ImVec2(center.x + irx * 0.55f, center.y - iry);
    q[3] = ImVec2(center.x + irx, center.y);
    q[4] = ImVec2(center.x + irx * 0.55f, center.y + iry);
    q[5] = ImVec2(center.x - irx * 0.55f, center.y + iry);
    q[6] = ImVec2(center.x - irx, center.y);
    dl->AddBezierCubic(q[0], q[1], q[2], q[3], inner, thick * 0.7f);
    dl->AddBezierCubic(q[3], q[4], q[5], q[6], inner, thick * 0.7f);
}

// ============================================================
// CHINESE HAT (Conical with brim)
// ============================================================

void DrawChineseHat(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float scale) {
    const float thick = 1.8f;

    // Hat sits on head top
    float baseY = g.headTop.y - g.headRadius * 0.15f;
    float topY = baseY - g.headRadius * 1.8f * scale;
    float hatW = g.headRadius * 2.2f * scale;
    float brimW = hatW * 1.15f;
    float brimH = g.headRadius * 0.18f * scale;

    ImVec2 top(g.headCenter.x, topY);
    ImVec2 leftBase(g.headCenter.x - hatW * 0.5f, baseY);
    ImVec2 rightBase(g.headCenter.x + hatW * 0.5f, baseY);

    // Cone sides (curved)
    DrawBezierOutlined(dl, leftBase,
        ImVec2(leftBase.x - hatW * 0.15f, (leftBase.y + topY) * 0.5f),
        ImVec2(top.x - hatW * 0.1f, (top.y + baseY) * 0.5f),
        top, col, thick);

    DrawBezierOutlined(dl, rightBase,
        ImVec2(rightBase.x + hatW * 0.15f, (rightBase.y + topY) * 0.5f),
        ImVec2(top.x + hatW * 0.1f, (top.y + baseY) * 0.5f),
        top, col, thick);

    // Brim (horizontal ellipse)
    ImVec2 brimC(g.headCenter.x, baseY + brimH * 0.5f);
    DrawBezierOutlined(dl,
        ImVec2(brimC.x - brimW * 0.5f, brimC.y),
        ImVec2(brimC.x - brimW * 0.5f, brimC.y - brimH),
        ImVec2(brimC.x + brimW * 0.5f, brimC.y - brimH),
        ImVec2(brimC.x + brimW * 0.5f, brimC.y),
        col, thick);

    DrawBezierOutlined(dl,
        ImVec2(brimC.x + brimW * 0.5f, brimC.y),
        ImVec2(brimC.x + brimW * 0.5f, brimC.y + brimH),
        ImVec2(brimC.x - brimW * 0.5f, brimC.y + brimH),
        ImVec2(brimC.x - brimW * 0.5f, brimC.y),
        col, thick);

    // Structural lines on cone
    ImU32 lineCol = WithAlpha(col, 0.6f);
    for (int i = 1; i <= 3; ++i) {
        float t = i / 4.f;
        float y = baseY + (topY - baseY) * t;
        float w = hatW * (1.f - t);
        dl->AddLine(ImVec2(g.headCenter.x - w * 0.5f, y), ImVec2(g.headCenter.x + w * 0.5f, y), lineCol, thick * 0.6f);
    }
}

// ============================================================
// DEMON HORNS (Curved, from upper head sides)
// ============================================================

void DrawDemonHorns(ImDrawList* dl, const PlayerGeometry& g, ImU32 col) {
    const float thick = 2.f;

    // Base at upper sides of head
    float baseY = g.headCenter.y - g.headRadius * 0.3f;
    float baseX = g.headRadius * 0.85f;

    // Left horn: out -> up -> slightly in
    ImVec2 lb(g.headCenter.x - baseX, baseY);
    ImVec2 lm1(g.headCenter.x - baseX * 2.2f, baseY - g.headRadius * 0.4f);
    ImVec2 lm2(g.headCenter.x - baseX * 2.5f, baseY - g.headRadius * 1.1f);
    ImVec2 lt(g.headCenter.x - baseX * 2.0f, baseY - g.headRadius * 1.6f);
    DrawBezierOutlined(dl, lb, lm1, lm2, lt, col, thick);

    // Right horn: mirror
    ImVec2 rb(g.headCenter.x + baseX, baseY);
    ImVec2 rm1(g.headCenter.x + baseX * 2.2f, baseY - g.headRadius * 0.4f);
    ImVec2 rm2(g.headCenter.x + baseX * 2.5f, baseY - g.headRadius * 1.1f);
    ImVec2 rt(g.headCenter.x + baseX * 2.0f, baseY - g.headRadius * 1.6f);
    DrawBezierOutlined(dl, rb, rm1, rm2, rt, col, thick);
}

// ============================================================
// FLOATING CROWN (5-point, above head)
// ============================================================

void DrawFloatingCrown(ImDrawList* dl, const PlayerGeometry& g, ImU32 col) {
    const float thick = 1.8f;

    float bottomY = g.headTop.y - g.headRadius * 0.7f;
    float height = g.headRadius * 0.9f;
    float width = g.headRadius * 2.4f;
    int points = 5;

    ImVec2 pts[12];
    for (int i = 0; i < points; ++i) {
        float t = i / float(points - 1);
        float x = g.headCenter.x - width * 0.5f + width * t;
        float y = (i % 2 == 0) ? bottomY : bottomY - height;
        pts[i] = ImVec2(x, y);
    }
    // Close base
    pts[points] = ImVec2(g.headCenter.x + width * 0.5f, bottomY);
    pts[points + 1] = ImVec2(g.headCenter.x - width * 0.5f, bottomY);

    DrawPolylineOutlined(dl, pts, points + 2, col, thick, true);

    // Base line
    DrawLineOutlined(dl,
        ImVec2(g.headCenter.x - width * 0.5f, bottomY),
        ImVec2(g.headCenter.x + width * 0.5f, bottomY),
        col, thick);
}

// ============================================================
// SNAPLINES
// ============================================================

void DrawSnaplines(ImDrawList* dl, const PlayerGeometry& g, const ImVec2& origin, float width, float height, ImU32 col, float thick, int snaplinePos) {
    ImVec2 start;
    switch (snaplinePos) {
        case 0: start = ImVec2(origin.x + width * 0.5f, origin.y); break;
        case 1: start = ImVec2(origin.x + width * 0.5f, origin.y + height * 0.5f); break;
        default: start = ImVec2(origin.x + width * 0.5f, origin.y + height); break;
    }
    DrawLineOutlined(dl, start, g.feetCenter, col, thick);
}

// ============================================================
// EYE LINE
// ============================================================

void DrawEyeLine(ImDrawList* dl, const PlayerGeometry& g, ImU32 col, float thick, float length) {
    // Origin at eye level (slightly below head center)
    ImVec2 eyeOrigin(g.headCenter.x, g.headCenter.y - g.headRadius * 0.15f);

    // Direction: down-right
    ImVec2 dir(0.7f, 0.3f);
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    dir.x /= len; dir.y /= len;

    ImVec2 eyeEnd(eyeOrigin.x + dir.x * length, eyeOrigin.y + dir.y * length);

    DrawLineOutlined(dl, eyeOrigin, eyeEnd, col, thick);

    // Origin dot
    DrawCircleFilledOutlined(dl, eyeOrigin, 2.5f, col, OUTLINE_DARK_STRONG, 1.f);
}

// ============================================================
// TEXT ELEMENTS
// ============================================================

void DrawPlayerName(ImDrawList* dl, TextStack& stack, const PlayerGeometry& g, const char* name, ImU32 col, bool outline, ImU32 outlineCol, float outlineThick,
                    bool hasHalo, bool hasHat, bool hasHorns, bool hasCrown) {
    // Position above head, accounting for decorative elements
    float topY = g.headTop.y;

    float decorationHeight = 0.f;
    if (hasHalo) decorationHeight = (std::max)(decorationHeight, g.headRadius * 1.25f);
    if (hasHat) decorationHeight = (std::max)(decorationHeight, g.headRadius * 2.0f);
    if (hasHorns) decorationHeight = (std::max)(decorationHeight, g.headRadius * 1.9f);
    if (hasCrown) decorationHeight = (std::max)(decorationHeight, g.headRadius * 1.7f);
    topY -= decorationHeight + ImGui::CalcTextSize(name).y + 6.f;

    float oldY = stack.y;
    stack.y = topY;
    stack.DrawTextCentered(dl, name, col, outline, outlineCol, outlineThick);
    stack.y = oldY;
}

void DrawWeaponName(ImDrawList* dl, TextStack& stack, const char* name, ImU32 col, bool outline, ImU32 outlineCol, float outlineThick) {
    stack.DrawTextCentered(dl, name, col, outline, outlineCol, outlineThick);
}

void DrawAmmo(ImDrawList* dl, TextStack& stack, const char* label, float fraction, ImU32 col, ImU32 bgCol, bool outline, ImU32 outlineCol, float outlineThick) {
    stack.DrawBar(dl, label, fraction, bgCol, col, 80.f, 6.f, outline, outlineCol, outlineThick);
}

void DrawDistance(ImDrawList* dl, TextStack& stack, const char* text, ImU32 col, bool outline, ImU32 outlineCol, float outlineThick) {
    stack.DrawTextCentered(dl, text, col, outline, outlineCol, outlineThick);
}

// ============================================================
// MAIN DRAW FUNCTION
// ============================================================

void DrawPreview(ImDrawList* dl, const ImVec2& origin, float width, float height,
                 const IConfigAdapter& adapter,
                 const char* previewLabel) {

    const PreviewConfig& cfg = adapter.GetPreviewConfig();
    const ImGuiID previewId = ImGui::GetCurrentWindow()->GetID(
        previewLabel && *previewLabel ? previewLabel : "##esp_preview_motion");
    const float espAlpha = SmoothFeature(previewId, "esp", cfg.esp_enabled);

    // Build unified geometry
    PlayerGeometry g = BuildGeometry(origin, width, height);

    // Resolve colors
    ImU32 boxCol = adapter.GetBoxColor(cfg.rgb_mode, cfg.visibility_colors, cfg.visible_check);
    const bool sharedBoxColor = cfg.rgb_mode || (cfg.visibility_colors && cfg.visible_check);
    const ImU32 fullBoxCol = SmoothColor(previewId, "box_color", sharedBoxColor ? boxCol : ToImU32(cfg.col_box),
        espAlpha * SmoothFeature(previewId, "box", cfg.box));
    const ImU32 cornerBoxCol = SmoothColor(previewId, "corner_color", sharedBoxColor ? boxCol : ToImU32(cfg.col_box_corner),
        espAlpha * SmoothFeature(previewId, "corner", cfg.box_corner));
    ImU32 skeletonCol = SmoothColor(previewId, "skeleton_color", ToImU32(cfg.col_skeleton),
        espAlpha * SmoothFeature(previewId, "skeleton", cfg.skeleton));
    ImU32 jointsCol = SmoothColor(previewId, "joints_color", ToImU32(cfg.col_joints),
        espAlpha * SmoothFeature(previewId, "joints", cfg.skeleton_joints && cfg.skeleton));
    ImU32 headCol = SmoothColor(previewId, "head_color", ToImU32(cfg.col_head),
        espAlpha * SmoothFeature(previewId, "head", cfg.head_dot));
    ImU32 haloCol = SmoothColor(previewId, "halo_color", ToImU32(cfg.col_halo),
        espAlpha * SmoothFeature(previewId, "halo", cfg.head_halo));
    ImU32 healthCol = SmoothColor(previewId, "health_color", ToImU32(cfg.col_health),
        espAlpha * SmoothFeature(previewId, "health", cfg.health_bar));
    ImU32 armorCol = SmoothColor(previewId, "armor_color", ToImU32(cfg.col_armor),
        espAlpha * SmoothFeature(previewId, "armor", cfg.armor_bar));
    ImU32 nameCol = SmoothColor(previewId, "name_color", ToImU32(cfg.col_name),
        espAlpha * SmoothFeature(previewId, "name", cfg.name));
    ImU32 distCol = SmoothColor(previewId, "distance_color", ToImU32(cfg.col_distance),
        espAlpha * SmoothFeature(previewId, "distance", cfg.distance));
    const bool anyWeapon = cfg.weapon_name || cfg.weapon_ammo;
    ImU32 weaponCol = SmoothColor(previewId, "weapon_color", ToImU32(cfg.col_weapon),
        espAlpha * SmoothFeature(previewId, "weapon", anyWeapon));
    ImU32 snapCol = SmoothColor(previewId, "snap_color", ToImU32(cfg.col_snaplines),
        espAlpha * SmoothFeature(previewId, "snap", cfg.snaplines));
    ImU32 trailCol = SmoothColor(previewId, "trail_color", ToImU32(cfg.col_trail),
        espAlpha * SmoothFeature(previewId, "trail", cfg.trails));
    const bool anyFun = cfg.angel_wings || cfg.chinese_hat || cfg.devil_horns || cfg.floating_crown;
    ImU32 funCol = SmoothColor(previewId, "fun_color", ToImU32(cfg.col_fun_effects),
        espAlpha * SmoothFeature(previewId, "fun", anyFun));
    ImU32 lookCol = SmoothColor(previewId, "look_color", ToImU32(cfg.col_look),
        espAlpha * SmoothFeature(previewId, "look", cfg.look_direction));
    ImU32 outlineTextCol = SmoothColor(previewId, "outline_color", ToImU32(cfg.col_text_outline), espAlpha);

    // Text stack for bottom elements
    TextStack stack(g.textStackY, g.boundsCenter.x);

    // ===== LAYER ORDER =====

    // Background must be first; the old order painted over both effects.
    DrawBackground(dl, origin, width, height);
    dl->PushClipRect(origin, ImVec2(origin.x + width, origin.y + height), true);
    if ((trailCol >> 24) != 0) DrawTrails(dl, g, trailCol);
    if ((funCol >> 24) != 0 && cfg.angel_wings) DrawAngelWings(dl, g, funCol);
    DrawCharacterTexture(dl, origin, width, height);

    // 4. Boxes
    const float previewBoxThickness = (std::max)(cfg.box_thickness, 2.f);
    if ((fullBoxCol >> 24) != 0) DrawBox2D(dl, g, fullBoxCol, previewBoxThickness, cfg.box_rounding,
        cfg.box_fill, SmoothColor(previewId, "box_fill_color", ToImU32(cfg.col_box_fill),
        espAlpha * SmoothFeature(previewId, "box_fill", cfg.box && cfg.box_fill)));
    if ((cornerBoxCol >> 24) != 0) DrawCornerBox(dl, g, cornerBoxCol, previewBoxThickness);

    // 5. Skeleton
    if ((skeletonCol >> 24) != 0) DrawSkeleton(dl, g, skeletonCol, cfg.skeleton_thickness);

    // 6. Joints
    if ((jointsCol >> 24) != 0) DrawJoints(dl, g, jointsCol);

    // 7. Health/Armor bars (preview fractions: 75% / 65%)
    if ((healthCol >> 24) != 0) DrawHealthBar(dl, g, healthCol, 0.75f, cfg.health_value);
    if ((armorCol >> 24) != 0) DrawArmorBar(dl, g, armorCol, 0.65f, cfg.armor_value);

    // 8. Head circle
    if ((headCol >> 24) != 0) DrawHeadCircle(dl, g, headCol, cfg.head_circle_thickness);

    // 9. Decorative head elements
    if ((haloCol >> 24) != 0) DrawHalo(dl, g, haloCol);
    if (cfg.chinese_hat) DrawChineseHat(dl, g, funCol, cfg.chinese_hat_scale);
    if (cfg.devil_horns) DrawDemonHorns(dl, g, funCol);
    if (cfg.floating_crown) DrawFloatingCrown(dl, g, funCol);

    // 10. Snaplines
    if ((snapCol >> 24) != 0) DrawSnaplines(dl, g, origin, width, height, snapCol, cfg.snapline_thickness, cfg.snapline_position);

    // 11. Eye line
    if ((lookCol >> 24) != 0) DrawEyeLine(dl, g, lookCol, cfg.eye_line_thickness, cfg.look_direction_length);

    // 12. Text elements (stacked at bottom)
    if ((nameCol >> 24) != 0) DrawPlayerName(dl, stack, g, "Enemy", nameCol, cfg.text_outline, outlineTextCol, cfg.text_outline_thickness,
                                 cfg.head_halo, cfg.chinese_hat, cfg.devil_horns, cfg.floating_crown);
    if (cfg.weapon_name && (weaponCol >> 24) != 0) DrawWeaponName(dl, stack, "AK-47", weaponCol, cfg.text_outline, outlineTextCol, cfg.text_outline_thickness);
    if (cfg.weapon_ammo && (weaponCol >> 24) != 0) DrawAmmo(dl, stack, "24 / 30", 0.8f, weaponCol, IM_COL32(18, 18, 18, 235), cfg.text_outline, outlineTextCol, cfg.text_outline_thickness);
    if ((distCol >> 24) != 0) DrawDistance(dl, stack, "18m", distCol, cfg.text_outline, outlineTextCol, cfg.text_outline_thickness);

    // Preview label
    if (previewLabel && *previewLabel)
        dl->AddText(ImVec2(origin.x + 12.f, origin.y + 12.f), IM_COL32(215, 215, 210, 210), previewLabel);
    dl->PopClipRect();

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace EspPreview
