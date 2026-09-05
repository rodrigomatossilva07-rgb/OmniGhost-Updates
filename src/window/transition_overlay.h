#pragma once

#include <string_view>

namespace OmniGhost::UI {

struct TransitionOverlayOptions {
    std::string_view title;
    std::string_view subtitle;
    float elapsedSeconds{};
    float alpha{1.0f};
    bool success{};
};

// Draws the shared full-screen visual used only for major context changes.
// It does not own a window, perform a resize or block the render loop.
void DrawTransitionOverlay(const TransitionOverlayOptions& options);

} // namespace OmniGhost::UI
