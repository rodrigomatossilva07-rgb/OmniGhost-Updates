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
#include "third_party/ImGui/imgui.h"
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

    static uintptr_t last_beeped_lock_ped = 0;
    static std::chrono::steady_clock::time_point lock_start{};
    static std::chrono::steady_clock::time_point last_trigger{};
    static bool trigger_pressed = false;
    static std::chrono::steady_clock::time_point trigger_release_at{};
    static std::chrono::steady_clock::time_point trigger_burst_started{};
    static std::chrono::steady_clock::time_point trigger_next_allowed{};
    static uintptr_t trigger_target = 0;
    static std::chrono::steady_clock::time_point trigger_target_entered{};
    static float trigger_target_delay = 0.f;
    static bool beeped_for_lock = false;
    static std::chrono::steady_clock::time_point target_locked_at{};
    static std::chrono::steady_clock::time_point last_target_switch{};
    static float effective_fov = 100.f;
    static std::mt19937 rng{ std::random_device{}() };


    static std::unique_ptr<Gameplay::UnifiedAim::UnifiedAimbot> g_unified_aimbot;







    static float previous_error_x = 0.f;
    static float previous_error_y = 0.f;
    static bool have_previous_error = false;
    static float adaptive_gain_scale = 1.f;
    static int reversal_burst = 0;
    static std::chrono::steady_clock::time_point last_reversal{};
    static std::chrono::steady_clock::time_point last_aim_move{};



    static void ResetAimMotionState() {
        previous_error_x = 0.f;
        previous_error_y = 0.f;
        have_previous_error = false;
        adaptive_gain_scale = 1.f;
        reversal_burst = 0;
        last_reversal = {};
        last_aim_move = {};
    }

    static void ResetAimStabilityState() {
        ResetAimMotionState();
    }

    static bool IsFiniteVec3(const Vec3& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z) &&
               fabsf(v.x) < 100000.f && fabsf(v.y) < 100000.f && fabsf(v.z) < 100000.f;
    }

    static bool IsFiniteVec2(const Vec2& v) {
        return std::isfinite(v.x) && std::isfinite(v.y) &&
               fabsf(v.x) < 100000.f && fabsf(v.y) < 100000.f;
    }

    static float WeaponRangeLimit() {
        using Category = WeaponRangeCategory;
        switch (config.weapon_range_category) {
        case Category::Melee:   return config.weapon_range_melee;
        case Category::Pistol:  return config.weapon_range_pistol;
        case Category::Smg:     return config.weapon_range_smg;
        case Category::Shotgun: return config.weapon_range_shotgun;
        case Category::Rifle:   return config.weapon_range_rifle;
        case Category::Sniper:  return config.weapon_range_sniper;
        case Category::Heavy:   return config.weapon_range_heavy;
        default:                return config.weapon_range_custom;
        }
    }

    static float EffectiveMaxDistance(bool forTrigger) {
        float result = (std::max)(1.f, config.max_distance);
        if (config.respect_weapon_range && (!forTrigger || config.trigger_respect_weapon_range))
            result = (std::min)(result, (std::max)(1.f, WeaponRangeLimit()));
        return result;
    }

    static float FovForDistance(float distance) {
        if (config.dynamic_fov_mode == DynamicFovMode::Static)
            return (std::max)(5.f, config.fov_size);
        const float nearDistance = (std::max)(0.f, config.dynamic_fov_near_distance);
        const float farDistance = (std::max)(nearDistance + 1.f, config.dynamic_fov_far_distance);
        float t = std::clamp((distance - nearDistance) / (farDistance - nearDistance), 0.f, 1.f);
        t = std::pow(t, std::clamp(config.dynamic_fov_curve, .2f, 4.f));
        const float lo = (std::min)(config.dynamic_fov_min, config.dynamic_fov_max);
        const float hi = (std::max)(config.dynamic_fov_min, config.dynamic_fov_max);
        return std::clamp(hi + (lo - hi) * t, 5.f, 500.f);
    }

    static void UpdateEffectiveFov(float targetDistance) {
        const float desired = FovForDistance(targetDistance);
        const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.f, .1f);
        const float speed = std::clamp(config.dynamic_fov_transition_speed, 1.f, 30.f);
        const float alpha = 1.f - std::exp(-speed * dt);
        if (!std::isfinite(effective_fov) || effective_fov < 1.f)
            effective_fov = desired;
        else
            effective_fov += (desired - effective_fov) * alpha;
    }

    void Initialize() {
        if (trigger_pressed) aim_type::LeftClickRelease();
        trigger_pressed = false;
        trigger_release_at = {};
        trigger_burst_started = {};
        trigger_next_allowed = {};
        trigger_target = 0;
        trigger_target_entered = {};
        trigger_target_delay = 0.f;
        tracking_active = false;
        locked_target = 0;
        target_locked_at = {};
        last_target_switch = {};
        effective_fov = config.fov_size;
        current_target = {};
        beeped_for_lock = false;
        ResetAimStabilityState();
    }

    void ResetTrackingState() {
        if (trigger_pressed) {
            aim_type::LeftClickRelease();
            trigger_pressed = false;
            trigger_release_at = {};
        }
        trigger_target = 0;
        trigger_target_entered = {};
        trigger_target_delay = 0.f;
        trigger_burst_started = {};
        tracking_active = false;
        locked_target = 0;
        target_locked_at = {};
        current_target = {};
        beeped_for_lock = false;
        last_beeped_lock_ped = 0;
        ResetAimStabilityState();
    }

    int HitboxToBoneIndex(Hitbox hb) {
        switch (hb) {
        case Hitbox::Head:   return 0;
        case Hitbox::Neck:   return 7;
        case Hitbox::Torso:  return 8;
        case Hitbox::Pelvis: return 8;
        case Hitbox::Legs:   return 1;
        default:             return 0;
        }
    }

    static Vec3 ResolveHitboxWorld(uintptr_t ped, Hitbox hb) {
        using namespace FiveM;
        if (!ped)
            return {};

        auto bone = [&](int index) -> Vec3 {
            Vec3 prepared{};
            if (esp::try_get_prepared_bone_position(ped, index, prepared) &&
                !prepared.IsZero() &&
                IsFiniteVec3(prepared)) {
                return prepared;
            }
            return {};
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

    static bool IsVisibleForAim(uintptr_t ped) {
        using namespace FiveM;
        if (!config.visible_check)
            return true;
        return FiveM::Visibility::IsPedVisible(ped);
    }



    static bool BindDown(int vk) {
        if (vk <= 0) return false;
        return aim_type::IsDown(vk) || ((GetKeyState(vk) & 0x8000) != 0);
    }









    static bool AimHoldDown() {

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


    static std::vector<uintptr_t> s_aimPeds;
    static std::vector<Vec3> s_aimPos;

    static void CollectPedsForAim() {
        using namespace FiveM;
        s_aimPeds.clear();
        s_aimPos.clear();

        if (!ESP::validPeds.empty() && ESP::validPeds.size() == ESP::positions.size()) {
            s_aimPeds = ESP::validPeds;
            s_aimPos = ESP::positions;
            return;
        }

    }


    static bool FindBestTargetForHitbox(TargetInfo& out, float fov_px,
                                        float max_dist, Hitbox selectedHitbox,
                                        bool applySticky = true) {
        out = {};
        using namespace FiveM;



        if (!ESP::FrameCacheValid() || !offset::localplayer)
            return false;
        const Matrix view_matrix = ESP::GetFrameViewMatrix();
        const Vec3 localPos = ESP::GetFrameLocalPos();
        if (localPos.IsZero()) return false;

        ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x < 1.f || display.y < 1.f) return false;
        float cx = display.x * 0.5f;
        float cy = display.y * 0.5f;

        float bestScore = 1e30f;
        TargetInfo best{};
        TargetInfo lockedCandidate{};

        const std::vector<uintptr_t>* pedsPtr = nullptr;
        const std::vector<Vec3>* posPtr = nullptr;
        auto snapshot = FiveM::ESP::GetValidPedsSnapshot();
        if (!snapshot.validPeds.empty()) {
            pedsPtr = &snapshot.validPeds;
            posPtr = &snapshot.positions;
        } else {
            CollectPedsForAim();
            pedsPtr = &s_aimPeds;
            posPtr = &s_aimPos;
        }
        const auto& peds = *pedsPtr;
        const auto& positions = *posPtr;
        if (peds.empty())
            return false;
        const bool skipFriends = friends::config.ignore_aim && friends::HasFriends();

        for (size_t i = 0; i < peds.size(); ++i) {
            uintptr_t ped = peds[i];
            if (!ped || ped == offset::localplayer)
                continue;

            if (skipFriends) {
                uint32_t networkId = 0;


                if (!esp::try_get_prepared_network_id(ped, networkId) ||
                    friends::IsFriend(networkId))
                    continue;
            }


            float health = 0.f;
            if (!esp::try_get_prepared_health(ped, health)) {

                continue;
            }
            if (health <= 1.f || health > 500.f)
                continue;

            Vec3 world = ResolveHitboxWorld(ped, selectedHitbox);
            const bool haveBone = !world.IsZero() && IsFiniteVec3(world);

            if (!haveBone) {


                if (i < positions.size() && !positions[i].IsZero() &&
                    IsFiniteVec3(positions[i])) {
                    world = positions[i];
                    world.z += (selectedHitbox == Hitbox::Head) ? 0.85f : 0.55f;
                } else continue;
            } else if (i < positions.size() && !positions[i].IsZero() &&
                       IsFiniteVec3(positions[i])) {

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

            if (screen.x < -50.f || screen.y < -50.f ||
                screen.x > display.x + 50.f || screen.y > display.y + 50.f)
                continue;

            float dx = screen.x - cx;
            float dy = screen.y - cy;
            float cross = sqrtf(dx * dx + dy * dy);
            const float candidateFov = config.dynamic_fov_mode == DynamicFovMode::Static
                ? fov_px : FovForDistance(dist);
            const float allowedFov = (applySticky && ped == locked_target)
                ? candidateFov + (std::max)(0.f, config.sticky_tolerance)
                : candidateFov;
            if (cross > allowedFov)
                continue;


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
            if (ped == locked_target)
                lockedCandidate = { ped, world, screen, dist, cross, true };
        }

        if (!applySticky) {
            out = best;
            return best.valid;
        }

        const auto now = std::chrono::steady_clock::now();
        const bool currentValid = lockedCandidate.valid;
        const bool retentionActive = currentValid && target_locked_at.time_since_epoch().count() != 0 &&
            std::chrono::duration<float>(now - target_locked_at).count() <
                (std::max)(0.f, config.sticky_retention);
        const bool switchDelayActive = last_target_switch.time_since_epoch().count() != 0 &&
            std::chrono::duration<float>(now - last_target_switch).count() <
                (std::max)(0.f, config.switch_delay);

        if (config.sticky_aim && currentValid && (retentionActive || switchDelayActive ||
            !best.valid || lockedCandidate.crosshair_dist <=
                best.crosshair_dist + (std::max)(0.f, config.sticky_tolerance))) {
            out = lockedCandidate;
        } else if (best.valid && (currentValid || config.switch_immediately_if_invalid ||
                   !switchDelayActive || locked_target == 0)) {
            out = best;
        } else {
            out = currentValid ? lockedCandidate : TargetInfo{};
        }

        if (out.valid && out.ped != locked_target) {
            locked_target = out.ped;
            target_locked_at = now;
            last_target_switch = now;
        }
        return out.valid;
    }

    bool FindBestTarget(TargetInfo& out, float fov_px, float max_dist) {
        return FindBestTargetForHitbox(out, fov_px, max_dist, config.hitbox);
    }












    static bool FindBestTriggerTarget(TargetInfo& out, float fov_px,
                                       float max_dist) {
        out = {};
        using namespace FiveM;

        if (!ESP::FrameCacheValid() || !offset::localplayer)
            return false;
        const Matrix view_matrix = ESP::GetFrameViewMatrix();
        const Vec3 localPos = ESP::GetFrameLocalPos();
        if (localPos.IsZero()) return false;

        ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x < 1.f || display.y < 1.f) return false;
        float cx = display.x * 0.5f;
        float cy = display.y * 0.5f;

        const std::vector<uintptr_t>* pedsPtr = nullptr;
        const std::vector<Vec3>* posPtr = nullptr;
        auto snapshot = FiveM::ESP::GetValidPedsSnapshot();
        if (!snapshot.validPeds.empty()) {
            pedsPtr = &snapshot.validPeds;
            posPtr = &snapshot.positions;
        } else {
            CollectPedsForAim();
            pedsPtr = &s_aimPeds;
            posPtr = &s_aimPos;
        }
        const auto& peds = *pedsPtr;
        const auto& positions = *posPtr;
        if (peds.empty())
            return false;
        const bool skipFriends = friends::config.ignore_aim && friends::HasFriends();

        constexpr Hitbox kTriggerHitboxes[] = {
            Hitbox::Head, Hitbox::Neck, Hitbox::Torso, Hitbox::Pelvis, Hitbox::Legs
        };




        float bestScore = 1e30f;
        bool bestFromHead = false;
        TargetInfo best{};

        for (size_t i = 0; i < peds.size(); ++i) {
            uintptr_t ped = peds[i];
            if (!ped || ped == offset::localplayer)
                continue;

            if (skipFriends) {
                uint32_t networkId = 0;
                if (!esp::try_get_prepared_network_id(ped, networkId) ||
                    friends::IsFriend(networkId))
                    continue;
            }

            float health = 0.f;
            if (!esp::try_get_prepared_health(ped, health))
                continue;
            if (health <= 1.f || health > 500.f)
                continue;

            if (!IsVisibleForAim(ped))
                continue;



            for (const Hitbox hitbox : kTriggerHitboxes) {
                Vec3 world = ResolveHitboxWorld(ped, hitbox);
                const bool haveBone = !world.IsZero() && IsFiniteVec3(world);

                if (!haveBone) {
                    if (i < positions.size() && !positions[i].IsZero() &&
                        IsFiniteVec3(positions[i])) {
                        world = positions[i];
                        world.z += (hitbox == Hitbox::Head) ? 0.85f : 0.55f;
                    } else continue;
                } else if (i < positions.size() && !positions[i].IsZero() &&
                           IsFiniteVec3(positions[i])) {
                    const float boneOffset = positions[i].distance_to(world);
                    if (!std::isfinite(boneOffset) || boneOffset > 4.5f)
                        continue;
                }

                if (!IsFiniteVec3(world))
                    continue;

                const float dist = localPos.distance_to(world);
                if (!std::isfinite(dist) || dist > max_dist || dist < 0.35f)
                    continue;

                Vec2 screen;
                if (!world.world_to_screen(view_matrix, screen) || !IsFiniteVec2(screen))
                    continue;

                if (screen.x < -50.f || screen.y < -50.f ||
                    screen.x > display.x + 50.f || screen.y > display.y + 50.f)
                    continue;

                float dx = screen.x - cx;
                float dy = screen.y - cy;
                float cross = sqrtf(dx * dx + dy * dy);
                if (cross > fov_px)
                    continue;

                float score = config.closest_to_crosshair ? cross : dist;
                if (config.threat_priority)
                    score = cross * 1.0f + dist * 0.35f;

                const bool fromHead = hitbox == Hitbox::Head;

                const bool better = !best.valid || (fromHead && !bestFromHead) ||
                    (fromHead == bestFromHead && score < bestScore);
                if (better) {
                    bestScore = score;
                    bestFromHead = fromHead;
                    best.ped = ped;
                    best.world_pos = world;
                    best.screen_pos = screen;
                    best.distance = dist;
                    best.crosshair_dist = cross;
                    best.valid = true;
                }
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


        if (errLen0 > 900.f || !std::isfinite(errLen0)) {
            ResetAimMotionState();
            return;
        }

        if (targetDistance > 70.f && errLen0 > 350.f) {
            ResetAimMotionState();
            return;
        }



        if (config.smooth_x <= 1.f) {
            int left_x = static_cast<int>(lroundf(dx));
            int left_y = static_cast<int>(lroundf(dy));
            int maxPkt = 16;
            int stepCap = 127;
            if (targetDistance > 100.f) { maxPkt = 2; stepCap = 12; }
            else if (targetDistance > 70.f) { maxPkt = 3; stepCap = 20; }
            else if (targetDistance > 45.f) { maxPkt = 5; stepCap = 40; }
            else if (targetDistance > 25.f) { maxPkt = 8; stepCap = 64; }

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
















        if (!std::isfinite(dx) || !std::isfinite(dy)) {
            ResetAimMotionState();
            return;
        }

        float smooth = config.smooth_x;
        if (smooth < 0.f) smooth = 0.f;
        if (smooth > 100.f) smooth = 100.f;






        float smoothY = config.smooth_y;
        if (!std::isfinite(smoothY) || smoothY < 0.f) smoothY = smooth;
        if (smoothY > 100.f) smoothY = 100.f;

        float strength = (100.f - smooth) / 100.f;
        float strengthY = (100.f - smoothY) / 100.f;
        if (config.humanize && strength > 0.f && strength < 1.f)
            strength = powf(strength, 0.92f);
        if (config.humanize && strengthY > 0.f && strengthY < 1.f)
            strengthY = powf(strengthY, 0.92f);

        if (strength <= 0.0001f) {
            ResetAimMotionState();
            return;
        }

        const float errX = dx;
        const float errY = dy;
        const float errLen = sqrtf(errX * errX + errY * errY);



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



        int minimumIntervalMs = 8;
        if (targetDistance > 120.f) minimumIntervalMs = 14;
        else if (targetDistance > 80.f) minimumIntervalMs = 12;
        else if (targetDistance > 50.f) minimumIntervalMs = 10;

        if (last_aim_move.time_since_epoch().count() != 0 &&
            now - last_aim_move < std::chrono::milliseconds(minimumIntervalMs)) {
            return;
        }



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
        float gainY = strengthY * outputScale * adaptive_gain_scale;



        if (reversedX)
            gainX *= 0.25f;
        if (reversedY)
            gainY *= 0.25f;

        previous_error_x = errX;
        previous_error_y = errY;
        have_previous_error = true;

        float mx = errX * gainX;
        float my = errY * gainY;






        float maxStep = 18.f;
        if (smooth <= 15.f) maxStep = 22.f;
        if (smooth <= 1.f) maxStep = 28.f;

        if (targetDistance > 120.f) maxStep = (std::min)(maxStep, 2.5f);
        else if (targetDistance > 90.f) maxStep = (std::min)(maxStep, 3.5f);
        else if (targetDistance > 70.f) maxStep = (std::min)(maxStep, 5.f);
        else if (targetDistance > 50.f) maxStep = (std::min)(maxStep, 7.f);
        else if (targetDistance > 30.f) maxStep = (std::min)(maxStep, 11.f);


        const float proportionalCap = (std::max)(1.f, errLen * 0.28f);
        maxStep = (std::min)(maxStep, proportionalCap);

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



        if (ix == 0 && iy == 0)
            return;

        const int hardCap = static_cast<int>((std::min)(maxStep, 127.f));
        ix = std::clamp(ix, -hardCap, hardCap);
        iy = std::clamp(iy, -hardCap, hardCap);

        if (ix == 0 && iy == 0)
            return;


        aim_type::Move(ix, iy);
        last_aim_move = now;
    }




    static void RunTriggerLogic();

    static void RunAimbotLogic() {

        if (!g_unified_aimbot)
            g_unified_aimbot = Gameplay::UnifiedAim::CreateAimbotForGame("FiveM");
        if (g_unified_aimbot) {
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


        if (!AimHoldDown()) {
            s_poseMiss = 0;
            if (tracking_active || locked_target)
                ResetTrackingState();
            return;
        }


        TargetInfo tgt;
        if (!FindBestTarget(tgt, config.fov_size, EffectiveMaxDistance(false))) {
            ++s_poseMiss;
            current_target = {};
            if (s_poseMiss > 10) {
                if (tracking_active || locked_target)
                    ResetTrackingState();
            }
            return;
        }

        s_poseMiss = 0;


        current_target = tgt;
        UpdateEffectiveFov(tgt.distance);





        if (config.lock_beep && tgt.ped != 0 && tgt.ped != last_beeped_lock_ped) {
            last_beeped_lock_ped = tgt.ped;
            beeped_for_lock = true;
            MessageBeep(MB_OK);
        }


        float dx = tgt.screen_pos.x - ImGui::GetIO().DisplaySize.x * 0.5f;
        float dy = tgt.screen_pos.y - ImGui::GetIO().DisplaySize.y * 0.5f;
        HumanizedMove(dx, dy);


        Gameplay::UnifiedAim::AimContext ctx;
        ctx.dt = ImGui::GetIO().DeltaTime;
        g_unified_aimbot->Update(ctx);


    }

    static void RunTriggerLogic() {
        const auto triggerNow = std::chrono::steady_clock::now();
        if (trigger_pressed) {
            if (!config.trigger_enabled || triggerNow >= trigger_release_at) {
                aim_type::LeftClickRelease();
                trigger_pressed = false;
            } else {
                return;
            }
        }
        if (!config.trigger_enabled) {
            trigger_target = 0;
            trigger_burst_started = {};
            return;
        }

        const bool active = config.trigger_always_on || BindDown(config.trigger_bind);
        if (!active) {
            trigger_target = 0;
            trigger_burst_started = {};
            return;
        }

        TargetInfo tgt{};
        bool found = false;
        if (config.trigger_head_only) {
            found = FindBestTargetForHitbox(tgt, config.trigger_fov,
                                            EffectiveMaxDistance(true), Hitbox::Head, false);
        } else {




            found = FindBestTriggerTarget(tgt, config.trigger_fov,
                                           EffectiveMaxDistance(true));
        }
        if (!found) {
            trigger_target = 0;
            trigger_burst_started = {};
            return;
        }


        current_target = tgt;

        auto now = std::chrono::steady_clock::now();
        if (tgt.ped != trigger_target) {
            trigger_target = tgt.ped;
            trigger_target_entered = now;
            trigger_burst_started = {};
            trigger_target_delay = (std::max)(config.trigger_delay, 0.f);
            if (config.trigger_random_extra > 0.f) {
                std::uniform_real_distribution<float> randomDelay(0.f, config.trigger_random_extra);
                trigger_target_delay += randomDelay(rng);
            }
        }
        const float reactionElapsed = std::chrono::duration<float>(now - trigger_target_entered).count();
        const float shotElapsed = std::chrono::duration<float>(now - last_trigger).count();
        if (reactionElapsed < trigger_target_delay || now < trigger_next_allowed)
            return;

        if (config.trigger_burst) {
            if (trigger_burst_started.time_since_epoch().count() == 0)
                trigger_burst_started = now;
            const float burstElapsed = std::chrono::duration<float>(now - trigger_burst_started).count();
            if (burstElapsed >= (std::max)(.02f, config.trigger_burst_duration)) {
                trigger_burst_started = {};
                trigger_next_allowed = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<float>((std::max)(config.trigger_burst_interval,
                                                           config.trigger_cooldown)));
                return;
            }
        }

        const float minimumShotInterval = config.trigger_burst
            ? .06f : (std::max)(.06f, config.trigger_cooldown);
        if (shotElapsed < minimumShotInterval)
            return;

        last_trigger = now;
        aim_type::LeftClick();
        trigger_pressed = true;
        trigger_release_at = now + std::chrono::milliseconds(20);
        if (!config.trigger_burst)
            trigger_next_allowed = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<float>((std::max)(0.f, config.trigger_cooldown)));
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
            UpdateEffectiveFov(current_target.valid ? current_target.distance : config.dynamic_fov_near_distance);
            draw_style(config.dynamic_fov_mode == DynamicFovMode::Static
                ? config.fov_size : effective_fov, col, config.fov_style);
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
        config.humanize = true;
        config.trigger_delay = 0.08f;
    }

    void ApplyRageProfile() {
        config.smooth_x = 0.f;
        config.smooth_y = 0.f;
        config.fov_size = 180.f;
        config.humanize = false;
        config.trigger_delay = 0.02f;
    }

    void Run() {



        RunAimbotLogic();
        RunTriggerLogic();

        if (config.aimbot_enabled || config.trigger_enabled || config.crosshair_enabled)
            DrawFOV();
    }

}

#endif
