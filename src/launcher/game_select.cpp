#include "game_select.h"

#include "launcher_assets.h"
#include "changelog_service.h"
#include "updates_page.h"
#include "license_page.h"
#include "launcher_status.h"
#include "../app_version.h"
#include "../updater/update_ui.h"
#include "../updater/update_service.h"
#include "../platform/app_paths.h"
#include "../platform/runtime_bootstrap.h"
#include "../platform/build_info.h"
#include "../platform/diagnostics_package.h"
#include "../platform/session_log.h"
#include "../platform/system_info.h"
#include "../platform/support_error.h"
#include "../platform/text_encoding.h"
#include "../config/app_settings.h"
#include "../config/config_manager.h"
#include "../window/digital_rain.h"
#include "../window/brand_assets.h"
#include "../window/localization.h"
#include "../window/performance_mode.h"
#include "../window/theme.h"
#include "../window/ui_format.h"
#include "../window/widgets.h"
#include "../window/InputDevicesCard.h"
#include "../platform/monitor_utils.h"
#include "../window/window.hpp"
#include "../globals.h"
#include "../auth/local_auth_service.h"
#include "../licensing/license_service.h"
#include "../platform/offset_auto.h"
#include "../../Cs2/cs2_game.h"
#include "../../Rust/rust_game.h"
#include "../../Warzone/warzone_game.h"
#include "../../Valorant/valorant_game.h"
#include "../../Fortnite/fortnite_game.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../../Fivem/aimbot/aim_type.h"
#include "../makcu/makcu_wrapper.h"
#include "../kmbox/kmbox_net.h"
#include "../ferrum/ferrum_device.h"
#include "window/fonts.h"
#include "imgui.h"

#include <Windows.h>
#include <TlHelp32.h>
#include <Shellapi.h>
#include <commdlg.h>

#pragma comment(lib, "Comdlg32.lib")

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <future>
#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <string_view>
#include <thread>
#include <vector>

namespace Launcher {
namespace {

// Launcher colors are aliases of the single product-wide CyberTheme palette.
// The launcher no longer owns a parallel gold/text palette.
float S(float logical) { return CyberTheme::Px(logical); }

ImU32 C_BG(int alpha = 255) { return CyberTheme::WithAlpha(CyberTheme::Colors.Background, alpha / 255.0f); }
ImU32 C_PANEL(int alpha = 255) { return CyberTheme::WithAlpha(CyberTheme::Colors.Surface, alpha / 255.0f); }
ImU32 C_CARD(int alpha = 255) { return CyberTheme::WithAlpha(CyberTheme::Colors.Card, alpha / 255.0f); }
ImU32 C_CARD_TOP(int alpha = 255) { return CyberTheme::WithAlpha(CyberTheme::Colors.CardHover, alpha / 255.0f); }
ImU32 C_GOLD() { return CyberTheme::U32(CyberTheme::Colors.Gold); }
ImU32 C_GOLD_LT() { return CyberTheme::U32(CyberTheme::Colors.GoldHover); }
ImU32 C_TEXT() { return CyberTheme::U32(CyberTheme::Colors.Text); }
ImU32 C_MUTED() { return CyberTheme::U32(CyberTheme::Colors.TextDisabled); }
ImU32 C_MUTED2() { return CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.68f); }
ImU32 C_GREEN() { return CyberTheme::U32(CyberTheme::Colors.Success); }
ImU32 C_RED() { return CyberTheme::U32(CyberTheme::Colors.Error); }

ImU32 WithAlpha(ImU32 color, int alpha) {
    return (color & 0x00FFFFFF) | (static_cast<ImU32>(std::clamp(alpha, 0, 255)) << 24);
}

ImU32 GameColor(const RgbColor& color, int alpha = 255) {
    return IM_COL32(color.r, color.g, color.b, alpha);
}

enum class ToastAction { None, OpenUpdates, OpenLogs };

struct GameRuntime {
    const GameDefinition* definition = nullptr;
    CardState state = CardState::Ready;
    bool installed = false;
    float hover = 0.f;
    float entry = 0.f;
    float launch_timer = 0.f;
};

enum class NavPage : int {
    Home = 0,
    Library = 1,
    Marketplace = 2,
    Updates = 3,
    Diagnostics = 4,
    Settings = 5,
    Account = 6,
};

std::vector<GameRuntime> g_games;
GameId g_selected = GameId::None;
GameId g_detail_game = GameId::None;
GameId g_help_game = GameId::None;
GameId g_reset_settings_game = GameId::None;
GameId g_card_menu_game = GameId::None;
float g_time = 0.f;
float g_page_transition = 1.f;
int g_nav = static_cast<int>(NavPage::Home);
int g_filter = 0;
int g_keyboard_index = 0;
bool g_keyboard_active = false;
bool g_profile_menu_open = false;
bool g_logout_requested = false;
char g_search[96]{};
char g_license_input[520]{};
std::string g_license_feedback;
bool g_license_feedback_success = false;
struct LicenseOperationResult {
    bool success = false;
    std::string message;
};
std::future<LicenseOperationResult> g_license_operation;
bool g_license_operation_pending = false;
app_settings::SettingsPage g_settings_page = app_settings::SettingsPage::General;

std::future<OmniGhost::OffsetAuto::Result> g_offset_refresh;
GameId g_offset_refresh_game = GameId::None;
bool g_offset_refresh_pending = false;
std::future<void> g_startup_offset_checks;

void ChangeNavigation(int page);
void PushToast(const char* text, ImU32 color, ToastAction action, const char* action_label);
ActiveGame ToActiveGame(GameId id);
void RequestOffsetRefresh(GameId id, bool silent);

#include "game_select_session.inl"
#include "game_select_presentation.inl"
#include "game_select_pages.inl"
#include "game_select_modals.inl"
} // namespace

void Reset(EntryReason reason) {
    PerformanceMode::Reset();
    g_selected = GameId::None;
    g_detail_game = GameId::None;
    g_time = 0.f;
    g_page_transition = reason != EntryReason::Normal && app_settings::MotionEnabled()
        ? 0.f : 1.f;
    g_nav = static_cast<int>(NavPage::Home);
    g_filter = 0;
    g_keyboard_index = 0;
    g_keyboard_active = false;
    g_profile_menu_open = false;
    g_help_game = GameId::None;
    g_reset_settings_game = GameId::None;
    g_card_menu_game = GameId::None;
    // Let the UI transition settle before the first remote process inventory.
    // This avoids hitting the FPGA again in the same instant that startup
    // finishes opening the device.
    g_next_remote_process_scan = ImGui::GetTime() + 6.0;
    g_logout_requested = false;
    g_search[0] = 0;
    g_license_input[0] = 0;
    g_license_feedback.clear();
    g_license_feedback_success = false;
    g_settings_page = app_settings::SettingsPage::General;
    LoadLauncherState();
    switch (app_settings::config.start_behavior) {
    case app_settings::StartBehavior::Library:
        g_nav = static_cast<int>(NavPage::Library);
        break;
    case app_settings::StartBehavior::LastGame: {
        const GameId last = LastPlayedGame();
        if (last != GameId::None) {
            g_nav = static_cast<int>(NavPage::Library);
            g_detail_game = last;
        }
        break;
    }
    case app_settings::StartBehavior::Home:
    default:
        g_nav = static_cast<int>(NavPage::Home);
        break;
    }
    LauncherUpdates::Reset();
    g_games.clear();
    std::size_t count = 0;
    const GameDefinition* games = Games(count);
    g_games.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        GameRuntime runtime{};
        runtime.definition = &games[index];
        runtime.installed = DetectInstalled(games[index]);
        runtime.state = ResolveRuntimeState(runtime);
        g_games.push_back(runtime);
    }

    // Persisted live failures remain quarantined until the API supplies a new,
    // structurally valid candidate. A network failure never clears this state.
    for (const GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        if (runtime.definition->coming_soon) continue;
        const ActiveGame active = ToActiveGame(runtime.definition->launch_id);
        if (GetGameHistory(runtime.definition->launch_id).lastResult == SessionResult::OffsetsFailed)
            OmniGhost::OffsetAuto::MarkOutdated(active, "A validação no jogo falhou na última sessão");
    }
    if (!g_startup_offset_checks.valid()) {
        // Soft background check (ETag / If-None-Match) for every API-backed game
        for (ActiveGame g : {
                ActiveGame::Rust, ActiveGame::CS2,
                ActiveGame::Warzone, ActiveGame::FiveM, ActiveGame::Apex}) {
            OmniGhost::OffsetAuto::MarkChecking(g);
        }
        g_startup_offset_checks = std::async(std::launch::async, [] {
            OmniGhost::OffsetAuto::RefreshAllSupported(false);
        });
    }
}

bool Draw() {
    float delta = ImGui::GetIO().DeltaTime;
    if (delta > .05f) delta = .05f;
    g_time += delta;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    ImDrawList* foreground = ImGui::GetForegroundDrawList();
    if (!background || !foreground) return false;

    RefreshRuntimeStates();

    const PerformanceMode::State performance = PerformanceMode::Update(
        ImGui::GetIO().Framerate,
        delta,
        app_settings::config.performance_mode,
        app_settings::config.auto_performance);

    if (GetAsyncKeyState(VK_ESCAPE) & 1) {
        if (g_help_game != GameId::None) g_help_game = GameId::None;
        else if (g_reset_settings_game != GameId::None) g_reset_settings_game = GameId::None;
        else if (g_card_menu_game != GameId::None) g_card_menu_game = GameId::None;
        else if (g_profile_menu_open) g_profile_menu_open = false;
        else if (g_nav != static_cast<int>(NavPage::Home)) ChangeNavigation(static_cast<int>(NavPage::Home));
    }

    DrawBackground(background, display, performance.effective);

    if (g_nav == static_cast<int>(NavPage::Home)) {
        DrawHome(display);
    } else if (g_nav == static_cast<int>(NavPage::Library)) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(display);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::Begin("##launcher_interaction", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);
        DrawLibrary(foreground, display, delta);
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    } else if (g_nav == static_cast<int>(NavPage::Updates)) {
        LauncherUpdates::Draw(display, S(78.f), S(56.f));
    } else if (g_nav == static_cast<int>(NavPage::Marketplace)) {
        DrawMarketplace(display);
    } else if (g_nav == static_cast<int>(NavPage::Diagnostics)) {
        DrawDiagnostics(display);
    } else if (g_nav == static_cast<int>(NavPage::Settings)) {
        DrawSettings(display);
    } else if (g_nav == static_cast<int>(NavPage::Account)) {
        DrawAccount(display);
    }

    bool launch_ready = false;
    for (const GameRuntime& game : g_games) {
        if (game.state == CardState::Launching && game.launch_timer >= .45f) {
            launch_ready = true;
            break;
        }
    }

    const float animation = app_settings::AnimationScale();
    if (animation <= 0.f) g_page_transition = 1.f;
    else g_page_transition = (std::min)(1.f, g_page_transition + delta / (CyberTheme::Metrics::PageTransitionSeconds / (std::max)(0.35f, animation)));
    if (g_page_transition < 1.f) {
        const float eased = g_page_transition * g_page_transition * (3.f - 2.f * g_page_transition);
        foreground->AddRectFilled(ImVec2(0, S(58.f)), ImVec2(display.x, display.y - S(36.f)),
            C_BG(static_cast<int>((1.f - eased) * 205.f)));
        const float sweep_x = display.x * eased;
        foreground->AddRectFilled(ImVec2(sweep_x - S(24.f), S(58.f)),
            ImVec2(sweep_x, display.y - S(36.f)),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.035f * (1.f - eased)));
    }

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    const float headerInteractionHeight = g_profile_menu_open ? S(194.f) : S(58.f);
    ImGui::SetNextWindowSize(ImVec2(display.x, headerInteractionHeight));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin("##launcher_header_interaction", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    DrawHeader(foreground, display);
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    DrawFooter(foreground, display);
    OmniGhost::UpdateUI::Draw();
    DrawLauncherModals(foreground, display);
    return launch_ready && g_selected != GameId::None;
}

GameId Selected() { return g_selected; }
void SetSelected(GameId id) { g_selected = id; }

const char* SelectedName() {
    const GameDefinition* game = FindGame(g_selected);
    return game ? game->name : "—";
}

bool ConsumeLogoutRequest() {
    const bool requested = g_logout_requested;
    g_logout_requested = false;
    return requested;
}

} // namespace Launcher
