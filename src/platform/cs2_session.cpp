#include "cs2_session.h"

#include "../../Cs2/cs2_game.h"

#include <utility>

namespace OmniGhost::Platform {

Cs2Session::Cs2Session(const ServiceContainer& services)
    : services_(services), lastTick_(std::chrono::steady_clock::now()) {
    (void)services_;
    status_ = "CS2 desligado";
}

Cs2Session::~Cs2Session() { Shutdown(); }

void Cs2Session::EmitState(SessionState newState, std::string_view message) {
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

Result<void> Cs2Session::Attach() noexcept {
    try {
        EmitState(SessionState::Attaching, "A ligar ao CS2");
        if (!CS2::Attach()) {
            EmitState(SessionState::Failed, CS2::status);
            return Err(CS2::status.empty() ? "Falha ao ligar ao CS2" : CS2::status);
        }
        attached_ = true;
        offsetsLoaded_ = CS2::offsets.loaded;
        EmitState(SessionState::Attached, "CS2 ligado");
        return Ok();
    } catch (...) {
        EmitState(SessionState::Failed, "Erro inesperado ao ligar ao CS2");
        return Err("Exceção durante o attach CS2");
    }
}

Result<void> Cs2Session::LoadOffsets() noexcept {
    if (!attached_) return Err("Sessão CS2 ainda não está ligada");
    try {
        EmitState(SessionState::LoadingOffsets, "A validar offsets CS2");
        offsetsLoaded_ = CS2::offsets.loaded || CS2::LoadOffsetsFromJson(nullptr);
        if (offsetsLoaded_ && !CS2::SoftProbeLobbyOffsets())
            offsetsLoaded_ = CS2::RecoverCriticalOffsets() && CS2::SoftProbeLobbyOffsets();
        if (!offsetsLoaded_) {
            EmitState(SessionState::Failed, "Offsets CS2 incompatíveis");
            return Err("Offsets CS2 inválidos para esta build");
        }
        EmitState(SessionState::OffsetsLoaded, "Offsets CS2 validados");
        return Ok();
    } catch (...) {
        EmitState(SessionState::Failed, "Erro ao carregar offsets CS2");
        return Err("Exceção ao carregar offsets CS2");
    }
}

Result<void> Cs2Session::Initialize() noexcept {
    if (!attached_ || !offsetsLoaded_)
        return Err("Attach e offsets são necessários antes de inicializar");
    CS2::SubmitAcquisitionConfig(CS2::config);
    CS2::EnsureAcquisitionStarted();
    initialized_ = true;
    lastTick_ = std::chrono::steady_clock::now();
    EmitState(SessionState::Running, "Sessão CS2 ativa");
    return Ok();
}

void Cs2Session::Tick() noexcept {
    if (!initialized_ || state_.load(std::memory_order_relaxed) != SessionState::Running)
        return;
    CS2::SubmitAcquisitionConfig(CS2::config);
    CS2::EnsureAcquisitionStarted();
    ++frameCount_;
    if ((frameCount_ % 60u) == 0u && !IsProcessAliveImpl()) {
        terminationReason_ = "Processo cs2.exe terminou";
        EmitState(SessionState::Terminating, terminationReason_);
    }
    lastTick_ = std::chrono::steady_clock::now();
}

Result<bool> Cs2Session::SoftProbeOffsets() noexcept {
    if (!attached_ || !offsetsLoaded_) return Ok(false);
    EmitState(SessionState::SoftProbing, "A verificar offsets CS2");
    const bool valid = CS2::SoftProbeLobbyOffsets();
    EmitState(valid ? SessionState::Running : SessionState::OffsetsLoaded,
              valid ? "Offsets CS2 operacionais" : "Offsets CS2 precisam de atualização");
    return Ok(valid);
}

Result<bool> Cs2Session::ValidateLiveOffsets() noexcept {
    if (!attached_ || !offsetsLoaded_) return Ok(false);
    return Ok(CS2::ValidateLiveOffsets());
}

bool Cs2Session::IsProcessAliveImpl() const noexcept { return CS2::IsGameProcessAlive(); }

Result<bool> Cs2Session::IsGameProcessAlive() const noexcept {
    return Ok(IsProcessAliveImpl());
}

std::string_view Cs2Session::GetStatus() const noexcept { return status_; }
SessionState Cs2Session::GetState() const noexcept {
    return state_.load(std::memory_order_acquire);
}
std::string_view Cs2Session::GetTerminationReason() const noexcept {
    return terminationReason_;
}

void Cs2Session::Shutdown() noexcept {
    if (!attached_ && !initialized_) return;
    EmitState(SessionState::Cleanup, "A terminar sessão CS2");
    CS2::Shutdown();
    attached_ = false;
    offsetsLoaded_ = false;
    initialized_ = false;
    EmitState(SessionState::Detached, "CS2 desligado");
}

void Cs2Session::SetStateCallback(
    std::function<void(const SessionEvent&)> callback) noexcept {
    stateCallback_ = std::move(callback);
}

} // namespace OmniGhost::Platform
