#include "game_adapter_registry.h"
#include "game_launch_service.h"

#include "../globals.h"
#include "../makcu/makcu_wrapper.h"
#include "../platform/offset_auto.h"
#include "../../Cs2/cs2_game.h"
#include "../../Cs2/cs2_esp.h"
#include "../../Cs2/cs2_aim.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../../Fivem/aimbot/aim_type.h"
#include "../../Fivem/game/game.h"
#include "../../Fivem/object_esp/object_esp.h"
#include "../../Fortnite/fortnite_game.h"
#include "../../Rust/rust_game.h"
#include "../../Rust/rust_esp.h"
#include "../../Rust/rust_aim.h"
#include "../../Valorant/valorant_game.h"
#include "../../Valorant/valorant_esp.h"
#include "../../Valorant/valorant_aim.h"
#include "../../Warzone/warzone_game.h"

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

    FunctionGameAdapter(OmniGhost::Launcher::AdapterDescriptor descriptor,
                        BoolOperation attach, VoidOperation shutdown, StatusOperation status,
                        TickOperation tick = [] {},
                        IsAliveOperation isAlive = [] { return false; },
                        ValidateOffsetsOperation validateOffsets = [] { return true; },
                        TerminationReasonOperation terminationReason = [] { return "Processo terminado"; },
                        GameIdOperation gameId = [] { return ActiveGame::FiveM; })
        : descriptor_(descriptor), attach_(std::move(attach)), shutdown_(std::move(shutdown)),
          status_(std::move(status)), tick_(std::move(tick)),
          isAlive_(std::move(isAlive)), validateOffsets_(std::move(validateOffsets)),
          terminationReason_(std::move(terminationReason)), gameId_(std::move(gameId)) {}

    bool Attach() override {
        attached_ = attach_ && attach_();
        if (attached_) {
            lastError_ = {};
        } else {
            std::string detail;
            if (status_) {
                const auto st = status_();
                if (!st.empty())
                    detail.assign(st.data(), st.size());
            }
            lastError_ = { OmniGhost::Launcher::AdapterErrorCode::AttachFailed, std::move(detail) };
        }
        return attached_;
    }

    bool Validate() override {
        if (!attached_) lastError_ = { OmniGhost::Launcher::AdapterErrorCode::ValidationFailed, {} };
        return attached_;
    }
    bool Initialize() override {
        if (!attached_) lastError_ = { OmniGhost::Launcher::AdapterErrorCode::InitializationFailed, {} };
        return attached_;
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
        return status_ ? status_() : std::string_view{};
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
    OmniGhost::Launcher::AdapterError GetLastError() const noexcept override { return lastError_; }

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
    bool attached_ = false;
    OmniGhost::Launcher::AdapterError lastError_{};
};

bool StartCs2() {
    try {
        g_activeGame = ActiveGame::CS2;
        return CS2::Attach();
    } catch (const std::exception& ex) {
        std::cerr << "[CS2] CRASH em StartCs2: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[CS2] CRASH desconhecido em StartCs2" << std::endl;
        return false;
    }
}

bool StartRust() {
    g_activeGame = ActiveGame::Rust;
    if (!Rust::offsets.loaded) Rust::LoadOffsetsFromJson(nullptr);
    if (!Rust::offsets.loaded) Rust::ApplyEmbeddedDefaults();
    std::cout << "[Rust] offsets loaded source=" << Rust::offsets.source << '\n';
    return Rust::StartBackendAsync();
}

bool StartWarzone() {
    g_activeGame = ActiveGame::Warzone;
    if (Warzone::Attach()) return true;
    std::cerr << "[Warzone] Falha na comunicacao com DMA/FPGA.\n";
    return false;
}

bool StartValorant() {
    g_activeGame = ActiveGame::Valorant;
    return Valorant::Attach();
}

bool StartFortnite() {
    g_activeGame = ActiveGame::Fortnite;
    return Fortnite::Attach();
}

bool StartFiveM() {
    try {
        OmniGhost::GameContext::Instance().SetActiveGame(ActiveGame::FiveM);
        if (!mem.Init(std::string(), true, false)) {
            std::cerr << "[FiveM] Falha a abrir o dispositivo DMA/FPGA.\n";
            return false;
        }
        std::string executable = OmniGhost::GameLaunch::FindFiveMProcessViaDma();
        OmniGhost::GameContext::Instance().SetValidExecutable(executable);
        if (executable.empty()) {
            std::cerr << "[FiveM] Processo GTAProcess nao encontrado no PC do jogo.\n";
            return false;
        }
        if (!mem.Init(executable, true, false)) {
            std::cerr << "[FiveM] Falha ao anexar DMA a " << executable << ".\n";
            return false;
        }
        FiveM::Setup();
        if (!FiveM::IsBuildSupported()) {
            constexpr std::string_view reason = "Build FiveM não suportada pela tabela validada do OmniGhost.";
            OmniGhost::OffsetAuto::MarkOutdated(ActiveGame::FiveM, std::string(reason));
            std::cerr << "[FiveM] " << reason << '\n';
            return false;
        }
        OmniGhost::OffsetAuto::MarkLiveValid(ActiveGame::FiveM, "Build FiveM confirmada na tabela validada");
        FiveM::ESP::InitializeContainers();
        if (!object_esp::GetObjectESPManager().Initialize()) {
            std::cerr << "[FiveM][ObjectESP] Inicializacao indisponivel; a sessao continua sem Object ESP.\n";
        }
        if (!aim_type::IsConnected()) makcu_wrapper::MakcuInitialize("");
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[FiveM] CRASH em StartFiveM: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "[FiveM] CRASH desconhecido em StartFiveM" << std::endl;
        return false;
    }
}

std::string_view FiveMStatus() noexcept {
    return OmniGhost::GameContext::Instance().GetValidExecutable().empty() ? "FiveM não ligado" : "FiveM ligado";
}

// Static functions for adapter operations to avoid lambda/std::function issues
bool FivemIsAlive() {
    if (OmniGhost::GameContext::Instance().GetValidExecutable().empty()) return false;
    return !::OmniGhost::GameLaunch::FindFiveMProcessViaDma().empty();
}
bool FivemValidateOffsets() { return true; }
std::string_view FivemTerminationReason() { return "Processo FiveM/GTA terminou"; }
ActiveGame FivemGameId() { return ActiveGame::FiveM; }

bool Cs2IsAlive() { return CS2::IsGameProcessAlive(); }
bool Cs2ValidateOffsets() { return CS2::ValidateLiveOffsets(); }
std::string_view Cs2TerminationReason() { return "Processo cs2.exe terminou"; }
ActiveGame Cs2GameId() { return ActiveGame::CS2; }

bool RustIsAlive() { return Rust::IsGameProcessAlive(); }
bool RustValidateOffsets() { return Rust::ValidateLiveOffsets(); }
std::string_view RustTerminationReason() { return "Processo Rust terminou"; }
ActiveGame RustGameId() { return ActiveGame::Rust; }

bool WarzoneIsAlive() { return Warzone::IsGameProcessAlive(); }
bool WarzoneValidateOffsets() { return Warzone::ValidateLiveOffsets(); }
std::string_view WarzoneTerminationReason() { return "Processo Warzone terminou"; }
ActiveGame WarzoneGameId() { return ActiveGame::Warzone; }

bool ValorantIsAlive() { return Valorant::IsGameProcessAlive(); }
bool ValorantValidateOffsets() { return Valorant::ValidateLiveOffsets(); }
std::string_view ValorantTerminationReason() { return "Processo Valorant terminou"; }
ActiveGame ValorantGameId() { return ActiveGame::Valorant; }

bool FortniteIsAlive() { return Fortnite::IsGameProcessAlive(); }
bool FortniteValidateOffsets() { return Fortnite::ValidateLiveOffsets(); }
std::string_view FortniteTerminationReason() { return "Processo Fortnite terminou"; }
ActiveGame FortniteGameId() { return ActiveGame::Fortnite; }

IGameAdapter* FindGameAdapter(::Launcher::GameId game) noexcept {
    using Capability = AdapterCapability;
    static FunctionGameAdapter fivem({ ::Launcher::GameId::FiveM, "FiveM", AdapterMaturity::Stable,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay | Capability::Radar },
        StartFiveM, [] { object_esp::GetObjectESPManager().Shutdown(); mem.InvalidateProcess(); }, FiveMStatus,
        [] { 
            try {
                if (!g_validExecutable.empty()) {
                    FiveM::ESP::RunESP();
                    object_esp::GetObjectESPManager().Update();
                }
            } catch (const std::exception& ex) {
                std::cerr << "[FiveM] CRASH em Tick: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[FiveM] CRASH desconhecido em Tick" << std::endl;
            }
        },
        FivemIsAlive, FivemValidateOffsets, FivemTerminationReason, FivemGameId);
    static FunctionGameAdapter cs2({ ::Launcher::GameId::CS2, "Counter-Strike 2", AdapterMaturity::Stable,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay | Capability::Radar },
        StartCs2, [] { CS2::Shutdown(); CS2::ready = false; },
        [] { return std::string_view(CS2::status); },
        [] { 
            try {
                if (CS2::ready) {
                    CS2::RunFrame(); // data + radar; early-outs in lobby
                    CS2_ESP::Draw(CS2::runtime, CS2::config);
                    CS2_Aim::Run(CS2::runtime, CS2::config);
                }
            } catch (const std::exception& ex) {
                std::cerr << "[CS2] CRASH em Tick: " << ex.what() << std::endl;
            } catch (...) {
                std::cerr << "[CS2] CRASH desconhecido em Tick" << std::endl;
            }
        },
        Cs2IsAlive, Cs2ValidateOffsets, Cs2TerminationReason, Cs2GameId);
    static FunctionGameAdapter rust({ ::Launcher::GameId::Rust, "Rust", AdapterMaturity::Stable,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay | Capability::Radar },
        StartRust, [] { Rust::Shutdown(); Rust::ready = false; },
        [] { return std::string_view(Rust::status); },
        [] { 
            if (Rust::ready) {
                Rust::RunFrame();
                Rust_ESP::Draw(Rust::runtime, Rust::config);
                Rust_Aim::Run(Rust::runtime, Rust::config);
            }
        },
        RustIsAlive, RustValidateOffsets, RustTerminationReason, RustGameId);
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
    static FunctionGameAdapter valorant({ ::Launcher::GameId::Valorant, "Valorant", AdapterMaturity::Beta,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay },
        StartValorant, [] { Valorant::Detach(); },
        [] { return std::string_view(Valorant::StatusText()); },
        [] { 
            Valorant::Tick();
            Valorant::DrawESP();
            Valorant::RunAim();
        },
        ValorantIsAlive, ValorantValidateOffsets, ValorantTerminationReason, ValorantGameId);
    static FunctionGameAdapter fortnite({ ::Launcher::GameId::Fortnite, "Fortnite", AdapterMaturity::Beta,
        Capability::Menu | Capability::ReadOnlyMemory | Capability::Overlay },
        StartFortnite, [] { Fortnite::Detach(); },
        [] { return std::string_view(Fortnite::StatusText()); },
        [] { 
            Fortnite::Tick();
            Fortnite::DrawESP();
            Fortnite::RunAim();
        },
        FortniteIsAlive, FortniteValidateOffsets, FortniteTerminationReason, FortniteGameId);

    switch (game) {
    case ::Launcher::GameId::FiveM: return &fivem;
    case ::Launcher::GameId::CS2: return &cs2;
    case ::Launcher::GameId::Rust: return &rust;
    case ::Launcher::GameId::Warzone: return &warzone;
    case ::Launcher::GameId::Valorant: return &valorant;
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
