#include "vehicle_esp.h"
#include "../game/game.h"
#include "../game/offsets.h"
#include "../game/esp_manager.h"
#include "third_party/ImGui/imgui.h"
#include <Memory/Memory.h>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace vehicle_esp {

    Config config;
    std::vector<VehicleData> vehicles;

    static const int MAX_VEHICLES = 64;


    static std::unordered_map<uintptr_t, Matrix> g_matrices;
    static Matrix g_vehicleViewMatrix{};
    static std::chrono::steady_clock::time_point g_vehicleSampleTime{};
    static std::mutex g_vehicleMutex;
    static VMMDLL_SCATTER_HANDLE g_vehicleScatter = nullptr;
    struct GearState { int8_t gear = 0; float engine = 0.f; std::chrono::steady_clock::time_point seen{}; };
    static std::unordered_map<uintptr_t, GearState> g_gearCache;
    static std::chrono::steady_clock::time_point g_lastGearRefresh{};
    static std::unordered_map<uintptr_t, std::pair<Vec3, float>> g_speedHistory;

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


    static bool IsLockedState(uint32_t state) {


        return state >= 2 && state <= 10;
    }


    static void MatrixAxes(const Matrix& m, Vec3& right, Vec3& forward, Vec3& up, Vec3& pos) {
        right   = Vec3(m._11, m._12, m._13);
        forward = Vec3(m._21, m._22, m._23);
        up      = Vec3(m._31, m._32, m._33);
        pos     = Vec3(m._41, m._42, m._43);

        if (right.IsZero() && forward.IsZero()) {
            right = Vec3(1, 0, 0);
            forward = Vec3(0, 1, 0);
            up = Vec3(0, 0, 1);
        }
    }

    void Collect(const Config& frameConfig, const std::vector<uintptr_t>& peds,
                 const Vec3& frameLocalPos,
                 const Matrix& cameraMatrix, bool force) {
        static auto s_lastPose = std::chrono::steady_clock::time_point{};
        auto nowc = std::chrono::steady_clock::now();


        if (!force && s_lastPose.time_since_epoch().count() != 0 &&
            nowc - s_lastPose < std::chrono::milliseconds(100))
            return;
        s_lastPose = nowc;
        std::vector<VehicleData> nextVehicles;
        std::unordered_map<uintptr_t, Matrix> nextMatrices;
        std::chrono::steady_clock::time_point positionSampleTime{};
        const auto publish = [&] (const Matrix& view) {
            std::scoped_lock lock(g_vehicleMutex);
            vehicles.swap(nextVehicles);
            g_matrices.swap(nextMatrices);
            g_vehicleViewMatrix = view;
            g_vehicleSampleTime = positionSampleTime.time_since_epoch().count()
                ? positionSampleTime : std::chrono::steady_clock::now();
        };
        Matrix view_matrix = cameraMatrix;

        if (!force && !frameConfig.enabled) { publish(view_matrix); return; }

        using namespace FiveM;

        if (!offset::replay) {
            publish(view_matrix);
            return;
        }

        if (!g_vehicleScatter && mem.vHandle)
            g_vehicleScatter = mem.CreateScatterHandle();
        auto handle = g_vehicleScatter;
        if (!handle) { publish(view_matrix); return; }

        const Vec3 localPos = frameLocalPos;
        uintptr_t vehicle_interface = 0;
        uintptr_t vehicle_interface_alt = 0;



        mem.AddScatterReadRequest(handle, offset::replay + offset::replayVehicleInterface, &vehicle_interface, sizeof(uintptr_t));
        if (offset::replayVehicleInterfaceAlt)
            mem.AddScatterReadRequest(handle, offset::replay + offset::replayVehicleInterfaceAlt, &vehicle_interface_alt, sizeof(uintptr_t));
        mem.SetDmaCallTag("FiveM.VehicleRoots");
        mem.ExecuteReadScatter(handle);
        if (localPos.IsZero()) {
            publish(view_matrix);
            return;
        }

        if (!IsValidPtr(vehicle_interface) && IsValidPtr(vehicle_interface_alt))
            vehicle_interface = vehicle_interface_alt;
        if (!IsValidPtr(vehicle_interface)) {
            publish(view_matrix);
            return;
        }

        uintptr_t vehicleListBase = 0, vehicleListBaseAlt = 0;
        int vehicleCount = 0, vehicleCountAlt = 0;
        mem.AddScatterReadRequest(handle, vehicle_interface + FiveM::offset::vehicleList, &vehicleListBase, sizeof(uintptr_t));
        mem.AddScatterReadRequest(handle, vehicle_interface + FiveM::offset::vehicleCount, &vehicleCount, sizeof(int));
        if (IsValidPtr(vehicle_interface_alt) && vehicle_interface_alt != vehicle_interface) {
            mem.AddScatterReadRequest(handle, vehicle_interface_alt + FiveM::offset::vehicleList, &vehicleListBaseAlt, sizeof(uintptr_t));
            mem.AddScatterReadRequest(handle, vehicle_interface_alt + FiveM::offset::vehicleCount, &vehicleCountAlt, sizeof(int));
        }
        mem.SetDmaCallTag("FiveM.VehicleListPointer");
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
            publish(view_matrix);
            return;
        }

        const int listCount = (vehicleCount > 0 && vehicleCount <= 2048)
            ? (std::min)(vehicleCount, MAX_VEHICLES) : MAX_VEHICLES;
        std::vector<uintptr_t> rawPtrs(listCount, 0);
        for (int i = 0; i < listCount; ++i)
            mem.AddScatterReadRequest(handle, vehicleListBase + (uintptr_t)i * FiveM::offset::vehicleEntryStride,
                                      &rawPtrs[i], sizeof(uintptr_t));
        mem.SetDmaCallTag("FiveM.VehicleEntities");
        mem.ExecuteReadScatter(handle);

        std::vector<uintptr_t> valid;
        valid.reserve(MAX_VEHICLES);
        for (int i = 0; i < listCount; ++i) {
            if (IsValidPtr(rawPtrs[i]))
                valid.push_back(rawPtrs[i]);
        }

        if (valid.empty()) {
            publish(view_matrix);
            return;
        }

        std::vector<Vec3> positions(valid.size());
        std::vector<Matrix> matrices(valid.size());
        std::vector<uint32_t> lockState(valid.size(), UINT32_MAX);
        std::vector<uintptr_t> driverPrimary(valid.size(), 0), driverFallback(valid.size(), 0);

        const bool needMatrix = frameConfig.box_3d;
        const bool needLock = frameConfig.lock_status;
        const bool needOccupied = frameConfig.ignore_occupied || frameConfig.show_occupants;
        for (size_t i = 0; i < valid.size(); ++i) {
            mem.AddScatterReadRequest(handle, valid[i] + FiveM::offset::vehiclePosition, &positions[i], sizeof(Vec3));
            if (needMatrix)
                mem.AddScatterReadRequest(handle, valid[i] + FiveM::offset::vehicleMatrix, &matrices[i], sizeof(Matrix));
            if (needLock)
                mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleLock,
                                          &lockState[i], sizeof(uint32_t));
            if (needOccupied) {
                const uintptr_t buildDriver = offset::buildVersion >= 3751
                    ? offset::vehicleDriverNew : offset::vehicleDriver;
                mem.AddScatterReadRequest(handle, valid[i] + buildDriver,
                                          &driverPrimary[i], sizeof(uintptr_t));
                if (buildDriver != offset::vehicleDriver)
                    mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleDriver,
                                              &driverFallback[i], sizeof(uintptr_t));
            }
        }
        mem.SetDmaCallTag("FiveM.VehicleCore");
        const auto coreReadBegin = std::chrono::steady_clock::now();
        mem.ExecuteReadScatter(handle);
        const auto coreReadEnd = std::chrono::steady_clock::now();
        positionSampleTime = coreReadBegin + (coreReadEnd - coreReadBegin) / 2;

        const bool refreshGear = g_lastGearRefresh.time_since_epoch().count() == 0 ||
            nowc - g_lastGearRefresh >= std::chrono::milliseconds(250);
        if (refreshGear) {
            std::vector<int8_t> gears(valid.size());
            std::vector<float> engines(valid.size());
            std::vector<bool> requested(valid.size(), false);
            int queued = 0;
            for (size_t i = 0; i < valid.size() && queued < 40; ++i) {
                if (positions[i].IsZero() || positions[i].distance_to(localPos) > frameConfig.max_distance)
                    continue;
                mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleGear,
                    &gears[i], sizeof(gears[i]));
                mem.AddScatterReadRequest(handle, valid[i] + offset::vehicleEngineHp,
                    &engines[i], sizeof(engines[i]));
                requested[i] = true;
                ++queued;
            }
            if (queued) {
                mem.SetDmaCallTag("FiveM.VehicleGear");
                mem.ExecuteReadScatter(handle);
            }
            for (size_t i = 0; i < valid.size(); ++i)
                if (requested[i]) g_gearCache[valid[i]] = GearState{gears[i], engines[i], nowc};
            g_lastGearRefresh = nowc;
        }


        std::vector<uintptr_t> pedVehicles;
        if (needOccupied && !peds.empty()) {
            pedVehicles.assign(peds.size(), 0);
            for (size_t i = 0; i < peds.size(); ++i)
                mem.AddScatterReadRequest(handle, peds[i] + offset::pedVehicle,
                                          &pedVehicles[i], sizeof(uintptr_t));
            mem.SetDmaCallTag("FiveM.VehicleOccupancy");
            mem.ExecuteReadScatter(handle);
        }

        std::unordered_set<uintptr_t> occupiedVehicles;
        occupiedVehicles.reserve(pedVehicles.size() * 2 + valid.size());
        for (uintptr_t vehicle : pedVehicles)
            if (IsValidPtr(vehicle)) occupiedVehicles.insert(vehicle);
        for (size_t i = 0; i < valid.size(); ++i) {
            uintptr_t driver = IsValidPtr(driverPrimary[i]) ? driverPrimary[i] : driverFallback[i];
            if (IsValidPtr(driver)) occupiedVehicles.insert(valid[i]);
        }

        for (size_t i = 0; i < valid.size(); ++i) {






            if (!std::isfinite(positions[i].x) ||
                !std::isfinite(positions[i].y) ||
                !std::isfinite(positions[i].z))
                continue;
            if (positions[i].IsZero())
                continue;

            float dist = positions[i].distance_to(localPos);
            if (!std::isfinite(dist) || dist > frameConfig.max_distance || dist < 0.1f)
                continue;

            bool occupied = occupiedVehicles.find(valid[i]) != occupiedVehicles.end();

            if (frameConfig.ignore_occupied && occupied)
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
            if (const auto gear = g_gearCache.find(valid[i]); gear != g_gearCache.end()) {
                vd.gear = gear->second.gear;
                vd.engine_health = gear->second.engine;
            }
            vd.valid = true;


            vd.sample_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
            nextVehicles.push_back(vd);



            if (needMatrix && std::isfinite(matrices[i]._11) && std::isfinite(matrices[i]._22) &&
                std::isfinite(matrices[i]._33) && std::isfinite(matrices[i]._41) &&
                std::isfinite(matrices[i]._42) && std::isfinite(matrices[i]._43))
                nextMatrices[valid[i]] = matrices[i];
        }
        publish(view_matrix);
        if (g_gearCache.size() > 256) {
            for (auto it = g_gearCache.begin(); it != g_gearCache.end();) {
                if (nowc - it->second.seen > std::chrono::seconds(10)) it = g_gearCache.erase(it);
                else ++it;
            }
        }
    }


    static bool Project(const Vec3& world, const Matrix& view, Vec2& out) {
        return world.world_to_screen(const_cast<Matrix&>(view), out);
    }


    static void DrawOrientedBox3D(ImDrawList* dl, const Matrix& entityMat, const Matrix& view,
                                  ImU32 col, float thickness)
    {
        Vec3 right, forward, up, pos;
        MatrixAxes(entityMat, right, forward, up, pos);
        if (pos.IsZero())
            return;


        const float hx = 1.05f;
        const float hy = 2.30f;
        const float hz = 0.85f;


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


        Vec3 center = Vec3(pos.x + up.x * hz, pos.y + up.y * hz, pos.z + up.z * hz);


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


        static const int edges[12][2] = {
            {0,1},{1,3},{3,2},{2,0},
            {4,5},{5,7},{7,6},{6,4},
            {0,4},{1,5},{2,6},{3,7}
        };
        for (int e = 0; e < 12; ++e) {
            int a = edges[e][0], b = edges[e][1];
            if (!ok[a] || !ok[b]) continue;
            dl->AddLine(ImVec2(sc[a].x, sc[a].y), ImVec2(sc[b].x, sc[b].y), col, thickness);
        }
    }

    void Render() {
        if (!config.enabled) return;

        std::vector<VehicleData> frameVehicles;
        std::unordered_map<uintptr_t, Matrix> frameMatrices;
        Matrix vehicleView{};
        std::chrono::steady_clock::time_point vehicleSampleTime{};
        {
            std::scoped_lock lock(g_vehicleMutex);
            frameVehicles = vehicles;
            frameMatrices = g_matrices;
            vehicleView = g_vehicleViewMatrix;
            vehicleSampleTime = g_vehicleSampleTime;
        }
        if (frameVehicles.empty()) return;
        if (vehicleSampleTime.time_since_epoch().count() == 0 ||
            std::chrono::steady_clock::now() - vehicleSampleTime > std::chrono::milliseconds(200))
            return;
        static double lastSpeedPrune = 0.0;
        const double renderNow = ImGui::GetTime();
        if (g_speedHistory.size() > 128 || renderNow - lastSpeedPrune >= 5.0) {
            lastSpeedPrune = renderNow;
            std::unordered_set<uintptr_t> present;
            present.reserve(frameVehicles.size() * 2);
            for (const auto& vehicle : frameVehicles) present.insert(vehicle.address);
            for (auto it = g_speedHistory.begin(); it != g_speedHistory.end();) {
                if (present.find(it->first) == present.end()) it = g_speedHistory.erase(it);
                else ++it;
            }
        }

        using namespace FiveM;



        Matrix view_matrix{};
        if (FiveM::ESP::FrameCacheValid()) {
            view_matrix = FiveM::ESP::GetFrameViewMatrix();
        } else {
            view_matrix = vehicleView;
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;
        ImVec2 screen = ImGui::GetIO().DisplaySize;

        ImU32 boxCol = config.rgb_mode ? GetRGBColor() : config.color_box_3d;
        ImU32 snapCol = config.rgb_mode ? GetRGBColor() : config.color_snaplines;
        ImU32 distCol = config.color_distance;

        uintptr_t nearestAddr = 0;
        float nearestD = 1e9f;
        for (const auto& v : frameVehicles) {
            if (v.valid && v.distance < nearestD) {
                nearestD = v.distance;
                nearestAddr = v.address;
            }
        }


        std::vector<const VehicleData*> drawList;
        drawList.reserve(frameVehicles.size());




        for (const auto& v : frameVehicles)
            if (v.valid && std::isfinite(v.distance)) drawList.push_back(&v);
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


            ImU32 useBox = boxCol;
            if (v.address == nearestAddr)
                useBox = IM_COL32(255, 220, 50, 255);

            if (config.box_3d) {
                auto it = frameMatrices.find(v.address);
                if (it != frameMatrices.end()) {
                    DrawOrientedBox3D(dl, it->second, view_matrix, useBox, v.address == nearestAddr ? 2.4f : 1.6f);
                } else {

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

                float now = (float)ImGui::GetTime();
                float speedKmh = 0.f;
                auto it = g_speedHistory.find(v.address);
                if (it != g_speedHistory.end()) {
                    float dt = now - it->second.second;
                    if (dt > 0.02f && dt < 1.0f) {
                        float meters = v.position.distance_to(it->second.first);
                        speedKmh = (meters / dt) * 3.6f;
                    }
                }
                g_speedHistory[v.address] = { v.position, now };
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




            if (config.show_speed) {
                char buf[48];
                if (v.gear >= -1 && v.gear <= 8) {
                    snprintf(buf, sizeof(buf), "G%d", (int)v.gear);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY),
                        IM_COL32(255, 220, 120, 220), buf);
                    textY += ts.y + 2.f;
                }
                if (v.engine_health > 1.f && v.engine_health <= 1000.f) {
                    snprintf(buf, sizeof(buf), "Motor %.0f", v.engine_health);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY),
                        v.engine_health < 300.f ? IM_COL32(255, 80, 80, 220) : IM_COL32(180, 255, 180, 220), buf);
                    textY += ts.y + 2.f;
                }
            }
        }
    }

    void Run() {
        Render();
    }

    void Shutdown() {
        if (g_vehicleScatter) {
            mem.CloseScatterHandle(g_vehicleScatter);
            g_vehicleScatter = nullptr;
        }
        std::scoped_lock lock(g_vehicleMutex);
        vehicles.clear();
        g_matrices.clear();
        g_vehicleViewMatrix = {};
        g_vehicleSampleTime = {};
        g_gearCache.clear();
        g_lastGearRefresh = {};
        g_speedHistory.clear();
    }

}
