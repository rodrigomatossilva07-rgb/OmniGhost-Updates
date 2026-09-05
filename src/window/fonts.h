#pragma once
#include "../ImGui/imgui.h"

namespace CyberFonts {

    void LoadFonts(float scale = 1.0f);
    void ReloadFonts(float scale = 1.0f);
    ImFont* GetTitleFont();
    ImFont* GetBodyFont();
    ImFont* GetMonoFont();

} // namespace CyberFonts
