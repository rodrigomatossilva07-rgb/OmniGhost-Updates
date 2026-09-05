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

            // The reliable b3258 path compares the global current-visible frame
            // with CPed::m_lastVisibleFrame. Treating 0/4/36 as magic booleans was
            // the reason the old option hid or accepted players at random.
            if (offset::framecountlastvisible && offset::pedVisibilityOffset) {
                uint8_t currentFrame = 0;
                uint8_t lastVisibleFrame = 0;
                if (mem.Read(offset::framecountlastvisible, &currentFrame, sizeof(currentFrame)) &&
                    mem.Read(ped + offset::pedVisibilityOffset, &lastVisibleFrame, sizeof(lastVisibleFrame))) {
                    const bool result = IsRecentlyVisible(currentFrame, lastVisibleFrame);
                    cache[ped] = { result, now };
                    return result;
                }
            }

            // Fail open when an unsupported build has no verified visibility
            // pair. A failed DMA read must never make every entity disappear.
            cache[ped] = { true, now };
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
            auto handle = mem.CreateScatterHandle();
            if (!handle) return;
            for (size_t i = 0; i < peds.size(); ++i) {
                if (peds[i]) {
                    mem.AddScatterReadRequest(handle,
                        peds[i] + offset::pedVisibilityOffset,
                        &lastVisibleFrames[i], sizeof(uint8_t));
                }
            }
            mem.ExecuteReadScatter(handle);
            mem.CloseScatterHandle(handle);

            auto& cache = Cache();
            const auto now = std::chrono::steady_clock::now();
            for (size_t i = 0; i < peds.size(); ++i) {
                const bool visible = peds[i] &&
                    IsRecentlyVisible(currentFrame, lastVisibleFrames[i]);
                visibilityResults[i] = visible;
                if (peds[i]) cache[peds[i]] = { visible, now };
            }

            if (cache.size() > 512)
                cache.clear();
        }

        inline void ClearCache() {
            Cache().clear();
        }
    }
}
