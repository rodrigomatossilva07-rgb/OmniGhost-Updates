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
#include <cfloat>
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
    struct MotionSample { Vec3 position{}; std::chrono::steady_clock::time_point sampled{}; };
    static std::unordered_map<uintptr_t, MotionSample> g_motionSamples;

    std::vector<VehicleData> GetSnapshot() {
        std::scoped_lock lock(g_vehicleMutex);
        return vehicles;
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

        const int configuredLimit = std::clamp(frameConfig.max_vehicles, 8, 128);
        const int listCount = (vehicleCount > 0 && vehicleCount <= 2048)
            ? (std::min)(vehicleCount, configuredLimit) : configuredLimit;
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

        const bool needMatrix = frameConfig.box_3d || frameConfig.box_2d ||
            frameConfig.show_direction || frameConfig.marker;
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

        std::unordered_map<uintptr_t, int> occupantCounts;
        occupantCounts.reserve(pedVehicles.size() * 2 + valid.size());
        for (uintptr_t vehicle : pedVehicles)
            if (IsValidPtr(vehicle)) ++occupantCounts[vehicle];
        for (size_t i = 0; i < valid.size(); ++i) {
            uintptr_t driver = IsValidPtr(driverPrimary[i]) ? driverPrimary[i] : driverFallback[i];
            if (IsValidPtr(driver) && occupantCounts[valid[i]] == 0) occupantCounts[valid[i]] = 1;
        }

        uintptr_t localVehicle = 0;
        if (IsValidPtr(offset::localplayer) && offset::pedVehicle) {
            mem.AddScatterReadRequest(handle, offset::localplayer + offset::pedVehicle,
                                      &localVehicle, sizeof(localVehicle));
            mem.SetDmaCallTag("FiveM.LocalVehicle");
            mem.ExecuteReadScatter(handle);
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

            const int occupants = occupantCounts[valid[i]];
            bool occupied = occupants > 0;

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
            vd.occupants = occupants;
            vd.local_vehicle = valid[i] == localVehicle;
            if (const auto previous = g_motionSamples.find(valid[i]); previous != g_motionSamples.end()) {
                const float seconds = std::chrono::duration<float>(nowc - previous->second.sampled).count();
                if (seconds >= .02f && seconds <= 1.0f) {
                    const float measured = positions[i].distance_to(previous->second.position) / seconds * 3.6f;
                    if (std::isfinite(measured) && measured >= 0.f && measured < 700.f)
                        vd.speed_kmh = measured;
                }
            }
            g_motionSamples[valid[i]] = {positions[i], nowc};
            if (needMatrix) {
                const float fx = matrices[i]._21, fy = matrices[i]._22;
                if (std::isfinite(fx) && std::isfinite(fy) && std::fabs(fx) + std::fabs(fy) > .001f)
                    vd.heading_degrees = std::atan2(fx, fy) * 57.2957795f;
            }
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
        if (g_motionSamples.size() > 256) {
            std::unordered_set<uintptr_t> present(valid.begin(), valid.end());
            for (auto it = g_motionSamples.begin(); it != g_motionSamples.end();) {
                if (!present.count(it->first)) it = g_motionSamples.erase(it); else ++it;
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

    static bool ProjectVehicleBounds(const Matrix& entityMat, const Matrix& view,
                                     ImVec2& minimum, ImVec2& maximum) {
        Vec3 right, forward, up, pos;
        MatrixAxes(entityMat, right, forward, up, pos);
        auto norm = [](Vec3 v) {
            const float length = std::sqrt(v.x*v.x + v.y*v.y + v.z*v.z);
            return length > 1.0e-4f ? Vec3(v.x/length, v.y/length, v.z/length) : Vec3{};
        };
        right = norm(right); forward = norm(forward); up = norm(up);
        if (pos.IsZero() || right.IsZero() || forward.IsZero()) return false;
        if (up.IsZero()) up = Vec3(0, 0, 1);
        constexpr float hx = 1.05f, hy = 2.30f, hz = .85f;
        const Vec3 center(pos.x + up.x*hz, pos.y + up.y*hz, pos.z + up.z*hz);
        minimum = ImVec2(FLT_MAX, FLT_MAX); maximum = ImVec2(-FLT_MAX, -FLT_MAX);
        int projected = 0;
        for (int i = 0; i < 8; ++i) {
            const float sx = (i & 1) ? hx : -hx, sy = (i & 2) ? hy : -hy, sz = (i & 4) ? hz : -hz;
            const Vec3 point(center.x + right.x*sx + forward.x*sy + up.x*sz,
                             center.y + right.y*sx + forward.y*sy + up.y*sz,
                             center.z + right.z*sx + forward.z*sy + up.z*sz);
            Vec2 screen{};
            if (!Project(point, view, screen)) continue;
            minimum.x = (std::min)(minimum.x, screen.x); minimum.y = (std::min)(minimum.y, screen.y);
            maximum.x = (std::max)(maximum.x, screen.x); maximum.y = (std::max)(maximum.y, screen.y);
            ++projected;
        }
        return projected >= 4 && maximum.x > minimum.x && maximum.y > minimum.y;
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

        ImU32 boxCol = config.color_box_3d;
        ImU32 snapCol = config.color_snaplines;
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
            if (config.highlight_local && v.local_vehicle)
                useBox = config.color_local;
            else if (v.address == nearestAddr)
                useBox = IM_COL32(255, 220, 50, 255);

            const auto matrixIt = frameMatrices.find(v.address);
            if (config.box_2d && matrixIt != frameMatrices.end()) {
                ImVec2 mn{}, mx{};
                if (ProjectVehicleBounds(matrixIt->second, view_matrix, mn, mx))
                    dl->AddRect(mn, mx, useBox, 3.f, 0, config.box_thickness);
            }

            if (config.box_3d) {
                if (matrixIt != frameMatrices.end()) {
                    DrawOrientedBox3D(dl, matrixIt->second, view_matrix, useBox,
                                      v.local_vehicle ? 2.4f : config.box_thickness);
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

            if (config.marker) {
                const float markerSize = v.local_vehicle ? 8.f : 6.f;
                const ImU32 markerColor = v.local_vehicle ? config.color_local : config.color_marker;
                if (config.show_direction) {
                    const float angle = (v.heading_degrees - 90.f) * 0.0174532925f;
                    const ImVec2 direction(std::cos(angle), std::sin(angle));
                    const ImVec2 side(-direction.y, direction.x);
                    dl->AddTriangleFilled(
                        ImVec2(screenPos.x + direction.x*markerSize, screenPos.y + direction.y*markerSize),
                        ImVec2(screenPos.x - direction.x*markerSize*.6f + side.x*markerSize*.65f,
                               screenPos.y - direction.y*markerSize*.6f + side.y*markerSize*.65f),
                        ImVec2(screenPos.x - direction.x*markerSize*.6f - side.x*markerSize*.65f,
                               screenPos.y - direction.y*markerSize*.6f - side.y*markerSize*.65f), markerColor);
                } else dl->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), markerSize*.65f, markerColor, 12);
            }

            if (config.show_name) {
                const char* name = v.local_vehicle ? "Veículo local" : v.display_name.c_str();
                const ImVec2 ts = ImGui::CalcTextSize(name);
                dl->AddText(ImVec2(screenPos.x - ts.x*.5f, textY), useBox, name);
                textY += ts.y + 2.f;
            }

            if (config.distance) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0fm", v.distance);
                ImVec2 ts = ImGui::CalcTextSize(buf);
                dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY), distCol, buf);
                textY += ts.y + 2.f;
            }

            if (config.show_speed) {

                if (v.speed_kmh > 0.5f) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.0f km/h", v.speed_kmh);
                    ImVec2 ts = ImGui::CalcTextSize(buf);
                    dl->AddText(ImVec2(screenPos.x - ts.x * 0.5f, textY),
                        IM_COL32(180, 220, 255, 230), buf);
                    textY += ts.y + 2.f;
                }
            }

            if (config.show_occupants) {
                char occupantText[32]{};
                if (v.occupied) snprintf(occupantText, sizeof(occupantText), "%d ocupante%s",
                                         v.occupants, v.occupants == 1 ? "" : "s");
                else snprintf(occupantText, sizeof(occupantText), "Desocupado");
                const char* occ = occupantText;
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
        g_motionSamples.clear();
    }

}
