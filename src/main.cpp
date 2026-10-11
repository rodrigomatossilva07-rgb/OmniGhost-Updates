#include <Windows.h>
#include <Shellapi.h>

#include "platform/application_core.h"
#include "platform/thread_utils.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    // Per-monitor-v2 DPI awareness
    using SetDpiContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto setDpiContext = reinterpret_cast<SetDpiContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return OmniGhost::ApplicationCore{}.Run(0, nullptr);

    OmniGhost::ApplicationCore app;
    int result = app.Run(argc, argv);

    LocalFree(argv);
    return result;
}