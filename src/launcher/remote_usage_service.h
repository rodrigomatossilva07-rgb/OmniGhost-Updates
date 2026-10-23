#pragma once

#include <cstdint>

namespace Launcher { enum class GameId : int; }

namespace OmniGhost::RemoteUsage {
struct Summary {
    std::uint64_t totalActiveSeconds{};
    std::uint64_t lastSessionSeconds{};
    std::uint32_t sessionCount{};
};
void Begin(::Launcher::GameId game);
void Finish(::Launcher::GameId game) noexcept;
void Refresh();
void Shutdown() noexcept;



void SuppressFinalFlush() noexcept;
[[nodiscard]] bool TryGetSummary(::Launcher::GameId game, Summary& summary) noexcept;
}
