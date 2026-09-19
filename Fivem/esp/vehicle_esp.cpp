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

    static bool IsValidPtr(uintptr_t p) {
        return p > 0x10000ULL && p < 0x7FFFFFFFFFFFULL;
    }

    // eCarLockState values used by GTA V
    static bool IsLockedState(uint32_t state) {
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
        static auto s_lastPose = std::chrono::steady_clock::time_point{};
        static auto s_lastDiscovery = std::chrono::steady_clock::time_point{};
        auto nowc = std::chrono::steady_clock::now();
        // Pose/matrix refresh ~30 Hz for smooth vehicle ESP; list discovery slower.
        if (!force && s_lastPose.time_since_epoch().count() != 0 &&
            nowc - s_lastPose < std::chrono::milliseconds(33))
            return;
        s_lastPose = nowc;
        const bool rediscover = force || s_lastDiscovery.time_since_epoch().count() == 0 ||
            (nowc - s_lastDiscovery) >= std::chrono::milliseconds(100);
        if (rediscover)
            s_lastDiscovery = nowc;
        vehicles.clear();
        g_matrices.clear();
        // ESP draw gated in Render(); force=true lets lock/repair work with toggle off
        if (!force && !config.enabled) return;

        using namespace FiveM;

        if (!offset::replay || !offset::viewport || !offset::localplayer)
            return;

        auto handle = mem.CreateScatterHandle();

        Matrix view_matrix{};
        Vec3 localPos{};
        uintptr_t vehicle_interface = 0;
        uintptr_t vehicle_interface_alt = 0;

        mem.AddScatterReadRequest(handle, offset::viewport + 0x24C, &view_matrix, sizeof(Matrix));
        mem.AddScatterReadRequest(handle, offset::localplayer + offset::playerPosition, &localPos, sizeof(Vec3));
        // Normal CReplayInterface layout uses +0x10.  The supplied b3258 dump
        // also advertises +0xD10, so probe it as a fallback and validate the
        // resulting list rather than trusting either blindly.
        mem.AddScatterReadRequest(handle, offset::replay + 0x10, &vehicle_interface, sizeof(uintptr_t));
        mem.AddScatterReadRequest(handle, offset::replay + 0xD10, &vehicle_interface_alt, sizeof(uintptr_t));
        mem.ExecuteReadScatter(handle);

        if (!IsValidPtr(vehicle_interface) && IsValidPtr(vehicle_interface_alt))
            vehicle_interface = vehicle_interface_alt;
        if (!IsValidPtr(vehicle_interface)) {
            mem.CloseScatterHandle(handle);
            return;
        }

        uintptr_t vehicleListBase = 0, vehicleListBaseAlt = 0;
        int vehicleCount = 0, vehicleCountAlt = 0;
        mem.AddScatterReadRequest(handle, vehicle_interface + VEHICLE_LIST_OFFSET, &vehicleListBase, sizeof(uintptr_t));
        mem.AddScatterReadRequest(handle, vehicle_interface + VEHICLE_COUNT_OFFSET, &vehicleCount, sizeof(int));
        if (IsValidPtr(vehicle_interface_alt) && vehicle_interface_alt != vehicle_interface) {
            mem.AddScatterReadRequest(handle, vehicle_interface_alt + VEHICLE_LIST_OFFSET, &vehicleListBaseAlt, sizeof(uintptr_t));
            mem.AddScatterReadRequest(handle, vehicle_interface_alt + VEHICLE_COUNT_OFFSET, &vehicleCountAlt, sizeof(int));
        }
        mem.ExecuteReadScatter(handle);

        auto validList = [](uintptr_t list, int count) {
            return IsValidPtr(list) && count > 0 && count <= 2048;
        };
        if (!validList(vehicleListBase, vehicleCount) && validList(vehicleListBaseAlt, vehicleCountAlt)) {
            vehicle_interface = vehicle_interface_alt;
            vehicleListBase = vehicleListBaseAlt;
            vehicleCount = vehicleCountAlt;
        }
        if (!IsValidPtr(vehicleListBase)) {
            mem.CloseScatterHandle(handle);
            return;
        }

        const int listCount = (vehicleCount > 0 && vehicleCount <= 2048)
            ? (std::min)(vehicleCount, MAX_VEHICLES) : MAX_VEHICLES;
        std::vector<uintptr_t> rawPtrs(listCount, 0);
        for (int i = 0; i < listCount; ++i)
            mem.AddScatterReadRequest(handle, vehicleListBase + (uintptr_t)i * VEHICLE_ENTRY_STRIDE,
                                      &rawPtrs[i], sizeof(uintptr_t));
        mem.ExecuteReadScatter(handle);

        std::vector<uintptr_t> valid;
        valid.reserve(MAX_VEHICLES);
        for (int i = 0; i < listCount; ++i) {
            if (IsValidPtr(rawPtrs[i]))
                valid.push_back(rawPtrs[i]);
        }

        if (valid.empty()) {
            mem.CloseScatterHandle(handle);
            return;
        }

        std::vector<Vec3> positions(valid.size());
        std::vector<Matrix> matrices(valid.size());
        std::vector<uint32_t> lockState(valid.size(), UINT32_MAX);
        std::vector<uintptr_t> driverPrimary(valid.size(), 0), driverFallback(valid.size(), 0);

        const bool needMatrix = config.box_3d;
        const bool needLock = config.lock_status;
        const bool needOccupied = config.ignore_occupied || config.show_occupants;
        for (size_t i = 0; i < valid.size(); ++i) {
            mem.AddScatterReadRequest(handle, valid[i] + VEHICLE_POSITION_OFFSET, &positions[i], sizeof(Vec3));
            if (needMatrix)
                mem.AddScatterReadRequest(handle, valid[i] + VEHICLE_MATRIX_OFFSET, &matrices[i], sizeof(Matrix));
            if (needLock)
                mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleLock,
                                          &lockState[i], sizeof(uint32_t));
            if (needOccupied) {
                const uintptr_t buildDriver = offset::buildVersion >= 3751 ? 0xCA8 : offset::vehicleDriver;
                mem.AddScatterReadRequest(handle, valid[i] + buildDriver,
                                          &driverPrimary[i], sizeof(uintptr_t));
                if (buildDriver != offset::vehicleDriver)
                    mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleDriver,
                                              &driverFallback[i], sizeof(uintptr_t));
            }
        }
        mem.ExecuteReadScatter(handle);

        // Only walk ped->vehicle when occupied filtering/labels are needed.
        std::vector<uintptr_t> pedVehicles;
        if (needOccupied && !FiveM::ESP::validPeds.empty()) {
            pedVehicles.assign(FiveM::ESP::validPeds.size(), 0);
            for (size_t i = 0; i < FiveM::ESP::validPeds.size(); ++i)
                mem.AddScatterReadRequest(handle, FiveM::ESP::validPeds[i] + offset::pedVehicle,
                                          &pedVehicles[i], sizeof(uintptr_t));
            mem.ExecuteReadScatter(handle);
        }
        mem.CloseScatterHandle(handle);

        std::unordered_set<uintptr_t> occupiedVehicles;
        occupiedVehicles.reserve(pedVehicles.size() * 2 + valid.size());
        for (uintptr_t vehicle : pedVehicles)
            if (IsValidPtr(vehicle)) occupiedVehicles.insert(vehicle);
        for (size_t i = 0; i < valid.size(); ++i) {
            uintptr_t driver = IsValidPtr(driverPrimary[i]) ? driverPrimary[i] : driverFallback[i];
            if (IsValidPtr(driver)) occupiedVehicles.insert(valid[i]);
        }

        for (size_t i = 0; i < valid.size(); ++i) {
            if (positions[i].IsZero())
                continue;

            float dist = positions[i].distance_to(localPos);
            if (dist > config.max_distance || dist < 0.1f)
                continue;

            bool occupied = occupiedVehicles.find(valid[i]) != occupiedVehicles.end();

            if (config.ignore_occupied && occupied)
                continue;

            const bool lockKnown = lockState[i] <= 10u;
            bool locked = lockKnown && IsLockedState(lockState[i]);

            VehicleData vd;
            vd.address = valid[i];
            vd.position = positions[i];
            vd.distance = dist;
            vd.locked = locked;
            vd.lock_state_known = lockKnown;
            vd.occupied = occupied;
            vd.valid = true;
            vehicles.push_back(vd);
            g_matrices[valid[i]] = matrices[i];
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
            // Fallback only when the player ESP frame did not publish a matrix.
            mem.Read(offset::viewport + 0x24C, &view_matrix, sizeof(Matrix));
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

            // Gear / engine: medium-rate cache (not per-frame DMA)
            {
                struct GearCache { int8_t gear = 0; float eng = 0.f; double t = 0.0; };
                static std::unordered_map<uintptr_t, GearCache> s_gear;
                const double nowG = ImGui::GetTime();
                GearCache& gc = s_gear[v.address];
                if (nowG - gc.t > 0.080) {
                    gc.gear = mem.Read<int8_t>(v.address + FiveM::offset::vehicleGear);
                    gc.eng = mem.Read<float>(v.address + FiveM::offset::vehicleEngineHp);
                    gc.t = nowG;
                }
                char buf[48];
                if (gc.gear >= -1 && gc.gear <= 8) {
                    snprintf(buf, sizeof(buf), "G%d", (int)gc.gear);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY),
                        IM_COL32(255, 220, 120, 220), buf);
                    textY += ts.y + 2.f;
                }
                if (gc.eng > 1.f && gc.eng <= 1000.f) {
                    snprintf(buf, sizeof(buf), "Motor %.0f", gc.eng);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY),
                        gc.eng < 300.f ? IM_COL32(255, 80, 80, 220) : IM_COL32(180, 255, 180, 220), buf);
                    textY += ts.y + 2.f;
                }
            }
        }
    }

    void Run() {
        Collect();
        Render();
    }

} // namespace vehicle_esp
