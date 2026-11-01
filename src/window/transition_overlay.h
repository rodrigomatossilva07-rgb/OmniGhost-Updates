#pragma once

#include <string_view>

namespace OmniGhost::UI {

struct TransitionOverlayOptions {
    std::string_view title;
    std::string_view subtitle;
    float elapsedSeconds{};
    float alpha{1.0f};
    bool success{};
    bool minimal{};
};



void DrawTransitionOverlay(const TransitionOverlayOptions& options);

}
