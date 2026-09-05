#pragma once

#include <Windows.h>
#include <d3d11.h>
#include "../ImGui/imgui.h"

namespace BrandAssets {

    bool Initialize(ID3D11Device* device, HINSTANCE instance = nullptr);
    void Shutdown();

    ImTextureID GetLogoTexture(); // nullptr if unavailable
    int GetLogoWidth();
    int GetLogoHeight();

    void ApplyWindowIcon(HWND hwnd, HINSTANCE instance = nullptr);

} // namespace BrandAssets
