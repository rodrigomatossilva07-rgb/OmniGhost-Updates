#include "window.hpp"

#include "fonts.h"
#include "menu_tab.h"
#include "globals.h"
#include "theme.h"
#include "widgets.h"
#include "brand_assets.h"
#include "digital_rain.h"
#include "performance_mode.h"
#include "performance_manager.h"
#include "hardware_monitor.h"
#include "config_history.h"
#include "hotkeys.h"
#include "onboarding.h"
#include "changelog.h"
#include "../config/app_settings.h"
#include "../platform/app_paths.h"
#include "../platform/monitor_utils.h"
#include "../platform/text_encoding.h"
#include "game/esp_manager.h"
#include "../../Cs2/cs2_game.h"
#include "../../Rust/rust_game.h"
#include "../../Warzone/warzone_game.h"
#include "../../Valorant/valorant_game.h"
#include "../../Fivem/fivem_radar.h"
#include "../../Fivem/fivem_radar_config.h"
#include <shellapi.h>
#ifndef UI_PREVIEW
#include "../launcher/launcher_assets.h"
#endif
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#endif
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#endif

namespace {

    std::string g_imgui_ini_path;

void DrawCurrentTab(MenuTab tab, Overlay* overlay)
{
    // Debug: Log tab switch
    std::cout << "[WINDOW] DrawCurrentTab: tab=" << static_cast<int>(tab) << std::endl;
    std::cout.flush();
    
    switch (tab) {
case MenuTab::TAB_VISUALS:     std::cout << "[WINDOW] -> DrawVisuals()" << std::endl; std::cout.flush(); DrawVisuals(); break;
        case MenuTab::TAB_AIM:         std::cout << "[WINDOW] -> DrawAim()" << std::endl; std::cout.flush(); DrawAim(); break;
        case MenuTab::TAB_VEHICLES:    std::cout << "[WINDOW] -> DrawVehicles()" << std::endl; std::cout.flush(); DrawVehicles(); break;
        case MenuTab::TAB_FRIENDS:     std::cout << "[WINDOW] -> DrawFriends()" << std::endl; std::cout.flush(); DrawFriends(); break;
        case MenuTab::TAB_STATUS:      std::cout << "[WINDOW] -> DrawFiveMStatus()" << std::endl; std::cout.flush(); DrawFiveMStatus(); break;
        case MenuTab::TAB_CONFIGS:     std::cout << "[WINDOW] -> DrawConfigs()" << std::endl; std::cout.flush(); DrawConfigs(overlay); break;
        case MenuTab::TAB_SAVECONFIG:  std::cout << "[WINDOW] -> DrawSaveConfigs()" << std::endl; std::cout.flush(); DrawSaveConfigs(); break;
        case MenuTab::TAB_CS2_VISUALS: std::cout << "[WINDOW] -> DrawCs2Visuals()" << std::endl; std::cout.flush(); DrawCs2Visuals(); break;
        case MenuTab::TAB_CS2_AIM:     std::cout << "[WINDOW] -> DrawCs2Aim()" << std::endl; std::cout.flush(); DrawCs2Aim(); break;
        case MenuTab::TAB_CS2_MISC:    std::cout << "[WINDOW] -> DrawCs2Misc()" << std::endl; std::cout.flush(); DrawCs2Misc(); break;
        case MenuTab::TAB_RUST_VISUALS: std::cout << "[WINDOW] -> DrawRustVisuals()" << std::endl; std::cout.flush(); DrawRustVisuals(); break;
        case MenuTab::TAB_RUST_AIM:     std::cout << "[WINDOW] -> DrawRustAim()" << std::endl; std::cout.flush(); DrawRustAim(); break;
        case MenuTab::TAB_RUST_WORLD:   std::cout << "[WINDOW] -> DrawRustWorld()" << std::endl; std::cout.flush(); DrawRustWorld(); break;
        case MenuTab::TAB_RUST_PLAYERS: std::cout << "[WINDOW] -> DrawRustPlayers()" << std::endl; std::cout.flush(); DrawRustPlayers(); break;
        case MenuTab::TAB_RUST_RADAR:   std::cout << "[WINDOW] -> DrawRustRadar()" << std::endl; std::cout.flush(); DrawRustRadar(); break;
        case MenuTab::TAB_RUST_MISC:    std::cout << "[WINDOW] -> DrawRustMisc()" << std::endl; std::cout.flush(); DrawRustMisc(); break;
        case MenuTab::TAB_RUST_DEBUG:   std::cout << "[WINDOW] -> DrawRustDebug()" << std::endl; std::cout.flush(); DrawRustDebug(); break;
        case MenuTab::TAB_WARZONE_AIM:      std::cout << "[WINDOW] -> DrawWarzoneAim()" << std::endl; std::cout.flush(); DrawWarzoneAim(); break;
        case MenuTab::TAB_WARZONE_VISUALS:  std::cout << "[WINDOW] -> DrawWarzoneVisuals()" << std::endl; std::cout.flush(); DrawWarzoneVisuals(); break;
        case MenuTab::TAB_WARZONE_RADAR:    std::cout << "[WINDOW] -> DrawWarzoneRadar()" << std::endl; std::cout.flush(); DrawWarzoneRadar(); break;
        case MenuTab::TAB_WARZONE_WORLD:    std::cout << "[WINDOW] -> DrawWarzoneWorld()" << std::endl; std::cout.flush(); DrawWarzoneWorld(); break;
        case MenuTab::TAB_WARZONE_PLAYERS:  std::cout << "[WINDOW] -> DrawWarzonePlayers()" << std::endl; std::cout.flush(); DrawWarzonePlayers(); break;
        case MenuTab::TAB_WARZONE_MISC:     std::cout << "[WINDOW] -> DrawWarzoneMisc()" << std::endl; std::cout.flush(); DrawWarzoneMisc(); break;
        case MenuTab::TAB_VALORANT_VISUALS:  std::cout << "[WINDOW] -> DrawValorantVisuals()" << std::endl; std::cout.flush(); DrawValorantVisuals(); break;
        case MenuTab::TAB_VALORANT_AIM:      std::cout << "[WINDOW] -> DrawValorantAim()" << std::endl; std::cout.flush(); DrawValorantAim(); break;
        case MenuTab::TAB_VALORANT_STATUS:   std::cout << "[WINDOW] -> DrawValorantStatus()" << std::endl; std::cout.flush(); DrawValorantStatus(); break;
        case MenuTab::TAB_FORTNITE_VISUALS:  std::cout << "[WINDOW] -> DrawFortniteVisuals()" << std::endl; std::cout.flush(); DrawFortniteVisuals(); break;
        case MenuTab::TAB_FORTNITE_AIM:      std::cout << "[WINDOW] -> DrawFortniteAim()" << std::endl; std::cout.flush(); DrawFortniteAim(); break;
        case MenuTab::TAB_FORTNITE_STATUS:   std::cout << "[WINDOW] -> DrawFortniteStatus()" << std::endl; std::cout.flush(); DrawFortniteStatus(); break;
        case MenuTab::TAB_UNIFIED_AIM:       std::cout << "[WINDOW] -> DrawUnifiedAim()" << std::endl; std::cout.flush(); DrawUnifiedAim(); break;
        case MenuTab::TAB_WEB_RADAR:         std::cout << "[WINDOW] -> DrawWebRadar()" << std::endl; std::cout.flush(); DrawWebRadar(); break;
        case MenuTab::TAB_SOUND_ESP:         std::cout << "[WINDOW] -> DrawSoundESP()" << std::endl; std::cout.flush(); DrawSoundESP(); break;
        case MenuTab::TAB_SPECTATOR_LIST:    std::cout << "[WINDOW] -> DrawSpectatorList()" << std::endl; std::cout.flush(); DrawSpectatorList(); break;
        case MenuTab::TAB_TRIGGERBOT:        std::cout << "[WINDOW] -> DrawTriggerbot()" << std::endl; std::cout.flush(); DrawTriggerbot(); break;
        case MenuTab::TAB_RECOIL_CONTROL:    std::cout << "[WINDOW] -> DrawRecoilControl()" << std::endl; std::cout.flush(); DrawRecoilControl(); break;
        case MenuTab::TAB_PREDICTION:        std::cout << "[WINDOW] -> DrawPrediction()" << std::endl; std::cout.flush(); DrawPrediction(); break;
        case MenuTab::TAB_VISIBILITY:        std::cout << "[WINDOW] -> DrawVisibility()" << std::endl; std::cout.flush(); DrawVisibility(); break;
        case MenuTab::TAB_BONE_SYSTEM:       std::cout << "[WINDOW] -> DrawBoneSystem()" << std::endl; std::cout.flush(); DrawBoneSystem(); break;
        case MenuTab::TAB_SMOOTH_CURVES:     std::cout << "[WINDOW] -> DrawSmoothCurves()" << std::endl; std::cout.flush(); DrawSmoothCurves(); break;
        case MenuTab::TAB_RECOIL_PATTERNS:   std::cout << "[WINDOW] -> DrawRecoilPatterns()" << std::endl; std::cout.flush(); DrawRecoilPatterns(); break;
        case MenuTab::TAB_ENTITY_CACHE:      std::cout << "[WINDOW] -> DrawEntityCache()" << std::endl; std::cout.flush(); DrawEntityCache(); break;
        case MenuTab::TAB_PROFILE_MANAGER:   std::cout << "[WINDOW] -> DrawProfileManager()" << std::endl; std::cout.flush(); DrawProfileManager(); break;
        case MenuTab::TAB_OFFSET_MANAGER:    std::cout << "[WINDOW] -> DrawOffsetManager()" << std::endl; std::cout.flush(); DrawOffsetManager(); break;
        case MenuTab::TAB_RESOLUTION:        std::cout << "[WINDOW] -> DrawResolution()" << std::endl; std::cout.flush(); DrawResolution(); break;
        case MenuTab::TAB_GAME_ADAPTER:      std::cout << "[WINDOW] -> DrawGameAdapter()" << std::endl; std::cout.flush(); DrawGameAdapter(); break;

        // FiveM specific pages
        case MenuTab::TAB_FIVEM_WEB_RADAR:   std::cout << "[WINDOW] -> DrawFivemWebRadar()" << std::endl; std::cout.flush(); CyberWidgets::DrawFivemWebRadar(); break;
        case MenuTab::TAB_FIVEM_OBJECT_ESP:  std::cout << "[WINDOW] -> DrawFivemObjectESP()" << std::endl; std::cout.flush(); CyberWidgets::DrawFivemObjectESP(); break;

        default:
            if (overlay) { /* keep */ }
            DrawVisuals();
            break;
        }
    }

    void DrawTechGrid(ImDrawList* dl, const ImVec2& window_pos, const ImVec2& window_size,
                      bool performance_mode)
    {
        const float scale = CyberTheme::UiScale();
        const ImU32 grid_col = CyberTheme::WithAlpha(CyberTheme::Colors.Gold,
            performance_mode ? 0.010f : 0.018f);
        const float step = 38.0f * scale;
        const float x0 = window_pos.x;
        const float y0 = window_pos.y;
        const float x1 = window_pos.x + window_size.x;
        const float y1 = window_pos.y + window_size.y;

        dl->PushClipRect(window_pos, ImVec2(x1, y1), true);
        for (float x = x0 + step; x < x1; x += step)
            dl->AddLine(ImVec2(x, y0), ImVec2(x, y1), grid_col, 1.0f);
        for (float y = y0 + step; y < y1; y += step)
            dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), grid_col, 1.0f);

        if (!performance_mode && app_settings::MotionEnabled() &&
            app_settings::config.animation_intensity == app_settings::EffectLevel::Full) {
            const float spacing = 13.0f * scale;
            const float scan = std::fmod(static_cast<float>(ImGui::GetTime()) * 10.f * scale, spacing);
            for (float y = y0 + scan; y < y1; y += spacing)
                dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), IM_COL32(255, 255, 255, 1), 1.f);
        }

        const ImU32 mark = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.10f);
        const float m = 12.0f * scale;
        const float inset = 8.0f * scale;
        auto corner = [&](float cx, float cy, float sx, float sy) {
            dl->AddLine(ImVec2(cx, cy), ImVec2(cx + sx * m, cy), mark, 1.2f * scale);
            dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy + sy * m), mark, 1.2f * scale);
        };
        corner(x0 + inset, y0 + inset, 1, 1);
        corner(x1 - inset, y0 + inset, -1, 1);
        corner(x0 + inset, y1 - inset, 1, -1);
        corner(x1 - inset, y1 - inset, -1, -1);
        dl->PopClipRect();
    }
    
    
void WaitForEvents(std::chrono::milliseconds timeout) {
        const DWORD timeoutMs = static_cast<DWORD>(timeout.count());
        const DWORD result = MsgWaitForMultipleObjectsEx(
            0, nullptr, timeoutMs,
            QS_ALLINPUT, MWMO_ALERTABLE);
        (void)result;
    }
} // namespace

void Overlay::SetupOverlay(const char* window_name)
{
    CreateOverlay(window_name);
    if (!overlay || !CreateDevice() || !CreateImGui())
        shouldRun = false;
}

bool Overlay::CreateImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    OmniGhost::Paths::EnsureUserDirectories();
    g_imgui_ini_path = OmniGhost::Platform::WideToUtf8(
        (OmniGhost::Paths::LocalData() / L"imgui.ini").wstring());
    io.IniFilename = g_imgui_ini_path.empty() ? nullptr : g_imgui_ini_path.c_str();

    if (!ImGui_ImplWin32_Init(overlay)) {
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplDX11_Init(device, device_context)) {
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        return false;
    }

    CyberTheme::Initialize();
    HardwareMonitor::Initialize();
    ConfigHistory::Initialize();
    Hotkeys::Initialize();
    Onboarding::Initialize();
    Changelog::Initialize();
    const float initialScale = std::clamp(
        OmniGhost::Platform::DpiScaleForWindow(overlay) * app_settings::UiScalePreference(),
        0.75f, 2.50f);
    CyberTheme::SetUiScale(initialScale);
    CyberFonts::LoadFonts(initialScale);
    applied_ui_scale_ = initialScale;
    applied_monitor_index_ = app_settings::config.monitor_index;
    display_configuration_dirty_ = false;
    DigitalRain::Initialize();
    PerformanceMode::Reset();
    BrandAssets::Initialize(device, GetModuleHandleW(nullptr));
#ifndef UI_PREVIEW
    LauncherAssets::Initialize(device);
#endif
    BrandAssets::ApplyWindowIcon(overlay, GetModuleHandleW(nullptr));
    imgui_initialized_ = true;
    return true;
}

static MenuTab DefaultTabForGame(OmniGhost::ActiveGame game) {
    if (game == OmniGhost::ActiveGame::CS2) return MenuTab::TAB_CS2_VISUALS;
    if (game == OmniGhost::ActiveGame::Rust) return MenuTab::TAB_RUST_VISUALS;
    if (game == OmniGhost::ActiveGame::Warzone) return MenuTab::TAB_WARZONE_AIM;
    if (game == OmniGhost::ActiveGame::Valorant) return MenuTab::TAB_VALORANT_VISUALS;
    return MenuTab::TAB_VISUALS;
}

void Overlay::Render()
{
    auto& ctx = OmniGhost::GameContext::Instance();
    static OmniGhost::ActiveGame tab_game = ctx.GetActiveGame();
    static MenuTab current_tab = DefaultTabForGame(ctx.GetActiveGame());
    static MenuTab transition_tab = current_tab;
    static float tab_transition = 1.0f;
    if (tab_game != ctx.GetActiveGame()) {
        tab_game = ctx.GetActiveGame();
        current_tab = DefaultTabForGame(ctx.GetActiveGame());
        transition_tab = current_tab;
        tab_transition = app_settings::MotionEnabled() ? 0.0f : 1.0f;
    }

#ifndef UI_PREVIEW
    // Capture policy belongs to the owned overlay HWND and must be updated even
    // when the in-game menu is closed. Never exclude the launcher itself.
    SetCaptureExclusion(!RenderMenu && ctx.GetActiveGame() == OmniGhost::ActiveGame::CS2 && CS2::config.stream_proof);
#else
    SetCaptureExclusion(false);
#endif

    if (!app_settings::menu_open && !RenderMenu)
        return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const ImVec2 menu_size(
        (std::min)(CyberTheme::Metrics::WindowWidth,
                   (std::max)(CyberTheme::Px(760.0f), display.x - CyberTheme::Px(32.0f))),
        (std::min)(CyberTheme::Metrics::WindowHeight,
                   (std::max)(CyberTheme::Px(520.0f), display.y - CyberTheme::Px(32.0f))));
    ImGui::SetNextWindowPos(
        ImVec2((display.x - menu_size.x) * 0.5f, (display.y - menu_size.y) * 0.5f),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(menu_size, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 2.0f));
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowRounding, CyberTheme::Metrics::WindowRounding);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CyberTheme::Colors.Background);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    constexpr ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("OmniGhostMenu", nullptr, window_flags);

    const ImVec2 window_pos = ImGui::GetWindowPos();
    const ImVec2 window_size = ImGui::GetWindowSize();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // Pure-black menu base.
    draw_list->AddRectFilled(
        window_pos,
        ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
        CyberTheme::U32(CyberTheme::Colors.Background),
        CyberTheme::Metrics::WindowRounding);
    draw_list->AddRectFilledMultiColor(
        window_pos,
        ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
        IM_COL32(0, 0, 0, 245),
        IM_COL32(0, 0, 0, 225),
        IM_COL32(0, 0, 0, 245),
        IM_COL32(0, 0, 0, 238));

const PerformanceMode::State performance = PerformanceMode::Update(
        ImGui::GetIO().Framerate,
        ImGui::GetIO().DeltaTime,
        app_settings::config.performance_mode,
        app_settings::config.auto_performance);

    // Configure and tick dynamic performance manager
    static bool perfManagerConfigured = false;
    if (!perfManagerConfigured) {
        CyberPerformance::PerformanceManager::Config perfConfig;
        perfConfig.targetFrameTimeMs = 16.67f;
        perfConfig.warningThresholdMs = 20.0f;
        perfConfig.criticalThresholdMs = 33.33f;
        perfConfig.smoothingFrames = 60;
        perfConfig.adjustmentCooldownSec = 2.0f;
        perfConfig.reductionStep = 0.15f;
        perfConfig.restoreStep = 0.08f;
        CyberPerformance::g_performanceManager.Configure(perfConfig);
        perfManagerConfigured = true;
    }
    CyberPerformance::g_performanceManager.Tick(ImGui::GetIO().DeltaTime * 1000.0f);

    // Apply dynamic performance scaling to digital rain
    const auto perfLevels = CyberPerformance::g_performanceManager.GetEffectLevels();
    DigitalRain::SetDynamicDensity(perfLevels.digitalRainDensity);
    DigitalRain::SetDynamicMotionScale(perfLevels.motionScale);
    DigitalRain::SetDynamicConstellationDensity(perfLevels.constellationDensity);

    CyberTheme::DrawSubtleNoise(draw_list, window_pos,
        ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
        performance.effective ? 0.008f : 0.014f, 0x4F474D4Eu,
        performance.effective ? 100 : 210);

    // Shared Off/Subtle/Full rain. Subtle is the default and uses ~28% density.
    const float rainDensity = app_settings::DigitalRainDensity();
    // User preference is the base, dynamic scaling is applied inside DigitalRain::Draw
    DigitalRain::Draw(
        draw_list,
        window_pos,
        window_size,
        rainDensity > 0.0f && app_settings::menu_open,
        performance.effective,
        CyberTheme::Metrics::SidebarWidth,
        app_settings::DigitalRainOpacity(),
        rainDensity,
        app_settings::AnimationScale(),
        false);

    // Static structure stays deliberately quiet; gold is an accent, not a frame.
    DrawTechGrid(draw_list, window_pos, window_size, performance.effective);
    draw_list->AddRect(
        window_pos,
        ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y),
        CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.64f),
        CyberTheme::Metrics::WindowRounding, 0, CyberTheme::Px(1.0f));
    draw_list->AddLine(
        ImVec2(window_pos.x + CyberTheme::Px(28.f), window_pos.y + CyberTheme::Px(1.f)),
        ImVec2(window_pos.x + window_size.x - CyberTheme::Px(28.f), window_pos.y + CyberTheme::Px(1.f)),
        CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.26f), CyberTheme::Px(1.f));

#ifdef UI_PREVIEW
    const auto& status = preview_runtime::GetStatus();
    CyberWidgets::DrawHeader(
        window_pos, window_size, status.fps, status.dma_connected,
        status.build, status.ping_ms, status.players);
    CyberWidgets::DrawFooter(
        window_pos, window_size, status.fps, status.dma_connected,
        status.build, status.ping_ms, status.players);
#else
    {
        const float fps = ImGui::GetIO().Framerate;
        const float frame_time_ms = 1000.0f / std::max(0.1f, fps);
        HardwareMonitor::RecordFPS(fps, frame_time_ms);
        Hotkeys::Update();
        
        bool connected = false;
        const char* build = "Execu��o";
        int players = -1;

        switch (ctx.GetActiveGame()) {
        case OmniGhost::ActiveGame::CS2:
            connected = CS2::ready;
            build = "CS2";
            players = static_cast<int>(CS2::runtime.players.size());
            break;
        case OmniGhost::ActiveGame::Rust:
            connected = Rust::ready;
            build = "Rust";
            players = static_cast<int>(Rust::runtime.players.size());
            break;
        case OmniGhost::ActiveGame::Warzone:
            connected = Warzone::ready;
            build = "Warzone � BETA";
            players = static_cast<int>(Warzone::runtime.players.size());
            break;
        case OmniGhost::ActiveGame::Valorant:
            connected = Valorant::runtime.attached;
            build = "Valorant � BETA";
            players = static_cast<int>(Valorant::runtime.players.size());
            break;
        case OmniGhost::ActiveGame::FiveM:
        default:
            connected = true;
            build = "FiveM";
            players = static_cast<int>(FiveM::ESP::validPeds.size());
            break;
        }

        CyberWidgets::DrawHeader(window_pos, window_size, fps, connected, build, -1, players);
        CyberWidgets::DrawFooter(window_pos, window_size, fps, connected, build, -1, players);
    }
#endif

    // Draw onboarding wizard if needed
    if (Onboarding::ShouldRunOnboarding() && !Onboarding::IsActive()) {
        Onboarding::Start();
    }
    Onboarding::DrawWizard();

    CyberWidgets::DrawSidebar(window_pos, window_size, &current_tab);

    if (current_tab != transition_tab) {
        transition_tab = current_tab;
        tab_transition = app_settings::MotionEnabled() ? 0.0f : 1.0f;
    }
    const float motionScale = app_settings::AnimationScale();
    if (motionScale <= 0.0f) {
        tab_transition = 1.0f;
    } else {
        // 180-220 ms class transition, shortened in Subtle mode but never
        // allowed to linger. Only content moves; navigation remains stable.
        const float duration = CyberTheme::Metrics::PageTransitionSeconds / (std::max)(0.45f, motionScale);
        tab_transition = (std::min)(1.0f,
            tab_transition + (std::min)(ImGui::GetIO().DeltaTime, 0.05f) / duration);
    }
    const float tabEase = tab_transition * tab_transition * (3.0f - 2.0f * tab_transition);
    const float tabSlide = motionScale > 0.0f ? (1.0f - tabEase) * CyberTheme::Px(10.0f) : 0.0f;

    const ImVec2 content_pos(
        window_pos.x + CyberTheme::Metrics::SidebarWidth +
            CyberTheme::Metrics::ContentInset + tabSlide,
        window_pos.y + CyberTheme::Metrics::HeaderHeight +
            CyberTheme::Metrics::ContentInset);
    const ImVec2 content_size(
        window_size.x - CyberTheme::Metrics::SidebarWidth -
            CyberTheme::Metrics::ContentInset * 2.0f,
        window_size.y - CyberTheme::Metrics::HeaderHeight -
            CyberTheme::Metrics::FooterHeight -
            CyberTheme::Metrics::ContentInset * 2.0f);

    ImGui::SetCursorScreenPos(content_pos);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(
            CyberTheme::Metrics::ContentPaddingX,
            CyberTheme::Metrics::ContentPaddingY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(CyberTheme::Spacing::Sm, CyberTheme::Spacing::Xs));
    ImGui::BeginChild(
        "Content", content_size, false,
        ImGuiWindowFlags_NoBackground);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, (std::max)(0.18f, tabEase));
    DrawCurrentTab(current_tab, this);
    // Keep last widgets clear of the footer
    ImGui::Dummy(ImVec2(0.0f, CyberTheme::Spacing::Xl));
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleVar(2);

    ImGui::SetCursorScreenPos(window_pos);
    ImGui::InvisibleButton(
        "##menu_drag",
        ImVec2(window_size.x, CyberTheme::Metrics::HeaderHeight));
    if (ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const ImVec2 delta = ImGui::GetIO().MouseDelta;
        ImGui::SetWindowPos(
            ImVec2(window_pos.x + delta.x, window_pos.y + delta.y));
    }

    // Universal in-game session exit. The main loop consumes the request after
    // the frame, shuts the active adapter down, and returns to the launcher.
    {
        const float buttonSize = CyberTheme::Px(32.0f);
        const ImVec2 buttonMin(
            window_pos.x + window_size.x - buttonSize - CyberTheme::Px(16.0f),
            window_pos.y + (CyberTheme::Metrics::HeaderHeight - buttonSize) * 0.5f);
        ImGui::SetCursorScreenPos(buttonMin);
        ImGui::InvisibleButton("##close_game_menu", ImVec2(buttonSize, buttonSize));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        const ImU32 border = CyberTheme::WithAlpha(
            hovered ? CyberTheme::Colors.Error : CyberTheme::Colors.Border,
            hovered ? 0.72f : 0.48f);
        const ImU32 fill = CyberTheme::WithAlpha(
            hovered ? CyberTheme::Colors.Error : CyberTheme::Colors.Surface,
            hovered ? 0.13f : 0.42f);
        const ImVec2 buttonMax(buttonMin.x + buttonSize, buttonMin.y + buttonSize);
        draw_list->AddRectFilled(buttonMin, buttonMax, fill, buttonSize * 0.5f);
        draw_list->AddRect(buttonMin, buttonMax, border, buttonSize * 0.5f, 0, CyberTheme::Px(1.0f));
        const float inset = CyberTheme::Px(10.0f);
        const ImU32 cross = CyberTheme::U32(
            hovered ? CyberTheme::Colors.Error : CyberTheme::Colors.TextDisabled);
        draw_list->AddLine(ImVec2(buttonMin.x + inset, buttonMin.y + inset),
                           ImVec2(buttonMax.x - inset, buttonMax.y - inset), cross, CyberTheme::Px(1.5f));
        draw_list->AddLine(ImVec2(buttonMax.x - inset, buttonMin.y + inset),
                           ImVec2(buttonMin.x + inset, buttonMax.y - inset), cross, CyberTheme::Px(1.5f));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            RequestReturnToLauncher();
    }

    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
}

void Overlay::WaitForEvents(std::chrono::milliseconds timeout) {
    const DWORD timeoutMs = static_cast<DWORD>(timeout.count());
    const DWORD result = MsgWaitForMultipleObjectsEx(
        0, nullptr, timeoutMs,
        QS_ALLINPUT, MWMO_ALERTABLE);
    (void)result;
}


// FiveM Web Radar page - CS2-parity controls + tokenized URLs
void DrawFivemWebRadar() {
    using namespace Fivem_Radar;
    using namespace CyberWidgets;

    if (config.enabled) {
        const int port = (config.port >= 1024 && config.port <= 65535) ? config.port : 8080;
        config.port = port;
        EnsureRunning(port, config.lan);
        if (config.cloudflare && !CloudflareRunning())
            StartCloudflareTunnel(port);
        if (!config.cloudflare && CloudflareRunning())
            StopCloudflareTunnel();
    }

    BeginCard("FiveM Web Radar");

    const bool wasEnabled = config.enabled;
    ToggleSwitch("Ativar Web Radar", &config.enabled);
    if (wasEnabled && !config.enabled) {
        config.cloudflare = false;
        Shutdown();
        SaveConfig();
    } else if (!wasEnabled && config.enabled) {
        SaveConfig();
    }

    if (config.enabled) {
        CardGap();
        SectionTitle("Servidor HTTP");
        int port = config.port;
        if (ImGui::SliderInt("Porta", &port, 1024, 65535))
            config.port = port;
        ToggleSwitch("Acesso LAN", &config.lan);

        CardGap();
        SectionTitle("Cloudflare (HTTPS público)");
        ToggleSwitch("Ativar Cloudflare", &config.cloudflare);
        TextLine("Usa libs/cloudflared.exe do runtime privado.", TextTone::Secondary);

        CardGap();
        SectionTitle("Exibição");
        ToggleSwitch("Mostrar jogador local", &config.show_local);
        ToggleSwitch("Mostrar NPCs", &config.show_npcs);
        SliderFloat("Taxa de atualização (Hz)", &config.update_rate_hz, 5.0f, 30.0f, "%.0f");
        SliderFloat("Distância máxima (m)", &config.max_distance, 500.0f, 20000.0f, "%.0f");

        CardGap();
        SectionTitle("Status");
        TextLine(Status() ? Status() : "Offline",
                 IsRunning() ? TextTone::Success : (IsStarting() ? TextTone::Warning : TextTone::Secondary));

        const char* localUrl = LocalUrl();
        if (localUrl && localUrl[0]) {
            ImGui::TextWrapped("URL local: %s", localUrl);
            if (CyberButton("Abrir local", ImVec2(120, 30)))
                ShellExecuteA(nullptr, "open", localUrl, nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::SameLine();
            if (CyberButton("Copiar local", ImVec2(120, 30)))
                CopyToClipboard(localUrl, "URL local copiada");
        }

        if (config.lan) {
            TextLine("LAN protegida por token temporário.", TextTone::Warning);
            const char* lanUrl = LanUrl();
            if (lanUrl && lanUrl[0]) {
                ImGui::TextWrapped("URL LAN: %s", lanUrl);
                if (CyberButton("Copiar LAN", ImVec2(120, 30)))
                    CopyToClipboard(lanUrl, "URL LAN copiada");
            }
        }

        if (config.cloudflare) {
            const char* pub = PublicUrl();
            if (pub && pub[0]) {
                ImGui::TextWrapped("URL público: %s", pub);
                if (CyberButton("Abrir público", ImVec2(120, 30)))
                    ShellExecuteA(nullptr, "open", pub, nullptr, nullptr, SW_SHOWNORMAL);
                ImGui::SameLine();
                if (CyberButton("Copiar público", ImVec2(120, 30)))
                    CopyToClipboard(pub, "Link Cloudflare copiado");
            } else {
                TextLine("Cloudflare a obter link…", TextTone::Warning);
            }
        }
    } else {
        TextLine("Web Radar desativado.", TextTone::Secondary);
    }

    CardGap();
    if (CyberButton("Salvar configuração", ImVec2(160, 32))) {
        SaveConfig();
        TextLine("Guardado.", TextTone::Success);
    }
    ImGui::SameLine();
    if (CyberButton("Carregar configuração", ImVec2(160, 32))) {
        LoadConfig();
        TextLine("Carregado.", TextTone::Success);
    }

    EndCard();
}





