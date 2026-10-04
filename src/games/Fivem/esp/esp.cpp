// Forward declarations for motion visual sub-functions
static void DrawTrails(uintptr_t ped, Matrix viewport, const Vec3& origin, const double now, const bool visible, const bool veryFar, ImDrawList* draw);
static void DrawHeadHalo(uintptr_t ped, Matrix viewport, const Vec3& head, const bool visible, ImDrawList* draw);
static void DrawChineseHat(uintptr_t ped, Matrix viewport, const Vec3& head, const float hatScale, ImDrawList* draw);
static void DrawAngelWings(uintptr_t ped, Matrix viewport, const Vec3& origin, const Vec3& head, const Vec3& forward, const Vec3& side, const float fxScale, const double now, ImDrawList* draw);
static void DrawDevilHorns(uintptr_t ped, Matrix viewport, const Vec3& head, const Vec3& forward, const Vec3& side, const float fxScale, ImDrawList* draw);
static void DrawFloatingCrown(uintptr_t ped, Matrix viewport, const Vec3& head, const Vec3& forward, const Vec3& side, const float fxScale, const double now, ImDrawList* draw);
static void DrawEyeLine(uintptr_t ped, Matrix viewport, const Vec3& head, const esp::BatchSkeletonData* skeleton, const float line_length, const float eye_line_thickness, ImDrawList* draw);

static void DrawMotionVisuals(uintptr_t ped, Matrix viewport, const PedData* cached) {
    const auto& cfg = esp::config;
    if (EspPedIsDead(ped) && !cfg.show_dead)
        return;
    if (!cfg.trails && !cfg.head_halo && !cfg.look_direction && !cfg.chinese_hat &&
        !cfg.angel_wings && !cfg.devil_horns && !cfg.floating_crown)
        return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw) return;

    const esp::BatchSkeletonData* skeleton = FindPreparedSkeleton(ped);
    const PreparedEspData* prepared = FindPreparedEsp(ped);
    Vec3 origin = skeleton ? skeleton->origin : Vec3{};
    if (origin.IsZero() && prepared) origin = prepared->origin;
    if (origin.IsZero() && cached) origin = cached->position_origin;
    if (origin.IsZero()) return;

    // P1-5: Frustum culling - skip all motion visuals if origin is off-screen
    Vec2 originScreen{};
    if (!origin.world_to_screen(viewport, originScreen)) return;
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    const float margin = 100.f; // Allow some margin for effects that extend beyond origin
    if (originScreen.x < -margin || originScreen.x > displaySize.x + margin ||
        originScreen.y < -margin || originScreen.y > displaySize.y + margin) {
        return;
    }

    // Distance LOD: heavy ornaments are pure GPU+CPU cost at range.
    float motionDist = 0.f;
    if (FiveM::ESP::FrameCacheValid()) {
        const Vec3& lp = FiveM::ESP::GetFrameLocalPos();
        if (!lp.IsZero()) motionDist = origin.distance_to(lp);
    }
    const bool farOrnament = motionDist > 70.f;
    const bool veryFar = motionDist > 120.f;

    const bool visible = EspPedVisible(ped);
    const double now = ImGui::GetTime();

    // --- Trails ---
    if (cfg.trails) DrawTrails(ped, viewport, origin, now, visible, veryFar, draw);

    // --- Head ornaments (screen-projected world polylines; scale with head) ---
    const float fxScale = std::clamp(cfg.fun_effects_scale, .5f, 2.5f);
    const float hatScale = std::clamp(cfg.chinese_hat_scale, 0.4f, 2.5f);
    Vec3 forward(1.f, 0.f, 0.f);
    const esp::BatchSkeletonData* skeleton = FindPreparedSkeleton(ped);
    if (skeleton) {
        forward = Vec3(skeleton->bone_matrix._21, skeleton->bone_matrix._22, 0.f);
        const float len = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        if (len > .001f) { forward.x /= len; forward.y /= len; }
        else forward = Vec3(1.f, 0.f, 0.f);
    }
    const Vec3 side(-forward.y, forward.x, 0.f);

    Vec3 head = skeleton ? PreparedBonePosition(skeleton, 0) : Vec3{};
    if (!BoneLooksValid(head, origin)) {
        head = origin;
        head.z += 0.92f;
    }

    // --- Head ornaments (screen-projected world polylines; scale with head) ---
    if (cfg.head_halo) DrawHeadHalo(ped, viewport, head, visible, draw);
    if (cfg.chinese_hat && !farOrnament) DrawChineseHat(ped, viewport, head, std::clamp(cfg.chinese_hat_scale, 0.4f, 2.5f), draw);
    if (cfg.angel_wings && !farOrnament) DrawAngelWings(ped, viewport, origin, head, forward, side, std::clamp(cfg.fun_effects_scale, .5f, 2.5f), now, draw);
    if (cfg.devil_horns && !farOrnament) DrawDevilHorns(ped, viewport, head, forward, side, std::clamp(cfg.fun_effects_scale, .5f, 2.5f), draw);
    if (cfg.floating_crown && !farOrnament) DrawFloatingCrown(ped, viewport, head, forward, side, std::clamp(cfg.fun_effects_scale, .5f, 2.5f), now, draw);
    if (cfg.look_direction && skeleton) DrawEyeLine(ped, viewport, head, skeleton, std::clamp(cfg.look_direction_length, 0.5f, 5.f), std::clamp(cfg.eye_line_thickness, 1.f, 6.f), draw);
}

// --- Sub-functions for each motion visual effect ---

static void DrawTrails(uintptr_t ped, Matrix viewport, const Vec3& origin, const double now, const bool visible, const bool veryFar, ImDrawList* draw) {
    const auto& cfg = esp::config;
    if (!cfg.trails || veryFar) return;

    struct TrailEntry {
        OmniGhost::Gameplay::FixedTrailHistory<128> history;
        uint64_t entry_generation = 0; // P0-1: detects ped pointer reuse within session
    };
    static std::unordered_map<uintptr_t, TrailEntry> trails;
    static uint64_t trailsSessionGen = 0;
    const uint64_t currentSessionGen = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);
    if (trailsSessionGen != currentSessionGen) {
        trails.clear();
        trailsSessionGen = currentSessionGen;
    }
    static int cleanup_frame = -1;

    // P0-1: Get current ped generation from cache to detect pointer reuse
    uint64_t pedGeneration = 0;
    {
        FiveM::PedData pedData;
        if (FiveM::g_pedCacheManager.getPedData(ped, pedData)) {
            pedGeneration = pedData.entry_generation;
        }
    }

    auto& entry = trails[ped];
    // P0-1: If ped generation changed (pointer reused), clear stale trail
    if (entry.entry_generation != pedGeneration) {
        entry.history.Clear();
        entry.entry_generation = pedGeneration;
    }
    entry.history.Push(origin.x, origin.y, origin.z + 0.04f, now, 0.10f, 0.035);
    const double duration = std::clamp(static_cast<double>(esp::config.trail_duration), 0.20, 2.50);
    const ImU32 base = EspPedColor(ped, esp::config.color_trail, visible);
    for (std::size_t i = 1; i < entry.history.Size(); ++i) {
        const auto& a = entry.history.At(i - 1);
        const auto& b = entry.history.At(i);
        const double age = now - b.time;
        if (age < 0.0 || age > duration) continue;
        Vec2 sa{}, sb{};
        if (!Vec3(a.x, a.y, a.z).world_to_screen(viewport, sa) ||
            !Vec3(b.x, b.y, b.z).world_to_screen(viewport, sb))
            continue;
        const float fade = static_cast<float>(1.0 - age / duration);
        const ImU32 col = esp::config.rainbow_trails
            ? OmniGhost::Gameplay::EspFx::RainbowFade(static_cast<float>(b.time) * 0.35f, fade > 0.f ? 1.f - fade : 1.f)
            : MultiplyAlpha(base, fade * fade);
        draw->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y),
            col, std::clamp(esp::config.trail_thickness, 1.f, 8.f));
    }

    const int frame = ImGui::GetFrameCount();
    if (frame != cleanup_frame && (frame % 120) == 0) {
        cleanup_frame = frame;
        for (auto it = trails.begin(); it != trails.end();) {
            // P0-1: Check if ped still exists in cache with same generation
            bool pedValid = false;
            FiveM::PedData pedData;
            if (FiveM::g_pedCacheManager.getPedData(it->first, pedData)) {
                pedValid = (pedData.entry_generation == it->second.entry_generation);
            }
            if (!pedValid || it->second.history.Stale(now, 3.0)) {
                it = trails.erase(it);
            } else {
                ++it;
            }
        }
    }
}

static void DrawHeadHalo(uintptr_t ped, Matrix viewport, const Vec3& head, const bool visible, ImDrawList* draw) {
    const auto& cfg = esp::config;
    if (!cfg.head_halo) return;

    const ImU32 color = EspPedColor(ped, cfg.color_halo, visible);
    const ImU32 glow = MultiplyAlpha(color, 0.35f);
    constexpr int segments = 32;
    const float rx = 0.22f;
    const float ry = 0.11f;
    const float z = head.z + 0.16f;
    Vec2 prev{}, first{};
    bool prev_ok = false, first_ok = false;
    for (int i = 0; i <= segments; ++i) {
        const float a = static_cast<float>(i) * 6.28318530718f / segments;
        Vec3 point(
            head.x + std::cos(a) * rx,
            head.y + std::sin(a) * ry,
            z);
        Vec2 screen{};
        const bool ok = point.world_to_screen(viewport, screen);
        if (ok && prev_ok) {
            draw->AddLine(ImVec2(prev.x, prev.y), ImVec2(screen.x, screen.y), color, 2.0f);
            Vec3 gout(
                head.x + std::cos(a) * rx * 1.18f,
                head.y + std::sin(a) * ry * 1.18f,
                z);
            Vec2 gs{};
            if (gout.world_to_screen(viewport, gs) && i > 0) {
                // approximate glow by second ring segment only when both ok
            }
        }
        if (i == 0) { first = screen; first_ok = ok; }
        prev = screen;
        prev_ok = ok;
    }
    // Dedicated outer glow ring
    prev_ok = false;
    for (int i = 0; i <= segments; ++i) {
        const float a = static_cast<float>(i) * 6.28318530718f / segments;
        Vec3 point(
            head.x + std::cos(a) * rx * 1.2f,
            head.y + std::sin(a) * ry * 1.2f,
            z);
        Vec2 screen{};
        const bool ok = point.world_to_screen(viewport, screen);
        if (ok && prev_ok)
            draw->AddLine(ImVec2(prev.x, prev.y), ImVec2(screen.x, screen.y), glow, 1.2f);
        prev = screen;
        prev_ok = ok;
    }
}

static void DrawChineseHat(uintptr_t ped, Matrix viewport, const Vec3& head, const float hatScale, ImDrawList* draw) {
    const auto& cfg = esp::config;
    if (!cfg.chinese_hat) return;

    auto project = [&](float wx, float wy, float wz, float& sx, float& sy) -> bool {
        Vec2 screen{};
        if (!Vec3(wx, wy, wz).world_to_screen(viewport, screen)) return false;
        sx = screen.x; sy = screen.y;
        return true;
    };
    OmniGhost::Gameplay::EspFx::DrawChineseHat(
        draw, head.x, head.y, head.z, project, static_cast<float>(ImGui::GetTime()), hatScale, true);
}

static void DrawAngelWings(uintptr_t ped, Matrix viewport, const Vec3& origin, const Vec3& head,
                           const Vec3& forward, const Vec3& side, const float fxScale,
                           const double now, ImDrawList* draw) {
    const auto& cfg = esp::config;
    if (!cfg.angel_wings) return;

    // Stylized wing silhouette from shoulder: top arc + lower tip + inner feathers.
    const float flap = std::sin(static_cast<float>(now) * 3.0f) * 0.06f * fxScale;
    const Vec3 shoulder(
        origin.x - forward.x * 0.06f,
        origin.y - forward.y * 0.06f,
        origin.z + 0.62f);
    for (int wingSide = -1; wingSide <= 1; wingSide += 2) {
        const float s = static_cast<float>(wingSide);
        const Vec3 root(
            shoulder.x + side.x * s * 0.12f * fxScale,
            shoulder.y + side.y * s * 0.12f * fxScale,
            shoulder.z);
        // Upper edge (6 segments)
        Vec3 upper[7]{};
        for (int i = 0; i <= 6; ++i) {
            const float t = static_cast<float>(i) / 6.f;
            const float out = (0.15f + t * 0.70f) * fxScale;
            const float back = (0.02f + t * 0.16f) * fxScale;
            const float up = (0.05f + std::sin(t * 1.4f) * 0.38f) * fxScale + flap * (1.f - t * 0.4f);
            upper[i] = Vec3(
                root.x + side.x * s * out - forward.x * back,
                root.y + side.y * s * out - forward.y * back,
                root.z + up);
        }
        // Lower tip path
        Vec3 lower[5]{};
        for (int i = 0; i <= 4; ++i) {
            const float t = static_cast<float>(i) / 4.f;
            const float out = (0.18f + t * 0.55f) * fxScale;
            const float back = (0.04f + t * 0.12f) * fxScale;
            const float up = (0.02f - t * 0.28f) * fxScale + flap * 0.5f;
            lower[i] = Vec3(
                root.x + side.x * s * out - forward.x * back,
                root.y + side.y * s * out - forward.y * back,
                root.z + up);
        }
        auto worldPoly = [&](const Vec3* pts, int n, ImU32 color, float thickness, bool closed) {
            auto worldLine = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
                Vec2 sa{}, sb{};
                if (a.world_to_screen(viewport, sa) && b.world_to_screen(viewport, sb))
                    ImGui::GetForegroundDrawList()->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), color, thickness);
            };
            for (int i = 0; i < n - 1; ++i)
                worldLine(pts[i], pts[i + 1], color, thickness);
            if (closed && n > 2)
                worldLine(pts[n - 1], pts[0], color, thickness);
        };
        auto fxColor = [&](float phase, float alpha = 1.f) -> ImU32 {
            return esp::config.fun_effects_rainbow
                ? OmniGhost::Gameplay::EspFx::Hsv(static_cast<float>(ImGui::GetTime()) * .16f + phase, .88f, 1.f, alpha)
                : MultiplyAlpha(EspPedColor(0, esp::config.color_fun_effects, true), alpha);
        };
        worldPoly(upper, 7, fxColor(wingSide > 0 ? 0.08f : 0.55f), 2.0f, false);
        worldPoly(lower, 5, fxColor(wingSide > 0 ? 0.18f : 0.65f), 1.7f, false);
        auto worldLine = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
            Vec2 sa{}, sb{};
            if (a.world_to_screen(viewport, sa) && b.world_to_screen(viewport, sb))
                ImGui::GetForegroundDrawList()->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), color, thickness);
        };
        worldLine(upper[6], lower[4], fxColor(wingSide > 0 ? 0.22f : 0.70f), 1.6f);
        worldLine(root, upper[0], fxColor(wingSide > 0 ? 0.05f : 0.50f), 1.8f);
        // Inner feathers (4), shorter near body
        for (int f = 0; f < 4; ++f) {
            const float t = 0.2f + f * 0.18f;
            const int ui = std::clamp(static_cast<int>(t * 6.f), 0, 6);
            const Vec3 base = upper[ui];
            const float featherOut = (0.10f + f * 0.04f) * fxScale;
            const float featherDown = (0.14f + f * 0.06f) * fxScale;
            const Vec3 tip(
                base.x + side.x * s * featherOut - forward.x * 0.04f * fxScale,
                base.y + side.y * s * featherOut - forward.y * 0.04f * fxScale,
                base.z - featherDown);
            worldLine(base, tip, fxColor(0.12f * f + (wingSide > 0 ? 0.f : 0.5f), 0.9f), 1.4f);
        }
    }
}

static void DrawDevilHorns(uintptr_t ped, Matrix viewport, const Vec3& head,
                           const Vec3& forward, const Vec3& side, const float fxScale,
                           ImDrawList* draw) {
    const auto& cfg = esp::config;
    if (!cfg.devil_horns) return;

    // Curved demonic horns: 6 segments, thick near base → thin tip.
    for (int hornSide = -1; hornSide <= 1; hornSide += 2) {
        const float s = static_cast<float>(hornSide);
        Vec3 pts[7]{};
        for (int i = 0; i <= 6; ++i) {
            const float t = static_cast<float>(i) / 6.f;
            const float lateral = (0.10f + 0.14f * t * (1.f - t * 0.85f)) * fxScale;
            const float forwardAmt = (0.02f + 0.10f * t * t) * fxScale;
            const float up = (0.04f + 0.28f * t) * fxScale;
            pts[i] = Vec3(
                head.x + side.x * s * lateral + forward.x * forwardAmt,
                head.y + side.y * s * lateral + forward.y * forwardAmt,
                head.z + up);
        }
        auto worldLine = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
            Vec2 sa{}, sb{};
            if (a.world_to_screen(viewport, sa) && b.world_to_screen(viewport, sb))
                ImGui::GetForegroundDrawList()->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), color, thickness);
        };
        auto fxColor = [&](float phase, float alpha = 1.f) -> ImU32 {
            return esp::config.fun_effects_rainbow
                ? OmniGhost::Gameplay::EspFx::Hsv(static_cast<float>(ImGui::GetTime()) * .16f + phase, .88f, 1.f, alpha)
                : MultiplyAlpha(EspPedColor(0, esp::config.color_fun_effects, true), alpha);
        };
        for (int i = 0; i < 6; ++i) {
            const float th = 2.6f - i * 0.28f;
            worldLine(pts[i], pts[i + 1], fxColor(hornSide > 0 ? 0.05f + i * 0.03f : 0.55f + i * 0.03f), th);
        }
    }
}

static void DrawFloatingCrown(uintptr_t ped, Matrix viewport, const Vec3& head,
                              const Vec3& forward, const Vec3& side, const float fxScale,
                              const double now, ImDrawList* draw) {
    const auto& cfg = esp::config;
    if (!cfg.floating_crown) return;

    // True crown silhouette: base + 5 peaks (center tallest).
    const float bob = std::sin(static_cast<float>(now) * 2.2f) * 0.02f;
    const float zBase = head.z + (0.20f + bob) * fxScale;
    const float halfW = 0.16f * fxScale;
    const float baseH = 0.04f * fxScale;
    // Base trapezoid corners (front-ish using forward bias small)
    const Vec3 bl(
        head.x - side.x * halfW - forward.x * 0.02f * fxScale,
        head.y - side.y * halfW - forward.y * 0.02f * fxScale,
        zBase);
    const Vec3 br(
        head.x + side.x * halfW - forward.x * 0.02f * fxScale,
        head.y + side.y * halfW - forward.y * 0.02f * fxScale,
        zBase);
    const Vec3 tl(
        head.x - side.x * halfW * 0.92f - forward.x * 0.02f * fxScale,
        head.y - side.y * halfW * 0.92f - forward.y * 0.02f * fxScale,
        zBase + baseH);
    const Vec3 tr(
        head.x + side.x * halfW * 0.92f - forward.x * 0.02f * fxScale,
        head.y + side.y * halfW * 0.92f - forward.y * 0.02f * fxScale,
        zBase + baseH);
    auto worldLine = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
        Vec2 sa{}, sb{};
        if (a.world_to_screen(viewport, sa) && b.world_to_screen(viewport, sb))
            ImGui::GetForegroundDrawList()->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), color, thickness);
    };
    auto fxColor = [&](float phase, float alpha = 1.f) -> ImU32 {
        return esp::config.fun_effects_rainbow
            ? OmniGhost::Gameplay::EspFx::Hsv(static_cast<float>(now) * .16f + phase, .88f, 1.f, alpha)
            : MultiplyAlpha(EspPedColor(0, esp::config.color_fun_effects, true), alpha);
    };
    worldLine(bl, br, fxColor(0.1f), 2.0f);
    worldLine(bl, tl, fxColor(0.15f), 1.8f);
    worldLine(br, tr, fxColor(0.15f), 1.8f);
    worldLine(tl, tr, fxColor(0.2f), 1.8f);

    // Peak heights: outer low, mid medium, center high
    const float peaksX[5] = { -0.85f, -0.42f, 0.f, 0.42f, 0.85f };
    const float peaksH[5] = { 0.08f, 0.14f, 0.22f, 0.14f, 0.08f };
    Vec3 peakPts[5]{};
    Vec3 basePts[5]{};
    for (int i = 0; i < 5; ++i) {
        const float px = peaksX[i] * halfW * 0.95f;
        basePts[i] = Vec3(
            head.x + side.x * px - forward.x * 0.02f * fxScale,
            head.y + side.y * px - forward.y * 0.02f * fxScale,
            zBase + baseH);
        peakPts[i] = Vec3(
            basePts[i].x,
            basePts[i].y,
            zBase + baseH + peaksH[i] * fxScale);
    }
    // Continuous silhouette: base-left → peaks zigzag → base-right
    worldLine(tl, basePts[0], fxColor(0.25f), 1.7f);
    for (int i = 0; i < 5; ++i) {
        worldLine(basePts[i], peakPts[i], fxColor(0.3f + i * 0.05f), 1.8f);
        if (i < 4)
            worldLine(peakPts[i], basePts[i + 1], fxColor(0.35f + i * 0.05f), 1.8f);
    }
    worldLine(basePts[4], tr, fxColor(0.25f), 1.7f);
}

static void DrawEyeLine(uintptr_t ped, Matrix viewport, const Vec3& head,
                        const esp::BatchSkeletonData* skeleton,
                        const float line_length, const float eye_line_thickness,
                        ImDrawList* draw) {
    if (!skeleton) return;
    Vec3 lookForward(skeleton->bone_matrix._21, skeleton->bone_matrix._22, 0.f);
    const float length = std::sqrt(lookForward.x * lookForward.x + lookForward.y * lookForward.y);
    if (length > 0.001f) {
        lookForward.x /= length;
        lookForward.y /= length;
        Vec3 end(head.x + lookForward.x * line_length,
                 head.y + lookForward.y * line_length,
                 head.z);
        Vec2 from{}, to{};
        if (head.world_to_screen(viewport, from) && end.world_to_screen(viewport, to)) {
            const ImU32 color = EspPedColor(ped, esp::config.color_look_direction, true);
            draw->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), color, std::clamp(eye_line_thickness, 1.f, 6.f));
            draw->AddCircleFilled(ImVec2(to.x, to.y), 2.2f, color, 8);
        }
    }
}