#pragma once

#include "game_adapter.h"
#include "launcher_data.h"

namespace OmniGhost::Launcher {

[[nodiscard]] IGameAdapter* FindGameAdapter(::Launcher::GameId game) noexcept;
[[nodiscard]] AdapterStartResult StartGameAdapter(::Launcher::GameId game);

} // namespace OmniGhost::Launcher
