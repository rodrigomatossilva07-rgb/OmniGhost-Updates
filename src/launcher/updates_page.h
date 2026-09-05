#pragma once

#include "imgui.h"

namespace LauncherUpdates {

void Reset();
void OnOpened();
void Draw(const ImVec2& display_size, float content_top, float content_bottom);

} // namespace LauncherUpdates
