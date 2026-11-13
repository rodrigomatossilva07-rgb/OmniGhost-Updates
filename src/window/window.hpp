#pragma once

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <chrono>

#include "src/project/third_party/ImGui/imgui.h"
#include "src/project/third_party/ImGui/imgui_impl_dx11.h"
#include "src/project/third_party/ImGui/imgui_impl_win32.h"

class Overlay;


extern Overlay* g_overlay_instance;

class Overlay {
public:
    Overlay() = default;
    ~Overlay();

    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    void SetupOverlay(const char* window_name);
    void CreateOverlay(const char* window_name);
    bool CreateDevice();
    bool CreateImGui();

    void StartRender();
    void Render();
    void EndRender();
    void Shutdown();
    void RequestClose();
    void RequestReturnToLauncher() noexcept;
    [[nodiscard]] bool ConsumeReturnToLauncherRequest() noexcept;
    void SetCaptureExclusion(bool enabled);
    void SetCompactMode(bool enabled);
    void ApplyDisplayConfiguration(bool force = false);

    void WaitForEvents(std::chrono::milliseconds timeout = std::chrono::milliseconds(1));

    bool shouldRun = true;
    bool RenderMenu = true;

    HWND overlay = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* device_context = nullptr;
    IDXGISwapChain* swap_chain = nullptr;
    ID3D11RenderTargetView* render_target_view = nullptr;

private:
    void CreateRenderTarget();
    void CleanupRenderTarget();
    void ApplyPendingResize();
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    WNDCLASSEXW window_class_{};
    bool class_registered_ = false;
    bool imgui_initialized_ = false;
    bool shutdown_ = false;
    bool capture_exclusion_enabled_ = false;
    bool interactive_style_enabled_ = true;
    bool compact_mode_ = false;
    int applied_monitor_index_ = -999;
    float applied_ui_scale_ = 0.0f;
    bool display_configuration_dirty_ = true;
    bool exit_confirmation_requested_ = false;
    bool return_to_launcher_requested_ = false;
    HANDLE window_owner_mutex_ = nullptr;
    bool owns_window_owner_mutex_ = false;
    UINT pending_resize_width_ = 0;
    UINT pending_resize_height_ = 0;
    bool resize_pending_ = false;
    bool high_resolution_timer_active_ = false;



    bool window_visible_ = false;
	std::chrono::steady_clock::time_point frame_render_begin_{};
	std::chrono::steady_clock::time_point next_frame_deadline_{};
	bool frame_render_timing_active_ = false;
};
