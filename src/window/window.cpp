#include "window.hpp"

#include "fonts.h"
#include "menu_tab.h"
#include "globals.h"
#include "theme.h"
#include "widgets.h"
#include "localization.h"
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
#include "../config/config_manager.h"
#include "../platform/app_paths.h"
#include "../platform/monitor_utils.h"
#include "../platform/text_encoding.h"
#include "game/esp_manager.h"
#include "../../Cs2/cs2_game.h"
#include "../../Warzone/warzone_game.h"
#include "../../Valorant/valorant_game.h"
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
#include "../launcher/game_adapter.h"

namespace {

    struct PagePresentation {
        const char* title;
        const char* description;
    };

    PagePresentation PresentationFor(MenuTab tab)
    {
        switch (tab) {
        case MenuTab::TAB_VISUALS:          return { Loc::Tr("nav.visuals"), app_settings::T("Jogadores, informação e aparência", "Players, information and appearance") };
        case MenuTab::TAB_AIM:              return { Loc::Tr("nav.aim"), app_settings::T("Alvo, suavidade e disparo", "Targeting, smoothing and trigger") };
        case MenuTab::TAB_VEHICLES:         return { Loc::Tr("nav.vehicles"), app_settings::T("ESP, estado e personalização", "ESP, status and customization") };
        case MenuTab::TAB_RADAR:            return { Loc::Tr("nav.radar"), app_settings::T("Posição, alcance e apresentação", "Position, range and appearance") };
        case MenuTab::TAB_FRIENDS:          return { Loc::Tr("nav.friends"), app_settings::T("Lista segura e jogadores próximos", "Friend list and nearby players") };
        case MenuTab::TAB_STATUS:           return { Loc::Tr("nav.status"), app_settings::T("Ligação, offsets e diagnóstico", "Connection, offsets and diagnostics") };
        case MenuTab::TAB_CONFIGS:          return { Loc::Tr("nav.configs"), app_settings::T("Interface, desempenho e preferências", "Interface, performance and preferences") };
        case MenuTab::TAB_SAVECONFIG:       return { Loc::Tr("nav.save"), app_settings::T("Guardar, carregar e organizar perfis", "Save, load and organize profiles") };
        case MenuTab::TAB_CS2_VISUALS:      return { "VISUAIS CS2", "ESP de jogadores e pré-visualização" };
        case MenuTab::TAB_CS2_AIM:          return { "MIRA CS2", "Perfis e assistência de mira" };
        case MenuTab::TAB_CS2_MISC:         return { "DIVERSOS CS2", "Widgets, bomba e utilitários" };
        case MenuTab::TAB_WARZONE_AIM:      return { "MIRA WARZONE", "Alvo, precisão e suavidade" };
        case MenuTab::TAB_WARZONE_VISUALS:  return { "VISUAIS WARZONE", "ESP de jogadores" };
        case MenuTab::TAB_WARZONE_RADAR:    return { "RADAR WARZONE", "Posições e orientação" };
        case MenuTab::TAB_WARZONE_WORLD:    return { "MUNDO WARZONE", "Objetos e informação" };
        case MenuTab::TAB_WARZONE_PLAYERS:  return { "JOGADORES WARZONE", "Lista e prioridades" };
        case MenuTab::TAB_WARZONE_MISC:     return { "EXTRAS WARZONE", "Sistema e utilidades" };
        case MenuTab::TAB_VALORANT_VISUALS: return { "VISUAIS VALORANT", "ESP e informação de jogadores" };
        case MenuTab::TAB_VALORANT_AIM:     return { "MIRA VALORANT", "Alvo e assistência" };
        case MenuTab::TAB_VALORANT_STATUS:  return { "SISTEMA VALORANT", "Ligação e diagnóstico" };
        case MenuTab::TAB_FORTNITE_VISUALS: return { "VISUAIS FORTNITE", "ESP e informação" };
        case MenuTab::TAB_FORTNITE_AIM:     return { "MIRA FORTNITE", "Alvo e assistência" };
        case MenuTab::TAB_FORTNITE_STATUS:  return { "SISTEMA FORTNITE", "Ligação e diagnóstico" };
        case MenuTab::TAB_RUST_VISUALS:     return { "VISUAIS RUST", "ESP de jogadores" };
        case MenuTab::TAB_RUST_AIM:         return { "MIRA RUST", "Aimbot" };
        case MenuTab::TAB_RUST_SYSTEM:      return { "SISTEMA RUST", "DMA, DTB e offsets" };
        default:                            return { "OMNIGHOST", "Centro de controlo" };
        }
    }

    void DrawPageHeading(MenuTab tab)
    {
        const PagePresentation page = PresentationFor(tab);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float headingWidth = ImGui::GetContentRegionAvail().x;
        const float headingHeight = CyberTheme::Px(52.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 end(pos.x + headingWidth, pos.y + headingHeight);

        dl->AddRectFilled(pos, end,
            CyberTheme::U32(CyberTheme::Mix(CyberTheme::Colors.Panel,
                                            CyberTheme::Colors.Background, 0.22f)),
            CyberTheme::Metrics::CardRounding);
        dl->AddRect(pos, end, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.54f),
            CyberTheme::Metrics::CardRounding, 0, CyberTheme::Px(1.0f));
        dl->AddRectFilled(
            ImVec2(pos.x, pos.y + CyberTheme::Px(10.0f)),
            ImVec2(pos.x + CyberTheme::Px(3.0f), end.y - CyberTheme::Px(10.0f)),
            CyberTheme::U32(CyberTheme::Colors.Gold), CyberTheme::Px(1.5f));
        dl->AddText(ImVec2(pos.x + CyberTheme::Px(16.0f), pos.y + CyberTheme::Px(8.0f)),
            CyberTheme::U32(CyberTheme::Colors.Text), page.title);
        dl->AddText(ImVec2(pos.x + CyberTheme::Px(16.0f), pos.y + CyberTheme::Px(28.0f)),
            CyberTheme::U32(CyberTheme::Colors.TextDisabled), page.description);

        ImGui::Dummy(ImVec2(headingWidth, headingHeight + CyberTheme::Metrics::GridGap));
    }

    std::string g_imgui_ini_path;

    void DrawCurrentTab(MenuTab tab, Overlay* overlay)
    {
        switch (tab) {
        case MenuTab::TAB_VISUALS:     DrawVisuals(); break;
        case MenuTab::TAB_AIM:         DrawAim(); break;
        case MenuTab::TAB_VEHICLES:    DrawVehicles(); break;
        case MenuTab::TAB_RADAR:       DrawRadar(); break;
        case MenuTab::TAB_FRIENDS:     DrawFriends(); break;
        case MenuTab::TAB_STATUS:      DrawFiveMStatus(); break;
        case MenuTab::TAB_CONFIGS:     DrawConfigs(overlay); break;
        case MenuTab::TAB_SAVECONFIG:  DrawSaveConfigs(); break;
        case MenuTab::TAB_CS2_VISUALS: DrawCs2Visuals(); break;
        case MenuTab::TAB_CS2_AIM:     DrawCs2Aim(); break;
        case MenuTab::TAB_CS2_MISC:    DrawCs2Misc(); break;
        case MenuTab::TAB_CS2_RADAR:   DrawCs2Radar(); break;
        case MenuTab::TAB_WARZONE_AIM:      DrawWarzoneAim(); break;
        case MenuTab::TAB_WARZONE_VISUALS:  DrawWarzoneVisuals(); break;
        case MenuTab::TAB_WARZONE_RADAR:    DrawWarzoneRadar(); break;
        case MenuTab::TAB_WARZONE_WORLD:    DrawWarzoneWorld(); break;
        case MenuTab::TAB_WARZONE_PLAYERS:  DrawWarzonePlayers(); break;
        case MenuTab::TAB_WARZONE_MISC:     DrawWarzoneMisc(); break;
        case MenuTab::TAB_VALORANT_VISUALS:  DrawValorantVisuals(); break;
        case MenuTab::TAB_VALORANT_AIM:      DrawValorantAim(); break;
        case MenuTab::TAB_VALORANT_STATUS:   DrawValorantStatus(); break;
        case MenuTab::TAB_FORTNITE_VISUALS:  DrawFortniteVisuals(); break;
        case MenuTab::TAB_FORTNITE_AIM:      DrawFortniteAim(); break;
        case MenuTab::TAB_FORTNITE_STATUS:   DrawFortniteStatus(); break;
        case MenuTab::TAB_RUST_VISUALS:      DrawRustVisuals(); break;
        case MenuTab::TAB_RUST_AIM:          DrawRustAim(); break;
        case MenuTab::TAB_RUST_SYSTEM:       DrawRustSystem(); break;
        
        // Unified system pages
        case MenuTab::TAB_UNIFIED_AIM:       DrawUnifiedAim(); break;
        case MenuTab::TAB_SOUND_ESP:         DrawSoundESP(); break;
        case MenuTab::TAB_SPECTATOR_LIST:    DrawSpectatorList(); break;
        case MenuTab::TAB_TRIGGERBOT:        DrawTriggerbot(); break;
        case MenuTab::TAB_RECOIL_CONTROL:    DrawRecoilControl(); break;
        case MenuTab::TAB_PREDICTION:        DrawPrediction(); break;
        case MenuTab::TAB_VISIBILITY:        DrawVisibility(); break;
        case MenuTab::TAB_BONE_SYSTEM:       DrawBoneSystem(); break;
        case MenuTab::TAB_SMOOTH_CURVES:     DrawSmoothCurves(); break;
        case MenuTab::TAB_RECOIL_PATTERNS:   DrawRecoilPatterns(); break;
        case MenuTab::TAB_ENTITY_CACHE:      DrawEntityCache(); break;
        case MenuTab::TAB_PROFILE_MANAGER:   DrawProfileManager(); break;
        case MenuTab::TAB_OFFSET_MANAGER:    DrawOffsetManager(); break;
        case MenuTab::TAB_RESOLUTION:        DrawResolution(); break;
        case MenuTab::TAB_GAME_ADAPTER:      DrawGameAdapter(); break;
        
        // FiveM specific pages
        case MenuTab::TAB_FIVEM_OBJECT_ESP:  CyberWidgets::DrawFivemObjectESP(); break;
        
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
    if (game == OmniGhost::ActiveGame::Warzone) return MenuTab::TAB_WARZONE_AIM;
    if (game == OmniGhost::ActiveGame::Valorant) return MenuTab::TAB_VALORANT_VISUALS;
    if (game == OmniGhost::ActiveGame::Fortnite) return MenuTab::TAB_FORTNITE_VISUALS;
    if (game == OmniGhost::ActiveGame::Rust) return MenuTab::TAB_RUST_VISUALS;
    return MenuTab::TAB_VISUALS;
}

void Overlay::Render()
{
    // Config persistence is game-independent. Keep ticking even while the menu
    // is hidden so a change made just before closing it is still committed.
    config_manager::TickAutoSave();

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
    DigitalRain::SetQualityFromEffectLevel(static_cast<int>(app_settings::config.digital_rain_level));
    DigitalRain::Draw(
        draw_list,
        window_pos,
        window_size,
        rainDensity > 0.0f && (app_settings::menu_open || RenderMenu),
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
        const char* build = "Execução";
        int players = -1;

        switch (ctx.GetActiveGame()) {
        case OmniGhost::ActiveGame::CS2: {
            const auto snapshot = CS2::AcquireRuntimeSnapshot();
            connected = CS2::ready;
            build = "CS2";
            players = snapshot ? static_cast<int>(snapshot->players.size()) : 0;
            break;
        }
        case OmniGhost::ActiveGame::Warzone:
            connected = Warzone::ready;
            build = "Warzone · BETA";
            players = static_cast<int>(Warzone::runtime.players.size());
            break;
        case OmniGhost::ActiveGame::Valorant:
            connected = Valorant::runtime.attached;
            build = "Valorant · BETA";
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
    DrawPageHeading(current_tab);
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
