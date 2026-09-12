#include "fivem_session.h"

#include "../globals.h"
#include "../launcher/game_launch_service.h"
#include "../../DMALibrary/Memory/Memory.h"
#include "../../Fivem/game/esp_manager.h"
#include "../../Fivem/game/game_setup.h"
#include "../../Fivem/game/offsets.h"

#include <utility>

namespace OmniGhost::Platform {

FivemSession::FivemSession(const ServiceContainer& services)
    : services_(services), lastTick_(std::chrono::steady_clock::now()) {
    (void)services_;
    status_ = "FiveM desligado";
}

FivemSession::~FivemSession() { Shutdown(); }

void FivemSession::EmitState(SessionState newState, std::string_view message) {
    const SessionState oldState = state_.exchange(newState, std::memory_order_acq_rel);
    if (!message.empty()) status_.assign(message);
    if (!stateCallback_) return;
    SessionEvent event{};
    event.type = SessionEvent::Type::StateChanged;
    event.oldState = oldState;
    event.newState = newState;
    event.message.assign(message);
    stateCallback_(event);
}

Result<void> FivemSession::Attach() noexcept {
    try {
        EmitState(SessionState::Attaching, "A ligar ao FiveM");
        GameContext::Instance().SetActiveGame(ActiveGame::FiveM);
        if (!mem.Init(std::string(), true, false)) {
            EmitState(SessionState::Failed, "Dispositivo DMA indisponível");
            return Err("Falha ao abrir o dispositivo DMA");
        }
        const std::string executable = GameLaunch::FindFiveMProcessViaDma();
        if (executable.empty()) {
            EmitState(SessionState::Failed, "Processo FiveM não encontrado");
            return Err("Processo FiveM/GTA não encontrado");
        }
        GameContext::Instance().SetValidExecutable(executable);
        if (!mem.Init(executable, true, false)) {
            EmitState(SessionState::Failed, "Falha ao anexar ao processo FiveM");
            return Err("Falha ao anexar DMA ao processo FiveM");
        }
        attached_ = true;
        EmitState(SessionState::Attached, "FiveM ligado");
        return Ok();
    } catch (...) {
        EmitState(SessionState::Failed, "Erro inesperado ao ligar ao FiveM");
        return Err("Exceção durante o attach FiveM");
    }
}

Result<void> FivemSession::LoadOffsets() noexcept {
    if (!attached_) return Err("Sessão FiveM ainda não está ligada");
    try {
        EmitState(SessionState::LoadingOffsets, "A validar offsets FiveM");
        FiveM::Setup();
        offsetsLoaded_ = FiveM::IsBuildSupported() && FiveM::offset::world &&
                         FiveM::offset::replay && FiveM::offset::viewport;
        if (!offsetsLoaded_) {
            EmitState(SessionState::Failed, "Build ou offsets FiveM incompatíveis");
            return Err("Offsets FiveM inválidos para esta build");
        }
        EmitState(SessionState::OffsetsLoaded, "Offsets FiveM validados");
        return Ok();
    } catch (...) {
        EmitState(SessionState::Failed, "Erro ao carregar offsets FiveM");
        return Err("Exceção ao carregar offsets FiveM");
    }
}

Result<void> FivemSession::Initialize() noexcept {
    if (!attached_ || !offsetsLoaded_)
        return Err("Attach e offsets são necessários antes de inicializar");
    FiveM::ESP::InitializeContainers();
    initialized_ = true;
    lastTick_ = std::chrono::steady_clock::now();
    EmitState(SessionState::Running, "Sessão FiveM ativa");
    return Ok();
}

void FivemSession::Tick() noexcept {
    if (!initialized_ || state_.load(std::memory_order_relaxed) != SessionState::Running)
        return;
    ++frameCount_;
    if ((frameCount_ % 60u) == 0u && !IsProcessAliveImpl()) {
        terminationReason_ = "Processo FiveM/GTA terminou";
        EmitState(SessionState::Terminating, terminationReason_);
    }
    lastTick_ = std::chrono::steady_clock::now();
}

Result<bool> FivemSession::SoftProbeOffsets() noexcept {
    if (!attached_ || !offsetsLoaded_) return Ok(false);
    EmitState(SessionState::SoftProbing, "A verificar offsets FiveM");
    const bool valid = FiveM::SoftProbeLobbyOffsets();
    EmitState(valid ? SessionState::Running : SessionState::OffsetsLoaded,
              valid ? "Offsets FiveM operacionais" : "Offsets FiveM precisam de atualização");
    return Ok(valid);
}

Result<bool> FivemSession::ValidateLiveOffsets() noexcept {
    if (!attached_ || !offsetsLoaded_ || !FiveM::IsBuildSupported()) return Ok(false);
    return Ok(FiveM::SoftProbeLobbyOffsets());
}

bool FivemSession::IsProcessAliveImpl() const noexcept {
    return !GameLaunch::FindFiveMProcessViaDma().empty();
}

Result<bool> FivemSession::IsGameProcessAlive() const noexcept {
    try { return Ok(IsProcessAliveImpl()); }
    catch (...) { return Err<bool>("Falha ao consultar o processo FiveM"); }
}

std::string_view FivemSession::GetStatus() const noexcept { return status_; }
SessionState FivemSession::GetState() const noexcept {
    return state_.load(std::memory_order_acquire);
}
std::string_view FivemSession::GetTerminationReason() const noexcept {
    return terminationReason_;
}

void FivemSession::Shutdown() noexcept {
    if (!attached_ && !initialized_) return;
    EmitState(SessionState::Cleanup, "A terminar sessão FiveM");
    FiveM::ESP::StopAcquisition();
    mem.InvalidateProcess();
    GameContext::Instance().ClearValidExecutable();
    attached_ = false;
    offsetsLoaded_ = false;
    initialized_ = false;
    EmitState(SessionState::Detached, "FiveM desligado");
}

void FivemSession::SetStateCallback(
    std::function<void(const SessionEvent&)> callback) noexcept {
    stateCallback_ = std::move(callback);
}

} // namespace OmniGhost::Platform
