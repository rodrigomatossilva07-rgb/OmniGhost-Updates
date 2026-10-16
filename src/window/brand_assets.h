#pragma once

#include <Windows.h>
#include <d3d11.h>
#include "third_party/ImGui/imgui.h"

namespace BrandAssets {

    bool Initialize(ID3D11Device* device, HINSTANCE instance = nullptr);
    void Shutdown();

    ImTextureID GetLogoTexture();
    int GetLogoWidth();
    int GetLogoHeight();

    void ApplyWindowIcon(HWND hwnd, HINSTANCE instance = nullptr);

}
