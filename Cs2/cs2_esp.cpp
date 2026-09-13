#include "cs2_esp.h"
#include "cs2_weapons.h"
#include "cs2_weapon_icons.h"
#include "cs2_aim.h"
#include "aimbot/aim_type.h"
#include "gameplay/esp_core.h"
#include "gameplay/trail_history.h"
#include "gameplay/esp_fx.h"
#include "gameplay/esp_optimizer.h"
#include "imgui.h"
#include "../src/window/window.hpp"
#include "../src/config/app_settings.h"
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>

namespace CS2_ESP {
namespace {

ImU32 Col(const float* c, float aMul = 1.f) {
    int a = (int)(c[3] * aMul * 255.f);
    if (a < 0) a = 0; if (a > 255) a = 255;
    return IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), a);
}

bool W2S(const float* world, const float* vm, float& sx, float& sy);

// Presentation-only smoothing. DMA snapshots and aim logic remain untouched.
// Each pawn keeps a tiny render state that interpolates toward a short,
// velocity-based prediction. The prediction is capped so packet/DMA stalls
// cannot make players drift across the screen.
struct VisualPlayerState {
    float position[3]{};
    float raw_position[3]{};
    float bone_positions[CS2::kBoneSlotCount][3]{};
    CS2::Player output{};
    double sample_time = 0.0;
    double last_seen = 0.0;
    int last_frame = -1;
    bool initialized = false;
    bool bones_initialized = false;
};

CS2::Player SmoothPlayerForPresentation(const CS2::Player& raw) {
    // Acquisition already publishes coherent snapshots at a high rate. Do not
    // add presentation interpolation: it delays the box and individual joints.
    return raw;
#if 0
    static std::unordered_map<uintptr_t, VisualPlayerState> states;
    static int cleanup_frame = -1;

    const int frame = ImGui::GetFrameCount();
    const double now = ImGui::GetTime();
    auto& state = states[raw.pawn];
    if (state.last_frame == frame)
        return state.output;

    const float raw_delta_x = raw.pos[0] - state.raw_position[0];
    const float raw_delta_y = raw.pos[1] - state.raw_position[1];
    const float raw_delta_z = raw.pos[2] - state.raw_position[2];
    const float raw_delta_sq = raw_delta_x * raw_delta_x + raw_delta_y * raw_delta_y + raw_delta_z * raw_delta_z;
    if (!state.initialized || raw_delta_sq > 0.0001f) {
        std::copy(std::begin(raw.pos), std::end(raw.pos), state.raw_position);
        state.sample_time = now;
    }

    const float dx = raw.pos[0] - state.position[0];
    const float dy = raw.pos[1] - state.position[1];
    const float dz = raw.pos[2] - state.position[2];
    const float distance_sq = dx * dx + dy * dy + dz * dz;
    const bool invalid = !std::isfinite(raw.pos[0]) || !std::isfinite(raw.pos[1]) || !std::isfinite(raw.pos[2]);
    constexpr float kTeleportDistance = 192.f;
    const float dt = std::clamp(ImGui::GetIO().DeltaTime, 0.001f, 0.050f);

    if (!state.initialized || invalid || distance_sq > kTeleportDistance * kTeleportDistance) {
        std::copy(std::begin(raw.pos), std::end(raw.pos), state.position);
        state.initialized = !invalid;
    } else {
        constexpr float kSmoothingSeconds = 0.026f;
        const float alpha = 1.f - std::exp(-dt / kSmoothingSeconds);
        const float sample_age = static_cast<float>(std::clamp(now - state.sample_time, 0.0, 0.024));
        const float speed_sq = raw.velocity[0] * raw.velocity[0] + raw.velocity[1] * raw.velocity[1] + raw.velocity[2] * raw.velocity[2];
        const float lead = speed_sq < 5000.f * 5000.f ? std::min(sample_age + 0.006f, 0.030f) : 0.f;
        for (int axis = 0; axis < 3; ++axis) {
            const float target = raw.pos[axis] + raw.velocity[axis] * lead;
            state.position[axis] += (target - state.position[axis]) * alpha;
        }
    }

    state.output = raw;
    const float shift[3] = {
        state.position[0] - raw.pos[0],
        state.position[1] - raw.pos[1],
        state.position[2] - raw.pos[2]
    };
    for (int axis = 0; axis < 3; ++axis) {
        state.output.pos[axis] += shift[axis];
        state.output.head[axis] += shift[axis];
    }
    if (state.output.bones_ok) {
        constexpr float kBoneSmoothingSeconds = 0.018f;
        const float bone_alpha = 1.f - std::exp(-dt / kBoneSmoothingSeconds);
        for (std::size_t bone = 0; bone < CS2::kBoneSlotCount; ++bone) {
            for (int axis = 0; axis < 3; ++axis) {
                const float target = raw.bones[bone][axis] + shift[axis];
                const float delta = target - state.bone_positions[bone][axis];
                if (!state.bones_initialized || !std::isfinite(delta) || std::fabs(delta) > 64.f)
                    state.bone_positions[bone][axis] = target;
                else
                    state.bone_positions[bone][axis] += delta * bone_alpha;
                state.output.bones[bone][axis] = state.bone_positions[bone][axis];
            }
        }
        state.bones_initialized = true;
        std::memcpy(state.output.head, state.output.bones[0], sizeof(state.output.head));
    } else {
        state.bones_initialized = false;
    }
    state.last_seen = now;
    state.last_frame = frame;

    if (cleanup_frame != frame && (frame % 240) == 0) {
        cleanup_frame = frame;
        for (auto it = states.begin(); it != states.end();) {
            if (now - it->second.last_seen > 2.0) it = states.erase(it);
            else ++it;
        }
    }
    return state.output;
#endif
}

void DrawMotionVisuals(ImDrawList* dl, const CS2::Runtime& rt,
                       const CS2::Config& cfg, const CS2::Player& player) {
    if (!dl || (!cfg.trails && !cfg.head_halo && !cfg.look_direction && !cfg.chinese_hat)) return;

    const double now = ImGui::GetTime();
    static std::unordered_map<uintptr_t, OmniGhost::Gameplay::FixedTrailHistory<18>> trails;
    static int cleanup_frame = -1;

    if (cfg.trails) {
        auto& history = trails[player.pawn];
        // Trail from torso (spine/chest), not feet.
        float tx = player.pos[0], ty = player.pos[1], tz = player.pos[2] + 40.f;
        if (player.bones_ok) {
            // bone 2 ~ spine2 / chest region in expanded skeleton
            tx = player.bones[2][0];
            ty = player.bones[2][1];
            tz = player.bones[2][2];
        } else if (std::isfinite(player.head[0])) {
            tx = player.head[0];
            ty = player.head[1];
            tz = player.head[2] - 25.f;
        }
        history.Push(tx, ty, tz, now, 3.f, 0.035);
        const double duration = std::clamp(static_cast<double>(cfg.trail_duration), 0.20, 2.50);
        for (std::size_t i = 1; i < history.Size(); ++i) {
            const auto& a = history.At(i - 1);
            const auto& b = history.At(i);
            const double age = now - b.time;
            if (age < 0.0 || age > duration) continue;
            const float wa[3] = { a.x, a.y, a.z };
            const float wb[3] = { b.x, b.y, b.z };
            float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
            if (!W2S(wa, rt.view_matrix, ax, ay) || !W2S(wb, rt.view_matrix, bx, by))
                continue;
            const float fade = static_cast<float>(1.0 - age / duration);
            const ImU32 col = cfg.rainbow_trails
                ? OmniGhost::Gameplay::EspFx::RainbowFade(static_cast<float>(b.time) * 0.35f,
                    fade > 0.f ? 1.f - fade : 1.f)
                : Col(cfg.col_trail, fade * fade);
            dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), col,
                        std::clamp(cfg.trail_thickness, 1.f, 8.f));
        }
    }

    const int frame = ImGui::GetFrameCount();
    if (frame != cleanup_frame && (frame % 120) == 0) {
        cleanup_frame = frame;
        for (auto it = trails.begin(); it != trails.end();) {
            if (it->second.Stale(now, 3.0)) it = trails.erase(it);
            else ++it;
        }
    }

    if (cfg.head_halo) {
        constexpr int segments = 14;
        constexpr float radius = 5.2f;
        float previous_x = 0.f, previous_y = 0.f;
        bool previous_ok = false;
        for (int i = 0; i <= segments; ++i) {
            const float angle = static_cast<float>(i) * 6.28318530718f / segments;
            const float point[3] = {
                player.head[0] + std::cos(angle) * radius,
                player.head[1] + std::sin(angle) * radius,
                player.head[2] + 3.5f
            };
            float sx = 0.f, sy = 0.f;
            const bool ok = W2S(point, rt.view_matrix, sx, sy);
            if (ok && previous_ok)
                dl->AddLine(ImVec2(previous_x, previous_y), ImVec2(sx, sy),
                            Col(cfg.col_halo), 1.6f);
            previous_x = sx;
            previous_y = sy;
            previous_ok = ok;
        }
    }

    if (cfg.chinese_hat) {
        auto project = [&](float wx, float wy, float wz, float& sx, float& sy) -> bool {
            const float pt[3] = { wx, wy, wz };
            return W2S(pt, rt.view_matrix, sx, sy);
        };
        // CS2 units are larger than GTA — scale hat up.
        const float hatScale = 18.f * std::clamp(cfg.chinese_hat_scale, 0.4f, 3.0f);
        OmniGhost::Gameplay::EspFx::DrawChineseHat(
            dl, player.head[0], player.head[1], player.head[2],
            project, static_cast<float>(now), hatScale, true);
    }

    if (cfg.look_direction && std::isfinite(player.view_yaw)) {
        const float yaw = player.view_yaw * 0.01745329251f;
        const float length = std::clamp(cfg.look_direction_length, 30.f, 220.f);
        const float end[3] = {
            player.head[0] + std::cos(yaw) * length,
            player.head[1] + std::sin(yaw) * length,
            player.head[2]
        };
        float ax = 0.f, ay = 0.f, bx = 0.f, by = 0.f;
        if (W2S(player.head, rt.view_matrix, ax, ay) &&
            W2S(end, rt.view_matrix, bx, by)) {
            const ImU32 color = Col(cfg.col_look);
            dl->AddLine(ImVec2(ax, ay), ImVec2(bx, by), color,
                        std::clamp(cfg.eye_line_thickness, 0.5f, 6.f));
            dl->AddCircleFilled(ImVec2(bx, by), 2.2f, color, 8);
        }
    }
}


bool W2S(const float* world, const float* vm, float& sx, float& sy) {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float clipX = world[0] * vm[0]  + world[1] * vm[1]  + world[2] * vm[2]  + vm[3];
    const float clipY = world[0] * vm[4]  + world[1] * vm[5]  + world[2] * vm[6]  + vm[7];
    const float clipW = world[0] * vm[12] + world[1] * vm[13] + world[2] * vm[14] + vm[15];
    if (!std::isfinite(clipW) || clipW < 0.01f) return false;
    float inv = 1.f / clipW;
    sx = (ds.x * 0.5f) + (0.5f * clipX * inv * ds.x);
    sy = (ds.y * 0.5f) - (0.5f * clipY * inv * ds.y);
    return std::isfinite(sx) && std::isfinite(sy);
}

// Expanded slots (see cs2_game.h):
// 0 head 1 neck 2 spine2 3 spine1 4 spine0 5 pelvis
// 6 clav_l 7 sh_l 8 elb_l 9 hand_l | 10 clav_r 11 sh_r 12 elb_r 13 hand_r
// 14 hip_l 15 knee_l 16 ankle_l   | 17 hip_r 18 knee_r 19 ankle_r
// The skeleton and joints share this projection cache.  Previously every line
// projected both endpoints again, meaning a complete skeleton could perform
// almost sixty identical world-to-screen transforms per player and frame.
void DrawBoneLine(ImDrawList* dl, const float bones[][3], const ImVec2 projected[],
                  const bool projected_ok[], int a, int b, ImU32 col,
                  float thickness = 1.6f) {
    const float wx = bones[a][0] - bones[b][0];
    const float wy = bones[a][1] - bones[b][1];
    const float wz = bones[a][2] - bones[b][2];
    const float wlen2 = wx * wx + wy * wy + wz * wz;
    if (wlen2 < 0.25f || wlen2 > 130.f * 130.f) return;

    if (!projected_ok[a] || !projected_ok[b]) return;
    const float dx = projected[a].x - projected[b].x;
    const float dy = projected[a].y - projected[b].y;
    if (dx * dx + dy * dy > 520.f * 520.f) return;
    dl->AddLine(projected[a], projected[b], col, thickness);
}

const char* WeaponIconCode(int def) {
    // Compact white codes (no TTF required)
    switch (def) {
    case 7:  return "AK";
    case 8:  return "AUG";
    case 9:  return "AWP";
    case 10: return "FAM";
    case 13: return "GAL";
    case 16: return "M4";
    case 60: return "M4S";
    case 39: return "SG";
    case 38: return "SCAR";
    case 11: return "G3";
    case 40: return "SCOUT";
    case 17: return "MAC";
    case 19: return "P90";
    case 24: return "UMP";
    case 26: return "BIZ";
    case 33: return "MP7";
    case 34: return "MP9";
    case 23: return "MP5";
    case 1:  return "DEAG";
    case 4:  return "GLOCK";
    case 61: return "USP";
    case 32: return "P2K";
    case 36: return "P250";
    case 30: return "TEC9";
    case 63: return "CZ";
    case 64: return "R8";
    case 2:  return "DUAL";
    case 3:  return "57";
    case 25: return "XM";
    case 27: return "MAG7";
    case 29: return "SAWED";
    case 35: return "NOVA";
    case 14: return "M249";
    case 28: return "NEGEV";
    case 31: return "ZEUS";
    case 43: return "FLASH";
    case 44: return "HE";
    case 45: return "SMOKE";
    case 46: case 48: return "MOLLY";
    case 47: return "DECOY";
    case 49: return "C4";
    default:
        if (def >= 500 && def <= 526) return "KNIFE";
        if (def == 41 || def == 42 || def == 59) return "KNIFE";
        return nullptr;
    }
}

void DrawRadar2D(ImDrawList* dl, const CS2::Runtime& rt, CS2::Config& cfg) {
    float& ox = cfg.radar_2d_x;
    float& oy = cfg.radar_2d_y;
    const float size = cfg.radar_2d_size > 80.f ? cfg.radar_2d_size : 160.f;

    // Keep the drag hitbox attached to the visible radar after every move.
    if (ImGui::GetCurrentContext() && app_settings::menu_open) {
        ImGui::SetNextWindowPos(ImVec2(ox, oy), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(size, size), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##radar2d_drag", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove);
        if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ox += ImGui::GetIO().MouseDelta.x;
            oy += ImGui::GetIO().MouseDelta.y;
            const ImVec2 display = ImGui::GetIO().DisplaySize;
            ox = std::clamp(ox, 0.f, std::max(0.f, display.x - size));
            oy = std::clamp(oy, 0.f, std::max(0.f, display.y - size));
        }
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    const ImVec2 origin(ox, oy);
    const ImVec2 center(origin.x + size * 0.5f, origin.y + size * 0.5f);
    dl->AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), IM_COL32(8, 8, 10, 190), 6.f);
    dl->AddRect(origin, ImVec2(origin.x + size, origin.y + size), IM_COL32(212, 175, 55, 90), 6.f, 0, 1.2f);
    dl->AddCircle(center, 4.f, IM_COL32(212, 175, 55, 255), 12, 1.5f);
    const float scale = 0.08f;
    const float yaw = rt.local_view_yaw * 0.01745329251f;
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    for (const auto& raw : rt.players) {
        const CS2::Player p = SmoothPlayerForPresentation(raw);
        if (p.is_local) continue;
        if (cfg.team_check && p.team == rt.local_team) continue;
        float dx = p.pos[0] - rt.local_pos[0];
        float dy = p.pos[1] - rt.local_pos[1];
        float rx = dx * cy + dy * sy;
        float ry = -dx * sy + dy * cy;
        float px = center.x + rx * scale;
        float py = center.y - ry * scale;
        px = std::clamp(px, origin.x + 4.f, origin.x + size - 4.f);
        py = std::clamp(py, origin.y + 4.f, origin.y + size - 4.f);
        const ImU32 c = (p.team == rt.local_team)
            ? Col(cfg.col_team) : Col(cfg.col_enemy);
        dl->AddCircleFilled(ImVec2(px, py), 3.5f, c, 8);
    }
}

void DrawSpectatorList(ImDrawList* dl, const CS2::Runtime& rt) {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float x = ds.x - 240.f;
    float y = 40.f;
    dl->AddRectFilled(ImVec2(x - 8.f, y - 4.f), ImVec2(ds.x - 12.f, y + 20.f + 14.f * 12),
        IM_COL32(8, 8, 10, 160), 4.f);
    char title[48];
    std::snprintf(title, sizeof(title), "A observar-te (%d)", rt.spectator_count);
    dl->AddText(ImVec2(x, y), IM_COL32(212, 175, 55, 255), title);
    y += 18.f;
    int shown = 0;
    // Prefer dedicated spectators vector when populated
    const auto& list = !rt.spectators.empty() ? rt.spectators : rt.players;
    for (const auto& p : list) {
        if (!p.is_spectator && &list == &rt.players) continue;
        if (p.is_local) continue;
        char line[80];
        std::snprintf(line, sizeof(line), "- %s", p.name[0] ? p.name : "Jogador");
        dl->AddText(ImVec2(x, y), IM_COL32(200, 200, 200, 220), line);
        y += 14.f;
        if (++shown >= 12) break;
    }
    if (shown == 0)
        dl->AddText(ImVec2(x, y), IM_COL32(120, 120, 120, 180), "(ninguem a observar)");
}

// Directional arrow around the FOV ring. Always drawn for every match player
// (does NOT hide when looking toward the target). Radius tracks aim FOV + 1px.
void DrawFovRingArrow(ImDrawList* dl, float dirX, float dirY, float cx, float cy,
                      float radius, ImU32 col) {
    float len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 0.001f) return;
    dirX /= len;
    dirY /= len;
    if (radius < 8.f) radius = 8.f;

    const float ax = cx + dirX * radius;
    const float ay = cy + dirY * radius;
    const float px = -dirY;
    const float py = dirX;
    // Slightly larger tip so it stays readable at small FOVs
    dl->AddTriangleFilled(
        ImVec2(ax + dirX * 11.f, ay + dirY * 11.f),
        ImVec2(ax - dirX * 7.f + px * 8.f, ay - dirY * 7.f + py * 8.f),
        ImVec2(ax - dirX * 7.f - px * 8.f, ay - dirY * 7.f - py * 8.f),
        col);
    // Soft outline for contrast on bright maps
    dl->AddTriangle(
        ImVec2(ax + dirX * 11.f, ay + dirY * 11.f),
        ImVec2(ax - dirX * 7.f + px * 8.f, ay - dirY * 7.f + py * 8.f),
        ImVec2(ax - dirX * 7.f - px * 8.f, ay - dirY * 7.f - py * 8.f),
        IM_COL32(0, 0, 0, 160), 1.f);
}

void DrawHotkeyOverlay(ImDrawList* dl, const CS2::Config& cfg) {
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float x = 14.f, y = ds.y - 110.f;
    auto line = [&](const char* t, bool on) {
        dl->AddText(ImVec2(x, y), on ? IM_COL32(120, 220, 140, 230) : IM_COL32(140, 140, 140, 180), t);
        y += 14.f;
    };
    line(cfg.aim_enabled ? "AIM ON" : "AIM off", cfg.aim_enabled);
    line(cfg.trigger_enabled ? "TRIG ON" : "TRIG off", cfg.trigger_enabled);
    line(cfg.esp_enabled ? "ESP ON" : "ESP off", cfg.esp_enabled);
    line(aim_type::StatusText(), aim_type::IsConnected());
    if (cfg.aim_enabled) {
        dl->AddText(ImVec2(x, y), IM_COL32(212, 175, 55, 220), CS2_Aim::DebugStatus());
        y += 14.f;
    }
}



struct KillEvent { char name[48]; float ttl; };
static KillEvent g_kills[8]{};
static int g_prev_hp[128]{};
static uintptr_t g_prev_pawn[128]{};

void UpdateKillFeed(const CS2::Runtime& rt) {
    for (const auto& p : rt.players) {
        if (p.is_local) continue;
        int slot = -1;
        for (int i = 0; i < 128; ++i) {
            if (g_prev_pawn[i] == p.pawn) { slot = i; break; }
        }
        if (slot < 0) {
            for (int i = 0; i < 128; ++i) {
                if (g_prev_pawn[i] == 0) { slot = i; break; }
            }
        }
        if (slot < 0) continue;
        const int prev = g_prev_hp[slot];
        if (prev > 0 && p.health <= 0) {
            for (int k = 7; k > 0; --k) g_kills[k] = g_kills[k - 1];
            std::snprintf(g_kills[0].name, sizeof(g_kills[0].name), "%s", p.name[0] ? p.name : "Jogador");
            g_kills[0].ttl = 4.f;
        }
        g_prev_hp[slot] = p.health;
        g_prev_pawn[slot] = p.pawn;
    }
    const float dt = ImGui::GetIO().DeltaTime;
    for (auto& k : g_kills) {
        if (k.ttl > 0.f) k.ttl -= dt;
    }
}

void DrawKillFeed(ImDrawList* dl) {
    float x = 18.f, y = 80.f;
    for (int i = 0; i < 8; ++i) {
        if (g_kills[i].ttl <= 0.f) continue;
        char line[64];
        std::snprintf(line, sizeof(line), "KILL  %s", g_kills[i].name);
        const int a = (int)std::clamp(g_kills[i].ttl / 4.f, 0.f, 1.f) * 255;
        dl->AddText(ImVec2(x + 1, y + 1), IM_COL32(0, 0, 0, a), line);
        dl->AddText(ImVec2(x, y), IM_COL32(212, 175, 55, a), line);
        y += 16.f;
    }
}

} // namespace

void Draw(const CS2::Runtime& rt, const CS2::Config& cfg) {
    // Non-const for radar drag — safe: config is global mutable
    CS2::Config& mut_cfg = const_cast<CS2::Config&>(cfg);

    if (!cfg.esp_enabled && !cfg.radar_2d && !cfg.spectator_list && !cfg.aim_enabled
        && !cfg.bomb_timer && !cfg.hotkey_overlay && !cfg.offscreen_arrows)
        return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;

    if (cfg.radar_2d)
        DrawRadar2D(dl, rt, mut_cfg);

    if (cfg.spectator_list)
        DrawSpectatorList(dl, rt);

    if (cfg.aim_enabled && cfg.aim_draw_fov) {
        ImVec2 c(ds.x * 0.5f, ds.y * 0.5f);
        float r = cfg.aim_fov > 1.f ? cfg.aim_fov : 80.f;
        ImU32 col = Col(cfg.col_fov);
        if (cfg.aim_fov_rgb) {
            float hue = fmodf((float)ImGui::GetTime() * 0.25f, 1.f);
            float rr, gg, bb;
            ImGui::ColorConvertHSVtoRGB(hue, 0.85f, 1.f, rr, gg, bb);
            col = IM_COL32((int)(rr * 255), (int)(gg * 255), (int)(bb * 255), 180);
        }
        if (cfg.aim_fov_style == 1)
            dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 0.f, 0, 1.5f);
        else if (cfg.aim_fov_style == 2) {
            dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.2f);
            dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, 1.2f);
        } else
            dl->AddCircle(c, r, col, 64, 1.5f);
        if (cfg.aim_deadzone > 0.5f)
            dl->AddCircle(c, cfg.aim_deadzone, IM_COL32(212, 175, 55, 60), 32, 1.f);
    }

    UpdateKillFeed(rt);
    DrawKillFeed(dl);
    if (cfg.hotkey_overlay)
        DrawHotkeyOverlay(dl, cfg);

    // Always show compact aim status when aim is on (helps DMA 2-PC debug)
    if (cfg.aim_enabled) {
        const char* st = CS2_Aim::DebugStatus();
        ImVec2 ts = ImGui::CalcTextSize(st);
        dl->AddText(ImVec2(ds.x * 0.5f - ts.x * 0.5f, ds.y - 28.f),
            IM_COL32(212, 175, 55, 200), st);
    }


    // Bomb timer — banner + world marker + soft beep under 5s
    if (cfg.bomb_timer && rt.bomb.planted && !rt.bomb.defused &&
        rt.bomb.blow_time > 0.f && rt.bomb.blow_time <= 45.f) {
        float sx = 0.f, sy = 0.f;
        bool on_screen = W2S(rt.bomb.pos, rt.view_matrix, sx, sy);
        char bomb_text[64];
        if (rt.bomb.defusing && rt.bomb.defuse_time > 0.f && rt.bomb.defuse_time <= 15.f)
            std::snprintf(bomb_text, sizeof(bomb_text), "BOMB  %.1fs  DEFUSING %.1fs",
                rt.bomb.blow_time, rt.bomb.defuse_time);
        else
            std::snprintf(bomb_text, sizeof(bomb_text), "BOMB  %.1fs", rt.bomb.blow_time);
        const ImU32 bomb_col = rt.bomb.blow_time < 5.f
            ? IM_COL32(255, 60, 60, 255) : IM_COL32(255, 180, 40, 255);

        ImVec2 ts = ImGui::CalcTextSize(bomb_text);
        const float bx = ds.x * 0.5f - ts.x * 0.5f;
        const float by = 22.f;
        dl->AddRectFilled(ImVec2(bx - 12.f, by - 4.f), ImVec2(bx + ts.x + 12.f, by + ts.y + 6.f),
                          IM_COL32(8, 8, 10, 200), 4.f);
        dl->AddRect(ImVec2(bx - 12.f, by - 4.f), ImVec2(bx + ts.x + 12.f, by + ts.y + 6.f),
                    bomb_col, 4.f, 0, 1.2f);
        dl->AddText(ImVec2(bx + 1, by + 1), IM_COL32(0, 0, 0, 200), bomb_text);
        dl->AddText(ImVec2(bx, by), bomb_col, bomb_text);

        if (on_screen) {
            dl->AddCircle(ImVec2(sx, sy), 10.f, bomb_col, 24, 2.f);
            dl->AddCircleFilled(ImVec2(sx, sy), 3.f, bomb_col, 12);
        }

        if (rt.bomb.blow_time < 5.f) {
            static float last_beep_bucket = -1.f;
            const float bucket = std::floor(rt.bomb.blow_time);
            if (bucket != last_beep_bucket) {
                last_beep_bucket = bucket;
                MessageBeep(MB_ICONEXCLAMATION);
            }
        }
    }

    const float screenCx = ds.x * 0.5f, screenCy = ds.y * 0.5f;

    // ── FOV-ring arrows for ALL match players (always visible, size = FOV+5) ──
    if (cfg.offscreen_arrows) {
        const float arrowRadius = (cfg.aim_fov > 1.f ? cfg.aim_fov : 80.f) + 5.f;
        for (int pi = 0; pi < (int)rt.players.size(); ++pi) {
            const CS2::Player p = SmoothPlayerForPresentation(rt.players[pi]);
            if (p.is_local && !cfg.self_esp) continue;
            if (!p.is_local && cfg.team_check && p.team == rt.local_team) continue;
            if (!p.alive && p.health <= 0) continue;

            const float* colBase = (p.team == rt.local_team) ? cfg.col_team : cfg.col_enemy;
            const ImU32 teamCol = Col(colBase);

            float dirX = 0.f, dirY = 0.f;
            float hx = 0.f, hy = 0.f;
            if (W2S(p.head, rt.view_matrix, hx, hy)) {
                dirX = hx - screenCx;
                dirY = hy - screenCy;
            } else {
                // Behind camera / no projection — use world yaw relative direction
                const float dx = p.pos[0] - rt.local_pos[0];
                const float dy = p.pos[1] - rt.local_pos[1];
                const float yaw = rt.local_view_yaw * 0.01745329251f;
                const float c = std::cos(yaw), s = std::sin(yaw);
                const float rx = dx * c + dy * s;
                const float ry = -dx * s + dy * c;
                dirX = rx;
                dirY = -ry; // screen Y grows downward
            }
            if (dirX * dirX + dirY * dirY < 0.0001f) continue;
            DrawFovRingArrow(dl, dirX, dirY, screenCx, screenCy, arrowRadius, teamCol);
        }
    }

    if (!cfg.esp_enabled) return;

    // Texture initialization is global, not player-specific. Keep it outside
    // the player loop even though EnsureLoaded has its own fast guard.
    if (cfg.weapon_icons && g_overlay_instance && g_overlay_instance->device)
        CS2_WeaponIcons::EnsureLoaded(g_overlay_instance->device);

    for (int pi = 0; pi < (int)rt.players.size(); ++pi) {
        const CS2::Player p = SmoothPlayerForPresentation(rt.players[pi]);
        if (p.is_local && !cfg.self_esp) continue;
        if (!p.is_local && cfg.team_check && p.team == rt.local_team) continue;
        if (p.distance > cfg.max_distance) continue;
        if (cfg.visible_check && !p.spotted)
            continue;

        // Team/enemy color for ALL ESP elements except weapon icons
        const float* colBase = (p.team == rt.local_team) ? cfg.col_team : cfg.col_enemy;
        // Cores por visibilidade (spotted)
        float colVis[4] = { colBase[0], colBase[1], colBase[2], colBase[3] };
        if (cfg.visibility_colors && !p.is_local) {
            if (p.spotted) {
                colVis[0] = 0.25f; colVis[1] = 0.95f; colVis[2] = 0.35f;
            } else {
                colVis[0] = 0.95f; colVis[1] = 0.35f; colVis[2] = 0.30f;
            }
        }
        const ImU32 teamCol = Col(colVis);

        // Prefer the current animated pose for crouching, jumping and leaning.
        // Fall back to the conventional 72-unit standing hull when no validated
        // skeleton is available.
        const float boxHead[3] = { p.pos[0], p.pos[1], p.pos[2] + 72.f };
        const float boxFeet[3] = { p.pos[0], p.pos[1], p.pos[2] };
        float hx, hy, fx, fy;
        if (!W2S(boxHead, rt.view_matrix, hx, hy))
            continue;
        if (!W2S(boxFeet, rt.view_matrix, fx, fy)) continue;

        float h = fabsf(fy - hy);
        if (h < 8.f) h = 8.f;
        float w = h * 0.42f;
        if (p.bones_ok) {
            float headX = 0.f, headY = 0.f, pelvisX = 0.f, pelvisY = 0.f;
            float shoulderLX = 0.f, shoulderLY = 0.f, shoulderRX = 0.f, shoulderRY = 0.f;
            float hipLX = 0.f, hipLY = 0.f, hipRX = 0.f, hipRY = 0.f;
            float ankleLX = 0.f, ankleLY = 0.f, ankleRX = 0.f, ankleRY = 0.f;
            const bool pose_ok =
                W2S(p.bones[0], rt.view_matrix, headX, headY) &&
                W2S(p.bones[5], rt.view_matrix, pelvisX, pelvisY) &&
                W2S(p.bones[7], rt.view_matrix, shoulderLX, shoulderLY) &&
                W2S(p.bones[11], rt.view_matrix, shoulderRX, shoulderRY) &&
                W2S(p.bones[14], rt.view_matrix, hipLX, hipLY) &&
                W2S(p.bones[17], rt.view_matrix, hipRX, hipRY) &&
                W2S(p.bones[16], rt.view_matrix, ankleLX, ankleLY) &&
                W2S(p.bones[19], rt.view_matrix, ankleRX, ankleRY);
            if (pose_ok) {
                const float pose_bottom = (std::max)(ankleLY, ankleRY);
                const float pose_h = pose_bottom - headY;
                if (pose_h >= 8.f && pose_h < ImGui::GetIO().DisplaySize.y * 1.5f) {
                    hx = (headX + pelvisX) * 0.5f;
                    hy = headY;
                    fy = pose_bottom;
                    h = pose_h;
                    const float body_left = (std::min)({shoulderLX, shoulderRX, hipLX, hipRX});
                    const float body_right = (std::max)({shoulderLX, shoulderRX, hipLX, hipRX});
                    w = (std::max)(h * 0.32f, (body_right - body_left) * 1.20f);
                }
            }
        }
        float left = hx - w * 0.5f;
        float right = hx + w * 0.5f;
        float top = hy - h * 0.08f;
        float bottom = fy + h * 0.025f;

        // Do not run cosmetic or bone work for an entity wholly outside the
        // drawable area.  Off-screen arrows were handled above, so this does
        // not remove the player's directional cue.  The margin avoids a pop
        // at the edge of the display while keeping the hot path bounded.
        constexpr float kScreenCullMargin = 128.f;
        if (right < -kScreenCullMargin || left > ds.x + kScreenCullMargin ||
            bottom < -kScreenCullMargin || top > ds.y + kScreenCullMargin)
            continue;

        DrawMotionVisuals(dl, rt, cfg, p);

        if (cfg.box || cfg.box_corner) {
            ImU32 c = teamCol;
            const float thickness = std::clamp(cfg.box_thickness, 0.5f, 8.f);
            if (cfg.box && !cfg.box_corner) {
                dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), c, 0.f, 0, thickness);
            } else {
                OmniGhost::Gameplay::EspCore::DrawCornerBox(
                    dl, ImVec2(left, top), ImVec2(right, bottom), c, thickness);
            }
        }

        // Bone data arrives as one contiguous snapshot, so drawing the complete
        // chain does not add DMA reads. Keep the same quality at every distance.
        if (cfg.skeleton && p.bones_ok) {
            ImU32 sc = teamCol;
            const float th = std::clamp(cfg.skeleton_thickness, 0.5f, 8.f);

            // Project every required bone once.  Lines and optional joint
            // dots then consume the same stable per-player data.
            ImVec2 projected[CS2::kBoneSlotCount]{};
            bool projectedOk[CS2::kBoneSlotCount]{};
            for (std::size_t bone = 0; bone < CS2::kBoneSlotCount; ++bone) {
                float sx = 0.f, sy = 0.f;
                projectedOk[bone] = W2S(p.bones[bone], rt.view_matrix, sx, sy);
                if (projectedOk[bone]) projected[bone] = ImVec2(sx, sy);
            }

            // Spine column (head → neck → spine chain → pelvis)
            DrawBoneLine(dl, p.bones, projected, projectedOk, 0, 1, sc, th);
            DrawBoneLine(dl, p.bones, projected, projectedOk, 1, 2, sc, th);
            DrawBoneLine(dl, p.bones, projected, projectedOk, 2, 3, sc, th);
            DrawBoneLine(dl, p.bones, projected, projectedOk, 3, 4, sc, th);
            DrawBoneLine(dl, p.bones, projected, projectedOk, 4, 5, sc, th);

            if (true) { // always full arms
                // L: neck/clav → shoulder → elbow → hand
                DrawBoneLine(dl, p.bones, projected, projectedOk, 1, 6, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 6, 7, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 7, 8, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 8, 9, sc, th);
                // R
                DrawBoneLine(dl, p.bones, projected, projectedOk, 1, 10, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 10, 11, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 11, 12, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 12, 13, sc, th);
            }
            if (true) { // always full legs
                DrawBoneLine(dl, p.bones, projected, projectedOk, 5, 14, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 14, 15, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 15, 16, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 5, 17, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 17, 18, sc, th);
                DrawBoneLine(dl, p.bones, projected, projectedOk, 18, 19, sc, th);
            }

            if (cfg.skeleton_joints) {
                const ImU32 jc = Col(cfg.col_joints);
                constexpr float jr = 2.8f;
                static const int kAll[] = {
                    0,1,2,3,4,5, 6,7,8,9, 10,11,12,13, 14,15,16, 17,18,19
                };
                const int* ids = kAll;
                constexpr int nIds = 20;
                for (int i = 0; i < nIds; ++i) {
                    const int bone = ids[i];
                    if (!projectedOk[bone]) continue;
                    dl->AddCircleFilled(projected[bone], jr, jc, 12);
                    dl->AddCircle(projected[bone], jr, IM_COL32(0, 0, 0, 200), 12, 1.0f);
                }
            }
        }

        if (cfg.head_dot) {
            const ImU32 headCol = cfg.visibility_colors
                ? (p.spotted ? Col(cfg.col_head) : Col(cfg.col_occluded))
                : Col(cfg.col_head);
            dl->AddCircle(ImVec2(hx, hy), (std::max)(2.f, h * 0.06f), headCol, 16,
                std::clamp(cfg.head_circle_thickness, 0.5f, 6.f));
        }

        if (cfg.health_bar) {
            float pct = (std::min)(1.f, (std::max)(0.f, p.health / 100.f));
            const float bar_w = 4.f;
            float bx = left - 6.f;
            const ImU32 health_top = Col(cfg.col_health);
            const ImVec4 health_value = ImGui::ColorConvertU32ToFloat4(health_top);
            const ImU32 health_bottom = ImGui::ColorConvertFloat4ToU32(ImVec4(
                health_value.x * 0.48f, health_value.y * 0.48f,
                health_value.z * 0.48f, health_value.w));
            OmniGhost::Gameplay::EspCore::DrawVerticalBar(
                dl, ImVec2(bx - bar_w, top), ImVec2(bx, bottom), pct,
                health_top, health_bottom);
            char hpBuf[16];
            std::snprintf(hpBuf, sizeof(hpBuf), "%d", p.health);
            ImVec2 ts = ImGui::CalcTextSize(hpBuf);
            dl->AddText(ImVec2(bx - bar_w * 0.5f - ts.x * 0.5f, top - ts.y - 2.f), IM_COL32(255,255,255,240), hpBuf);
        }

        if (cfg.armor_bar && p.armor > 0.5f) {
            float pct = (std::min)(1.f, p.armor / 100.f);
            const float bar_w = 3.5f;
            float bx = right + 6.f;
            OmniGhost::Gameplay::EspCore::DrawVerticalBar(
                dl, ImVec2(bx, top), ImVec2(bx + bar_w, bottom), pct,
                Col(cfg.col_armor), Col(cfg.col_armor, 0.48f));
        }

        if (cfg.snaplines) {
            dl->AddLine(ImVec2(ds.x * 0.5f, ds.y), ImVec2(fx, fy), Col(cfg.col_snaplines),
                        std::clamp(cfg.snapline_thickness, 0.5f, 8.f));
        }

        // Name above head
        float textY = top - 16.f;
        if (cfg.name) {
            char label[96];
            if (p.is_bot) std::snprintf(label, sizeof(label), "%s [BOT]", p.name);
            else if (p.is_scoped && cfg.scope_check) std::snprintf(label, sizeof(label), "%s [MIRA]", p.name);
            else std::snprintf(label, sizeof(label), "%s", p.name);
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(hx - ts.x * 0.5f + 1, textY + 1), IM_COL32(0, 0, 0, 180), label);
            dl->AddText(ImVec2(hx - ts.x * 0.5f, textY), teamCol, label);
            textY -= 14.f;
        }

        if (cfg.hit_chance_ui && cfg.aim_enabled) {
            float chance = 100.f - p.distance * 0.35f - (100 - p.health) * 0.15f;
            if (chance < 5.f) chance = 5.f;
            if (chance > 98.f) chance = 98.f;
            char hc[32];
            std::snprintf(hc, sizeof(hc), "%.0f%%", chance);
            ImVec2 ts = ImGui::CalcTextSize(hc);
            dl->AddText(ImVec2(hx - ts.x * 0.5f, textY), IM_COL32(200, 200, 120, 200), hc);
            textY -= 14.f;
        }

        // Below feet: weapon icon (PNG) — updates live with p.weapon_def each frame.
        // Text weapon name was removed; icons are the only weapon display path.
        float belowY = fy + 4.f;
        if (cfg.weapon_icons && p.weapon_def > 0) {
            if (ID3D11ShaderResourceView* srv = CS2_WeaponIcons::Get(p.weapon_def)) {
                int iw = 0, ih = 0;
                CS2_WeaponIcons::GetSize(p.weapon_def, iw, ih);
                const float target_h = 18.f;
                float aspect = (iw > 0 && ih > 0) ? (float)iw / (float)ih : 2.2f;
                const float draw_h = target_h;
                const float draw_w = draw_h * aspect;
                const float x0 = hx - draw_w * 0.5f;
                const float y0 = belowY;
                dl->AddImage((ImTextureID)srv,
                    ImVec2(x0, y0), ImVec2(x0 + draw_w, y0 + draw_h),
                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 245));
                belowY += draw_h + 2.f;
            } else {
                // Fallback glyph/code only when PNG is missing for this def
                const char* icon = WeaponIconCode(p.weapon_def);
                const char* show = (icon && icon[0]) ? icon : nullptr;
                if (show && show[0]) {
                    ImVec2 ts = ImGui::CalcTextSize(show);
                    dl->AddText(ImVec2(hx - ts.x * 0.5f + 1, belowY + 1), IM_COL32(0, 0, 0, 200), show);
                    dl->AddText(ImVec2(hx - ts.x * 0.5f, belowY), IM_COL32(255, 255, 255, 245), show);
                    belowY += 14.f;
                }
            }
        }
        if (cfg.distance) {
            char db[32];
            if (cfg.distance_feet)
                std::snprintf(db, sizeof(db), "%.0fft", p.distance * 3.28084f);
            else
                std::snprintf(db, sizeof(db), "%.0fm", p.distance);
            ImVec2 ts = ImGui::CalcTextSize(db);
            dl->AddText(ImVec2(hx - ts.x * 0.5f + 1, belowY + 1), IM_COL32(0, 0, 0, 180), db);
            dl->AddText(ImVec2(hx - ts.x * 0.5f, belowY), teamCol, db);
            belowY += 14.f;
        }
        if (cfg.smoke_flash && p.is_flashed) {
            dl->AddText(ImVec2(hx - 20.f, belowY), IM_COL32(255, 255, 120, 220), "FLASHED");
        }
    }
}

} // namespace CS2_ESP
