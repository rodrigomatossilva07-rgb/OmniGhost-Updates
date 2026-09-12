#include "window.hpp"

#include "../config/app_settings.h"
#include "../platform/monitor_utils.h"
#include "fonts.h"
#include "theme.h"
#include "widgets.h"
#include "hardware_monitor.h"
#include "config_history.h"
#include "hotkeys.h"
#include "onboarding.h"
#include "changelog.h"
#include "../updater/update_service.h"
#include "../launcher/launcher_assets.h"
#include "../platform/offset_auto.h"
#include "../../Rust/rust_game.h"

#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <iostream>
#include <iomanip>

#pragma comment(lib, "dwmapi.lib")

// Algumas versões do imgui_impl_win32.h não expõem esta declaração.
extern LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
);

// Definition matches extern in window.hpp (must NOT live in anonymous namespace).
Overlay* g_overlay_instance = nullptr;

namespace {

constexpr float kCompactWindowWidth = 480.0f;
// Compact desktop-launcher footprint. The authentication card adapts inside
// this surface and enables scrolling only on unusually small displays/scales.
constexpr float kCompactWindowHeight = 520.0f;

std::wstring Widen(const char* text) {
    if (!text || !*text) {
        return L"OmniGhostOverlay";
    }

    const int count = MultiByteToWideChar(
        CP_UTF8,
        0,
        text,
        -1,
        nullptr,
        0
    );

    if (count <= 1) {
        return L"OmniGhostOverlay";
    }

    std::wstring result(
        static_cast<std::size_t>(count),
        L'\0'
    );

    MultiByteToWideChar(
        CP_UTF8,
        0,
        text,
        -1,
        result.data(),
        count
    );

    result.pop_back();
    return result;
}

void ApplyWindowShape(HWND window, bool compact, float dpiScale) {
    if (!window)
        return;
    if (!compact) {
        SetWindowRgn(window, nullptr, TRUE);
        return;
    }

    RECT client{};
    if (!GetClientRect(window, &client))
        return;
    const int radius = (std::max)(16, static_cast<int>(24.0f * dpiScale));
    HRGN region = CreateRoundRectRgn(
        client.left, client.top, client.right + 1, client.bottom + 1,
        radius, radius);
    if (!region)
        return;
    // After a successful SetWindowRgn, Windows owns the region handle.
    if (!SetWindowRgn(window, region, TRUE))
        DeleteObject(region);
}

} // namespace

Overlay::~Overlay() {
    Shutdown();
}

void Overlay::CreateOverlay(const char* window_name) {
    if (overlay) {
        return;
    }

    // Window ownership has its own process-independent guard in addition to the
    // application-level mutex in main.cpp. This sits directly on the only code
    // path that can create the product HWND, so even a future startup path that
    // accidentally bypasses main's guard cannot create a second OmniGhost UI.
    constexpr wchar_t kWindowOwnerMutex[] = L"Local\\OmniGhostWindowOwner-6A670FC7";
    SetLastError(ERROR_SUCCESS);
    window_owner_mutex_ = CreateMutexW(nullptr, TRUE, kWindowOwnerMutex);
    const DWORD ownerMutexError = GetLastError();
    if (!window_owner_mutex_) {
        std::cerr << "[WINDOW][Owner] mutex creation failed win32=" << ownerMutexError << "\n";
        shouldRun = false;
        return;
    }
    if (ownerMutexError == ERROR_ALREADY_EXISTS) {
        std::clog << "[WINDOW][Owner] another process already owns the OmniGhost UI; refusing CreateWindowEx.\n";
        CloseHandle(window_owner_mutex_);
        window_owner_mutex_ = nullptr;
        shouldRun = false;
        return;
    }
    owns_window_owner_mutex_ = true;

    g_overlay_instance = this;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    const std::wstring title = Widen(window_name);

    window_class_ = {};
    window_class_.cbSize = sizeof(window_class_);
    window_class_.style = CS_CLASSDC;
    window_class_.lpfnWndProc = WindowProc;
    window_class_.hInstance = instance;

    // MAKEINTRESOURCEW força a utilização da versão Unicode.
    window_class_.hCursor = LoadCursorW(
        nullptr,
        MAKEINTRESOURCEW(32512)
    );

    window_class_.lpszClassName = L"OmniGhostOverlayClass";

    class_registered_ = RegisterClassExW(&window_class_) != 0;

    if (!class_registered_ &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        shouldRun = false;
        return;
    }

    class_registered_ = true;

    const OmniGhost::Platform::MonitorInfo targetMonitor =
        OmniGhost::Platform::SelectMonitor(app_settings::config.monitor_index, nullptr);
    const int monitor_width = targetMonitor.rect.right - targetMonitor.rect.left;
    const int monitor_height = targetMonitor.rect.bottom - targetMonitor.rect.top;
    int window_width = monitor_width;
    int window_height = monitor_height;
    int window_left = targetMonitor.rect.left;
    int window_top = targetMonitor.rect.top;
    if (compact_mode_) {
        const float systemScale = static_cast<float>(GetDpiForSystem()) / 96.0f;
        window_width = (std::min)(monitor_width, static_cast<int>(kCompactWindowWidth * systemScale));
        window_height = (std::min)(monitor_height, static_cast<int>(kCompactWindowHeight * systemScale));
        window_left += (monitor_width - window_width) / 2;
        window_top += (monitor_height - window_height) / 2;
    }

    overlay = CreateWindowExW(
        WS_EX_TOPMOST |
            WS_EX_LAYERED |
            WS_EX_APPWINDOW,
        window_class_.lpszClassName,
        title.c_str(),
        WS_POPUP,
        window_left,
        window_top,
        window_width,
        window_height,
        nullptr,
        nullptr,
        instance,
        nullptr
    );

    if (!overlay) {
        shouldRun = false;
        return;
    }

    std::clog << "[WINDOW][Create] pid=" << GetCurrentProcessId()
              << " hwnd=0x" << std::hex << reinterpret_cast<std::uintptr_t>(overlay)
              << std::dec << " compact=" << (compact_mode_ ? "YES" : "NO") << "\n";

    SetLayeredWindowAttributes(
        overlay,
        RGB(0, 0, 0),
        255,
        LWA_ALPHA
    );

    const MARGINS margins{
        -1,
        -1,
        -1,
        -1
    };

    DwmExtendFrameIntoClientArea(
        overlay,
        &margins
    );

    // Do not expose the HWND until ImGui has produced and DXGI has presented a
    // complete frame. Showing it here lets DWM briefly display the raw layered
    // surface before authentication is drawn, which is perceived as a second
    // window even though the HWND audit still reports exactly one handle.
    window_visible_ = false;
    ApplyWindowShape(overlay, compact_mode_,
        static_cast<float>(GetDpiForWindow(overlay)) / 96.0f);
    applied_monitor_index_ = app_settings::config.monitor_index;
    display_configuration_dirty_ = true;
}

void Overlay::ApplyDisplayConfiguration(bool force) {
    if (!overlay)
        return;

    const int requestedMonitor = app_settings::config.monitor_index;
    const bool monitorChanged = requestedMonitor != applied_monitor_index_;
    if (force || monitorChanged || display_configuration_dirty_) {
        const OmniGhost::Platform::MonitorInfo monitor =
            OmniGhost::Platform::SelectMonitor(requestedMonitor, overlay);
        int width = monitor.rect.right - monitor.rect.left;
        int height = monitor.rect.bottom - monitor.rect.top;
        int left = monitor.rect.left;
        int top = monitor.rect.top;
        if (compact_mode_) {
            const float dpi = OmniGhost::Platform::DpiScaleForWindow(overlay);
            width = (std::min)(width, static_cast<int>(kCompactWindowWidth * dpi));
            height = (std::min)(height, static_cast<int>(kCompactWindowHeight * dpi));
            left = monitor.rect.left + ((monitor.rect.right - monitor.rect.left) - width) / 2;
            top = monitor.rect.top + ((monitor.rect.bottom - monitor.rect.top) - height) / 2;
        }
        if (width > 0 && height > 0) {
            RECT current{};
            const bool haveCurrent = GetWindowRect(overlay, &current) != FALSE;
            const bool geometryChanged = !haveCurrent || current.left != left || current.top != top ||
                (current.right - current.left) != width || (current.bottom - current.top) != height;
            // IMPORTANT: never resize DXGI from WM_SIZE and never force a synchronous
            // Present/Redraw while SetWindowPos is on the call stack. On some secondary
            // PCs/drivers that re-entrant path can deadlock exactly when the compact
            // hardware screen expands into the launcher, making it look as if the DMA
            // probe froze the machine. WM_SIZE only queues the new size; StartRender()
            // performs ResizeBuffers after SetWindowPos has fully returned.
            SetWindowPos(overlay, HWND_TOPMOST, left, top,
                         width, height, SWP_NOACTIVATE | SWP_FRAMECHANGED);
            ApplyWindowShape(overlay, compact_mode_,
                OmniGhost::Platform::DpiScaleForWindow(overlay));

            if (geometryChanged) {
                InvalidateRect(overlay, nullptr, FALSE);
            }
        }
        applied_monitor_index_ = requestedMonitor;
        display_configuration_dirty_ = false;
    }

    const float dpiScale = OmniGhost::Platform::DpiScaleForWindow(overlay);
    const float requestedScale = std::clamp(
        dpiScale * app_settings::UiScalePreference(), 0.75f, 2.50f);
    if (!force && std::fabs(requestedScale - applied_ui_scale_) < 0.01f)
        return;

    applied_ui_scale_ = requestedScale;
    CyberTheme::SetUiScale(requestedScale);

    if (imgui_initialized_ && device) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        CyberFonts::ReloadFonts(requestedScale);
        if (!ImGui_ImplDX11_CreateDeviceObjects()) {
            shouldRun = false;
        }
    }
}

void Overlay::ApplyPendingResize() {
    if (!resize_pending_ || !swap_chain || !device)
        return;

    const UINT width = pending_resize_width_;
    const UINT height = pending_resize_height_;
    resize_pending_ = false;
    pending_resize_width_ = 0;
    pending_resize_height_ = 0;

    if (width == 0 || height == 0)
        return;

    std::clog << "[UI][Resize] applying deferred swap-chain resize "
              << width << "x" << height << "\n";

    CleanupRenderTarget();
    const HRESULT result = swap_chain->ResizeBuffers(
        0, width, height, DXGI_FORMAT_UNKNOWN, 0);

    if (SUCCEEDED(result)) {
        CreateRenderTarget();
        std::clog << "[UI][Resize] deferred ResizeBuffers PASS\n";
    } else {
        // ResizeBuffers failure must not leave the application with a permanently
        // missing render target. The old swap-chain buffers may still be usable.
        CreateRenderTarget();
        std::cerr << "[UI][Resize] deferred ResizeBuffers FAIL hr=0x"
                  << std::hex << static_cast<unsigned long>(result) << std::dec << "\n";
        if (!render_target_view) {
            std::cerr << "[UI][Resize] render target recovery failed; stopping UI safely.\n";
            shouldRun = false;
        }
    }
}

void Overlay::SetCompactMode(bool enabled) {
    if (compact_mode_ == enabled)
        return;
    compact_mode_ = enabled;
    display_configuration_dirty_ = true;
    ApplyDisplayConfiguration(true);
}

bool Overlay::CreateDevice() {
    if (!overlay) {
        return false;
    }

    if (device && device_context && swap_chain) {
        return true;
    }

    DXGI_SWAP_CHAIN_DESC description{};
    description.BufferCount = 2;
    description.BufferDesc.Format =
        DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferDesc.RefreshRate.Numerator = 60;
    description.BufferDesc.RefreshRate.Denominator = 1;
    description.BufferUsage =
        DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = overlay;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.Windowed = TRUE;
    description.SwapEffect =
        DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL feature_level{};

    constexpr D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0
    };

    const HRESULT result =
        D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            feature_levels,
            static_cast<UINT>(
                std::size(feature_levels)
            ),
            D3D11_SDK_VERSION,
            &description,
            &swap_chain,
            &device,
            &feature_level,
            &device_context
        );

    if (FAILED(result)) {
        shouldRun = false;
        return false;
    }

    CreateRenderTarget();

    return render_target_view != nullptr;
}

void Overlay::CreateRenderTarget() {
    CleanupRenderTarget();

    if (!swap_chain || !device) {
        return;
    }

    ID3D11Texture2D* back_buffer = nullptr;

    const HRESULT result = swap_chain->GetBuffer(
        0,
        IID_PPV_ARGS(&back_buffer)
    );

    if (SUCCEEDED(result) && back_buffer) {
        device->CreateRenderTargetView(
            back_buffer,
            nullptr,
            &render_target_view
        );

        back_buffer->Release();
        back_buffer = nullptr;
    }
}

void Overlay::CleanupRenderTarget() {
    if (render_target_view) {
        render_target_view->Release();
        render_target_view = nullptr;
    }
}

void Overlay::StartRender() {
    if (!shouldRun || !imgui_initialized_) {
        return;
    }

    MSG message{};

    while (PeekMessageW(
        &message,
        nullptr,
        0U,
        0U,
        PM_REMOVE
    )) {
        TranslateMessage(&message);
        DispatchMessageW(&message);

        if (message.message == WM_QUIT) {
            shouldRun = false;
        }
    }

    if (!shouldRun) {
        return;
    }

    ApplyDisplayConfiguration(false);
    if (!shouldRun)
        return;

    // WM_SIZE only records dimensions. Perform DXGI buffer work here, outside the
    // Win32 message callback and outside SetWindowPos's synchronous call stack.
    ApplyPendingResize();
    if (!shouldRun || !render_target_view)
        return;

    // In game, ESC only closes the menu. In launcher mode we intentionally do
    // not consume the key here so the launcher can use ESC for navigation.
    // Exiting the application is an explicit window action (X / WM_CLOSE).
    if (!RenderMenu && app_settings::menu_open && (GetAsyncKeyState(VK_ESCAPE) & 1))
        app_settings::menu_open = false;

    // Menu toggle: Insert by default. Use both the Win32 transition bit and a
    // held-state edge because the overlay does not always own keyboard focus.
    {
        int menu_vk = app_settings::config.menu_bind;
        if (menu_vk <= 0 || menu_vk > 0xFE)
            menu_vk = 0x2D; // VK_INSERT
        const SHORT menu_state = GetAsyncKeyState(menu_vk);
        const SHORT insert_state = menu_vk == VK_INSERT
            ? menu_state
            : GetAsyncKeyState(VK_INSERT);
        const bool any_down = ((menu_state | insert_state) & 0x8000) != 0;
        const bool transitioned = ((menu_state | insert_state) & 1) != 0;
        static bool s_prev_menu_down = false;
        static ULONGLONG s_last_menu_toggle = 0;
        const ULONGLONG now = GetTickCount64();
        // Also accept physical Insert even if menu_bind was rebound incorrectly.
        if ((transitioned || (any_down && !s_prev_menu_down)) &&
            now - s_last_menu_toggle >= 120) {
            app_settings::menu_open = !app_settings::menu_open;
            s_last_menu_toggle = now;
        }
        s_prev_menu_down = any_down;
    }

    const bool interactive =
        RenderMenu || app_settings::menu_open;

    // Updating GWL_EXSTYLE every frame is unnecessary and can cause focus
    // flicker on some systems. Only touch it when interactivity changes.
    if (interactive != interactive_style_enabled_) {
        interactive_style_enabled_ = interactive;
        const LONG_PTR ex_style =
            WS_EX_TOPMOST |
            WS_EX_LAYERED |
            WS_EX_APPWINDOW |
            (interactive ? 0 : WS_EX_TRANSPARENT);

        SetWindowLongPtrW(overlay, GWL_EXSTYLE, ex_style);
        SetWindowPos(overlay, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Overlay::SetCaptureExclusion(bool enabled) {
    if (!overlay || enabled == capture_exclusion_enabled_)
        return;

    // WDA_EXCLUDEFROMCAPTURE (0x11) is supported on Windows 10 2004+. The
    // overlay owns the HWND, so capture policy belongs here rather than in a
    // game ESP module trying to discover a window by title/class.
    constexpr DWORD kExcludeFromCapture = 0x00000011;
    const DWORD affinity = enabled ? kExcludeFromCapture : WDA_NONE;
    if (SetWindowDisplayAffinity(overlay, affinity) != FALSE)
        capture_exclusion_enabled_ = enabled;
}

void Overlay::RequestClose() {
    using OmniGhost::Update::Status;
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    const bool updaterBusy =
        update.status == Status::Checking ||
        update.status == Status::Downloading ||
        update.status == Status::Installing;
    const bool importantOperation = updaterBusy || Rust::backend_busy.load() || OmniGhost::OffsetAuto::g_busy;

    if (importantOperation) {
        exit_confirmation_requested_ = true;
        return;
    }

    shouldRun = false;
    if (overlay)
        DestroyWindow(overlay);
    else
        PostQuitMessage(0);
}

void Overlay::RequestReturnToLauncher() noexcept {
    if (RenderMenu)
        return;
    return_to_launcher_requested_ = true;
    app_settings::menu_open = false;
}

bool Overlay::ConsumeReturnToLauncherRequest() noexcept {
    const bool requested = return_to_launcher_requested_;
    return_to_launcher_requested_ = false;
    return requested;
}

void Overlay::EndRender() {
    if (!shouldRun ||
        !imgui_initialized_ ||
        !device_context ||
        !swap_chain ||
        !render_target_view) {
        return;
    }

    if (exit_confirmation_requested_) {
        CyberWidgets::OpenModal("##exit_important_operation");
        const auto result = CyberWidgets::ConfirmModal(
            "##exit_important_operation",
            "Sair durante uma operação?",
            "Existe uma atualização, verificação de offsets ou operação de dispositivo em curso. Sair agora pode interrompê-la.",
            "Sair mesmo assim",
            "Continuar",
            CyberWidgets::ButtonStyle::Destructive,
            470.0f);
        if (result == CyberWidgets::ModalResult::Cancelled) {
            exit_confirmation_requested_ = false;
        } else if (result == CyberWidgets::ModalResult::Confirmed) {
            exit_confirmation_requested_ = false;
            OmniGhost::Update::UpdateService::Instance().Cancel();
            if (Rust::backend_busy.load())
                Rust::CancelBackendOperation();
            shouldRun = false;
            if (overlay)
                DestroyWindow(overlay);
            else
                PostQuitMessage(0);
        }
    }

    // Product-wide overlays are rendered once per frame, independent of whether
    // the current screen is auth, launcher, updater or an in-game page.
    CyberWidgets::DrawToasts();

    ImGui::Render();
    // Configurable background opacity with a pure RGB(0,0,0) framebuffer.
    float bl = app_settings::config.black_level;
    if (bl < 0.f) bl = 0.f;
    if (bl > 100.f) bl = 100.f;
    // Legacy toggle still forces full on/off when level not used
    if (app_settings::config.black_background && bl < 0.5f)
        bl = 100.f;
    if (!app_settings::config.black_background && bl > 99.5f)
        bl = 0.f;
    const float clear_color[4] = {
        0.0f,
        0.0f,
        0.0f,
        bl / 100.0f
    };

    device_context->OMSetRenderTargets(
        1,
        &render_target_view,
        nullptr
    );

    device_context->ClearRenderTargetView(
        render_target_view,
        clear_color
    );

    ImGui_ImplDX11_RenderDrawData(
        ImGui::GetDrawData()
    );

    const HRESULT presentResult = swap_chain->Present(
        app_settings::config.vsync ? 1U : 0U,
        0U
    );
    if (!window_visible_ && SUCCEEDED(presentResult) && overlay) {
        ShowWindow(overlay, SW_SHOW);
        SetForegroundWindow(overlay);
        window_visible_ = true;
        std::clog << "[WINDOW][Visibility] first rendered frame presented; single HWND shown\n";
    }
}

void Overlay::Shutdown() {
    if (shutdown_) {
        return;
    }

    shutdown_ = true;
    shouldRun = false;

    if (imgui_initialized_) {
        LauncherAssets::Shutdown();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();

        imgui_initialized_ = false;
    }
    
    HardwareMonitor::Shutdown();
    ConfigHistory::Shutdown();
    Hotkeys::Shutdown();
    Onboarding::Shutdown();
    Changelog::Shutdown();

    CleanupRenderTarget();

    if (swap_chain) {
        swap_chain->Release();
        swap_chain = nullptr;
    }

    if (device_context) {
        device_context->Release();
        device_context = nullptr;
    }

    if (device) {
        device->Release();
        device = nullptr;
    }

    if (overlay) {
        DestroyWindow(overlay);
        overlay = nullptr;
    }
    window_visible_ = false;

    if (class_registered_) {
        UnregisterClassW(
            window_class_.lpszClassName,
            window_class_.hInstance
        );

        class_registered_ = false;
    }

    if (window_owner_mutex_) {
        if (owns_window_owner_mutex_)
            ReleaseMutex(window_owner_mutex_);
        CloseHandle(window_owner_mutex_);
        window_owner_mutex_ = nullptr;
        owns_window_owner_mutex_ = false;
    }

    if (g_overlay_instance == this) {
        g_overlay_instance = nullptr;
    }
}

LRESULT CALLBACK Overlay::WindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wparam,
    LPARAM lparam
) {
    if (ImGui::GetCurrentContext() != nullptr) {
        if (ImGui_ImplWin32_WndProcHandler(
            hwnd,
            message,
            wparam,
            lparam
        )) {
            return TRUE;
        }
    }

    switch (message) {
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<const RECT*>(lparam);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        if (g_overlay_instance)
            g_overlay_instance->display_configuration_dirty_ = true;
        return 0;
    }

    case WM_DISPLAYCHANGE:
        if (g_overlay_instance)
            g_overlay_instance->display_configuration_dirty_ = true;
        return 0;

    case WM_SIZE:
        if (g_overlay_instance && wparam != SIZE_MINIMIZED) {
            const UINT width = static_cast<UINT>(LOWORD(lparam));
            const UINT height = static_cast<UINT>(HIWORD(lparam));
            if (width != 0 && height != 0) {
                g_overlay_instance->pending_resize_width_ = width;
                g_overlay_instance->pending_resize_height_ = height;
                g_overlay_instance->resize_pending_ = true;
            }
        }
        return 0;

    case WM_SYSCOMMAND:
        if ((wparam & 0xFFF0U) == SC_KEYMENU) {
            return 0;
        }
        if ((wparam & 0xFFF0U) == SC_MINIMIZE &&
            app_settings::config.minimize_behavior == app_settings::MinimizeBehavior::KeepOpen) {
            return 0;
        }

        break;

    case WM_CLOSE:
        if (app_settings::config.close_behavior == app_settings::CloseBehavior::Minimize) {
            ShowWindow(hwnd, SW_MINIMIZE);
            return 0;
        }
        if (g_overlay_instance) {
            g_overlay_instance->RequestClose();
            return 0;
        }

        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (g_overlay_instance &&
            g_overlay_instance->overlay == hwnd) {
            g_overlay_instance->overlay = nullptr;
        }

        PostQuitMessage(0);
        return 0;

    default:
        break;
    }

    return DefWindowProcW(
        hwnd,
        message,
        wparam,
        lparam
    );
}
