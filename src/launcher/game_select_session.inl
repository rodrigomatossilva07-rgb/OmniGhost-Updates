// This implementation fragment is included by game_select.cpp inside Launcher's private namespace.
// It is separated by responsibility to keep the launcher coordinator reviewable.

void StartLicenseOperation(bool createTemporary, std::string key = {}) {
    if (g_license_operation_pending)
        return;
    g_license_operation_pending = true;
    g_license_feedback_success = false;
    g_license_feedback = createTemporary ? "A criar licença local..." : "A validar licença...";
    g_license_operation = std::async(std::launch::async,
        [createTemporary, key = std::move(key)]() mutable {
            LicenseOperationResult result;
            try {
                result.success = createTemporary
                    ? OmniGhost::Licensing::CreateTemporaryLocalLicense(&result.message)
                    : OmniGhost::Licensing::ActivateLocalKey(key, &result.message);
            } catch (const std::exception& error) {
                result.message = std::string("A operação da licença falhou: ") + error.what();
            } catch (...) {
                result.message = "A operação da licença falhou inesperadamente.";
            }
            if (!key.empty())
                SecureZeroMemory(key.data(), key.size());
            return result;
        });
}

void StartRemoteLicenseUpgrade(std::string key) {
    if (g_license_operation_pending)
        return;
    const std::string username = OmniGhost::Licensing::RemoteUsername();
    if (username.empty()) {
        g_license_feedback_success = false;
        g_license_feedback = "Inicia sessão no KeyAuth antes de adicionar uma licença.";
        SecureZeroMemory(key.data(), key.size());
        return;
    }
    g_license_operation_pending = true;
    g_license_feedback_success = false;
    g_license_feedback = "A adicionar licença à conta...";
    g_license_operation = std::async(std::launch::async,
        [username, key = std::move(key)]() mutable {
            LicenseOperationResult result;
            try {
                const auto response = OmniGhost::Licensing::Upgrade(username, key);
                result.success = response.Ok();
                result.message = response.userMessage;
                if (result.success)
                    OmniGhost::Licensing::Refresh();
            } catch (const std::exception& error) {
                result.message = std::string("Não foi possível adicionar a licença: ") + error.what();
            } catch (...) {
                result.message = "Não foi possível adicionar a licença.";
            }
            if (!key.empty())
                SecureZeroMemory(key.data(), key.size());
            return result;
        });
}

void PollLicenseOperation() {
    using namespace std::chrono_literals;
    if (!g_license_operation_pending || !g_license_operation.valid() ||
        g_license_operation.wait_for(0ms) != std::future_status::ready)
        return;
    try {
        LicenseOperationResult result = g_license_operation.get();
        g_license_feedback_success = result.success;
        g_license_feedback = std::move(result.message);
    } catch (const std::exception& error) {
        g_license_feedback_success = false;
        g_license_feedback = std::string("A operação da licença falhou: ") + error.what();
    } catch (...) {
        g_license_feedback_success = false;
        g_license_feedback = "A operação da licença falhou inesperadamente.";
    }
    g_license_operation_pending = false;
}

std::filesystem::path RequiredDataPath(GameId id) {
    const std::filesystem::path root = OmniGhost::Paths::InstallDirectory();
    switch (id) {
    case GameId::FiveM: return {};
    case GameId::CS2:
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
        return root / L"data" / L"cs2_offsets.json";
#else
        return {}; // Customer builds load the compact embedded CS2 snapshot.
#endif
    case GameId::Warzone:
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
        return root / L"data" / L"warzone_offsets.json";
#else
        return {};
#endif
    case GameId::Valorant:
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
        return root / L"data" / L"valorant_offsets.json";
#else
        return {}; // Current compiled defaults; no plaintext customer snapshot.
#endif
    case GameId::Apex:
#if defined(OMNIGHOST_DEV_EXTERNAL_OFFSETS)
        return root / L"data" / L"apex_offsets.json";
#else
        return {};
#endif
    default: return {};
    }
}

bool DetectInstalled(const GameDefinition& game) {
    // Dev/product preference: never gate the launcher card on DLL presence.
    // Missing FPGA/DLLs are reported only when the user actually launches.
    if (game.coming_soon)
        return false;
    return true;
}

bool HasRequiredOffsets(const GameDefinition& game) {
    if (game.coming_soon)
        return false;
    const auto data = RequiredDataPath(game.launch_id);
    if (data.empty())
        return true;
    std::error_code error;
    return std::filesystem::is_regular_file(data, error);
}

bool LocalProcessRunning(std::initializer_list<const wchar_t*> names) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            for (const wchar_t* name : names) {
                if (name && _wcsicmp(entry.szExeFile, name) == 0) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

enum RemoteGameMask : std::uint32_t {
    RemoteCs2      = 1u << 0,
    RemoteWarzone  = 1u << 2,
    RemoteValorant = 1u << 3,
    RemoteFortnite = 1u << 4,
    RemoteFiveM    = 1u << 5,
};

std::future<std::uint32_t> g_remote_process_scan;
std::uint32_t g_remote_process_mask = 0;
double g_next_remote_process_scan = 0.0;

bool RemoteGameRunning(GameId id) noexcept {
    std::uint32_t bit = 0;
    switch (id) {
    case GameId::CS2: bit = RemoteCs2; break;
    case GameId::Warzone: bit = RemoteWarzone; break;
    case GameId::Valorant: bit = RemoteValorant; break;
    case GameId::Fortnite: bit = RemoteFortnite; break;
    case GameId::FiveM: bit = RemoteFiveM; break;
    default: return false;
    }
    return (g_remote_process_mask & bit) != 0;
}

void PollRemoteProcessScan(double now) {
    using namespace std::chrono_literals;
    if (g_remote_process_scan.valid()) {
        if (g_remote_process_scan.wait_for(0ms) != std::future_status::ready)
            return;
        try {
            g_remote_process_mask = g_remote_process_scan.get();
        } catch (const std::exception& error) {
            std::cerr << "[LAUNCHER] Remote process scan failed: " << error.what() << "\n";
            g_remote_process_mask = 0;
        } catch (...) {
            std::cerr << "[LAUNCHER] Remote process scan failed with an unknown error.\n";
            g_remote_process_mask = 0;
        }
    }
    if (now < g_next_remote_process_scan)
        return;
    g_next_remote_process_scan = now + 6.0;
    if (!mem.GetDiagnosticsSnapshot().deviceOpen) {
        g_remote_process_mask = 0;
        return;
    }
    // VMM process enumeration can wait on the FPGA. It must never run on the
    // ImGui/Win32 message thread, otherwise a normal refresh looks like a hang.
    g_remote_process_scan = std::async(std::launch::async, [] {
        std::uint32_t mask = 0;
        const std::vector<std::string> processes = mem.GetProcessNames();
        auto present = [&processes](std::initializer_list<const char*> names) {
            for (const std::string& process : processes) {
                for (const char* name : names)
                    if (name && _stricmp(process.c_str(), name) == 0) return true;
            }
            return false;
        };
        if (present({"cs2.exe"})) mask |= RemoteCs2;
        if (present({"cod.exe"})) mask |= RemoteWarzone;
        if (present({"VALORANT-Win64-Shipping.exe", "VALORANT.exe"})) mask |= RemoteValorant;
        if (present({"FortniteClient-Win64-Shipping.exe", "Fortnite.exe"})) mask |= RemoteFortnite;
        if (present({"FiveM_GTAProcess.exe", "FiveM_b3258_GTAProcess.exe",
                     "FiveM_b3407_GTAProcess.exe", "FiveM_b3570_GTAProcess.exe",
                     "GTAProcess.exe"})) mask |= RemoteFiveM;
        return mask;
    });
}

bool DetectRunning(GameId id) {
    // Prefer explicit adapter state, then check actual process presence. The
    // process check works locally and, when a DMA session is already open, on
    // the remote game PC as well. The launcher never opens DMA just to populate
    // this badge.
    switch (id) {
    case GameId::CS2:
        // `CS2::ready` can describe an earlier successful attachment. Only a
        // current cs2.exe process is allowed to produce the Running badge.
        return LocalProcessRunning({L"cs2.exe"}) || RemoteGameRunning(id);
    case GameId::Warzone:
        return Warzone::ready || LocalProcessRunning({L"cod.exe"}) || RemoteGameRunning(id);
    case GameId::Valorant:
        return Valorant::runtime.attached ||
               LocalProcessRunning({L"VALORANT-Win64-Shipping.exe", L"VALORANT.exe"}) ||
               RemoteGameRunning(id);
    case GameId::Fortnite:
        return Fortnite::runtime.attached ||
               LocalProcessRunning({L"FortniteClient-Win64-Shipping.exe", L"Fortnite.exe"}) ||
               RemoteGameRunning(id);
    case GameId::FiveM: {
        const auto dma = mem.GetDiagnosticsSnapshot();
        if (g_activeGame == ActiveGame::FiveM && dma.processInitialized)
            return true;
        return LocalProcessRunning({L"FiveM_GTAProcess.exe", L"GTAProcess.exe"}) ||
               RemoteGameRunning(id);
    }
    default: return false;
    }
}

// DMA process context is global to the application. Starting an adapter for a
// different game while another supported title is running can replace that
// context mid-session, which is unsafe for both adapters.
const GameDefinition* FindConflictingRunningGame(GameId selected) {
    std::size_t count = 0;
    const GameDefinition* games = Games(count);
    for (std::size_t i = 0; i < count; ++i) {
        const GameDefinition& candidate = games[i];
        if (candidate.launch_id == GameId::None || candidate.launch_id == selected)
            continue;
        if (DetectRunning(candidate.launch_id))
            return &candidate;
    }
    return nullptr;
}

bool CanStartGame(GameId selected) {
    const GameDefinition* conflict = FindConflictingRunningGame(selected);
    if (!conflict)
        return true;

    const GameDefinition* requested = FindGame(selected);
    char message[192]{};
    std::snprintf(message, sizeof(message), Loc::Tr("launcher.toast.close_other_game"),
                  conflict->name, requested ? requested->name : "o jogo selecionado");
    PushToast(message, C_RED(), ToastAction::None, nullptr);
    return false;
}

bool AdapterAttached(GameId id) {
    switch (id) {
    case GameId::CS2: return CS2::ready;
    case GameId::Warzone: return Warzone::ready;
    case GameId::Valorant: return Valorant::runtime.attached;
    case GameId::Fortnite: return Fortnite::runtime.attached;
    case GameId::FiveM:
        return g_activeGame == ActiveGame::FiveM && mem.GetDiagnosticsSnapshot().processInitialized;
    default: return false;
    }
}

CardState ResolveRuntimeState(GameRuntime& runtime) {
    if (!runtime.definition)
        return CardState::Error;
    const GameDefinition& game = *runtime.definition;
    runtime.installed = DetectInstalled(game);
    if (game.coming_soon)
        return CardState::ComingSoon;
    if (!OmniGhost::Licensing::HasGameAccess(game.id))
        return CardState::LicenseRequired;
    if (OmniGhost::Update::UpdateService::Instance().BlocksGameLaunch())
        return CardState::UpdateRequired;

    // Do not block the card on DLL detection, dependency integrity, sticky
    // history, or offset pre-checks. Clicking always proceeds to open FPGA/menu;
    // real failures surface during the explicit launch path.
    if (DetectRunning(game.launch_id))
        return CardState::Running;
    return game.beta ? CardState::Beta : CardState::Ready;
}

void PollOffsetRefresh() {
    if (!g_offset_refresh_pending || !g_offset_refresh.valid())
        return;
    using namespace std::chrono_literals;
    if (g_offset_refresh.wait_for(0ms) != std::future_status::ready)
        return;
    const auto result = g_offset_refresh.get();
    g_offset_refresh_pending = false;
    const GameId game = g_offset_refresh_game;
    g_offset_refresh_game = GameId::None;
    if (result == OmniGhost::OffsetAuto::Result::Ok ||
        result == OmniGhost::OffsetAuto::Result::Current) {
        RecordGameSession(game, SessionResult::None, {});
        PushToast(Loc::Tr("launcher.toast.offsets_updated"), C_GREEN(), ToastAction::None, nullptr);
    } else if (result == OmniGhost::OffsetAuto::Result::UsingCached) {
        PushToast(OmniGhost::OffsetAuto::g_status.c_str(), C_MUTED(), ToastAction::None, nullptr);
    } else if (result != OmniGhost::OffsetAuto::Result::Busy) {
        RecordGameSession(game, SessionResult::OffsetsFailed, OmniGhost::OffsetAuto::g_status);
        PushToast(Loc::Tr("launcher.toast.offsets_failed"), C_RED(), ToastAction::OpenLogs,
                  Loc::Tr("launcher.action.open_logs"));
    }
}

void RefreshRuntimeStates() {
    PollLicenseOperation();
    PollOffsetRefresh();
    static double nextRefresh = 0.0;
    static double nextStaleRetry = 0.0;
    const double now = ImGui::GetTime();
    // Remote inventory is useful only while the Library is visible. Keeping it
    // completely idle on Home prevents an otherwise pointless FPGA request
    // immediately after the startup probe succeeds.
    if (g_nav == static_cast<int>(NavPage::Library))
        PollRemoteProcessScan(now);
    if (now >= nextStaleRetry && !g_offset_refresh_pending && !OmniGhost::OffsetAuto::g_busy.load()) {
        nextStaleRetry = now + 600.0;
        for (const GameRuntime& runtime : g_games) {
            if (!runtime.definition) continue;
            if (runtime.definition->coming_soon) continue;
            const ActiveGame active = ToActiveGame(runtime.definition->launch_id);
            if (OmniGhost::OffsetAuto::BlocksLaunch(active) &&
                OmniGhost::OffsetAuto::SupportsAutomaticRefresh(active)) {
                RequestOffsetRefresh(runtime.definition->launch_id, true);
                break;
            }
        }
    }
    if (now < nextRefresh)
        return;
    nextRefresh = now + 0.75;

    for (GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        if (runtime.state == CardState::Launching)
            continue;
        runtime.state = ResolveRuntimeState(runtime);
    }
}

void ToastOpenUpdates() {
    ChangeNavigation(static_cast<int>(NavPage::Library));
}

void ToastOpenLogs() {
    const std::filesystem::path logs = OmniGhost::Paths::LocalData() / L"logs";
    ShellExecuteW(nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void PushToast(const char* text, ImU32 color = 0,
               ToastAction action = ToastAction::None,
               const char* action_label = nullptr) {
    const ImU32 resolved = color ? color : C_GOLD();
    CyberWidgets::ToastType type = CyberWidgets::ToastType::Info;
    if (resolved == C_RED()) type = CyberWidgets::ToastType::Error;
    else if (resolved == C_GREEN()) type = CyberWidgets::ToastType::Success;
    else if (resolved == CyberTheme::U32(CyberTheme::Colors.Warning)) type = CyberWidgets::ToastType::Warning;

    if (action == ToastAction::OpenUpdates) {
        CyberWidgets::NotifyAction(text, type,
            action_label && *action_label ? action_label : Loc::Tr("launcher.updates"), &ToastOpenUpdates);
    } else if (action == ToastAction::OpenLogs) {
        CyberWidgets::NotifyAction(text, type,
            action_label && *action_label ? action_label : "Abrir logs", &ToastOpenLogs);
    } else {
        CyberWidgets::Notify(text, type);
    }
}


ActiveGame ToActiveGame(GameId id) {
    switch (id) {
    case GameId::CS2: return ActiveGame::CS2;
    case GameId::Warzone: return ActiveGame::Warzone;
    case GameId::Valorant: return ActiveGame::Valorant;
    case GameId::Fortnite: return ActiveGame::Fortnite;
    case GameId::Apex: return ActiveGame::Apex;
    case GameId::FiveM:
    default: return ActiveGame::FiveM;
    }
}

ImU32 UpdateStatusColor(const OmniGhost::Update::Snapshot& snapshot) {
    using OmniGhost::Update::Status;
    switch (snapshot.status) {
    case Status::Error: return C_RED();
    case Status::Available:
    case Status::Downloading:
    case Status::Ready:
    case Status::Installing: return C_GOLD();
    case Status::Checking: return C_MUTED();
    default: return C_GREEN();
    }
}

std::string MaskEmail(std::string email) {
    if (email.empty()) return Loc::Tr("launcher.local_account");
    const std::size_t at = email.find('@');
    if (at == std::string::npos || at < 2)
        return email;
    const std::size_t visible = (std::min<std::size_t>)(2, at);
    return email.substr(0, visible) + "***" + email.substr(at);
}

std::string FormatLastUsed(std::uint64_t unixSeconds) {
    return UiFormat::DateTimeLocal(static_cast<std::time_t>(unixSeconds));
}

std::string FormatActiveDuration(std::uint64_t seconds) {
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto rest = seconds % 60;
    char text[64]{};
    if (hours) std::snprintf(text, sizeof(text), "%lluh %02llum",
        static_cast<unsigned long long>(hours), static_cast<unsigned long long>(minutes));
    else if (minutes) std::snprintf(text, sizeof(text), "%llum %02llus",
        static_cast<unsigned long long>(minutes), static_cast<unsigned long long>(rest));
    else std::snprintf(text, sizeof(text), "%llus", static_cast<unsigned long long>(rest));
    return text;
}

const char* CardStateLabel(CardState state) {
    switch (state) {
    case CardState::Running: return Loc::Tr("launcher.status.running");
    case CardState::NotInstalled: return Loc::Tr("launcher.status.not_installed");
    case CardState::NeedsOffsets: return Loc::Tr("launcher.status.needs_offsets");
    case CardState::DeviceMissing: return Loc::Tr("launcher.status.device_missing");
    case CardState::GameNotFound: return Loc::Tr("launcher.status.game_not_found");
    case CardState::LicenseRequired: return Loc::Tr("launcher.status.license_required");
    case CardState::UpdateRequired: return Loc::Tr("launcher.status.update_required");
    case CardState::Beta: return Loc::Tr("launcher.status.beta");
    case CardState::ComingSoon: return Loc::Tr("launcher.status.coming_soon");
    case CardState::LaunchFailed:
    case CardState::Error: return Loc::Tr("launcher.status.launch_failed");
    case CardState::Launching: return Loc::Tr("launcher.starting");
    default: return Loc::Tr("launcher.status.ready");
    }
}

ImU32 CardStateColor(CardState state) {
    switch (state) {
    case CardState::Ready:
    case CardState::Running: return C_GREEN();
    case CardState::Beta:
    case CardState::NeedsOffsets:
    case CardState::UpdateRequired:
    case CardState::LicenseRequired: return C_GOLD();
    case CardState::DeviceMissing:
    case CardState::GameNotFound:
    case CardState::LaunchFailed:
    case CardState::Error: return C_RED();
    default: return C_MUTED();
    }
}

const char* CardStateDescription(CardState state) {
    switch (state) {
    case CardState::NotInstalled: return Loc::Tr("launcher.desc.not_installed");
    case CardState::NeedsOffsets: return Loc::Tr("launcher.desc.needs_offsets");
    case CardState::DeviceMissing: return Loc::Tr("launcher.desc.device_missing");
    case CardState::GameNotFound: return Loc::Tr("launcher.desc.game_not_found");
    case CardState::LicenseRequired: return Loc::Tr("launcher.desc.license_required");
    case CardState::UpdateRequired: return Loc::Tr("launcher.desc.update_required");
    case CardState::ComingSoon: return Loc::Tr("launcher.desc.coming_soon");
    case CardState::LaunchFailed:
    case CardState::Error: return Loc::Tr("launcher.desc.launch_failed");
    case CardState::Running: return Loc::Tr("launcher.desc.running");
    case CardState::Beta: return Loc::Tr("launcher.desc.beta");
    default: return Loc::Tr("launcher.desc.ready");
    }
}

const char* SessionResultDisplay(SessionResult result) {
    switch (result) {
    case SessionResult::Completed: return Loc::Tr("launcher.result.completed");
    case SessionResult::LaunchFailed: return Loc::Tr("launcher.result.launch_failed");
    case SessionResult::OffsetsFailed: return Loc::Tr("launcher.result.offsets_failed");
    case SessionResult::DeviceMissing: return Loc::Tr("launcher.result.device_missing");
    case SessionResult::GameNotFound: return Loc::Tr("launcher.result.game_not_found");
    case SessionResult::ProcessEnded: return Loc::Tr("launcher.result.process_ended");
    default: return Loc::Tr("launcher.result.none");
    }
}

bool IsReadyState(CardState state) {
    // Allow launch from any non-terminal gate except ComingSoon / Launching.
    // License/Update are still checked inside ActivateGame.
    switch (state) {
    case CardState::Ready:
    case CardState::Beta:
    case CardState::NotInstalled:
    case CardState::DeviceMissing:
    case CardState::GameNotFound:
    case CardState::LaunchFailed:
    case CardState::NeedsOffsets:
    case CardState::Error:
        return true;
    default:
        return false;
    }
}

bool CanRetryState(CardState state) {
    return IsReadyState(state) || state == CardState::LaunchFailed ||
        state == CardState::DeviceMissing || state == CardState::GameNotFound;
}

void RequestOffsetRefresh(GameId id, bool silent = false) {
    if (g_offset_refresh_pending) {
        PushToast(Loc::Tr("launcher.toast.offsets_busy"), C_MUTED(), ToastAction::None, nullptr);
        return;
    }
    if (id == GameId::None || id == GameId::Fortnite ||
        !OmniGhost::OffsetAuto::SupportsAutomaticRefresh(ToActiveGame(id))) {
        PushToast(Loc::Tr("launcher.toast.offsets_unavailable"), C_MUTED(), ToastAction::None, nullptr);
        return;
    }
    g_offset_refresh_game = id;
    g_offset_refresh_pending = true;
    if (!silent)
        PushToast(Loc::Tr("launcher.toast.offsets_working"), C_GOLD(), ToastAction::None, nullptr);
    const ActiveGame game = ToActiveGame(id);
    g_offset_refresh = std::async(std::launch::async, [game] {
        return OmniGhost::OffsetAuto::EnsureOffsets(game, true); // force fetch from cheatoffsets.com
    });
}
