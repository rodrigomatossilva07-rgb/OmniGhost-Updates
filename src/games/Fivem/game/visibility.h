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
inline void BatchCheckVisibility(const std::vector<uintptr_t>& peds,
                                 std::vector<bool>& visibilityResults) {
    visibilityResults.assign(peds.size(), false);
    if (peds.empty() || !offset::pedVisibilityOffset) {
        g_pedCacheManager.clearPedVisibilities();
        return;
    }

    // Newly streamed peds are due immediately. Stable peds reuse the recent
    // result instead of consuming a DMA request on every 20 ms core scan.
    static std::vector<uintptr_t> duePeds;
    static std::vector<size_t> dueIndices;
    duePeds.clear();
    dueIndices.clear();
    const auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < peds.size(); ++i) {
        if (!peds[i]) continue;
        PedData cached;
        if (g_pedCacheManager.getPedData(peds[i], cached) && cached.visibility_known &&
            now - cached.visibility_updated < std::chrono::milliseconds(60)) {
            visibilityResults[i] = cached.visible;
        } else {
            duePeds.push_back(peds[i]);
            dueIndices.push_back(i);
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
                                             std::chrono::steady_clock::now());
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
