#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace CS2::CacheManager {




extern std::atomic<uint64_t> session_generation;

struct Sizes {
    std::atomic<std::size_t> recent_players{0};
    std::atomic<std::size_t> bones{0};
    std::atomic<std::size_t> controller_pawns{0};
    std::atomic<std::size_t> scene_bones{0};
    std::atomic<std::size_t> dropped_weapons{0};
    std::atomic<std::size_t> spectators{0};
};

[[nodiscard]] Sizes& Metrics() noexcept;
[[nodiscard]] uint64_t CurrentGeneration() noexcept;
[[nodiscard]] uint64_t AdvanceGeneration() noexcept;
void ResetMetrics() noexcept;

}
