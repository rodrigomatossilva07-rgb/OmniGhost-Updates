#include "game_adapter_registry.h"
#include "game_launch_service.h"

#include "../globals.h"
#include "../makcu/makcu_wrapper.h"
#include "../platform/offset_auto.h"
#include "../config/config_manager.h"
#include "src/games/Cs2/cs2_game.h"
#include "src/games/Cs2/aimbot/cs2_aim.h"
#include "src/games/Cs2/esp/cs2_esp.h"
#include "third_party/DMALibrary/Memory/Memory.h"
#include "src/games/Fivem/aimbot/aim_type.h"
#include "src/games/Fivem/game/game.h"
#include "src/games/Fivem/game/offset_auto.h"
#include "src/games/Fivem/game/offsets.h"
#include "src/games/Fortnite/fortnite_game.h"
#include "src/games/Warzone/warzone_game.h"
#include "imgui.h"

#include <functional>
#include <iostream>
#include <string>

namespace OmniGhost::Launcher {

class FunctionGameAdapter final : public IGameAdapter {
public:
    using BoolOperation = std::function<bool()>;
    using VoidOperation = std::function<void()>;
    using StatusOperation = std::function<std::string_view()>;
    using TickOperation = std::function<void()>;
    using IsAliveOperation = std::function<bool()>;
    using ValidateOffsetsOperation = std::function<bool()>;
    using TerminationReasonOperation = std::function<std::string_view()>;
    using GameIdOperation = std::function<ActiveGame()>;
    using FailureOperation = std::function<AdapterErrorCode()>;

    FunctionGameAdapter(OmniGhost::Launcher::AdapterDescriptor descriptor,
                        BoolOperation attach, VoidOperation shutdown, StatusOperation status,
                        TickOperation tick = [] {},
                        IsAliveOperation isAlive = [] { return false; },
                        ValidateOffsetsOperation validateOffsets = [] { return true; },
                        TerminationReasonOperation terminationReason = [] { return "Processo terminado"; },
                        GameIdOperation gameId = [] { return ActiveGame::FiveM; },
                        FailureOperation failure = [] { return AdapterErrorCode::AttachFailed; })
        : descriptor_(descriptor), attach_(std::move(attach)), shutdown_(std::move(shutdown)),
          status_(std::move(status)), tick_(std::move(tick)),
          isAlive_(std::move(isAlive)), validateOffsets_(std::move(validateOffsets)),
          terminationReason_(std::move(terminationReason)), gameId_(std::move(gameId)),
          failure_(std::move(failure)) {}

    bool Attach() override {
        try {
            attached_ = attach_ && attach_();
        } catch (const std::exception& ex) {
            attached_ = false;
            lastError_ = { AdapterErrorCode::UnexpectedException, ex.what() };
            return false;
        } catch (...) {
            attached_ = false;
            lastError_ = { AdapterErrorCode::UnexpectedException, "unknown attach exception" };
            return false;
        }
        if (attached_) {
            lastError_ = {};
        } else {
            std::string detail;
            try {
                if (status_) {
                    const auto st = status_();
                    if (!st.empty())
                        detail.assign(st.data(), st.size());
                }
            } catch (...) {
                detail = "status unavailable";
            }
            lastError_ = { failure_ ? failure_() : AdapterErrorCode::AttachFailed, std::move(detail) };
        }
        return attached_;
    }

    bool Validate() override {
        try {
            if (!attached_) lastError_ = { AdapterErrorCode::ValidationFailed, {} };
            return attached_;
        } catch (const std::exception& ex) {
            lastError_ = { AdapterErrorCode::UnexpectedException, ex.what() };
            return false;
        } catch (...) {
            lastError_ = { AdapterErrorCode::UnexpectedException, "unknown validation exception" };
            return false;
        }
    }
    bool Initialize() override {
        try {
            if (!attached_) lastError_ = { AdapterErrorCode::InitializationFailed, {} };
            return attached_;
        } catch (const std::exception& ex) {
            lastError_ = { AdapterErrorCode::UnexpectedException, ex.what() };
            return false;
        } catch (...) {
            lastError_ = { AdapterErrorCode::UnexpectedException, "unknown initialization exception" };
            return false;
        }
    }
    void Tick() override {
        tick_();
    }
    void Shutdown() noexcept override {
        if (!shutdown_) return;
        try {
            shutdown_();
        } catch (const std::exception& ex) {
            lastError_ = { OmniGhost::Launcher::AdapterErrorCode::InitializationFailed, "shutdown threw: " + std::string(ex.what()) };
        } catch (...) {
            lastError_ = { OmniGhost::Launcher::AdapterErrorCode::InitializationFailed, "shutdown threw unknown exception" };
        }
        attached_ = false;
    }
    std::string_view GetStatus() const noexcept override {
        try {
            return status_ ? status_() : std::string_view{};
        } catch (...) {
            return "Estado do adapter indisponível";
        }
    }
    bool IsGameProcessAlive() const override {
        return isAlive_ ? isAlive_() : false;
    }
    bool ValidateLiveOffsets() override {
        return validateOffsets_ ? validateOffsets_() : true;
    }
    std::string_view GetTerminationReason() const noexcept override {
        return terminationReason_ ? terminationReason_() : "Processo terminado";
    }
    ActiveGame GetGameId() const noexcept override {
        return gameId_ ? gameId_() : ActiveGame::FiveM;
    }
    OmniGhost::Launcher::AdapterDescriptor GetDescriptor() const noexcept override { return descriptor_; }
    OmniGhost::Launcher::AdapterError GetLastError() const override { return lastError_; }

private:
    OmniGhost::Launcher::AdapterDescriptor descriptor_;
    BoolOperation attach_;
    VoidOperation shutdown_;
    StatusOperation status_;
    TickOperation tick_;
    IsAliveOperation isAlive_;
    ValidateOffsetsOperation validateOffsets_;
    TerminationReasonOperation terminationReason_;
    GameIdOperation gameId_;
    FailureOperation failure_;
    bool attached_ = false;
    OmniGhost::Launcher::AdapterError lastError_{};
};

thread_local AdapterErrorCode s_fiveMStartFailure = AdapterErrorCode::AttachFailed;
thread_local bool s_cs2StartException = false;

AdapterErrorCode Cs2StartFailure() {
    if (s_cs2StartException)
        return AdapterErrorCode::UnexpectedException;
    const std::string& text = CS2::status;
    if (text.find("DMA offline") != std::string::npos ||
        text.find("inicializar DMA") != std::string::npos)
        return AdapterErrorCode::DeviceOpenFailed;
    if (text.find("Offsets") != std::string::npos ||
        text.find("offsets") != std::string::npos)
        return AdapterErrorCode::OffsetsMissing;
    if (text.find("client.dll") != std::string::npos)
        return AdapterErrorCode::ModuleNotFound;
    if (text.find("Falha ao anexar") != std::string::npos)
        return AdapterErrorCode::ProcessBindFailed;
    if (text.find("processo") != std::string::npos ||
        text.find("cs2.exe") != std::string::npos)
        return AdapterErrorCode::ProcessNotFound;
    return AdapterErrorCode::AttachFailed;
}

AdapterErrorCode FortniteStartFailure() {
    const char* statusText = Fortnite::StatusText();
    const std::string text = statusText ? statusText : "";
    if (text.find("Offsets") != std::string::npos)
        return AdapterErrorCode::OffsetsMissing;
    if (text.find("DMA/FPGA") != std::string::npos)
        return AdapterErrorCode::DeviceOpenFailed;
    if (text.find("nao encontrado") != std::string::npos)
        return AdapterErrorCode::ProcessNotFound;
    if (text.find("Base do modulo") != std::string::npos)
        return AdapterErrorCode::ModuleNotFound;
    if (text.find("Memoria") != std::string::npos ||
        text.find("memoria") != std::string::npos)
        return AdapterErrorCode::ProcessBindFailed;
    return AdapterErrorCode::AttachFailed;
}

namespace {
// LCH-001 Fix: attaching assigns g_activeGame BEFORE the DMA attach attempt.
// On failure the launcher was left bound to a game the user never entered, so
// config saves ran under the wrong prefix/metadata. This guard restores the
// previous game unless the start path explicitly commits.
struct ScopedActiveGame {
    ScopedActiveGame()
        : previous(OmniGhost::GameContext::Instance().GetActiveGame()),
          previousExecutable(OmniGhost::GameContext::Instance().GetValidExecutable()) {}
    ~ScopedActiveGame() {
        if (!commit) {
            OmniGhost::GameContext::Instance().SetActiveGame(previous);
            OmniGhost::GameContext::Instance().SetValidExecutable(previousExecutable);
        }
    }
    void Commit() { commit = true; }
    ActiveGame previous;
    std::string previousExecutable;
    bool commit = false;
};
} // namespace

bool StartCs2() {
    ScopedActiveGame gameGuard;
    try {
        s_cs2StartException = false;
        g_activeGame = ActiveGame::CS2;
        config_manager::ActivateCurrentGameConfig();
        if (!CS2::Attach())
            return false;
        gameGuard.Commit();
        return true;
    } catch (const std::exception& ex) {
        s_cs2StartException = true;
        std::cerr << "[CS2] CRASH em StartCs2: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        s_cs2StartException = true;
        std::cerr << "[CS2] CRASH desconhecido em StartCs2" << std::endl;
        return false;
    }
}

bool StartWarzone() {
    ScopedActiveGame gameGuard;
    g_activeGame = ActiveGame::Warzone;
    if (Warzone::Attach()) {
        gameGuard.Commit();
        return true;
    }
    std::cerr << "[Warzone] Falha na comunicacao com DMA/FPGA.\n";
    return false;
}

bool StartFortnite() {
    ScopedActiveGame gameGuard;
    g_activeGame = ActiveGame::Fortnite;
    if (!Fortnite::Attach())
        return false;
    gameGuard.Commit();
    return true;
}

bool StartFiveM() {
    ScopedActiveGame gameGuard;
    try {
        s_fiveMStartFailure = AdapterErrorCode::AttachFailed;
        OmniGhost::GameContext::Instance().SetActiveGame(ActiveGame::FiveM);
        if (!mem.Init(std::string(), true, false)) {
            s_fiveMStartFailure = AdapterErrorCode::DeviceOpenFailed;
            std::cerr << "[FiveM] Falha a abrir o dispositivo DMA/FPGA.\n";
            return false;
        }
        std::string executable = OmniGhost::GameLaunch::FindFiveMProcessViaDma();
        OmniGhost::GameContext::Instance().SetValidExecutable(executable);
        if (executable.empty()) {
            s_fiveMStartFailure = AdapterErrorCode::ProcessNotFound;
            std::cerr << "[FiveM] Processo GTAProcess nao encontrado no PC do jogo.\n";
            return false;
        }
        if (!mem.Init(executable, true, false)) {
            s_fiveMStartFailure = AdapterErrorCode::ProcessBindFailed;
            std::cerr << "[FiveM] Falha ao anexar DMA a " << executable << ".\n";
            return false;
        }
        FiveM::Setup();
        if (!FiveM::IsBuildSupported()) {
            s_fiveMStartFailure = AdapterErrorCode::UnsupportedBuild;
            constexpr std::string_view reason = "Build FiveM não suportada pela tabela validada do OmniGhost.";
            OmniGhost::OffsetAuto::MarkUnsupported(ActiveGame::FiveM, std::string(reason));
            std::cerr << "[FiveM] " << reason << '\n';
            return false;
        }
        if (!FiveM::OffsetAuto::PointersLookValid() || !FiveM::SoftProbeLobbyOffsets()) {
            s_fiveMStartFailure = AdapterErrorCode::GameDataUnverified;
            constexpr std::string_view reason = "Não foi possível validar os ponteiros FiveM; a leitura DMA ou os offsets podem estar indisponíveis.";
            std::cerr << "[FiveM] " << reason << '\n';
            return false;
        }
        OmniGhost::OffsetAuto::MarkLiveValid(ActiveGame::FiveM, "Build e leitura DMA FiveM confirmadas");
        FiveM::ESP::InitializeContainers();
        if (!aim_type::IsConnected()) makcu_wrapper::MakcuInitialize("");
        gameGuard.Commit();
        return true;
    } catch (const std::exception& ex) {
        s_fiveMStartFailure = AdapterErrorCode::UnexpectedException;
        std::cerr << "[FiveM] CRASH em StartFiveM: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        s_fiveMStartFailure = AdapterErrorCode::UnexpectedException;
        std::cerr << "[FiveM] CRASH desconhecido em StartFiveM" << std::endl;
        return false;
    }
}

std::string_view FiveMStatus() noexcept {
    return OmniGhost::GameContext::Instance().HasValidExecutable()
        ? "FiveM ligado" : "FiveM não ligado";
}

// Static functions for adapter operations to avoid lambda/std::function issues
bool FivemIsAlive() {
    if (OmniGhost::GameContext::Instance().GetValidExecutable().empty()) return false;
    return !::OmniGhost::GameLaunch::FindFiveMProcessViaDma().empty();
}
bool FivemValidateOffsets() {
    return FiveM::IsBuildSupported() && FiveM::OffsetAuto::PointersLookValid() &&
        FiveM::SoftProbeLobbyOffsets();
}
std::string_view FivemTerminationReason() { return "Processo FiveM/GTA terminou"; }
ActiveGame FivemGameId() { return ActiveGame::FiveM; }

bool Cs2IsAlive() { return CS2::IsGameProcessAlive(); }
bool Cs2ValidateOffsets() { return CS2::ValidateLiveOffsets(); }
std::string_view Cs2TerminationReason() { return "Processo cs2.exe terminou"; }
ActiveGame Cs2GameId() { return ActiveGame::CS2; }

bool WarzoneIsAlive() { return Warzone::IsGameProcessAlive(); }
bool WarzoneValidateOffsets() { return Warzone::ValidateLiveOffsets(); }
std::string_view WarzoneTerminationReason() { return "Processo Warzone terminou"; }
ActiveGame WarzoneGameId() { return ActiveGame::Warzone; }


bool FortniteIsAlive() { return Fortnite::IsGameProcessAlive(); }
bool FortniteValidateOffsets() { return Fortnite::ValidateLiveOffsets(); }
std::string_view FortniteTerminationReason() { return "Processo Fortnite terminou"; }
ActiveGame FortniteGameId() { return ActiveGame::Fortnite; }


IGameAdapter* FindGameAdapter(::Launcher::GameId game) noexcept {
    using Capability = AdapterCapability;
    static FunctionGameAdapter fivem({ ::Launcher::GameId::FiveM, "FiveM", AdapterMaturity::Stable,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay | Capability::Radar },
        StartFiveM, [] {
            FiveM::ESP::StopAcquisition();
            mem.InvalidateProcess();
        }, FiveMStatus,
        [] { 
            try {
                if (!g_validExecutable.empty()) {
                    FiveM::ESP::RunESP();
                }
            } catch (const std::exception& ex) {
                std::cerr << "[FiveM] CRASH em Tick: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[FiveM] CRASH desconhecido em Tick" << std::endl;
            }
        },
        FivemIsAlive, FivemValidateOffsets, FivemTerminationReason, FivemGameId,
        [] { return s_fiveMStartFailure; });
    static FunctionGameAdapter cs2({ ::Launcher::GameId::CS2, "Counter-Strike 2", AdapterMaturity::Stable,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay | Capability::Radar },
        StartCs2, [] { CS2::Shutdown(); CS2::ready = false; },
        [] { return std::string_view(CS2::status); },
        [] { 
            try {
                if (CS2::ready) {
                    CS2::SetPresentationFps(ImGui::GetIO().Framerate);
                    CS2::SubmitAcquisitionConfig(CS2::config);
                    CS2::EnsureAcquisitionStarted();
                    auto snapshot = CS2::AcquireRuntimeSnapshot();
                    if (snapshot) {
                        CS2::ESP::DrawPlayers(*snapshot, CS2::config);
                        CS2_Aim::Run(*snapshot, CS2::config);
                    }
                }
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] CRASH em Tick: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[CS2] CRASH desconhecido em Tick" << std::endl;
            }
        },
        Cs2IsAlive, Cs2ValidateOffsets, Cs2TerminationReason, Cs2GameId,
        Cs2StartFailure);
    static FunctionGameAdapter warzone({ ::Launcher::GameId::Warzone, "Call of Duty: Warzone", AdapterMaturity::Beta,
        Capability::Menu | Capability::ReadOnlyMemory },
        StartWarzone, [] { Warzone::Shutdown(); },
        [] { return std::string_view(Warzone::status); },
        [] { 
            if (Warzone::ready) {
                Warzone::RunFrame();
            }
        },
        WarzoneIsAlive, WarzoneValidateOffsets, WarzoneTerminationReason, WarzoneGameId);
    static FunctionGameAdapter fortnite({ ::Launcher::GameId::Fortnite, "Fortnite", AdapterMaturity::Beta,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay },
        StartFortnite, [] { Fortnite::Detach(); },
        [] { return std::string_view(Fortnite::StatusText()); },
        [] { 
            Fortnite::Tick();
            Fortnite::DrawESP();
            Fortnite::RunAim();
        },
        FortniteIsAlive, FortniteValidateOffsets, FortniteTerminationReason, FortniteGameId,
        FortniteStartFailure);

    switch (game) {
    case ::Launcher::GameId::FiveM: return &fivem;
    case ::Launcher::GameId::CS2: return &cs2;
    case ::Launcher::GameId::Warzone: return &warzone;
    case ::Launcher::GameId::Fortnite: return &fortnite;
    default: return nullptr;
    }
}

AdapterStartResult StartGameAdapter(::Launcher::GameId game) {
    IGameAdapter* adapter = FindGameAdapter(game);
    if (!adapter)
        return { false, { AdapterErrorCode::UnsupportedGame, {} } };
    if (!adapter->Attach())
        return { false, adapter->GetLastError() };
    if (!adapter->Validate())
        return { false, adapter->GetLastError() };
    if (!adapter->Initialize())
        return { false, adapter->GetLastError() };
    return { true, {} };
}
} // namespace OmniGhost::Launcher
