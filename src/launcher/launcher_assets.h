#pragma once

#include "launcher_data.h"

#include <d3d11.h>
#include "imgui.h"

namespace LauncherAssets {

struct Texture {
    ImTextureID id = nullptr;
    int width = 0;
    int height = 0;

    explicit operator bool() const { return id != nullptr; }
};

// Release decodes validated embedded PNG bytes directly in memory. Tester may
// explicitly prefer source files for rapid asset iteration.
void Initialize(ID3D11Device* device);
void Shutdown();
Texture Logo(Launcher::GameId game);
Texture Banner(Launcher::GameId game);
Texture RustEspPreview();

} // namespace LauncherAssets
