#include "cs2_esp.h"
#include "cs2_weapons.h"
#include "cs2_weapon_icons.h"
#include "cs2_aim.h"
#include "aimbot/aim_type.h"
#include "gameplay/esp_core.h"
#include "gameplay/trail_history.h"
#include "gameplay/esp_fx.h"
#include "gameplay/esp_optimizer.h"
#include "updater/http_client.h"
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
#include <future>
#include <wincodec.h>

namespace CS2_ESP {
namespace {

struct AvatarEntry {
    std::future<std::vector<unsigned char>> pending;
    ID3D11ShaderResourceView* texture = nullptr;
    bool requested = false;
};
static std::unordered_map<uint64_t, AvatarEntry> g_avatarCache;
static ID3D11Device* g_avatarDevice = nullptr;

std::vector<unsigned char> FetchSteamAvatar(uint64_t steamId) {
    std::atomic_bool cancelled{false};
    OmniGhost::Update::WinHttpClient http;
    const auto profile = http.GetText("https://steamcommunity.com/profiles/" +
        std::to_string(steamId) + "?xml=1", 4000, cancelled);
    if (profile.statusCode != 200) return {};
    constexpr const char* open = "<avatarMedium><![CDATA[";
    const auto begin = profile.body.find(open);
    if (begin == std::string::npos) return {};
    const auto urlBegin = begin + std::strlen(open);
    const auto end = profile.body.find("]]></avatarMedium>", urlBegin);
    if (end == std::string::npos) return {};
    const auto image = http.GetText(profile.body.substr(urlBegin, end - urlBegin),
                                    4000, cancelled);
    if (image.statusCode != 200 || image.body.size() > 2u * 1024u * 1024u) return {};
    return {image.body.begin(), image.body.end()};
}

ID3D11ShaderResourceView* DecodeAvatar(ID3D11Device* device,
                                        const std::vector<unsigned char>& bytes) {
    if (!device || bytes.empty() || bytes.size() > MAXDWORD) return nullptr;
    IWICImagingFactory* factory = nullptr; IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr; IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr; ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr; UINT w = 0, h = 0;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory)))) goto done;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes.data()),
                                            static_cast<DWORD>(bytes.size()))) ||
        FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&w, &h)) || !w || !h || w > 512 || h > 512) goto done;
    {
        std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4u);
        if (FAILED(converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(pixels.size()), pixels.data()))) goto done;
        D3D11_TEXTURE2D_DESC desc{}; desc.Width=w; desc.Height=h; desc.MipLevels=1; desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{}; data.pSysMem=pixels.data(); data.SysMemPitch=w*4u;
        if (FAILED(device->CreateTexture2D(&desc, &data, &texture)) ||
            FAILED(device->CreateShaderResourceView(texture, nullptr, &view))) view = nullptr;
    }
done:
    if (texture) texture->Release(); if (converter) converter->Release();
    if (frame) frame->Release(); if (decoder) decoder->Release();
    if (stream) stream->Release(); if (factory) factory->Release();
    return view;
}

ID3D11ShaderResourceView* SteamAvatar(uint64_t steamId) {
    if (!steamId || !g_overlay_instance || !g_overlay_instance->device) return nullptr;
    ID3D11Device* device = g_overlay_instance->device;
    if (g_avatarDevice != device) {
        for (auto& [_, entry] : g_avatarCache) if (entry.texture) entry.texture->Release();
        g_avatarCache.clear(); g_avatarDevice = device;
    }
    auto& entry = g_avatarCache[steamId];
    if (!entry.requested) {
        entry.requested = true;
        entry.pending = std::async(std::launch::async, FetchSteamAvatar, steamId);
    }
    if (!entry.texture && entry.pending.valid() &&
        entry.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        entry.texture = DecodeAvatar(device, entry.pending.get());
    return entry.texture;
}

ImU32 Col(const float* c, float aMul = 1.f) {
    int a = (int)(c[3] * aMul * 255.f);
    if (a < 0) a = 0; if (a > 255) a = 255;
    return IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255), a);
}

bool W2S(const float* world, const float* vm, float& sx, float& sy);

// The acquisition lane already validates and caches complete snapshots.  The
// renderer deliberately presents the newest complete sample: blending it here
// made the ESP visibly trail a running player and a fast camera turn.
static float g_presentViewMatrix[16]{};
static uint64_t g_viewSnapshotMs = 0;
static bool g_havePresentationView = false;

void UpdatePresentationViewMatrix(const CS2::Runtime& rt, const float* latestMatrix,
                                  uint64_t matrixTimestamp) {
    if (matrixTimestamp && matrixTimestamp != g_viewSnapshotMs) {
        std::memcpy(g_presentViewMatrix, latestMatrix ? latestMatrix : rt.view_matrix,
                    sizeof(g_presentViewMatrix));
        g_viewSnapshotMs = matrixTimestamp;
    }
    if (!g_viewSnapshotMs)
        std::memcpy(g_presentViewMatrix, latestMatrix ? latestMatrix : rt.view_matrix,
                    sizeof(g_presentViewMatrix));
    g_havePresentationView = true;
}

CS2::Player SmoothPlayerForPresentation(const CS2::Player& raw,
                                        const CS2::MotionSnapshot* motion) {
    CS2::Player output = raw;
    if (!motion || !raw.pawn) return output;

    for (uint32_t i = 0; i < motion->count; ++i) {
        const auto& sample = motion->players[i];
        if (sample.pawn != raw.pawn) continue;
        const float dx = sample.pos[0] - raw.pos[0];
        const float dy = sample.pos[1] - raw.pos[1];
        const float dz = sample.pos[2] - raw.pos[2];
        // Never apply a stale/recycled scene node as a visual teleport.
        if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz) ||
            dx * dx + dy * dy + dz * dz > 192.f * 192.f)
            return output;
        output.pos[0] += dx; output.pos[1] += dy; output.pos[2] += dz;
        output.head[0] += dx; output.head[1] += dy; output.head[2] += dz;
        if (output.bones_ok) {
            for (std::size_t bone = 0; bone < CS2::kBoneSlotCount; ++bone) {
                output.bones[bone][0] += dx;
                output.bones[bone][1] += dy;
                output.bones[bone][2] += dz;
            }
        }
        return output;
    }
    return output;
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
    // All W2S calls made while drawing a CS2 frame use the exact same
    // temporally interpolated camera.  This prevents bones, boxes and labels
    // from stepping differently when the player turns the view quickly.
    if (g_havePresentationView) vm = g_presentViewMatrix;
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
    if (rt.spectator_target != rt.local_pawn && rt.spectator_target_name[0])
        std::snprintf(title, sizeof(title), "A observar %s (%d)",
                      rt.spectator_target_name, rt.spectator_count);
    else
        std::snprintf(title, sizeof(title), "A observar-te (%d)", rt.spectator_count);
    dl->AddText(ImVec2(x, y), IM_COL32(212, 175, 55, 255), title);
    y += 18.f;
    int shown = 0;
    // Prefer dedicated spectators vector when populated
    const auto& list = !rt.spectators.empty() ? rt.spectators : rt.players;
    for (const auto& p : list) {
        if (!p.is_spectator && &list == &rt.players) continue;
        if (p.is_local) continue;
        if (auto* avatar = SteamAvatar(p.steam_id))
            dl->AddImage(reinterpret_cast<ImTextureID>(avatar), ImVec2(x, y), ImVec2(x + 18.f, y + 18.f));
        else
            dl->AddCircleFilled(ImVec2(x + 9.f, y + 9.f), 8.f, IM_COL32(60, 60, 66, 230));
        dl->AddText(ImVec2(x + 24.f, y + 2.f), IM_COL32(200, 200, 200, 220),
                    p.name[0] ? p.name : "Jogador");
        y += 22.f;
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
    const auto fastCamera = CS2::AcquireCameraSnapshot();
    // The camera lane runs independently at 2–4 ms.  Position/bone snapshots
    // can remain coherent and heavier, while rapid mouse turns are projected
    // with the freshest available matrix for this exact render frame.
    UpdatePresentationViewMatrix(rt, fastCamera ? fastCamera->view_matrix : rt.view_matrix,
        fastCamera ? fastCamera->timestamp_ms : rt.snapshot_timestamp_ms);
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

    // ── ESP style CS2-DMA: caixa (head→feet), corner, skeleton chains, bars ──
    if (cfg.weapon_icons && g_overlay_instance && g_overlay_instance->device)
        CS2_WeaponIcons::EnsureLoaded(g_overlay_instance->device);

    auto health_color = [](float ratio) -> ImU32 {
        ratio = std::clamp(ratio, 0.f, 1.f);
        if (ratio > 0.66f) return IM_COL32(96, 246, 113, 230);
        if (ratio > 0.33f) return IM_COL32(247, 214, 103, 230);
        return IM_COL32(255, 95, 95, 230);
    };

    auto draw_corner_box = [&](float x, float y, float w, float h, ImU32 col, float th, float frac) {
        const float lx = w * frac, ly = h * frac;
        // TL
        dl->AddLine(ImVec2(x, y), ImVec2(x + lx, y), col, th);
        dl->AddLine(ImVec2(x, y), ImVec2(x, y + ly), col, th);
        // TR
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w - lx, y), col, th);
        dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + ly), col, th);
        // BL
        dl->AddLine(ImVec2(x, y + h), ImVec2(x + lx, y + h), col, th);
        dl->AddLine(ImVec2(x, y + h), ImVec2(x, y + h - ly), col, th);
        // BR
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w - lx, y + h), col, th);
        dl->AddLine(ImVec2(x + w, y + h), ImVec2(x + w, y + h - ly), col, th);
    };

    // BoneSlot chains matching CS2-DMA BoneJointList (after reference index map)
    static constexpr int kChainTrunk[] = { 1, 2, 3, 4, 5 };           // neck..pelvis via spine
    static constexpr int kChainLArm[]  = { 1, 7, 8, 9 };              // neck, shoulder, elbow, hand
    static constexpr int kChainRArm[]  = { 1, 11, 12, 13 };
    static constexpr int kChainLLeg[]  = { 5, 14, 15, 16 };
    static constexpr int kChainRLeg[]  = { 5, 17, 18, 19 };
    static constexpr const int* kChains[] = { kChainTrunk, kChainLArm, kChainRArm, kChainLLeg, kChainRLeg };
    static constexpr int kChainLen[] = { 5, 4, 4, 4, 4 };

    for (int pi = 0; pi < (int)rt.players.size(); ++pi) {
        const CS2::Player p = SmoothPlayerForPresentation(rt.players[pi]);
        if (p.is_local && !cfg.self_esp) continue;
        if (!p.is_local && cfg.team_check && p.team == rt.local_team) continue;
        if (p.distance > cfg.max_distance) continue;
        if (cfg.visible_check && !p.spotted) continue;
        if (!p.alive && p.health <= 0) continue;

        float colVis[4];
        const float* colBase = (p.team == rt.local_team) ? cfg.col_team : cfg.col_enemy;
        for (int i = 0; i < 4; ++i) colVis[i] = colBase[i];
        if (cfg.visibility_colors && !p.is_local) {
            if (p.spotted) { colVis[0] = 0.25f; colVis[1] = 0.95f; colVis[2] = 0.35f; }
            else { colVis[0] = 0.95f; colVis[1] = 0.35f; colVis[2] = 0.30f; }
        }
        const ImU32 teamCol = Col(colVis);
        const ImU32 boxCol = cfg.box ? Col(cfg.col_box) : teamCol;

        // Screen: feet = origin, head = bone head or +72 hull (CS2-DMA Get2DBox)
        float fx = 0.f, fy = 0.f, hx = 0.f, hy = 0.f;
        if (!W2S(p.pos, rt.view_matrix, fx, fy))
            continue;
        bool head_ok = false;
        if (p.bones_ok) {
            head_ok = W2S(p.bones[0], rt.view_matrix, hx, hy);
        }
        if (!head_ok) {
            const float headWorld[3] = { p.pos[0], p.pos[1], p.pos[2] + 72.f };
            if (!W2S(headWorld, rt.view_matrix, hx, hy))
                continue;
        }

        // CS2-DMA: Size.y = (feetY - headY) * 1.09; Size.x = Size.y * 0.6
        float boxH = (fy - hy) * 1.09f;
        if (boxH < 4.f) boxH = fabsf(fy - hy);
        if (boxH < 8.f) boxH = 8.f;
        float boxW = boxH * 0.6f;
        float boxX = fx - boxW * 0.5f;
        float boxY = hy - boxH * 0.08f;
        if (boxH > 4000.f || boxW < 2.f) continue;
        const float left = boxX, right = boxX + boxW, top = boxY, bottom = boxY + boxH;

        // Box
        const float thBox = std::clamp(cfg.box_thickness, 0.5f, 8.f);
        if (cfg.box_corner) {
            draw_corner_box(left, top, boxW, boxH, Col(cfg.col_box_corner), thBox, 0.25f);
        } else if (cfg.box) {
            dl->AddRect(ImVec2(left, top), ImVec2(right, bottom), boxCol, 0.f, 0, thBox);
            dl->AddRect(ImVec2(left - 1.f, top - 1.f), ImVec2(right + 1.f, bottom + 1.f),
                        IM_COL32(0, 0, 0, 120), 0.f, 0, 1.f);
        }

        // Skeleton (CS2-DMA DrawBone chains)
        if (cfg.skeleton && p.bones_ok) {
            const float th = std::clamp(cfg.skeleton_thickness, 0.5f, 8.f);
            const ImU32 skCol = Col(cfg.col_skeleton);
            float screen[CS2::kBoneSlotCount][2]{};
            bool ok[CS2::kBoneSlotCount]{};
            for (std::size_t b = 0; b < CS2::kBoneSlotCount; ++b) {
                ok[b] = W2S(p.bones[b], rt.view_matrix, screen[b][0], screen[b][1]);
            }
            for (int c = 0; c < 5; ++c) {
                if (c >= 1 && c <= 2 && !cfg.bone_draw_arms) continue;
                if (c >= 3 && !cfg.bone_draw_legs) continue;
                for (int i = 1; i < kChainLen[c]; ++i) {
                    const int a = kChains[c][i - 1];
                    const int b = kChains[c][i];
                    if (a < 0 || b < 0 || a >= (int)CS2::kBoneSlotCount || b >= (int)CS2::kBoneSlotCount)
                        continue;
                    if (!ok[a] || !ok[b]) continue;
                    dl->AddLine(ImVec2(screen[a][0], screen[a][1]),
                                ImVec2(screen[b][0], screen[b][1]), skCol, th);
                }
            }
            if (cfg.skeleton_joints) {
                for (std::size_t b = 0; b < CS2::kBoneSlotCount; ++b) {
                    if (!ok[b]) continue;
                    dl->AddCircleFilled(ImVec2(screen[b][0], screen[b][1]), 2.2f, Col(cfg.col_joints), 8);
                }
            }
        }

        // Head dot
        if (cfg.head_dot) {
            float r = std::clamp(boxH * 0.06f, 2.f, 8.f);
            dl->AddCircle(ImVec2(hx, hy), r, Col(cfg.col_head), 16, 1.6f);
        }

        // Eye / look line
        if (cfg.eye_line || cfg.look_direction) {
            const float yaw = p.view_yaw * 0.01745329251f;
            const float len = 40.f;
            float endW[3] = {
                p.bones_ok ? p.bones[0][0] : p.pos[0],
                p.bones_ok ? p.bones[0][1] : p.pos[1],
                p.bones_ok ? p.bones[0][2] : p.pos[2] + 64.f
            };
            endW[0] += std::cos(yaw) * len;
            endW[1] += std::sin(yaw) * len;
            float ex = 0.f, ey = 0.f;
            if (W2S(endW, rt.view_matrix, ex, ey))
                dl->AddLine(ImVec2(hx, hy), ImVec2(ex, ey), Col(cfg.col_look), 1.4f);
        }

        // Health bar (CS2-DMA style left)
        if (cfg.health_bar) {
            const float ratio = std::clamp(p.health / 100.f, 0.f, 1.f);
            const float barW = 3.5f;
            const float bx = left - 6.f;
            dl->AddRectFilled(ImVec2(bx - 1.f, top - 1.f), ImVec2(bx + barW + 1.f, bottom + 1.f),
                              IM_COL32(20, 20, 20, 180));
            const float filled = boxH * ratio;
            dl->AddRectFilled(ImVec2(bx, bottom - filled), ImVec2(bx + barW, bottom), health_color(ratio));
            {
                char hp[16];
                std::snprintf(hp, sizeof(hp), "%d", p.health);
                ImVec2 ts = ImGui::CalcTextSize(hp);
                dl->AddText(ImVec2(bx - ts.x * 0.5f + barW * 0.5f, top - ts.y - 2.f),
                            IM_COL32(255, 255, 255, 240), hp);
            }
        }

        if (cfg.armor_bar && p.armor > 0) {
            const float ratio = std::clamp(p.armor / 100.f, 0.f, 1.f);
            const float barW = 3.f;
            const float bx = right + 4.f;
            dl->AddRectFilled(ImVec2(bx, top), ImVec2(bx + barW, bottom), IM_COL32(20, 20, 20, 160));
            dl->AddRectFilled(ImVec2(bx, bottom - boxH * ratio), ImVec2(bx + barW, bottom), Col(cfg.col_armor));
        }

        if (cfg.snaplines) {
            dl->AddLine(ImVec2(ds.x * 0.5f, ds.y), ImVec2(fx, fy), Col(cfg.col_snaplines),
                        std::clamp(cfg.snapline_thickness, 0.5f, 8.f));
        }

        float textY = top - 16.f;
        if (cfg.name) {
            char label[96];
            if (p.is_bot) std::snprintf(label, sizeof(label), "%s [BOT]", p.name);
            else std::snprintf(label, sizeof(label), "%s", p.name[0] ? p.name : "Jogador");
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(hx - ts.x * 0.5f + 1.f, textY + 1.f), IM_COL32(0, 0, 0, 180), label);
            dl->AddText(ImVec2(hx - ts.x * 0.5f, textY), Col(cfg.col_name), label);
            textY -= 14.f;
        }

        float belowY = bottom + 3.f;
        if (cfg.weapon || cfg.weapon_icons) {
            if (cfg.weapon_icons && p.weapon_def > 0) {
                if (ID3D11ShaderResourceView* srv = CS2_WeaponIcons::Get(p.weapon_def)) {
                    int iw = 0, ih = 0;
                    CS2_WeaponIcons::GetSize(p.weapon_def, iw, ih);
                    float aspect = (iw > 0 && ih > 0) ? (float)iw / (float)ih : 2.2f;
                    const float draw_h = 16.f;
                    const float draw_w = draw_h * aspect;
                    const float x0 = hx - draw_w * 0.5f;
                    dl->AddImage((ImTextureID)srv, ImVec2(x0, belowY), ImVec2(x0 + draw_w, belowY + draw_h));
                    belowY += draw_h + 2.f;
                }
            }
            if (cfg.weapon && p.weapon[0]) {
                ImVec2 ts = ImGui::CalcTextSize(p.weapon);
                dl->AddText(ImVec2(hx - ts.x * 0.5f, belowY), Col(cfg.col_weapon), p.weapon);
                belowY += 14.f;
            }
        }

        if (cfg.distance) {
            char db[32];
            if (cfg.distance_feet)
                std::snprintf(db, sizeof(db), "%.0fft", p.distance * 3.28084f);
            else
                std::snprintf(db, sizeof(db), "%.0fm", p.distance);
            ImVec2 ts = ImGui::CalcTextSize(db);
            dl->AddText(ImVec2(hx - ts.x * 0.5f, belowY), Col(cfg.col_distance), db);
            belowY += 14.f;
        }

        if (cfg.smoke_flash && p.is_flashed)
            dl->AddText(ImVec2(hx - 20.f, belowY), IM_COL32(255, 255, 120, 220), "FLASHED");
    }
}

} // namespace CS2_ESP
