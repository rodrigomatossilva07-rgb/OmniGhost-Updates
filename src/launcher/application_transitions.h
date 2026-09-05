#pragma once

#include <chrono>
#include <cstdint>

class Overlay;
namespace OmniGhost::Platform { class ShutdownCoordinator; }

namespace OmniGhost::UI {

void DrawDmaPreparation(std::chrono::steady_clock::time_point started);
void RenderTimedTransition(Overlay& application, const char* title,
                           const char* subtitle, float durationSeconds,
                           bool success = false);
void EndGameSessionWithTransition(
    Overlay& application,
    Platform::ShutdownCoordinator& coordinator,
    std::uint64_t generation,
    bool requestedByUser);

} // namespace OmniGhost::UI
