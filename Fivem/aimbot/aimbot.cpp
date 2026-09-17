#pragma warning(disable: 4100 4244 4505)
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "aimbot.h"

#ifdef UI_PREVIEW

namespace aimbot {
    Config config;
    TargetInfo current_target;
    void Initialize() {}
    void Run() {}
    void DrawFOV() {}
    int HitboxToBoneIndex(Hitbox hb) { return static_cast<int>(hb); }
    bool FindBestTarget(TargetInfo& out, float, float) { out = {}; return false; }
    void HumanizedMove(float, float) {}
    void ResetTrackingState() {}
    void ApplyLegitProfile() {
        config.smooth_x = 18.f; config.fov_size = 45.f; config.humanize = true;
    }
    void ApplyRageProfile() {
        config.smooth_x = 0.f; config.fov_size = 180.f; config.humanize = false;
    }
}

#else

#include "aim_type.h"
#include "makcu/makcu_wrapper.h"
#include "../esp/esp.h"
#include "../playerInfo/PedData.h"
#include "../friends/friends.h"
#include "../game/esp_manager.h"
#include "../game/offsets.h"
#include "../game/visibility.h"
#include "math/math.h"
#include "config/app_settings.h"
#include "../../ImGui/imgui.h"
#include <Memory/Memory.h>
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <random>
#include <chrono>
#include <cstdio>
#include <cstdint>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include "gameplay/unified_aim.h"
#include <memory>

namespace aimbot {

    Config config;
    TargetInfo current_target;

    static bool tracking_active = false;
    static uintptr_t locked_target = 0;
    static std::chrono::steady_clock::time_point lock_start{};
    static std::chrono::steady_clock::time_point last_trigger{};
    static bool beeped_for_lock = false;
    static std::mt19937 rng{ std::random_device{}() };

    // Unified aimbot instance (stub for Publish builds)
    static std::unique_ptr<Gameplay::UnifiedAim::UnifiedAimbot> g_unified_aimbot;

    // Held-aim controller state.
    //
    // IMPORTANT: this controller intentionally has NO movement momentum and
    // NO accumulating integral term. Every command is calculated from the
    // fresh screen-space error from the current frame. This prevents the
    // "hold RMB -> oscillation gets faster and faster" feedback loop.
    static float previous_error_x = 0.f;
    static float previous_error_y = 0.f;
    static bool have_previous_error = false;
    static float adaptive_gain_scale = 1.f;
    static int reversal_burst = 0;
    static std::chrono::steady_clock::time_point last_reversal{};
    static std::chrono::steady_clock::time_point last_aim_move{};

    // Pose history is used only for sanity checking CURRENT world reads.
    // We never aim using pose_filter_screen/world when a fresh read fails.
    static uintptr_t pose_filter_ped = 0;
    static Vec2 pose_filter_screen{};
    static Vec3 pose_filter_world{};
    static bool pose_filter_valid = false;
    static std::chrono::steady_clock::time_point pose_filter_time{};

    static void ResetAimMotionState() {
        previous_error_x = 0.f;
        previous_error_y = 0.f;
        have_previous_error = false;
        adaptive_gain_scale = 1.f;
        reversal_burst = 0;
        last_reversal = {};
        last_aim_move = {};
    }

    static void ResetPoseFilter() {
        pose_filter_ped = 0;
        pose_filter_screen = {};
        pose_filter_world = {};
        pose_filter_valid = false;
        pose_filter_time = {};
    }

    static void ResetAimStabilityState() {
        ResetAimMotionState();
        ResetPoseFilter();
    }

    static bool IsFiniteVec3(const Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
               fabsf(v.x) < 100000.f && fabsf(v.y) < 100000.f && fabsf(v.z) < 100000.f;
    }

    static bool IsFiniteVec2(const Vec2& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) &&
               fabsf(v.x) < 100000.f && fabsf(v.y) < 100000.f;
    }

    void Initialize() {
        tracking_active = false;
        locked_target = 0;
        current_target = {};
        beeped_for_lock = false;
        ResetAimStabilityState();
    }

    void ResetTrackingState() {
        tracking_active = false;
        locked_target = 0;
        current_target = {};
        beeped_for_lock = false;
        ResetAimStabilityState();
    }

    int HitboxToBoneIndex(Hitbox hb) {
        switch (hb) {
        case Hitbox::Head:   return 0;
        case Hitbox::Neck:   return 7;
        case Hitbox::Torso:  return 8; // torso is refined via neck/hip interpolation below
        case Hitbox::Pelvis: return 8;
        case Hitbox::Legs:   return 1;
        default:             return 0;
        }
    }

    // Read the bone matrix once per hitbox resolve. The old code re-read the
    // matrix for every bone (head/neck/hip), so a frame could mix several DMA
    // snapshots. That is especially visible on distant players as aim jitter.
    static Vec3 GetBoneWorldWithMatrix(uintptr_t ped, const Matrix& boneMatrix, int boneIndex) {
        using namespace FiveM;
        if (!ped)
            return {};

        Vector3 localOff = mem.Read<Vector3>(
            ped + esp::BONE_ARRAY_BASE +
            esp::BONE_SIZE * static_cast<uintptr_t>(boneIndex));

        if (!std::isfinite(localOff.x) || !std::isfinite(localOff.y) || !std::isfinite(localOff.z))
            return {};

        // Local bone offsets should be model-sized, not hundreds/thousands of units.
        if (fabsf(localOff.x) > 20.f || fabsf(localOff.y) > 20.f || fabsf(localOff.z) > 20.f)
            return {};

        DirectX::SimpleMath::Vector3 v(localOff.x, localOff.y, localOff.z);
        DirectX::SimpleMath::Vector3 w = DirectX::XMVector3Transform(v, boneMatrix);
        Vec3 result(w.x, w.y, w.z);

        if (!IsFiniteVec3(result))
            return {};

        return result;
    }

    static Vec3 ResolveHitboxWorld(uintptr_t ped, Hitbox hb) {
        using namespace FiveM;
        if (!ped)
            return {};

        // Prefer the frame's scatter-batched skeleton: matrix + bone offsets were
        // captured together, so this avoids mixing DMA snapshots. Fall back to a
        // single live matrix snapshot only when the prepared frame is unavailable.
        Matrix boneMatrix{};
        bool haveBoneMatrix = false;

        auto bone = [&](int index) -> Vec3 {
            Vec3 prepared{};
            if (esp::try_get_prepared_bone_position(ped, index, prepared) &&
                !prepared.IsZero() &&
                IsFiniteVec3(prepared)) {
                return prepared;
            }

            if (!haveBoneMatrix) {
                boneMatrix = mem.Read<Matrix>(ped + esp::BONE_MATRIX_OFFSET);
                haveBoneMatrix = true;
            }

            return GetBoneWorldWithMatrix(ped, boneMatrix, index);
        };

        switch (hb) {
        case Hitbox::Head: {
            const Vec3 head = bone(0);
            if (!head.IsZero())
                return head;
            return bone(7);
        }

        case Hitbox::Neck: {
            const Vec3 neck = bone(7);
            if (!neck.IsZero())
                return neck;
            return bone(0);
        }

        case Hitbox::Torso: {
            const Vec3 neck = bone(7);
            const Vec3 hip = bone(8);

            if (!neck.IsZero() && !hip.IsZero()) {
                return Vec3(
                    neck.x * 0.55f + hip.x * 0.45f,
                    neck.y * 0.55f + hip.y * 0.45f,
                    neck.z * 0.55f + hip.z * 0.45f);
            }

            return neck.IsZero() ? hip : neck;
        }

        case Hitbox::Pelvis: {
            const Vec3 hip = bone(8);
            if (!hip.IsZero())
                return hip;
            return bone(7);
        }

        case Hitbox::Legs: {
            const Vec3 leftFoot = bone(1);
            const Vec3 rightFoot = bone(2);

            if (!leftFoot.IsZero() && !rightFoot.IsZero()) {
                return Vec3(
                    (leftFoot.x + rightFoot.x) * 0.5f,
                    (leftFoot.y + rightFoot.y) * 0.5f,
                    (leftFoot.z + rightFoot.z) * 0.5f);
            }

            if (!leftFoot.IsZero())
                return leftFoot;
            if (!rightFoot.IsZero())
                return rightFoot;

            const Vec3 hip = bone(8);
            if (!hip.IsZero())
                return hip;
            return bone(7);
        }

        default:
            return bone(0);
        }
    }

    static bool IsPedInVehicle(uintptr_t ped) {
        using namespace FiveM;
        if (!ped) return false;
        uintptr_t veh = mem.Read<uintptr_t>(ped + offset::pedVehicle);
        if (veh > 0x10000ULL && veh < 0x7FFFFFFFFFFFULL) {
            uint8_t probe = 0;
            if (mem.Read(veh, &probe, 1))
                return true;
        }
        return false;
    }

    static bool IsVisibleForAim(uintptr_t ped) {
        using namespace FiveM;
        if (!config.visible_check)
            return true;
        // Peds in vehicles treated as visible (occlusion unreliable via DMA only)
        if (IsPedInVehicle(ped))
            return true;
        return FiveM::Visibility::IsPedVisible(ped);
    }

    // Mouse binds: Makcu stream (GAME-PC) preferred; local Windows when stream is
    // dead so single-PC setups still aim. Dual-PC needs the Makcu button stream.
    static bool BindDown(int vk) {
        if (vk <= 0) return false;
        return aim_type::IsDown(vk) || ((GetKeyState(vk) & 0x8000) != 0);
    }

    // FiveM aim hold policy:
    //  - RMB is the safe/default primary aim hold.
    //  - LMB is NEVER allowed to activate/re-activate the aimbot.
    //    This keeps firing completely independent from target tracking.
    //  - A non-LMB secondary bind is still supported (keyboard/side mouse).
    //
    // Runtime filtering is intentional even though the UI/config loader also
    // sanitize the binds: old config files must not be able to make LMB pull.
    static bool AimHoldDown() {
        // Respect user binds. If primary is unset, default to RMB.
        int primary = config.aimbot_bind;
        if (primary <= 0)
            primary = VK_RBUTTON;

        if (BindDown(primary))
            return true;

        const int secondary = config.aimbot_bind2;
        if (secondary > 0 && secondary != primary)
            return BindDown(secondary);

        return false;
    }

    // Independent ped list for aim when ESP::validPeds is empty this frame.
    static std::vector<uintptr_t> s_aimPeds;
    static std::vector<Vec3> s_aimPos;

    static void CollectPedsForAim() {
        using namespace FiveM;
        s_aimPeds.clear();
        s_aimPos.clear();
        // Exclusive consumer of main ESP snapshot — no parallel sequential scanner.
        if (!ESP::validPeds.empty() && ESP::validPeds.size() == ESP::positions.size()) {
            s_aimPeds = ESP::validPeds;
            s_aimPos = ESP::positions;
            return;
        }
        // Wait for a valid acquisition generation instead of hammering DMA.
    }


    bool FindBestTarget(TargetInfo& out, float fov_px, float max_dist) {
        out = {};
        using namespace FiveM;

        // Always resolve localplayer + matrix live if cache is cold
        // (do not depend solely on ESP frame cache — aim must work alone)
        if (!offset::viewport)
            return false;
        if (!offset::localplayer && offset::world) {
            uintptr_t lp = mem.Read<uintptr_t>(offset::world + 0x8);
            if (lp) offset::localplayer = lp;
        }
        if (!offset::localplayer)
            return false;

        // Use a live camera matrix for acquisition too. This keeps the FOV test
        // aligned with the actual crosshair at the moment RMB is pressed.
        Matrix view_matrix =
            mem.Read<Matrix>(offset::viewport + 0x24C);
        Vec3 localPos = ESP::FrameCacheValid()
            ? ESP::GetFrameLocalPos()
            : mem.Read<Vec3>(offset::localplayer + offset::playerPosition);
        if (localPos.IsZero()) {
            localPos = mem.Read<Vec3>(offset::localplayer + 0x90);
            if (localPos.IsZero())
                return false;
        }

        ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x < 1.f || display.y < 1.f) return false;
        float cx = display.x * 0.5f;
        float cy = display.y * 0.5f;

        float bestScore = 1e30f;
        TargetInfo best{};

        const std::vector<uintptr_t>* pedsPtr = &FiveM::ESP::validPeds;
        const std::vector<Vec3>* posPtr = &FiveM::ESP::positions;
        if (pedsPtr->empty()) {
            CollectPedsForAim();
            pedsPtr = &s_aimPeds;
            posPtr = &s_aimPos;
        }
        const auto& peds = *pedsPtr;
        const auto& positions = *posPtr;
        if (peds.empty())
            return false;

        for (size_t i = 0; i < peds.size(); ++i) {
            uintptr_t ped = peds[i];
            if (!ped || ped == offset::localplayer)
                continue;

            if (friends::config.ignore_aim && friends::IsFriendPed(ped))
                continue;

            // GTA/FiveM: alive peds typically 1..200 (some builds ~100–200 full)
            float health = 0.f;
            if (!esp::try_get_prepared_health(ped, health)) {
                // Prefer prepared snapshot; avoid per-target DMA on the hot path.
                continue;
            }
            if (health <= 1.f || health > 500.f)
                continue;

            Vec3 world = ResolveHitboxWorld(ped, config.hitbox);
            const bool haveBone = !world.IsZero() && IsFiniteVec3(world);

            if (!haveBone) {
                // Acquisition may use a CURRENT entity-origin fallback, but never
                // a non-finite/transformed garbage bone.
                if (i < positions.size() && !positions[i].IsZero() &&
                    IsFiniteVec3(positions[i])) {
                    world = positions[i];
                    world.z += (config.hitbox == Hitbox::Head) ? 0.85f : 0.55f;
                } else {
                    Vec3 origin = mem.Read<Vec3>(ped + offset::playerPosition);
                    if (origin.IsZero())
                        origin = mem.Read<Vec3>(ped + 0x90);
                    if (origin.IsZero() || !IsFiniteVec3(origin))
                        continue;
                    world = origin;
                    world.z += (config.hitbox == Hitbox::Head) ? 0.85f : 0.55f;
                }
            } else if (i < positions.size() && !positions[i].IsZero() &&
                       IsFiniteVec3(positions[i])) {
                // Reject a bone that is implausibly far from the entity origin.
                const float boneOffset = positions[i].distance_to(world);
                if (!std::isfinite(boneOffset) || boneOffset > 4.5f)
                    continue;
            }

            if (!IsFiniteVec3(world))
                continue;

            const float dist = localPos.distance_to(world);
            if (!std::isfinite(dist) || dist > max_dist || dist < 0.35f)
                continue;

            if (!IsVisibleForAim(ped))
                continue;

            Vec2 screen;
            if (!world.world_to_screen(view_matrix, screen) || !IsFiniteVec2(screen))
                continue;
            // On-screen soft gate (classic IsOnScreen)
            if (screen.x < -50.f || screen.y < -50.f ||
                screen.x > display.x + 50.f || screen.y > display.y + 50.f)
                continue;

            float dx = screen.x - cx;
            float dy = screen.y - cy;
            float cross = sqrtf(dx * dx + dy * dy);
            if (cross > fov_px)
                continue;

            // Classic: closest to crosshair wins
            float score = config.closest_to_crosshair ? cross : dist;
            if (config.threat_priority)
                score = cross * 1.0f + dist * 0.35f;
            if (score < bestScore) {
                bestScore = score;
                best.ped = ped;
                best.world_pos = world;
                best.screen_pos = screen;
                best.distance = dist;
                best.crosshair_dist = cross;
                best.valid = true;
            }
        }

        out = best;
        return best.valid;
    }

    void HumanizedMove(float dx, float dy) {
        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            ResetAimMotionState();
            return;
        }

        const float targetDistance =
            (current_target.valid && std::isfinite(current_target.distance))
                ? current_target.distance
                : 0.f;
        const float errLen0 = sqrtf(dx * dx + dy * dy);

        // Reject absurd screen deltas (bad W2S / matrix) — causes spin at range.
        if (errLen0 > 900.f || !std::isfinite(errLen0)) {
            ResetAimMotionState();
            return;
        }
        // At long range a head is a few pixels; >350px error is almost always noise.
        if (targetDistance > 70.f && errLen0 > 350.f) {
            ResetAimMotionState();
            return;
        }

        // SNAP PATH: smooth 0-1 must pull hard every frame while hold is down.
        // Cap packets by distance so far targets never get 16×127 bursts (spin).
        if (config.smooth_x <= 1.f) {
            int left_x = static_cast<int>(lroundf(dx));
            int left_y = static_cast<int>(lroundf(dy));
            int maxPkt = 16;
            int stepCap = 127;
            if (targetDistance > 100.f) { maxPkt = 2; stepCap = 12; }
            else if (targetDistance > 70.f) { maxPkt = 3; stepCap = 20; }
            else if (targetDistance > 45.f) { maxPkt = 5; stepCap = 40; }
            else if (targetDistance > 25.f) { maxPkt = 8; stepCap = 64; }
            // Also limit total travel this frame proportional to error.
            const int totalCap = (targetDistance > 70.f)
                ? (int)std::min(errLen0 * 0.25f, 40.f)
                : (int)std::min(errLen0 + 1.f, 200.f);
            int moved = 0;
            for (int n = 0; n < maxPkt && (left_x != 0 || left_y != 0); ++n) {
                if (moved >= totalCap) break;
                int sx = left_x; if (sx > stepCap) sx = stepCap; if (sx < -stepCap) sx = -stepCap;
                int sy = left_y; if (sy > stepCap) sy = stepCap; if (sy < -stepCap) sy = -stepCap;
                if (sx == 0 && sy == 0) break;
                aim_type::Move(sx, sy);
                left_x -= sx;
                left_y -= sy;
                moved += (int)(std::abs(sx) + std::abs(sy));
            }
            return;
        }

        // Closed-loop stable hold controller.
        //
        // The previous version filtered target screen coordinates and also mixed
        // the previous movement command into the next command. While the aimbot
        // itself moves the camera, that creates delayed feedback: the controller
        // keeps correcting where the target WAS, overshoots, reverses, and can
        // build a larger oscillation the longer RMB is held.
        //
        // This version is deliberately proportional-only:
        //   * fresh error only;
        //   * no movement momentum;
        //   * no random jitter while locked;
        //   * no multi-packet burst;
        //   * slower output for distant targets;
        //   * extra damping only when the FRESH error crosses the centre.
        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            ResetAimMotionState();
            return;
        }

        float smooth = config.smooth_x;
        if (smooth < 0.f) smooth = 0.f;
        if (smooth > 100.f) smooth = 100.f;

        float strength = (100.f - smooth) / 100.f;
        if (config.humanize && strength > 0.f && strength < 1.f)
            strength = powf(strength, 0.92f);

        if (strength <= 0.0001f) {
            ResetAimMotionState();
            return;
        }

        const float errX = dx;
        const float errY = dy;
        const float errLen = sqrtf(errX * errX + errY * errY);

        // Far players need a slightly larger dead-zone because a head can be
        // only a handful of screen pixels and animation/DMA noise becomes visible.
        float deadZone = 0.75f;
        if (targetDistance > 120.f) deadZone = 1.80f;
        else if (targetDistance > 80.f) deadZone = 1.40f;
        else if (targetDistance > 45.f) deadZone = 1.00f;

        if (errLen <= deadZone) {
            previous_error_x = errX;
            previous_error_y = errY;
            have_previous_error = true;
            return;
        }

        const auto now = std::chrono::steady_clock::now();

        // Slower command rate at long range. The target still gets read every
        // frame; only physical mouse output is rate-limited.
        int minimumIntervalMs = 8;
        if (targetDistance > 120.f) minimumIntervalMs = 14;
        else if (targetDistance > 80.f) minimumIntervalMs = 12;
        else if (targetDistance > 50.f) minimumIntervalMs = 10;

        if (last_aim_move.time_since_epoch().count() != 0 &&
            now - last_aim_move < std::chrono::milliseconds(minimumIntervalMs)) {
            return;
        }

        // Device-count scale. Screen pixels and injected mouse counts are not
        // 1:1 units, so raw screen error must never be sent directly as a delta.
        float outputScale = 0.36f;
        if (targetDistance > 120.f) outputScale = 0.10f;
        else if (targetDistance > 80.f) outputScale = 0.14f;
        else if (targetDistance > 50.f) outputScale = 0.19f;
        else if (targetDistance > 30.f) outputScale = 0.27f;

        bool reversedX = false;
        bool reversedY = false;

        if (have_previous_error) {
            reversedX =
                errX * previous_error_x < 0.f &&
                fabsf(errX) > 0.75f &&
                fabsf(previous_error_x) > 0.75f;

            reversedY =
                errY * previous_error_y < 0.f &&
                fabsf(errY) > 0.75f &&
                fabsf(previous_error_y) > 0.75f;
        }

        const bool reversed = reversedX || reversedY;

        // Anti-runaway adaptation: repeated centre crossings mean the loop is
        // overshooting. Instead of letting the oscillation build, automatically
        // reduce gain. It slowly recovers after a stable period.
        if (reversed) {
            if (last_reversal.time_since_epoch().count() != 0 &&
                now - last_reversal < std::chrono::milliseconds(180)) {
                ++reversal_burst;
            } else {
                reversal_burst = 1;
            }

            last_reversal = now;

            if (reversal_burst >= 2)
                adaptive_gain_scale = (std::max)(0.30f, adaptive_gain_scale * 0.72f);
        } else {
            if (last_reversal.time_since_epoch().count() == 0 ||
                now - last_reversal > std::chrono::milliseconds(250)) {
                adaptive_gain_scale =
                    (std::min)(1.f, adaptive_gain_scale + 0.015f);
                reversal_burst = 0;
            }
        }

        float gainX = strength * outputScale * adaptive_gain_scale;
        float gainY = strength * outputScale * adaptive_gain_scale;

        // A fresh sign change means the crosshair crossed the target on that
        // axis. Dampen only that axis immediately.
        if (reversedX)
            gainX *= 0.25f;
        if (reversedY)
            gainY *= 0.25f;

        previous_error_x = errX;
        previous_error_y = errY;
        have_previous_error = true;

        float mx = errX * gainX;
        float my = errY * gainY;

        // No random jitter here. "Humanize" is represented by the smooth gain.
        // Random per-frame mouse deltas around a tiny distant head create exactly
        // the wandering behaviour the user is trying to eliminate.

        // Hard radial cap by target distance — tighter than before to stop far spin.
        float maxStep = 18.f;
        if (smooth <= 15.f) maxStep = 22.f;
        if (smooth <= 1.f) maxStep = 28.f;

        if (targetDistance > 120.f) maxStep = (std::min)(maxStep, 2.5f);
        else if (targetDistance > 90.f) maxStep = (std::min)(maxStep, 3.5f);
        else if (targetDistance > 70.f) maxStep = (std::min)(maxStep, 5.f);
        else if (targetDistance > 50.f) maxStep = (std::min)(maxStep, 7.f);
        else if (targetDistance > 30.f) maxStep = (std::min)(maxStep, 11.f);

        // Proportional to error — never dump a large fixed step when already near.
        const float proportionalCap = (std::max)(1.f, errLen * 0.28f);
        maxStep = (std::min)(maxStep, proportionalCap);
        // Extra: far + medium error → almost no movement (noise / fight mouse).
        if (targetDistance > 80.f && errLen > 80.f)
            maxStep = (std::min)(maxStep, 3.f);

        const float moveLen = sqrtf(mx * mx + my * my);
        if (moveLen > maxStep && moveLen > 0.0001f) {
            const float scale = maxStep / moveLen;
            mx *= scale;
            my *= scale;
        }

        int ix = static_cast<int>(lroundf(mx));
        int iy = static_cast<int>(lroundf(my));

        // Do not force one-count motion near the dead-zone. A forced +/-1 every
        // update is enough to make distant targets shake continuously.
        if (ix == 0 && iy == 0)
            return;

        const int hardCap = static_cast<int>((std::min)(maxStep, 127.f));
        ix = std::clamp(ix, -hardCap, hardCap);
        iy = std::clamp(iy, -hardCap, hardCap);

        if (ix == 0 && iy == 0)
            return;

        // Exactly one command for this fresh sample.
        aim_type::Move(ix, iy);
        last_aim_move = now;
    }

    // Refresh the locked target from CURRENT DMA data only.
    // Previous coordinates are allowed to validate/filter a fresh sample, but a
    // failed read never produces mouse movement.
    static bool RefreshTargetPose(TargetInfo& tgt) {
        using namespace FiveM;

        if (!tgt.ped || !offset::viewport || !offset::localplayer)
            return false;

        // Continuous aim must use the newest camera matrix available. The ESP
        // frame cache is perfect for drawing, but it was captured earlier in the
        // frame; feeding that older matrix back into mouse control adds another
        // frame of phase delay and can turn a held aim into an oscillation.
        const Matrix view_matrix =
            mem.Read<Matrix>(offset::viewport + 0x24C);

        float health = 0.f;
        PedData pd{};
        bool havePd = g_pedCacheManager.getPedData(tgt.ped, pd) && pd.isValid;

        if (havePd) {
            health = pd.health;
            const auto age = std::chrono::steady_clock::now() - pd.lastUpdate;
            if (age > std::chrono::milliseconds(80))
                havePd = false;
        }

        // Locked aim only uses a CURRENT bone sample. If this read fails, this
        // frame sends no movement at all.
        Vec3 world = ResolveHitboxWorld(tgt.ped, config.hitbox);
        if (world.IsZero() || !IsFiniteVec3(world))
            return false;

        Vec3 origin{};
        if (!esp::try_get_prepared_origin(tgt.ped, origin) ||
            origin.IsZero() ||
            !IsFiniteVec3(origin)) {

            origin = mem.Read<Vec3>(tgt.ped + offset::playerPosition);
            if (origin.IsZero())
                origin = mem.Read<Vec3>(tgt.ped + 0x90);
        }

        if (origin.IsZero() || !IsFiniteVec3(origin))
            return false;

        // Reject an impossible bone relative to the entity root.
        const float boneOffset = origin.distance_to(world);
        if (!std::isfinite(boneOffset) || boneOffset > 4.5f)
            return false;

        Vec3 localPos = ESP::FrameCacheValid()
            ? ESP::GetFrameLocalPos()
            : mem.Read<Vec3>(offset::localplayer + offset::playerPosition);

        if (localPos.IsZero())
            localPos = mem.Read<Vec3>(offset::localplayer + 0x90);

        if (localPos.IsZero() || !IsFiniteVec3(localPos))
            return false;

        const float targetDistance = localPos.distance_to(world);
        if (!std::isfinite(targetDistance) ||
            targetDistance < 0.35f ||
            targetDistance > config.max_distance + 10.f) {
            return false;
        }

        const auto poseNow = std::chrono::steady_clock::now();

        // Validate world-space continuity. Unlike screen-space smoothing, this
        // does NOT lag behind when the camera is moved by the aimbot or by the
        // user's physical mouse. It only rejects physically impossible bone jumps.
        if (pose_filter_valid && pose_filter_ped == tgt.ped &&
            !pose_filter_world.IsZero() && IsFiniteVec3(pose_filter_world)) {

            float dt = 0.016f;
            if (pose_filter_time.time_since_epoch().count() != 0) {
                dt = std::chrono::duration<float>(poseNow - pose_filter_time).count();
                if (!std::isfinite(dt) || dt < 0.f)
                    dt = 0.016f;
            }

            dt = std::clamp(dt, 0.001f, 0.100f);

            const float worldJump = pose_filter_world.distance_to(world);

            // Generous allowance: animation + running + DMA timing. Anything
            // beyond this in one sample is almost certainly a bad bone/matrix read.
            const float maxWorldJump = 0.85f + 28.f * dt;

            if (!std::isfinite(worldJump) || worldJump > maxWorldJump)
                return false;
        }

        Vec3 vel{};
        if (config.velocity_prediction || !havePd) {
            auto h = mem.CreateScatterHandle();
            float liveHp = health;

            mem.AddScatterReadRequest(
                h,
                tgt.ped + offset::pedVelocity,
                &vel,
                sizeof(Vec3));

            if (!havePd) {
                mem.AddScatterReadRequest(
                    h,
                    tgt.ped + offset::playerHealth,
                    &liveHp,
                    sizeof(float));
            }

            mem.ExecuteReadScatter(h);
            mem.CloseScatterHandle(h);

            if (!havePd)
                health = liveHp;
        }

        if (!std::isfinite(health) || health <= 1.f || health > 500.f)
            return false;

        // Prediction is disabled for distant targets. At range, a tiny noisy
        // velocity value becomes visible as several pixels of aim movement and
        // contributes more instability than useful lead.
        if (config.velocity_prediction &&
            targetDistance < 55.f &&
            !vel.IsZero() &&
            IsFiniteVec3(vel)) {

            const float velLen =
                sqrtf(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);

            if (std::isfinite(velLen) && velLen < 120.f) {
                const float predictSeconds = 0.006f;

                float px = vel.x * predictSeconds;
                float py = vel.y * predictSeconds;
                float pz = vel.z * predictSeconds;

                const float predLen = sqrtf(px * px + py * py + pz * pz);

                if (predLen > 0.12f && predLen > 0.001f) {
                    const float scale = 0.12f / predLen;
                    px *= scale;
                    py *= scale;
                    pz *= scale;
                }

                world.x += px;
                world.y += py;
                world.z += pz;
            }
        }

        Vec2 rawScreen{};
        if (!world.world_to_screen(view_matrix, rawScreen) ||
            !IsFiniteVec2(rawScreen)) {
            return false;
        }

        const ImVec2 d = ImGui::GetIO().DisplaySize;
        if (d.x < 1.f || d.y < 1.f)
            return false;

        const float screenMargin = 300.f;
        if (rawScreen.x < -screenMargin ||
            rawScreen.y < -screenMargin ||
            rawScreen.x > d.x + screenMargin ||
            rawScreen.y > d.y + screenMargin) {
            return false;
        }

        // CRITICAL FIX:
        // Use the fresh screen coordinate directly. Do NOT apply an EMA such as
        // old + (new-old)*alpha here. Screen-space EMA introduces phase delay into
        // a closed loop because our own mouse command changes the camera. At long
        // distance the previous alpha was as low as 0.28, which made the aimbot
        // keep chasing an old head position and oscillate more on every correction.
        const Vec2 screen = rawScreen;

        pose_filter_ped = tgt.ped;
        pose_filter_screen = rawScreen; // validation/debug history only
        pose_filter_world = world;
        pose_filter_valid = true;
        pose_filter_time = poseNow;

        const float cx = d.x * 0.5f;
        const float cy = d.y * 0.5f;

        tgt.world_pos = world;
        tgt.screen_pos = screen;
        tgt.distance = targetDistance;
        tgt.crosshair_dist =
            sqrtf(
                (screen.x - cx) * (screen.x - cx) +
                (screen.y - cy) * (screen.y - cy));
        tgt.valid = true;

        return true;
    }

    static void RunTriggerLogic();

    static void RunAimbotLogic() {
        // Initialize unified aimbot if needed (stub for Publish)
        if (!g_unified_aimbot) {
            g_unified_aimbot = Gameplay::UnifiedAim::CreateAimbotForGame("FiveM");
            Gameplay::UnifiedAim::UnifiedConfig ucfg;
            ucfg.enabled = config.aimbot_enabled;
            ucfg.fov = config.fov_size > 5.f ? config.fov_size : 5.f;
            ucfg.smooth = config.smooth_x;
            g_unified_aimbot->SetConfig(ucfg);
        }

        static int s_poseMiss = 0;

        if (!config.aimbot_enabled) {
            s_poseMiss = 0;
            if (tracking_active || locked_target)
                ResetTrackingState();
            return;
        }

        // Use existing aim logic for actual aiming (unified aimbot is stub in Publish)
        if (!AimHoldDown()) {
            s_poseMiss = 0;
            if (tracking_active || locked_target)
                ResetTrackingState();
            return;
        }

        // Find best target using existing logic
        TargetInfo tgt;
        if (!FindBestTarget(tgt, config.fov_size, config.max_distance)) {
            ++s_poseMiss;
            if (s_poseMiss > 10) {
                if (tracking_active || locked_target)
                    ResetTrackingState();
            }
            return;
        }

        s_poseMiss = 0;

        // Update current target
        current_target = tgt;

        // Calculate mouse movement using existing HumanizedMove
        float dx = tgt.screen_pos.x - ImGui::GetIO().DisplaySize.x * 0.5f;
        float dy = tgt.screen_pos.y - ImGui::GetIO().DisplaySize.y * 0.5f;
        HumanizedMove(dx, dy);

        // Update unified aimbot stub (does nothing in Publish)
        Gameplay::UnifiedAim::AimContext ctx;
        ctx.dt = ImGui::GetIO().DeltaTime;
        g_unified_aimbot->Update(ctx);

        // Draw FOV if enabled
        if (config.aimbot_enabled || config.trigger_enabled || config.crosshair_enabled)
            DrawFOV();

                // Aim/trigger debug overlay removed (UI clean).

    }

    static void RunTriggerLogic() {
        if (!config.trigger_enabled)
            return;

        const bool active = config.trigger_always_on || config.trigger_bind <= 0 ||
            BindDown(config.trigger_bind);
        static uintptr_t triggerTarget = 0;
        static std::chrono::steady_clock::time_point targetEntered{};
        static float targetDelay = 0.f;
        if (!active) {
            triggerTarget = 0;
            return;
        }

        const Hitbox savedHb = config.hitbox;
        TargetInfo tgt{};
        bool found = false;
        if (config.trigger_head_only) {
            config.hitbox = Hitbox::Head;
            found = FindBestTarget(tgt, config.trigger_fov, config.max_distance);
        } else {
            // Body mode is independent of the configured aimbot bone. Check
            // every major anatomical region and keep the one actually closest
            // to the crosshair.
            constexpr Hitbox hitboxes[] = {
                Hitbox::Head, Hitbox::Neck, Hitbox::Torso, Hitbox::Pelvis, Hitbox::Legs
            };
            for (const Hitbox hitbox : hitboxes) {
                config.hitbox = hitbox;
                TargetInfo candidate{};
                if (!FindBestTarget(candidate, config.trigger_fov, config.max_distance))
                    continue;
                if (!found || candidate.crosshair_dist < tgt.crosshair_dist) {
                    tgt = candidate;
                    found = true;
                }
            }
        }
        config.hitbox = savedHb;
        if (!found) {
            triggerTarget = 0;
            return;
        }
        // Share the trigger target with visual feedback such as the 0.5 s hit
        // marker, including when triggerbot is used without aim assist.
        current_target = tgt;

        auto now = std::chrono::steady_clock::now();
        if (tgt.ped != triggerTarget) {
            triggerTarget = tgt.ped;
            targetEntered = now;
            targetDelay = (std::max)(config.trigger_delay, 0.f);
            if (config.trigger_random_extra > 0.f) {
                std::uniform_real_distribution<float> randomDelay(0.f, config.trigger_random_extra);
                targetDelay += randomDelay(rng);
            }
        }
        const float reactionElapsed = std::chrono::duration<float>(now - targetEntered).count();
        const float shotElapsed = std::chrono::duration<float>(now - last_trigger).count();
        if (reactionElapsed < targetDelay || shotElapsed < .06f)
            return;

        last_trigger = now;
        aim_type::LeftClick();
    }

    void DrawFOV() {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;

        ImVec2 display = ImGui::GetIO().DisplaySize;
        ImVec2 center(display.x * 0.5f, display.y * 0.5f);

        auto draw_style = [&](float radius, ImU32 col, FovStyle style) {
            switch (style) {
            case FovStyle::Square: {
                float s = radius;
                dl->AddRect(ImVec2(center.x - s, center.y - s), ImVec2(center.x + s, center.y + s), col, 0.f, 0, 1.5f);
                break;
            }
            case FovStyle::Cross:
                dl->AddLine(ImVec2(center.x - radius, center.y), ImVec2(center.x + radius, center.y), col, 1.2f);
                dl->AddLine(ImVec2(center.x, center.y - radius), ImVec2(center.x, center.y + radius), col, 1.2f);
                break;
            default:
                dl->AddCircle(center, radius, col, 40, 1.5f);
                break;
            }
        };

        if (config.aimbot_enabled && config.show_fov) {
            ImU32 col = config.fov_color;
            if (config.fov_rgb) {
                float t = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.4f, 1.f);
                col = IM_COL32(
                    (int)(sinf(t * 6.28f) * 127 + 128),
                    (int)(sinf(t * 6.28f + 2.09f) * 127 + 128),
                    (int)(sinf(t * 6.28f + 4.18f) * 127 + 128),
                    180);
            }
            draw_style(config.fov_size, col, config.fov_style);
        }

        if (config.trigger_enabled && config.trigger_show_fov) {
            dl->AddCircle(center, config.trigger_fov, IM_COL32(255, 180, 50, 160), 32, 1.2f);
        }

        if (config.crosshair_enabled) {
            float s = config.crosshair_size;
            float g = config.crosshair_gap;
            ImU32 col = config.crosshair_color;
            dl->AddLine(ImVec2(center.x - s - g, center.y), ImVec2(center.x - g, center.y), col, 1.5f);
            dl->AddLine(ImVec2(center.x + g, center.y), ImVec2(center.x + s + g, center.y), col, 1.5f);
            dl->AddLine(ImVec2(center.x, center.y - s - g), ImVec2(center.x, center.y - g), col, 1.5f);
            dl->AddLine(ImVec2(center.x, center.y + g), ImVec2(center.x, center.y + s + g), col, 1.5f);
        }
    }

    void ApplyLegitProfile() {
        config.smooth_x = 35.f;
        config.smooth_y = 35.f;
        config.fov_size = 45.f;
        config.reaction_time = 0.08f;
        config.humanize = true;
        config.jitter_amount = 0.4f;
        config.silent_legit = false; // silent removed
        config.silent_rage = false;
        config.trigger_delay = 0.08f;
    }

    void ApplyRageProfile() {
        config.smooth_x = 0.f;
        config.smooth_y = 0.f;
        config.fov_size = 180.f;
        config.reaction_time = 0.f;
        config.humanize = false;
        config.silent_rage = false; // silent removed
        config.silent_legit = false;
        config.trigger_delay = 0.02f;
    }

    void Run() {
        config.silent_enabled = false; // hard-disable legacy silent
        RunAimbotLogic();
        RunTriggerLogic();

        if (config.aimbot_enabled || config.trigger_enabled || config.crosshair_enabled)
            DrawFOV();

                // Aim/trigger debug overlay removed (UI clean).

    }

} // namespace aimbot

#endif // UI_PREVIEW
