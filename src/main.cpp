#include <Windows.h>
#include <TlHelp32.h>
#include <Shellapi.h>

#include "globals.h"
#include "app_version.h"
#include "window/window.hpp"
#include "window/theme.h"
#include "window/onboarding.h"
#include "config/app_settings.h"
#include "config/config_manager.h"
#include "launcher/game_select.h"
#include "launcher/changelog_service.h"
#include "launcher/steam_game_update_check.h"
#include "launcher/epic_game_update_check.h"
#include "launcher/auth_page.h"
#include "launcher/application_transitions.h"
#include "launcher/game_launch_service.h"
#include "launcher/game_adapter_registry.h"
#include "launcher/remote_usage_service.h"
#include "licensing/license_service.h"
#include "updater/update_service.h"
#include "updater/updater_mode.h"
#include "third_party/DMALibrary/Memory/Memory.h"
#include "src/games/Fivem/game/game.h"
#include "src/games/Fivem/aimbot/aim_type.h"
#include "src/games/Fivem/aimbot/aimbot.h"
#include "src/games/Fivem/friends/friends.h"
#include "src/games/Cs2/cs2_game.h"
#include "src/games/Cs2/aimbot/cs2_aim.h"
#include "src/games/Warzone/warzone_game.h"
#include "src/games/Fortnite/fortnite_game.h"
#include "platform/offset_auto.h"
#include "src/games/Fivem/game/offsets.h"
#include "platform/app_paths.h"
#include "platform/runtime_bootstrap.h"
#include "platform/hardware_manager.h"
#include "platform/startup_state.h"
#include "platform/crash_handler.h"
#include "platform/thread_utils.h"
#include "platform/unique_handle.h"
#include "platform/session_log.h"
#include "platform/support_error.h"
#include "platform/shutdown_coordinator.h"
#include "platform/text_encoding.h"
#include "platform/window_instance_guard.h"
#include "security/security_manager.h"
#include "window/performance_mode.h"
#include "window/widgets.h"
#include "window/ui_feedback.h"
#include "window/hardware_monitor.h"
#include "gameplay/dma_telemetry_log.h"
#include "third_party/ImGui/imgui.h"
#include "makcu/makcu_wrapper.h"
#include "gameplay/process_monitor.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <future>
#include <iostream>
#include <iterator>
#include <string>
#include <thread>

#if defined(OMNIGHOST_PUBLISH_BUILD) && (!defined(OMNIGHOST_KEYAUTH_ENABLED) || defined(OMNIGHOST_SKIP_KEYAUTH))
#error Publish builds must retain a configured entitlement provider.
#endif

namespace {

struct LaunchIssue {
    OmniGhost::Support::ErrorCode code;
    const char* title;
    const char* explanation;
    const char* action;
    Launcher::SessionResult result;
};

LaunchIssue DiagnoseLaunchFailure(
    const OmniGhost::GameLaunch::ProcessProbeResult& probe,
    const Memory::DiagnosticsSnapshot& dma,
    ActiveGame game,
    OmniGhost::Launcher::AdapterErrorCode adapterError) {
    using Code = OmniGhost::Support::ErrorCode;
    using Result = Launcher::SessionResult;
    using Probe = OmniGhost::GameLaunch::ProcessProbeState;
    if (!dma.dependencyIntegrityOk)
        return {Code::RuntimeDependenciesInvalid, "Componentes DMA inválidos",
            "Os componentes de leitura não passaram na verificação de integridade.",
            "Reinstala ou repara os componentes oficiais e tenta novamente.", Result::LaunchFailed};
    if (!dma.deviceOpen)
        return dma.deviceDetected
            ? LaunchIssue{Code::DmaOpenFailed, "DMA detetado, mas indisponível",
                "O dispositivo aparece no sistema, mas não foi possível abrir uma sessão de leitura.",
                "Confirma a ligação, reinicia o dispositivo e verifica a sua configuração.", Result::DeviceMissing}
            : LaunchIssue{Code::DmaUnavailable, "DMA não detetado",
                "Não foi encontrado um dispositivo DMA pronto para iniciar a sessão.",
                "Confirma a alimentação, a ligação e os componentes do dispositivo.", Result::DeviceMissing};
    using Adapter = OmniGhost::Launcher::AdapterErrorCode;
    if (adapterError == Adapter::UnsupportedGame)
        return {Code::GameAdapterUnavailable, "Jogo indisponível",
            "Esta versão do OmniGhost não inclui um adapter funcional para o jogo selecionado.",
            "Verifica se há uma atualização da aplicação.", Result::LaunchFailed};
    if (adapterError == Adapter::Cancelled)
        return {Code::LaunchCancelled, "Arranque interrompido",
            "A preparação do menu foi cancelada antes de terminar.",
            "Volta ao launcher e tenta novamente quando estiveres pronto.", Result::LaunchFailed};
    if (adapterError == Adapter::DeviceOpenFailed)
        return {Code::DmaOpenFailed, "Sessão DMA não iniciada",
            "O dispositivo foi detetado, mas a sessão de leitura necessária não ficou operacional.",
            "Confirma a ligação, o firmware e os componentes DMA; depois volta a tentar.", Result::DeviceMissing};
    if (adapterError == Adapter::ProcessNotFound)
        return probe.state == Probe::NotFound
            ? LaunchIssue{Code::GameNotFound, "Jogo não encontrado",
                "A lista de processos foi consultada e o jogo selecionado não apareceu.",
                "Confirma que está aberto no PC ligado ao DMA.", Result::GameNotFound}
            : LaunchIssue{Code::ProcessInventoryUnavailable, "Processo não confirmado",
                "O adapter não conseguiu confirmar o processo. Isto não prova que o jogo esteja fechado.",
                "Confirma a comunicação DMA e tenta novamente.", Result::LaunchFailed};
    if (adapterError == Adapter::ProcessBindFailed)
        return {Code::MemoryReadUnavailable, "Jogo encontrado, leitura indisponível",
            "O processo foi encontrado, mas o DMA não conseguiu validar a memória necessária para o menu.",
            "Confirma o firmware e o acesso à memória; depois volta a tentar.", Result::LaunchFailed};
    if (adapterError == Adapter::ModuleNotFound)
        return {Code::GameModuleUnavailable, "Módulo do jogo indisponível",
            "O processo foi ligado, mas um módulo necessário ainda não apareceu ou não pôde ser lido.",
            "Espera pelo menu principal do jogo e tenta novamente. Se persistir, verifica a leitura DMA.", Result::LaunchFailed};
    if (adapterError == Adapter::OffsetsMissing || adapterError == Adapter::UnsupportedBuild)
        return {Code::OffsetsUnsupported, "Versão ou offsets não suportados",
            "Os offsets necessários faltam ou a versão detetada do jogo não tem suporte validado.",
            "Procura uma atualização dos offsets antes de tentar novamente.", Result::OffsetsFailed};
    if (adapterError == Adapter::GameDataUnverified)
        return {Code::GameDataUnverified, "Dados do jogo não confirmados",
            "Os dados lidos não passaram na validação. Isto pode ser leitura DMA ou offsets; a causa ainda não é certa.",
            "Confirma o acesso à memória e verifica se há offsets atualizados.", Result::LaunchFailed};
    if (adapterError == Adapter::UnexpectedException)
        return {Code::GameInitializationFailed, "Falha inesperada no arranque",
            "O adapter encontrou uma exceção antes de abrir o menu.",
            "Consulta os logs para o detalhe técnico e volta a tentar.", Result::LaunchFailed};
    if (dma.processInitialized) {
        const auto compatibility = OmniGhost::OffsetAuto::Compatibility(game);
        if (compatibility.state == OmniGhost::OffsetAuto::CompatibilityState::Unsupported ||
            !OmniGhost::OffsetAuto::HasCriticalOffsets(game))
            return {Code::OffsetsUnsupported, "Versão do jogo não validada",
                "O processo foi ligado, mas os dados necessários não passaram na validação desta versão.",
                "Verifica se há offsets atualizados nos Diagnósticos antes de tentar novamente.", Result::OffsetsFailed};
        if (adapterError == OmniGhost::Launcher::AdapterErrorCode::InitializationFailed)
            return {Code::GameInitializationFailed, "Menu não inicializado",
                "A leitura do processo foi iniciada, mas um componente do menu falhou ao arrancar.",
                "Volta a tentar e consulta os logs se o problema continuar.", Result::LaunchFailed};
        return {Code::GameDataUnverified, "Dados do jogo não confirmados",
            "O processo foi ligado, mas a verificação dos dados do jogo falhou. Isto não confirma offsets desatualizados.",
            "Confirma a leitura DMA e o estado do jogo; se persistir, consulta os logs.", Result::LaunchFailed};
    }
    if (probe.state == Probe::NotFound)
        return {Code::GameNotFound, "Jogo não encontrado",
            "A lista de processos do PC do jogo foi consultada, mas o executável selecionado não apareceu.",
            "Confirma que escolheste o jogo certo e que está aberto no PC ligado ao DMA.", Result::GameNotFound};
    if (probe.state == Probe::Found)
        return {Code::MemoryReadUnavailable, "Jogo encontrado, leitura indisponível",
            "O executável foi detetado no PC do jogo, mas o DMA não conseguiu validar a memória do processo.",
            "Confirma que o firmware e o acesso à memória estão ativos. Depois volta a tentar.", Result::LaunchFailed};
    return {Code::ProcessInventoryUnavailable, "Deteção do jogo inconclusiva",
        "O DMA abriu, mas não foi possível confirmar a lista de processos. Não há prova de que o jogo esteja fechado.",
        "Confirma a leitura DMA e tenta novamente. Se persistir, consulta os logs.", Result::LaunchFailed};
}

void WritePostAuthStage(const char* stage) noexcept {
    wchar_t localAppData[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return;

    std::wstring directory(localAppData);
    directory += L"\\OmniGhost";
    CreateDirectoryW(directory.c_str(), nullptr);
    const std::wstring path = directory + L"\\post-auth-stage.txt";
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, stage, static_cast<DWORD>(std::strlen(stage)), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}










bool IsLocalFiveMProcessRunning() noexcept {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const wchar_t* name = entry.szExeFile;
            const bool buildProcess = wcsstr(name, L"FiveM_b") == name &&
                wcsstr(name, L"_GTAProcess.exe") != nullptr;
            if (_wcsicmp(name, L"GTAProcess.exe") == 0 ||
                _wcsicmp(name, L"FiveM_GTAProcess.exe") == 0 || buildProcess) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}





int RunDmaIntegrationTest(int argc, wchar_t** argv) {


    wchar_t ciEnv[256]{};
    if (!GetEnvironmentVariableW(L"OMNIGHOST_CI_INTEGRATION_TEST", ciEnv, 256) || ciEnv[0] == L'\0') {
        std::clog << "[SECURITY] RunDmaIntegrationTest blocked: OMNIGHOST_CI_INTEGRATION_TEST not set\n";
        return 100;
    }


    std::wstring providedToken;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::wstring key = argv[i];
        if (key == L"--token") {
            providedToken = argv[i + 1];
            break;
        }
    }
    if (!providedToken.empty()) {









        std::string tokenStr;
        tokenStr.reserve(providedToken.size());
        for (wchar_t ch : providedToken) {
            if (ch > 0x7F) { std::clog << "[SECURITY] Invalid token char\n"; return 101; }
            tokenStr.push_back(static_cast<char>(ch));
        }



        if (tokenStr.size() != 64 || tokenStr.substr(0, 8) != "OMNITEST") {
            std::clog << "[SECURITY] RunDmaIntegrationTest blocked: invalid token format or HMAC\n";
            return 101;
        }

    }

    OmniGhost::SessionLog::Initialize();
    if (!OmniGhost::Security::SecurityManager::Instance().VerifyHostExecutablePresent()) return 10;
    std::wstring executable;
    unsigned long expectedPid = 0;
    unsigned long long expectedHwnd = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::wstring key = argv[i];
        const std::wstring value = argv[i + 1];
        if (key == L"--target-exe") executable = value;
        else if (key == L"--target-pid") expectedPid = std::wcstoul(value.c_str(), nullptr, 10);
        else if (key == L"--target-hwnd") expectedHwnd = std::wcstoull(value.c_str(), nullptr, 10);
        else if (key.compare(L"--token") == 0) continue;
        else return 2;
    }

    const auto marker = executable.find(L"FiveM_b");
    const auto suffix = executable.find(L"_GTAProcess.exe");
    if (marker != 0 || suffix == std::wstring::npos || suffix <= 7 ||
        expectedPid == 0 || expectedHwnd == 0) return 2;
    const int build = std::wcstol(executable.c_str() + 7, nullptr, 10);
    if (build <= 0) return 2;
    DWORD windowPid = 0;
    GetWindowThreadProcessId(reinterpret_cast<HWND>(static_cast<uintptr_t>(expectedHwnd)), &windowPid);
    if (windowPid != expectedPid) return 3;
    if (FiveM::LoadOffsetsFromJson(nullptr) <= 0) return 4;
    const auto* offsets = FiveM::GetOffsetsForBuild(build);
    if (!offsets || !offsets->world_offset || !offsets->viewport_offset ||
        !offsets->ped_visibility_offset) return 5;
    std::string processName;
    processName.reserve(executable.size());
    for (const wchar_t character : executable) {
        if (character > 0x7f) return 2;
        processName.push_back(static_cast<char>(character));
    }

    static const std::vector<std::string> kAllowedProcesses = {"FiveM_b", "GTAProcess.exe"};
    bool allowed = false;
    for (const auto& p : kAllowedProcesses) {
        if (processName.find(p) != std::string::npos) { allowed = true; break; }
    }
    if (!allowed) return 102;
    if (OmniGhost::RuntimeBootstrap::Prepare() == OmniGhost::RuntimeBootstrap::Result::Failed) return 6;
    if (!OmniGhost::Security::SecurityManager::Instance().VerifyProtectedFiles()) return 11;
    if (!mem.Init(processName, true, false)) return 6;
    if (mem.GetPidFromName(processName) != expectedPid) return 7;
    const uintptr_t base = mem.GetBaseDaddy(processName);
    uintptr_t world = 0, viewport = 0, localPed = 0;
    if (!base || !mem.Read(base + offsets->world_offset, &world, sizeof(world)) ||
        !mem.Read(base + offsets->viewport_offset, &viewport, sizeof(viewport)) ||
        world < 0x10000 || viewport < 0x10000 ||
        !mem.Read(world + 0x8, &localPed, sizeof(localPed)) || localPed < 0x10000) return 8;
    unsigned char visibleFlag = 0;
    if (!mem.Read(localPed + offsets->ped_visibility_offset, &visibleFlag, sizeof(visibleFlag))) return 9;
    std::clog << "[DMA integration] build=" << build << " pid=" << expectedPid
              << " world/viewport/localPed/visibilityFlag read OK, flag="
              << static_cast<unsigned>(visibleFlag) << '\n';
    return 0;
}

int RunOmniGhost(int argc, wchar_t** argv) {
    if (argc > 1 && argv && _wcsicmp(argv[1], L"--integration-test") == 0)
        return RunDmaIntegrationTest(argc, argv);
    OmniGhost::Startup::StateMachine startupState;
    (void)startupState.Transition(OmniGhost::Startup::State::Boot,
                            OmniGhost::Startup::State::CheckingInstance);
    bool updaterInvocation = false;
    for (int i = 1; argv && i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--apply-update") == 0 ||
            _wcsicmp(argv[i], L"--updater") == 0) {
            updaterInvocation = true;
            break;
        }
    }






    OmniGhost::Platform::UniqueHandle startupOwner;
    if (!updaterInvocation) {
        SetLastError(ERROR_SUCCESS);
        HANDLE handle = CreateMutexW(nullptr, FALSE,
            L"Local\\OmniGhostStartupOwner-6A670FC7");
        const DWORD error = GetLastError();
        startupOwner.reset(handle);
        if (!startupOwner || error == ERROR_ALREADY_EXISTS) {
            OmniGhost::Platform::ActivateExistingOmniGhostWindow();
            return error == ERROR_ALREADY_EXISTS ? 3 : 2;
        }

        std::wstring legacyError;
        if (!OmniGhost::RuntimeBootstrap::RetireLegacyPrivateInstall(legacyError)) {
            MessageBoxW(nullptr, legacyError.c_str(), L"OmniGhost — limpeza da instalação antiga",
                        MB_OK | MB_ICONERROR);
            return 2;
        }
        OmniGhost::Paths::MigrateLegacyUserData();


        if (OmniGhost::Platform::ActivateExistingOmniGhostWindow())
            return 3;
    }

    OmniGhost::SessionLog::SetStartReason(
        (argc > 1 && argv && argv[1]) ? OmniGhost::Platform::WideToUtf8(argv[1]) : "normal");
    OmniGhost::SessionLog::Initialize();
    if (!OmniGhost::Security::SecurityManager::Instance().VerifyHostExecutablePresent()) {
        MessageBoxW(nullptr, L"Não foi possível confirmar o executável da aplicação (SEC-004).",
                    L"OmniGhost", MB_OK | MB_ICONERROR);
        OmniGhost::SessionLog::Shutdown();
        return 5;
    }
    const auto bootstrap = OmniGhost::RuntimeBootstrap::Prepare();
    if (bootstrap == OmniGhost::RuntimeBootstrap::Result::Failed) {
        OmniGhost::SessionLog::Shutdown();
        return 1;
    }
    (void)startupState.Transition(OmniGhost::Startup::State::CheckingInstance,
                            OmniGhost::Startup::State::InitializingRuntime);

    OmniGhost::Platform::SetCurrentThreadName(L"OmniGhost.Main");



    DisableProcessWindowsGhosting();
    OmniGhost::CrashHandler::Install();
    std::clog << "[RUNTIME] mode=SINGLE_PROCESS_PORTABLE\n"
                 "[RUNTIME] self_install=DISABLED\n"
                 "[RUNTIME] private_executable=DISABLED\n"
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
                 "[RUNTIME] build=PRIVATE_DEVELOPMENT_BUILD\n"
                 "[RUNTIME] distribution=NOT_FOR_DISTRIBUTION\n"
                 "[RUNTIME] static_vmm=YES source_integration\n"
                 "[RUNTIME] static_leechcore=YES source_integration\n"
#else
                 "[RUNTIME] static_vmm=NO import_library_only\n"
                 "[RUNTIME] static_leechcore=NO import_library_only\n"
#endif
                 "[RUNTIME] vendor_d3xx=EXTERNAL_FTD3XXWU_ON_FPGA_OPEN\n"
#if defined(OMNIGHOST_DISABLE_VMM_INFODB)
                 "[RUNTIME] info_db=DISABLED_BY_BUILD (-disable-infodb)\n"
#else
                 "[RUNTIME] info_db=EXTERNAL_REQUIRED_BY_MEMPROCFS\n"
#endif
#if defined(OMNIGHOST_DISABLE_VMM_SYMBOLS)
                 "[RUNTIME] pdbcrust=DISABLED_BY_BUILD (-disable-symbols)\n"
#else
                 "[RUNTIME] pdbcrust=OPTIONAL_EAT_FALLBACK\n"
#endif
                 "[RUNTIME] cloudflared=EXTERNAL_PROCESS_OPTIONAL\n";
    const auto previousRun = OmniGhost::CrashHandler::PreviousRun();
    std::clog << "[SESSION] previous_run="
              << OmniGhost::CrashHandler::PreviousRunStateName(previousRun)
              << "\n";
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup entered",
        {
            {"previous_run", OmniGhost::CrashHandler::PreviousRunStateName(previousRun), false},
            {"session_id", OmniGhost::SessionLog::CurrentSessionId(), false}
        });
    struct SessionShutdownGuard {
        ~SessionShutdownGuard() {
            OmniGhost::RemoteUsage::Shutdown();
            OmniGhost::Licensing::ShutdownSessionRevalidation();
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Info,
                OmniGhost::SessionLog::Subsystem::Core,
                "clean shutdown requested");
            OmniGhost::CrashHandler::MarkCleanShutdown();
            OmniGhost::SessionLog::Shutdown();
        }
    } session_shutdown_guard;
    if (const auto updater_result = OmniGhost::Update::RunUpdaterModeIfRequested(argc, argv))
        return *updater_result;

    if (!OmniGhost::Security::SecurityManager::Instance().VerifyStartup()) {
        MessageBoxW(nullptr, L"A instalação não passou a verificação de integridade. Reinstala a versão oficial (SEC-003/004).",
                    L"OmniGhost", MB_OK | MB_ICONERROR);
        return 5;
    }






    if (OmniGhost::Platform::ActivateExistingOmniGhostWindow()) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Platform,
            "existing OmniGhost window from another process reused");
        return 3;
    }







    constexpr wchar_t kSingleInstanceName[] = L"Local\\OmniGhostApp-6A670FC7";
    HANDLE singleInstanceHandle = CreateMutexW(nullptr, FALSE, kSingleInstanceName);
    const DWORD singleInstanceError = GetLastError();
    OmniGhost::Platform::UniqueHandle singleInstance(singleInstanceHandle);
    if (!singleInstance) {
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Platform,
            "single-instance mutex creation failed",
            {
                {"win32", std::to_string(singleInstanceError), false},
                {"executable", OmniGhost::Platform::WideToUtf8(
                    OmniGhost::Paths::Executable().wstring()), false}
            });
        std::cerr << "[ERRO] Nao foi possivel criar o controlo de instancia unica. win32="
                  << singleInstanceError << "\n";
        return 2;
    }
    if (singleInstanceError == ERROR_ALREADY_EXISTS) {
        const std::wstring installDirectory = OmniGhost::Paths::InstallDirectory().wstring();
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Warning,
            OmniGhost::SessionLog::Subsystem::Platform,
            "second application instance rejected",
            {
                {"install_directory", OmniGhost::Platform::WideToUtf8(installDirectory), false}
            });
        std::cerr << "[ERRO] O OmniGhost ja esta em execucao. A janela existente sera reutilizada.\n";



        OmniGhost::Platform::ActivateExistingOmniGhostWindow();
        return 3;
    }
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Platform,
        "single-instance guard acquired",
        {{"executable", OmniGhost::Platform::WideToUtf8(
            OmniGhost::Paths::Executable().wstring()), false}});






    OmniGhost::Update::ConfirmUpdaterStartupFromCommandLine(argc, argv);
    OmniGhost::Update::CaptureUpdaterStartupNoticeFromCommandLine(argc, argv);



    app_settings::Initialize();
    config_manager::Initialize();

    Overlay application;


    application.SetCompactMode(true);
#if defined(OMNIGHOST_PRIVATE_STATIC_VMM)
    application.SetupOverlay("OmniGhost — PRIVATE DEVELOPMENT BUILD — NOT FOR DISTRIBUTION");
#else
    application.SetupOverlay("OmniGhostOverlay");
#endif
    if (!application.shouldRun)
        return 1;
    OmniGhost::Platform::LogWindowAudit("after-create", application.overlay);

    OmniGhost::Platform::ShutdownCoordinator shutdownCoordinator;
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Ui, 100,
        [&application] { application.Shutdown(); });


    OmniGhost::CrashHandler::MarkStartupComplete();
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup UI ready");

    LauncherAuth::Reset();
    (void)startupState.Transition(OmniGhost::Startup::State::InitializingRuntime,
                            OmniGhost::Startup::State::Authenticating);
    bool authenticated = false;
while (application.shouldRun && !authenticated) {
        application.StartRender();
        if (!application.shouldRun)
            break;
        authenticated = LauncherAuth::Draw();
        if (authenticated)
            WritePostAuthStage("auth-draw-returned-success");
        if (LauncherAuth::ConsumeCloseRequest())
            application.RequestClose();
        application.EndRender();
        if (authenticated)
            WritePostAuthStage("auth-final-frame-presented");



        application.WaitForEvents(std::chrono::milliseconds(1));
    }
    if (!application.shouldRun || !authenticated) {
        (void)shutdownCoordinator.ShutdownAll("authentication-ended");
        return 0;
    }

    WritePostAuthStage("authenticated");







    OmniGhost::Licensing::StartSessionRevalidation();
    WritePostAuthStage("session-revalidation-armed");
    friends::Initialize();
    WritePostAuthStage("friends-initialized");
    aim_type::Initialize();
    WritePostAuthStage("input-initialized");
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Input, 500,
        [] { aim_type::Shutdown(); });
    aimbot::Initialize();
    WritePostAuthStage("aimbot-initialized");





    (void)startupState.Transition(OmniGhost::Startup::State::Authenticating,
                            OmniGhost::Startup::State::CheckingHardware);
    OmniGhost::Hardware::Manager hardwareManager;
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Hardware, 600,
        [&hardwareManager] { hardwareManager.Shutdown(); });
    hardwareManager.BeginStartupProbe();
    WritePostAuthStage("hardware-probe-started");
    (void)startupState.Transition(OmniGhost::Startup::State::CheckingHardware,
                            OmniGhost::Startup::State::Ready);
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "hardware discovery started in launcher background");
    std::clog << "[UI][Transition] authentication -> launcher begin; hardware probe remains background-only\n";
    const HWND startupHwnd = application.overlay;
    OmniGhost::Platform::LogWindowAudit("before-launcher-transition", startupHwnd);
    application.SetCompactMode(false);
    WritePostAuthStage("launcher-resize-requested");
    if (application.overlay != startupHwnd) {
        std::cerr << "[WINDOW][Invariant] FAIL: startup HWND changed during compact->launcher transition.\n";
        application.RequestClose();
        (void)shutdownCoordinator.ShutdownAll("window-invariant-failed");
        return 4;
    }
    OmniGhost::Platform::LogWindowAudit("after-launcher-transition", application.overlay);
    const OmniGhost::Platform::WindowAudit transitionAudit =
        OmniGhost::Platform::AuditOmniGhostWindows();
    if (transitionAudit.foreignProcessWindows != 0) {
        std::cerr << "[WINDOW][Invariant] FAIL: another OmniGhost process owns a top-level window during launcher transition. "
                  << "current=" << transitionAudit.currentProcessWindows
                  << " foreign=" << transitionAudit.foreignProcessWindows << "\n";
        if (transitionAudit.firstForeign) {
            if (IsIconic(transitionAudit.firstForeign))
                ShowWindow(transitionAudit.firstForeign, SW_RESTORE);
            SetForegroundWindow(transitionAudit.firstForeign);
        }
        application.RequestClose();
        (void)shutdownCoordinator.ShutdownAll("duplicate-window-detected");
        return 4;
    }




    if (transitionAudit.currentProcessWindows != 1) {
        std::clog << "[WINDOW][Invariant] transient launcher audit mismatch; continuing with valid HWND. current="
                  << transitionAudit.currentProcessWindows << "\n";
    }
    ShowWindow(application.overlay, SW_SHOWNORMAL);
    SetWindowPos(application.overlay, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    SetForegroundWindow(application.overlay);
    WritePostAuthStage("launcher-window-shown");
    std::clog << "[UI][Transition] SetCompactMode(false) returned; same HWND retained and DXGI resize is deferred to render loop\n";
    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "launcher display transition queued safely");

    auto& update_service = OmniGhost::Update::UpdateService::Instance();
    OmniGhost::Update::Configuration update_configuration{};
#if defined(OMNIGHOST_PUBLISH_BUILD)
    update_configuration.channel = OmniGhost::Update::Channel::Stable;
#else
    update_configuration.channel = app_settings::config.update_channel == app_settings::UpdateChannel::Beta
        ? OmniGhost::Update::Channel::Beta
        : OmniGhost::Update::Channel::Stable;
#endif
    update_configuration.automaticDownload = app_settings::config.update_auto_download;
    update_configuration.automaticInstallOnExit = app_settings::config.update_install_on_exit;
    update_service.Initialize(update_configuration);
    WritePostAuthStage("update-service-initialized");
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Updater, 800,
        [&update_service] { update_service.Shutdown(); });
    const auto startupUpdate = update_service.GetSnapshot();
    if (startupUpdate.recentlyUpdated && !startupUpdate.rollbackRestored) {
        const std::string version = startupUpdate.availableVersion.empty()
            ? std::string(OmniGhost::Version)
            : startupUpdate.availableVersion;
        const std::string subtitle = "Versão " + version;
        OmniGhost::UI::RenderTimedTransition(application, "✓ Atualização concluída",
            subtitle.c_str(), app_settings::MotionEnabled() ? 0.48f : 0.05f, true);
        CyberWidgets::NotifyUpdate(("OmniGhost atualizado para " + version).c_str(),
            CyberWidgets::ToastType::Success);
    }



    auto& changelog_service = Launcher::ChangelogService::Instance();
    changelog_service.Initialize();
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::Changelog, 700,
        [&changelog_service] { changelog_service.Shutdown(); });
    changelog_service.RefreshAsync(false);
    (void)shutdownCoordinator.Register(
        OmniGhost::Platform::ShutdownComponent::LibraryChecks, 690,
        [] {
            Launcher::SteamGameUpdateCheck::Shutdown();
            Launcher::EpicGameUpdateCheck::Shutdown();
        });
    WritePostAuthStage("launcher-ready");




    OmniGhost::SessionLog::Write(
        OmniGhost::SessionLog::Severity::Info,
        OmniGhost::SessionLog::Subsystem::Core,
        "startup complete");







    Launcher::EntryReason nextLauncherEntry = Launcher::EntryReason::Authenticated;
    for (;;) {
    application.RenderMenu = true;
    app_settings::menu_open = true;
    application.SetCaptureExclusion(false);
    Launcher::Reset(nextLauncherEntry);
    nextLauncherEntry = Launcher::EntryReason::Normal;
    Launcher::GameId selected = Launcher::GameId::None;
    bool logout_requested = false;
    while (application.shouldRun && selected == Launcher::GameId::None && !logout_requested) {
        hardwareManager.Poll();



        const auto hardware = hardwareManager.Snapshot();
        HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::DMA, true);
        HardwareMonitor::UpdateDeviceStatus(
            HardwareMonitor::DeviceType::DMA,
            hardware.dmaFinished && hardware.dmaAvailable,
            hardware.dmaFinished ? "PnP detected" : "Checking",
            hardware.dmaFinished ? "Not opened until game launch" : "");
        HardwareMonitor::SetDeviceEnabled(HardwareMonitor::DeviceType::Makcu, true);
        const auto makcuDiagnostics = makcu_wrapper::GetDiagnostics();
        HardwareMonitor::UpdateDeviceStatus(
            HardwareMonitor::DeviceType::Makcu,
            hardware.makcuFinished && hardware.makcuAvailable,
            makcuDiagnostics.port[0] ? makcuDiagnostics.port :
                (hardware.makcuFinished ? "Auto discovery" : "Searching automatically"),
            makcuDiagnostics.deviceInfo[0] ? makcuDiagnostics.deviceInfo :
                (hardware.makcuAvailable ? "Connected automatically" : "MAKCU auto-connect"),
            hardware.makcuFinished && !hardware.makcuAvailable ?
                "Not found; retrying automatically" : "");
        application.StartRender();
        if (!application.shouldRun)
            break;
        if (Launcher::Draw())
            selected = Launcher::Selected();
        logout_requested = Launcher::ConsumeLogoutRequest();
        application.EndRender();
        if (update_service.ShouldExitForUpdate())
            application.shouldRun = false;
    }



    if (application.shouldRun && logout_requested) {
        OmniGhost::UI::RenderTimedTransition(application, "A terminar sessão", "",
            app_settings::MotionEnabled() ? 0.25f : 0.0f);
        application.SetCompactMode(true);
        LauncherAuth::Reset();
        bool reauthenticated = false;
        while (application.shouldRun && !reauthenticated) {
            application.StartRender();
            if (!application.shouldRun)
                break;
            reauthenticated = LauncherAuth::Draw();
            if (LauncherAuth::ConsumeCloseRequest())
                application.RequestClose();
            application.EndRender();
            if (update_service.ShouldExitForUpdate())
                application.shouldRun = false;
        }
        if (!application.shouldRun)
            break;



        OmniGhost::Licensing::StartSessionRevalidation();
        application.SetCompactMode(false);
        nextLauncherEntry = Launcher::EntryReason::Authenticated;
        continue;
    }

    if (application.shouldRun && Onboarding::ShouldRunOnboarding() &&
        selected != Launcher::GameId::None) {
        nextLauncherEntry = Launcher::EntryReason::Normal;
        continue;
    }
    if (!application.shouldRun || selected == Launcher::GameId::None) {
        (void)shutdownCoordinator.ShutdownAll(
            update_service.ShouldExitForUpdate() ? "update-restart" : "launcher-exit");
        return 0;
    }




    const Launcher::GameDefinition* selectedDefinition = Launcher::FindGame(selected);
    if (!selectedDefinition ||
        !OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures() ||
        !OmniGhost::Licensing::HasGameAccess(selectedDefinition->id)) {
        std::clog << "[LICENSE] Game launch blocked: no entitlement for "
                  << (selectedDefinition ? selectedDefinition->id : "unknown") << ".\n";
        continue;
    }


    OmniGhost::Launcher::IGameAdapter* adapter =
        OmniGhost::Launcher::FindGameAdapter(selected);
    if (!adapter) {
        std::clog << "[LAUNCHER] No adapter found for game ID: "
                  << static_cast<int>(selected) << std::endl;
        (void)startupState.Transition(OmniGhost::Startup::State::StartingGame,
                                      OmniGhost::Startup::State::Ready);
        continue;
    }


    ActiveGame pending_game = ActiveGame::FiveM;
    switch (selected) {
        case Launcher::GameId::CS2: pending_game = ActiveGame::CS2; break;
        case Launcher::GameId::Warzone: pending_game = ActiveGame::Warzone; break;
        case Launcher::GameId::Fortnite: pending_game = ActiveGame::Fortnite; break;
        case Launcher::GameId::FiveM: pending_game = ActiveGame::FiveM; break;
        default: pending_game = ActiveGame::FiveM; break;
    }

    if (!startupState.Transition(OmniGhost::Startup::State::Ready,
                                 OmniGhost::Startup::State::StartingGame)) {
        startupState.Fail();
        std::cerr << "[STARTUP] Invalid Ready -> StartingGame transition; launch refused.\n";
        break;
    }


    if (pending_game != ActiveGame::Warzone) {
        bool need_refresh = false;

        if (pending_game == ActiveGame::CS2) {
            CS2::LoadOffsetsFromJson(nullptr);
            if (!OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game))
                need_refresh = true;
        } else if (!OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game)) {
            need_refresh = true;
        }

        if (need_refresh) {
            OmniGhost::OffsetAuto::g_status =
                "Offsets desatualizados ou em falta — a atualizar em fundo...";
            OmniGhost::OffsetAuto::g_busy = true;


            struct WorkerArg {
                ActiveGame game;
                OmniGhost::OffsetAuto::Result result;
            } warg{ pending_game, OmniGhost::OffsetAuto::Result::RefreshFailed };
            std::atomic<bool> workerFinished{false};
            std::jthread worker([&warg, &workerFinished](std::stop_token) {
                warg.result = OmniGhost::OffsetAuto::EnsureOffsets(warg.game, false);
                workerFinished.store(true, std::memory_order_release);
            });

            while (application.shouldRun && !workerFinished.load(std::memory_order_acquire)) {
                application.StartRender();

                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ds);
                ImGui::Begin("##offset_bootstrap", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(0, 0), ds, IM_COL32(8, 8, 10, 240));
                const char* title = "A carregar offsets";
                ImVec2 ts = ImGui::CalcTextSize(title);
                ImGui::SetCursorPos(ImVec2((ds.x - ts.x) * 0.5f, ds.y * 0.38f));
                ImGui::TextUnformatted(title);
                ImGui::Spacing();
                const std::string& st = OmniGhost::OffsetAuto::g_status;
                ImVec2 ss = ImGui::CalcTextSize(st.c_str());
                ImGui::SetCursorPosX((ds.x - ss.x) * 0.5f);
                ImGui::TextWrapped("%s", st.c_str());
                ImGui::SetCursorPosY(ds.y * 0.55f);
                ImGui::SetCursorPosX(ds.x * 0.5f - 80.f);
                ImGui::TextDisabled("Nao feches o jogo...");
                ImGui::End();
                application.EndRender();

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }


            if (worker.joinable()) worker.join();
            OmniGhost::OffsetAuto::g_busy = false;

            if (warg.result != OmniGhost::OffsetAuto::Result::Ok &&
                !OmniGhost::OffsetAuto::HasCriticalOffsets(pending_game)) {

                std::cout << "[Offsets] Refresh incomplete (non-blocking): "
                          << std::string(OmniGhost::OffsetAuto::g_status) << std::endl;
            }
        }
    }



    struct LaunchWork {
        OmniGhost::GameLaunch::ProcessProbeResult probe{};
        OmniGhost::Launcher::AdapterStartResult result{};
    } launchWork;
    std::atomic<int> launchPhase{0};
    std::atomic_bool launchFinished{false};
    mem.ClearCancel();
    std::jthread launchWorker([&](std::stop_token) {
        try {
            launchWork.probe = OmniGhost::GameLaunch::ProbeProcess(selected);
            OmniGhost::SessionLog::Write(
                OmniGhost::SessionLog::Severity::Info,
                OmniGhost::SessionLog::Subsystem::Adapter,
                "StartGameAdapter begin",
                {{"game", selectedDefinition ? selectedDefinition->id : "unknown"},
                 {"preflight_process_visible",
                  launchWork.probe.state == OmniGhost::GameLaunch::ProcessProbeState::Found ? "yes" : "no"}});
            launchPhase.store(1, std::memory_order_release);
            launchWork.result = OmniGhost::Launcher::StartGameAdapter(selected);
        } catch (const std::exception& ex) {
            launchWork.result = {false,
                {OmniGhost::Launcher::AdapterErrorCode::UnexpectedException, ex.what()}};
        } catch (...) {
            launchWork.result = {false,
                {OmniGhost::Launcher::AdapterErrorCode::UnexpectedException, "unknown startup exception"}};
        }
        launchFinished.store(true, std::memory_order_release);
    });
    while (application.shouldRun && !launchFinished.load(std::memory_order_acquire)) {
        application.StartRender();
        if (!application.shouldRun)
            break;
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ds);
        ImGui::Begin("##game_launch_progress", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(ImVec2(0, 0), ds,
            CyberTheme::U32(CyberTheme::Colors.Background));
        const float progressCardWidth = (std::min)(600.f, ds.x - 48.f);
        const ImVec2 cardMin((ds.x - progressCardWidth) * .5f, ds.y * .38f);
        const ImVec2 cardMax(cardMin.x + progressCardWidth, cardMin.y + 170.f);
        draw->AddRectFilled(cardMin, cardMax,
            CyberTheme::U32(CyberTheme::Colors.Card), 14.f);
        draw->AddRect(cardMin, cardMax,
            CyberTheme::U32(CyberTheme::Colors.Border), 14.f);
        CyberWidgets::DrawSpinner(draw, ImVec2(cardMin.x + 35.f, cardMin.y + 44.f), 12.f);
        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 63.f, cardMin.y + 27.f));
        ImGui::TextUnformatted("A preparar o menu");
        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 25.f, cardMin.y + 82.f));
        ImGui::PushTextWrapPos(cardMax.x - 25.f);
        ImGui::TextWrapped("%s", launchPhase.load(std::memory_order_acquire) == 0
            ? "A verificar a ligação DMA e a procurar o processo no PC do jogo..."
            : "Jogo encontrado ou pesquisa concluída. A validar memória, módulos e dados do jogo...");
        ImGui::PopTextWrapPos();
        ImGui::End();
        application.EndRender();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!application.shouldRun)
        mem.RequestCancel();
    if (launchWorker.joinable()) launchWorker.join();
    if (!application.shouldRun)
        break;
    const auto& processProbe = launchWork.probe;
    const auto& startResult = launchWork.result;
    if (!startResult.succeeded) {
        OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Error);

        const std::string_view errorMessage =
            OmniGhost::Launcher::AdapterErrorMessage(startResult.error.code);
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Adapter,
            "StartGameAdapter failed",
             {{"code", std::to_string(static_cast<int>(startResult.error.code))},
             {"message", std::string(errorMessage)},
             {"detail", std::string(startResult.error.detail)}});
        const auto dmaAfterAttach = mem.GetDiagnosticsSnapshot();
        const LaunchIssue issue = DiagnoseLaunchFailure(
            processProbe, dmaAfterAttach, pending_game, startResult.error.code);
        const std::string detail = std::string(OmniGhost::Support::Code(issue.code)) +
            " | " + issue.title + " | " + issue.explanation;
        OmniGhost::SessionLog::Write(
            OmniGhost::SessionLog::Severity::Error,
            OmniGhost::SessionLog::Subsystem::Adapter,
            "Launch failure classified",
            {{"support_code", std::string(OmniGhost::Support::Code(issue.code))},
             {"probe_state", std::to_string(static_cast<int>(processProbe.state))},
             {"device_open", dmaAfterAttach.deviceOpen ? "yes" : "no"},
             {"process_bound", dmaAfterAttach.processInitialized ? "yes" : "no"}});

        {




            const ULONGLONG errUntil = GetTickCount64() + 30000;
            bool dismissed = false;
            while (application.shouldRun && !dismissed && GetTickCount64() < errUntil) {
                application.StartRender();
                const ImVec2 ds = ImGui::GetIO().DisplaySize;
                ImGui::SetNextWindowPos(ImVec2(0, 0));
                ImGui::SetNextWindowSize(ds);
                ImGui::Begin("##attach_fail", nullptr,
                    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                    ImGuiWindowFlags_NoSavedSettings);
                ImDrawList* draw = ImGui::GetWindowDrawList();
                draw->AddRectFilled(ImVec2(0, 0), ds,
                    CyberTheme::U32(CyberTheme::Colors.Background));
                const float errorCardWidth = (std::min)(680.f, ds.x - 48.f);
                const float errorCardHeight = (std::min)(410.f, ds.y - 48.f);
                const ImVec2 cardMin((ds.x - errorCardWidth) * .5f,
                                     (ds.y - errorCardHeight) * .5f);
                const ImVec2 cardMax(cardMin.x + errorCardWidth,
                                     cardMin.y + errorCardHeight);
                draw->AddRectFilled(cardMin, cardMax,
                    CyberTheme::U32(CyberTheme::Colors.Card), 14.f);
                draw->AddRect(cardMin, cardMax,
                    CyberTheme::U32(CyberTheme::Colors.Border), 14.f);
                draw->AddRectFilled(cardMin, ImVec2(cardMin.x + 5.f, cardMax.y),
                    CyberTheme::U32(CyberTheme::Colors.Error), 14.f);
                const float left = cardMin.x + 34.f;
                ImGui::SetCursorScreenPos(ImVec2(left, cardMin.y + 28.f));
                ImGui::TextColored(CyberTheme::Colors.Error, "OMNIGHOST  /  DIAGNÓSTICO DE ARRANQUE");
                ImGui::SetCursorScreenPos(ImVec2(left, cardMin.y + 66.f));
                ImGui::TextUnformatted(issue.title);
                ImGui::SetCursorScreenPos(ImVec2(left, cardMin.y + 112.f));
                ImGui::PushTextWrapPos(cardMax.x - 34.f);
                ImGui::TextWrapped("%s", issue.explanation);
                ImGui::SetCursorScreenPos(ImVec2(left, cardMin.y + 185.f));
                ImGui::TextDisabled("O que fazer");
                ImGui::SetCursorScreenPos(ImVec2(left, cardMin.y + 212.f));
                ImGui::TextWrapped("%s", issue.action);
                ImGui::PopTextWrapPos();
                ImGui::SetCursorScreenPos(ImVec2(left, cardMax.y - 83.f));
                ImGui::TextDisabled("Código de suporte: %s",
                    OmniGhost::Support::Code(issue.code).data());
                ImGui::SetCursorScreenPos(ImVec2(left, cardMax.y - 53.f));
                dismissed = ImGui::Button("Voltar ao launcher", ImVec2(180.f, 34.f)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape, false);
                ImGui::SameLine();
                const ULONGLONG countdownNow = GetTickCount64();
                const ULONGLONG remainingMs = errUntil > countdownNow
                    ? errUntil - countdownNow : 0;
                ImGui::TextDisabled("Regresso automatico ao launcher em %llu s (ou clica no botao)",
                    static_cast<unsigned long long>(remainingMs / 1000 + 1));
                ImGui::End();
                application.EndRender();
            }
        }

        Launcher::RecordGameSession(selected, issue.result, detail);
        (void)startupState.Transition(OmniGhost::Startup::State::StartingGame,
                                      OmniGhost::Startup::State::Ready);
        continue;
    }




    if (!startupState.Transition(OmniGhost::Startup::State::StartingGame,
                                 OmniGhost::Startup::State::Running)) {
        startupState.Fail();
        std::cerr << "[STARTUP] Invalid StartingGame -> Running transition; session refused.\n";
        break;
    }


    if (adapter && (OmniGhost::OffsetAuto::SupportsAutomaticRefresh(pending_game) ||
        pending_game == ActiveGame::FiveM ||
        pending_game == ActiveGame::CS2 ||
        pending_game == ActiveGame::Warzone || pending_game == ActiveGame::Fortnite)) {
        const bool probe_ok = OmniGhost::OffsetAuto::SoftProbeLive(pending_game);
        if (probe_ok) {
            OmniGhost::OffsetAuto::MarkLiveValid(pending_game, "soft-probe lobby OK");
        } else {
            std::cout << "[Offsets] SoftProbe FAIL (non-blocking): "
                      << OmniGhost::OffsetAuto::SoftProbeFailReason(pending_game) << std::endl;
        }
    }

    {


        Launcher::MarkGameUsed(selected);
        Launcher::RecordGameSession(selected, Launcher::SessionResult::None, {});
    }

    OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::GameDetected);


    if (pending_game == ActiveGame::CS2) {
        if (!OmniGhost::OffsetAuto::ValidateLive(pending_game)) {
            std::cout << "[CS2] Self-test soft-fail — menu opens anyway; use Reload offsets se preciso." << std::endl;
            CS2::LoadOffsetsFromJson(nullptr);
        }
    }



    app_settings::menu_open = true;
    Launcher::BeginTimedGameSession(selected);
    application.RenderMenu = false;

    const std::uint64_t shutdownGeneration = shutdownCoordinator.BeginSession([&] {
        mem.RequestCancel();
        if (g_activeGame == ActiveGame::CS2) {
            CS2::Shutdown();
            CS2::ready = false;
        } else if (g_activeGame == ActiveGame::Warzone) {
            Warzone::Shutdown();
        } else if (g_activeGame == ActiveGame::Fortnite) {
            Fortnite::Detach();
        } else if (g_activeGame == ActiveGame::FiveM) {
            FiveM::ESP::StopAcquisition();
        }
        mem.InvalidateProcess();
    });


    int offset_probe_failures = 0;
    ULONGLONG next_offset_probe = GetTickCount64() + 5000;
    bool return_to_launcher = false;
    bool return_requested_by_user = false;
    ULONGLONG next_security_check = GetTickCount64() + 1000;
    ULONGLONG next_local_process_probe = 0;
    bool local_process_alive = false;
    std::chrono::steady_clock::time_point telemetry_next_record{};


    int game_monitor_type = 0;
    switch (g_activeGame) {
        case ActiveGame::CS2: game_monitor_type = 0; break;
        case ActiveGame::FiveM: game_monitor_type = 1; break;
        case ActiveGame::Warzone: game_monitor_type = 2; break;
        case ActiveGame::Fortnite: game_monitor_type = 3; break;
        default: game_monitor_type = 0; break;
    }
    OmniGhost::Gameplay::ProcessMonitor process_monitor(
        OmniGhost::Gameplay::GetDefaultMonitorConfig(game_monitor_type)
    );

    while (application.shouldRun && !return_to_launcher) {
        const ULONGLONG security_now = GetTickCount64();
        if (security_now >= next_security_check) {
            next_security_check = security_now + 1000;
            if (!OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures() ||
                !OmniGhost::Licensing::HasGameAccess(selectedDefinition->id)) {
                return_to_launcher = true;
                continue;
            }
        }
        application.StartRender();
        if (!application.shouldRun)
            break;


    if (adapter) {

        adapter->Tick();


        bool shouldReturnToLauncher = false;
        std::string terminationReason;

        bool dma_alive = false;
        bool local_alive = false;

        switch (g_activeGame) {
            case ActiveGame::CS2:
                dma_alive = CS2::IsGameProcessAlive();
                break;
            case ActiveGame::Warzone:
                dma_alive = Warzone::IsGameProcessAlive();
                break;
            case ActiveGame::Fortnite:
                dma_alive = Fortnite::IsGameProcessAlive();
                break;
            case ActiveGame::FiveM:
                dma_alive = FiveM::ESP::RemoteProcessRecentlySeen();
                if (GetTickCount64() >= next_local_process_probe) {
                    next_local_process_probe = GetTickCount64() + 1000;
                    local_process_alive = IsLocalFiveMProcessRunning();
                }
                local_alive = local_process_alive;
                break;
            default:
                break;
        }


        process_monitor.Update(dma_alive, local_alive);

        if (process_monitor.ShouldTerminateSession()) {
            shouldReturnToLauncher = true;
            terminationReason = "Processo terminado (monitor unificado)";
        }

        if (shouldReturnToLauncher) {
            std::cout << "[" << (g_activeGame == ActiveGame::CS2 ? "CS2" :
                                  g_activeGame == ActiveGame::Warzone ? "Warzone" :
                                  g_activeGame == ActiveGame::Fortnite ? "Fortnite" :
                                  "FiveM")
                      << "] " << terminationReason << " — a voltar ao launcher." << std::endl;
            return_to_launcher = true;
        }
    }




        const ULONGLONG probe_now = GetTickCount64();
        if (probe_now >= next_offset_probe &&
            g_activeGame == ActiveGame::CS2) {
            next_offset_probe = probe_now + 5000;
            bool can_probe = false;
            if (g_activeGame == ActiveGame::CS2) {
                const auto cs2_snapshot = CS2::AcquireRuntimeSnapshot();
                can_probe = CS2::ready && cs2_snapshot && cs2_snapshot->local_pawn != 0;
            }
            if (can_probe) {
                if (OmniGhost::OffsetAuto::ValidateLive(g_activeGame)) {
                    offset_probe_failures = 0;
                    OmniGhost::OffsetAuto::MarkLiveValid(g_activeGame);
                } else if (++offset_probe_failures >= 3) {

                    const std::string reason = "Os offsets falharam três validações consecutivas no jogo em execução.";
                    std::cerr << "[Offsets] " << reason << " (non-blocking; menu stays open)" << std::endl;
                    offset_probe_failures = 0;
                }
            }
        }

        const auto telemetry_now = std::chrono::steady_clock::now();
        if (telemetry_now >= telemetry_next_record) {
            telemetry_next_record = telemetry_now + std::chrono::milliseconds(250);
            int entity_players = 0;
            float acquire_p50_ms = 0.f;
            switch (g_activeGame) {
                case ActiveGame::CS2:
                    entity_players = CS2::PlayerCount();
                    acquire_p50_ms = OmniGhost::Gameplay::DmaTelemetry::CS2()
                        .acquire.acquire_p50_ms.load(std::memory_order_relaxed);
                    break;
                case ActiveGame::FiveM: {
                    const auto fivem_peds = FiveM::ESP::GetValidPedsSnapshot();
                    entity_players = static_cast<int>(fivem_peds.validPeds.size());
                    acquire_p50_ms = OmniGhost::Gameplay::DmaTelemetry::FiveM()
                        .acquire.acquire_p50_ms.load(std::memory_order_relaxed);
                    break;
                }
                case ActiveGame::Warzone:
                    entity_players = Warzone::runtime.player_count;
                    acquire_p50_ms = OmniGhost::Gameplay::DmaTelemetry::Warzone()
                        .acquire.acquire_p50_ms.load(std::memory_order_relaxed);
                    break;
                case ActiveGame::Fortnite:
                    acquire_p50_ms = OmniGhost::Gameplay::DmaTelemetry::Fortnite()
                        .acquire.acquire_p50_ms.load(std::memory_order_relaxed);
                    break;
                default:
                    break;
            }
            HardwareMonitor::RecordEntityCount(entity_players, 0);
            HardwareMonitor::RecordDMALatency(acquire_p50_ms, 0.f);
        }

        application.Render();
        application.EndRender();
        if (application.ConsumeReturnToLauncherRequest()) {
            std::clog << "[SESSION] user requested game shutdown and launcher return\n";
            return_to_launcher = true;
            return_requested_by_user = true;
        }
        if (update_service.ShouldExitForUpdate())
            application.shouldRun = false;
    }




        const Launcher::SessionResult sessionResult =
            (!application.shouldRun || return_requested_by_user)
                ? Launcher::SessionResult::Completed
                : Launcher::SessionResult::ProcessEnded;
        const char* sessionDetail = !application.shouldRun
            ? "Sessão encerrada pelo utilizador."
            : return_requested_by_user
                ? "Sessão terminada pelo utilizador."
                : "O processo do jogo deixou de estar disponível.";


        Launcher::EndTimedGameSession(selected, sessionResult, sessionDetail);

        if (application.shouldRun && return_to_launcher) {
            OmniGhost::UI::EndGameSessionWithTransition(application, shutdownCoordinator,
                shutdownGeneration, return_requested_by_user);
        } else {
            (void)shutdownCoordinator.EndSession(shutdownGeneration, "application-exit");
        }

        if (application.shouldRun) {
            (void)startupState.Transition(OmniGhost::Startup::State::Running,
                                         OmniGhost::Startup::State::Ready);
        }

        if (!application.shouldRun) {
            break;
        }
        if (return_to_launcher) {
            CyberWidgets::Notify(
                return_requested_by_user
                    ? "Sessão terminada"
                    : "Sessão terminada — o jogo foi fechado",
                CyberWidgets::ToastType::Info);
            nextLauncherEntry = Launcher::EntryReason::SessionEnded;
            std::cout << "[OmniGhost] A regressar ao launcher..." << std::endl;
            continue;
        }
        break;
    }

    (void)shutdownCoordinator.ShutdownAll(
        update_service.ShouldExitForUpdate() ? "update-restart" : "application-exit");
    return 0;
}

}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {



    using SetDpiContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto setDpiContext = reinterpret_cast<SetDpiContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"))) {
            setDpiContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int result = 0;
    try {
        result = argv ? RunOmniGhost(argc, argv) : RunOmniGhost(0, nullptr);
    } catch (const std::exception& exception) {
        std::string message = "O OmniGhost encontrou um erro inesperado durante o arranque.\n\nDetalhe: ";
        message += exception.what();
        OutputDebugStringA(("[OmniGhost][Startup] " + message + "\n").c_str());
        MessageBoxA(nullptr, message.c_str(), "OmniGhost", MB_OK | MB_ICONERROR);
        result = 10;
    } catch (...) {
        constexpr char message[] =
            "O OmniGhost encontrou um erro inesperado durante o arranque. Consulta os logs de diagnostico.";
        OutputDebugStringA("[OmniGhost][Startup] unknown startup exception\n");
        MessageBoxA(nullptr, message, "OmniGhost", MB_OK | MB_ICONERROR);
        result = 10;
    }
    if (argv)
        LocalFree(argv);
    return result;
}
