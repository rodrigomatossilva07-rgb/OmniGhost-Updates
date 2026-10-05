#pragma once
#include <chrono>
#include <cstdint>
#include <vector>
#include "../../DMALibrary/Memory/Memory.h"
#include "../playerInfo/PedData.h"
#include "offsets.h"

namespace FiveM::Visibility {

static_assert(!PedVisibilityFlagMeansVisible(0) &&
              !PedVisibilityFlagMeansVisible(4) &&
              !PedVisibilityFlagMeansVisible(36) &&
              PedVisibilityFlagMeansVisible(1));

inline VMMDLL_SCATTER_HANDLE& ScatterHandle() {
    static VMMDLL_SCATTER_HANDLE handle = nullptr;
    return handle;
}

inline bool IsPedVisibilityKnown(uintptr_t ped) {
    PedData data;
    return ped && offset::pedVisibilityOffset &&
        g_pedCacheManager.getPedData(ped, data) && data.visibility_known &&
        std::chrono::steady_clock::now() - data.visibility_updated <
            std::chrono::milliseconds(80);
}

inline bool IsPedVisible(uintptr_t ped) {
    PedData data;
    return ped && offset::pedVisibilityOffset &&
        g_pedCacheManager.getPedData(ped, data) && data.visibility_known &&
        std::chrono::steady_clock::now() - data.visibility_updated <
            std::chrono::milliseconds(80) && data.visible;
}

// Called only from the acquisition thread, at its ~16-24 ms cadence.
// now: current steady_clock time for cache TTL checks
// localPos: local player position for distance LOD
// viewMatrix: optional view matrix for frustum culling (if provided)
// maxDistance: max distance to check visibility (LOD)
inline void BatchCheckVisibility(const std::vector<uintptr_t>& peds,
                                 const std::vector<Vec3>& positions,
                                 const Vec3& localPos,
                                 const Matrix* viewMatrix,
                                 float maxDistance,
                                 std::vector<bool>& visibilityResults,
                                 std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
    visibilityResults.assign(peds.size(), false);
    if (peds.empty() || !offset::pedVisibilityOffset) {
        g_pedCacheManager.clearPedVisibilities();
        return;
    }

    // Distance LOD thresholds
    const float kVisMaxDist = maxDistance > 0 ? maxDistance : 200.0f;  // Cap

    // Newly streamed peds are due immediately. Stable peds reuse the recent
    // result instead of consuming a DMA request on every 20 ms core scan.
    static std::vector<uintptr_t> duePeds;
    static std::vector<size_t> dueIndices;
    static std::vector<Vec3> duePositions;
    duePeds.clear();
    dueIndices.clear();
    duePositions.clear();
    
    for (size_t i = 0; i < peds.size(); ++i) {
        if (!peds[i]) continue;
        
        // Distance LOD: skip far players
        float distSq = 0.f;
        if (i < positions.size() && localPos.x != 0 && localPos.y != 0 && localPos.z != 0) {
            float dx = positions[i].x - localPos.x;
            float dy = positions[i].y - localPos.y;
            float dz = positions[i].z - localPos.z;
            distSq = dx * dx + dy * dy + dz * dz;
            if (distSq > kVisMaxDist * kVisMaxDist) {
                visibilityResults[i] = false; // Too far, assume not visible
                continue;
            }
        }
        
        PedData cached;
        if (g_pedCacheManager.getPedData(peds[i], cached) && cached.visibility_known &&
            now - cached.visibility_updated < std::chrono::milliseconds(60)) {
            visibilityResults[i] = cached.visible;
        } else {
            // Frustum culling: skip if behind camera or off-screen
            bool skipFrustum = false;
            if (viewMatrix && i < positions.size()) {
                const Vec3& pos = positions[i];
                // Quick frustum check - project to clip space
                float clipX = pos.x * viewMatrix->_11 + pos.y * viewMatrix->_21 + pos.z * viewMatrix->_31 + viewMatrix->_41;
                float clipY = pos.x * viewMatrix->_12 + pos.y * viewMatrix->_22 + pos.z * viewMatrix->_32 + viewMatrix->_42;
                float clipW = pos.x * viewMatrix->_14 + pos.y * viewMatrix->_24 + pos.z * viewMatrix->_34 + viewMatrix->_44;
                if (clipW <= 0.01f || clipX < -clipW * 1.5f || clipX > clipW * 1.5f || 
                    clipY < -clipW * 1.5f || clipY > clipW * 1.5f) {
                    skipFrustum = true;
                }
            }
            
            if (!skipFrustum) {
                duePeds.push_back(peds[i]);
                dueIndices.push_back(i);
                if (i < positions.size()) duePositions.push_back(positions[i]);
            } else {
                visibilityResults[i] = false; // Off-screen, assume not visible
            }
        }
    }
    if (duePeds.empty()) return;

    // Unchanged slots signal a failed scatter read. 0xFF is treated as
    // unknown if it is ever a genuine flag value, rather than guessing visible.
    constexpr uint8_t unreadFlag = 0xFF;
    static std::vector<uint8_t> flags; // acquisition thread only
    flags.assign(duePeds.size(), unreadFlag);
    auto& handle = ScatterHandle();
    if (!handle && mem.vHandle)
        handle = mem.CreateScatterHandle();
    if (!handle) {
        g_pedCacheManager.clearPedVisibilities();
        return;
    }
    bool queued = false;
    for (size_t i = 0; i < duePeds.size(); ++i) {
        if (duePeds[i]) {
            mem.AddScatterReadRequest(handle, duePeds[i] + offset::pedVisibilityOffset,
                                      &flags[i], sizeof(uint8_t));
            queued = true;
        }
    }
    if (!queued) return;
    mem.SetDmaCallTag("FiveM.VisibilityFlags");
    mem.ExecuteReadScatter(handle);
    g_pedCacheManager.updatePedVisibilities(duePeds, flags, unreadFlag,
                                             now);
    for (size_t i = 0; i < duePeds.size(); ++i)
        visibilityResults[dueIndices[i]] = flags[i] != unreadFlag &&
            PedVisibilityFlagMeansVisible(flags[i]);
}

inline void ClearCache() { g_pedCacheManager.clearPedVisibilities(); }

// Called only after the acquisition thread has stopped.
inline void Shutdown() {
    auto& handle = ScatterHandle();
    if (handle) {
        mem.CloseScatterHandle(handle);
        handle = nullptr;
    }
    ClearCache();
}
}
