#pragma once
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <chrono>
#include "../../DMALibrary/Memory/Memory.h"
#include "offsets.h"

namespace FiveM {
    namespace Visibility {

        struct VisibilitySample {
            bool visible = true;
            std::chrono::steady_clock::time_point updated{};
        };

        inline std::unordered_map<uintptr_t, VisibilitySample>& Cache() {
            static std::unordered_map<uintptr_t, VisibilitySample> cache;
            return cache;
        }

        // oPedVisibility is a last-visible frame byte, not a boolean flag.
        // The subtraction is intentionally uint8_t so wrap-around at 255 is safe.
        inline constexpr bool IsRecentlyVisible(uint8_t currentFrame, uint8_t lastVisibleFrame) {
            // Game visibility frame counter ticks every render frame.
            // Allow a small window so brief stalls don't flicker red.
            const uint8_t age = static_cast<uint8_t>(currentFrame - lastVisibleFrame);
            return age <= 5;
        }

        static_assert(IsRecentlyVisible(1, 255), "visibility frame wrap must be supported");
        static_assert(IsRecentlyVisible(10, 5), "age==5 still counts as recently visible");
        static_assert(!IsRecentlyVisible(12, 5), "stale visibility samples must be rejected");

        inline bool IsPedVisible(uintptr_t ped) {
            if (!ped) return true;

            auto& cache = Cache();
            const auto now = std::chrono::steady_clock::now();
            const auto cached = cache.find(ped);
            // Short TTL so wall enter/exit recolors almost immediately
            if (cached != cache.end() &&
                now - cached->second.updated < std::chrono::milliseconds(8)) {
                return cached->second.visible;
            }

            // Cache miss on the presentation thread: do NOT issue DMA here.
            // Producer BatchCheckVisibility stamps the cache every acquisition.
            // Fail-open keeps ESP drawing instead of flickering everyone hidden.
            if (cached != cache.end())
                return cached->second.visible;
            return true;
        }

        inline void BatchCheckVisibility(const std::vector<uintptr_t>& peds,
                                         std::vector<bool>& visibilityResults) {
            visibilityResults.assign(peds.size(), true);
            if (peds.empty() || !offset::framecountlastvisible ||
                !offset::pedVisibilityOffset) {
                return;
            }

            uint8_t currentFrame = 0;
            if (!mem.Read(offset::framecountlastvisible, &currentFrame, sizeof(currentFrame)))
                return;

            std::vector<uint8_t> lastVisibleFrames(peds.size(), currentFrame);
            static VMMDLL_SCATTER_HANDLE visHandle = nullptr;
            if (!visHandle && mem.vHandle)
                visHandle = mem.CreateScatterHandle();
            if (!visHandle) return;
            for (size_t i = 0; i < peds.size(); ++i) {
                if (peds[i]) {
                    mem.AddScatterReadRequest(visHandle,
                        peds[i] + offset::pedVisibilityOffset,
                        &lastVisibleFrames[i], sizeof(uint8_t));
                }
            }
            mem.ExecuteReadScatter(visHandle);

            auto& cache = Cache();
            const auto now = std::chrono::steady_clock::now();
            for (size_t i = 0; i < peds.size(); ++i) {
                const bool visible = peds[i] &&
                    IsRecentlyVisible(currentFrame, lastVisibleFrames[i]);
                visibilityResults[i] = visible;
                if (peds[i]) cache[peds[i]] = { visible, now };
            }
            // Producer-stamped generation: render must not re-DMA within same gen
            static std::atomic<uint64_t> s_visGen{0};
            s_visGen.fetch_add(1, std::memory_order_relaxed);

            if (cache.size() > 512)
                cache.clear();
        }

        inline void ClearCache() {
            Cache().clear();
        }
    }
}
