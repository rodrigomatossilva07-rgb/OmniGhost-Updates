#pragma once

namespace CS2 { struct Runtime; struct Config; }

namespace CS2::ESP {

// Presentation-only player ESP. It never performs DMA reads; all data comes
// from the acquisition snapshot published by cs2_game.
void DrawPlayers(const Runtime& runtime, const Config& config);

} // namespace CS2::ESP
