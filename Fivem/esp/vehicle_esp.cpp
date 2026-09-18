#include "vehicle_esp.h"
#include "../game/game.h"
#include "../game/offsets.h"
#include "../game/esp_manager.h"
#include "../../ImGui/imgui.h"
#include <Memory/Memory.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

namespace vehicle_esp {

    Config config;
    std::vector<VehicleData> vehicles;

    static const int MAX_VEHICLES = 64;

    static constexpr uintptr_t VEHICLE_LIST_OFFSET = 0x180;
    static constexpr uintptr_t VEHICLE_COUNT_OFFSET = 0x188;
    static constexpr uintptr_t VEHICLE_ENTRY_STRIDE = 0x10;
    static constexpr uintptr_t VEHICLE_POSITION_OFFSET = 0x90;
    static constexpr uintptr_t VEHICLE_MATRIX_OFFSET = 0x60;

    // Cached entity matrices from last Collect (for oriented 3D boxes)
    static std::unordered_map<uintptr_t, Matrix> g_matrices;

    static ImU32 GetRGBColor() {
        float t = (float)ImGui::GetTime();
        int r = (int)(sinf(t * 2.0f) * 127.f + 128.f);
        int g = (int)(sinf(t * 2.0f + 2.094f) * 127.f + 128.f);
        int b = (int)(sinf(t * 2.0f + 4.188f) * 127.f + 128.f);
        return IM_COL32(r, g, b, 255);
    }

[[maybe_unused]] static bool IsValidPtr(uintptr_t p) {
        return p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL;
    }
    
    // eCarLockState values used by GTA V
    [[maybe_unused]] static bool IsLockedState(uint32_t state) {
        // GTA uses 0/1 for none/unlocked.  The remaining native enum values
        // (2..10) all restrict entry in some form.
        return state >= 2 && state <= 10;
    }

    // Extract basis vectors from a 4x4 row-major entity matrix (GTA style)
    static void MatrixAxes(const Matrix& m, Vec3& right, Vec3& forward, Vec3& up, Vec3& pos) {
        right   = Vec3(m._11, m._12, m._13);
        forward = Vec3(m._21, m._22, m._23);
        up      = Vec3(m._31, m._32, m._33);
        pos     = Vec3(m._41, m._42, m._43);
        // Fallback if matrix is empty: identity around stored position
        if (right.IsZero() && forward.IsZero()) {
            right = Vec3(1, 0, 0);
            forward = Vec3(0, 1, 0);
            up = Vec3(0, 0, 1);
        }
    }

    void Collect(bool force) {
        // Read from vehicle snapshot (populated by acquisition thread)
        // This is now zero-DMA on the render/presentation thread
        using namespace FiveM;
        
        const auto* vSnap = ESP::AcquireVehicleSnapshot();
        if (!vSnap || vSnap->count == 0) {
            vehicles.clear();
            g_matrices.clear();
            return;
        }
        
        // ESP draw gated in Render(); force=true lets lock/repair work with toggle off
        if (!force && !config.enabled) return;
        
        vehicles.clear();
        g_matrices.clear();
        
        // Convert snapshot data to VehicleData format
        for (int i = 0; i < vSnap->count; ++i) {
            const auto& vd = vSnap->vehicles[static_cast<size_t>(i)];
            if (!vd.valid) continue;
            
            VehicleData v;
            v.address = vd.address;
            v.position = vd.position;
            v.distance = vd.distance;
            v.locked = vd.locked;
            v.lock_state_known = vd.lock_state_known;
            v.occupied = vd.occupied;
            v.valid = true;
            vehicles.push_back(v);
            
            if (config.box_3d) {
                g_matrices[vd.address] = vd.matrix;
            }
        }
}

// Project a world point; returns false if behind camera / off-screen enough
    static bool Project(const Vec3& world, const Matrix& view, Vec2& out) {
        return world.world_to_screen(const_cast<Matrix&>(view), out);
    }

    // Oriented 3D box from entity matrix. Half-extents approximate a car.
    static void DrawOrientedBox3D(ImDrawList* dl, const Matrix& entityMat, const Matrix& view,
                                  ImU32 col, float thickness)
    {
        Vec3 right, forward, up, pos;
        MatrixAxes(entityMat, right, forward, up, pos);
        if (pos.IsZero())
            return;

        // Approximate sedan half-extents (metres). Scale slightly with no model data.
        const float hx = 1.05f;  // half-width
        const float hy = 2.30f;  // half-length
        const float hz = 0.85f;  // half-height

        // Normalize axes in case matrix has scale
        auto norm = [](Vec3 v) {
            float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
            if (l < 1e-4f) return Vec3(0, 0, 0);
            return Vec3(v.x / l, v.y / l, v.z / l);
        };
        right = norm(right);
        forward = norm(forward);
        up = norm(up);
        if (right.IsZero() || forward.IsZero())
            return;
        if (up.IsZero())
            up = Vec3(0, 0, 1);

        // Center box slightly above ground origin
        Vec3 center = Vec3(pos.x + up.x * hz, pos.y + up.y * hz, pos.z + up.z * hz);

        // Index bits: 0=right, 1=forward, 2=up
        Vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            float sx = (i & 1) ? hx : -hx;
            float sy = (i & 2) ? hy : -hy;
            float sz = (i & 4) ? hz : -hz;
            corners[i] = Vec3(
                center.x + right.x * sx + forward.x * sy + up.x * sz,
                center.y + right.y * sx + forward.y * sy + up.y * sz,
                center.z + right.z * sx + forward.z * sy + up.z * sz);
        }

        Vec2 sc[8];
        bool ok[8];
        int visible = 0;
        for (int i = 0; i < 8; ++i) {
            ok[i] = Project(corners[i], view, sc[i]);
            if (ok[i]) ++visible;
        }
        if (visible < 2)
            return;

        // 12 edges matching bit-index corners
        static const int edges[12][2] = {
            {0,1},{1,3},{3,2},{2,0}, // bottom (z-)
            {4,5},{5,7},{7,6},{6,4}, // top (z+)
            {0,4},{1,5},{2,6},{3,7}  // vertical
        };
        for (int e = 0; e < 12; ++e) {
            int a = edges[e][0], b = edges[e][1];
            if (!ok[a] || !ok[b]) continue;
            dl->AddLine(ImVec2(sc[a].x, sc[a].y), ImVec2(sc[b].x, sc[b].y), col, thickness);
        }
    }

    void Render() {
        if (!config.enabled || vehicles.empty())
            return;

        using namespace FiveM;

        Matrix view_matrix{};
        if (FiveM::ESP::FrameCacheValid()) {
            view_matrix = FiveM::ESP::GetFrameViewMatrix();
        } else {
            return; // No DMA fallback - skip if frame cache not valid
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;
        ImVec2 screen = ImGui::GetIO().DisplaySize;

        ImU32 boxCol = config.rgb_mode ? GetRGBColor() : config.color_box_3d;
        ImU32 snapCol = config.rgb_mode ? GetRGBColor() : config.color_snaplines;
        ImU32 distCol = config.color_distance;

        uintptr_t nearestAddr = 0;
        float nearestD = 1e9f;
        for (const auto& v : vehicles) {
            if (v.valid && v.distance < nearestD) {
                nearestD = v.distance;
                nearestAddr = v.address;
            }
        }

        // Draw closest vehicles first; hard-cap GPU/line work under load.
        std::vector<const VehicleData*> drawList;
        drawList.reserve(vehicles.size());
        for (const auto& v : vehicles)
            if (v.valid) drawList.push_back(&v);
        std::sort(drawList.begin(), drawList.end(),
            [](const VehicleData* a, const VehicleData* b) { return a->distance < b->distance; });
        constexpr size_t kMaxDraw = 40;
        if (drawList.size() > kMaxDraw)
            drawList.resize(kMaxDraw);

        for (const VehicleData* vp : drawList) {
            const auto& v = *vp;

            Vec2 screenPos;
            if (!Project(v.position, view_matrix, screenPos))
                continue;

            // ── Oriented 3D box (true 3D, size fixed in world metres) ──
            ImU32 useBox = boxCol;
            if (v.address == nearestAddr)
                useBox = IM_COL32(255, 220, 50, 255);

            if (config.box_3d) {
                auto it = g_matrices.find(v.address);
                if (it != g_matrices.end()) {
                    DrawOrientedBox3D(dl, it->second, view_matrix, useBox, v.address == nearestAddr ? 2.4f : 1.6f);
                } else {
                    // Fallback: simple screen box scaled by distance but clamped
                    float scale = std::clamp(180.0f / (v.distance + 2.0f), 20.0f, 120.0f);
                    float boxW = scale * 1.5f;
                    float boxH = scale * 0.9f;
                    dl->AddRect(
                        ImVec2(screenPos.x - boxW, screenPos.y - boxH),
                        ImVec2(screenPos.x + boxW, screenPos.y + boxH * 0.5f),
                        boxCol, 0.f, 0, 1.5f);
                }
            }

            if (config.snaplines) {
                ImVec2 start;
                if (config.snapline_pos == 0)
                    start = ImVec2(screen.x * 0.5f, 0.0f);
                else if (config.snapline_pos == 1)
                    start = ImVec2(screen.x * 0.5f, screen.y * 0.5f);
                else
                    start = ImVec2(screen.x * 0.5f, screen.y);
                dl->AddLine(start, ImVec2(screenPos.x, screenPos.y), snapCol, 1.2f);
            }

            float textY = screenPos.y + 8.f;

            if (config.distance) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0fm", v.distance);
                ImVec2 ts = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY), distCol, buf);
                textY += ts.y + 2.f;
            }

            if (config.show_speed) {
                // Speed from inter-frame position delta (no fragile velocity offset)
                static std::unordered_map<uintptr_t, std::pair<Vec3, float>> s_prev; // pos, last time
                float now = (float)ImGui::GetTime();
                float speedKmh = 0.f;
                auto it = s_prev.find(v.address);
                if (it != s_prev.end()) {
                    float dt = now - it->second.second;
                    if (dt > 0.02f && dt < 1.0f) {
                        float meters = v.position.distance_to(it->second.first);
                        speedKmh = (meters / dt) * 3.6f;
                    }
                }
                s_prev[v.address] = { v.position, now };
                if (speedKmh > 0.5f) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.0f km/h", speedKmh);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY),
                        IM_COL32(180, 220, 255, 230), buf);
                    textY += ts.y + 2.f;
                }
            }

            if (config.show_occupants) {
                const char* occ = v.occupied ? "Ocupado" : "Desocupado";
                ImU32 col = v.occupied ? config.color_occupants : IM_COL32(120, 200, 120, 220);
                ImVec2 ts = ImGui::CalcTextSize(occ);
                dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY), col, occ);
                textY += ts.y + 2.f;
            }

            if (config.lock_status) {
                const char* status = !v.lock_state_known ? "Estado desconhecido"
                    : (v.locked ? "Trancado" : "Destrancado");
                ImU32 col = !v.lock_state_known ? IM_COL32(160, 160, 160, 220)
                    : (v.locked ? config.color_locked : config.color_unlocked);
                ImVec2 ts = ImGui::CalcTextSize(status);
                dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY), col, status);
                textY += ts.y + 2.f;
            }

            // Gear / engine: use cached data from acquisition (no DMA in render)
            if (config.show_gear) {
                char buf[32];
                snprintf(buf, sizeof(buf), "G%d", v.gear);
                ImVec2 ts = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY),
                    config.color_gear, buf);
                textY += ts.y + 2.f;
            }
            if (config.show_engine) {
                char buf[32];
                snprintf(buf, sizeof(buf), "Motor %.0f", v.engine_hp);
                ImVec2 ts = ImGui::CalcTextSize(buf);
                ImU32 col = v.engine_hp < 300.f ? IM_COL32(255, 80, 80, 220) : IM_COL32(180, 255, 180, 220);
                dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY), col, buf);
                textY += ts.y + 2.f;
            }
        }
    }

    void Run() {
        Collect();
        Render();
    }

} // namespace vehicle_esp
