#include <Windows.h>

#include "config/app_settings.h"
#include "window/window.hpp"
#include "preview_runtime.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    app_settings::Initialize();
    app_settings::menu_open = true;
    preview_runtime::Initialize();

    Overlay preview;
    preview.RenderMenu = true;
    preview.SetupOverlay("OmniGhost UI Preview");

    while (preview.shouldRun) {
        preview.StartRender();
        if (!preview.shouldRun)
            break;

        preview.Render();
        preview.EndRender();
    }

    return 0;
}
