#include "game_session.h"
#include "service_container.h"
#include "result.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace OmniGhost::Platform {

namespace {
class FivemSession final : public IGameSession {
public:
    explicit FivemSession(const ServiceContainer&) : state_(SessionState::Detached) {}
    Result<void> Attach() noexcept override {
        state_ = SessionState::Attached;
        return Ok();
    }
    Result<void> LoadOffsets() noexcept override {
        state_ = SessionState::OffsetsLoaded;
        return Ok();
    }
    Result<void> Initialize() noexcept override {
        state_ = SessionState::Running;
        return Ok();
    }
    void Tick() noexcept override {}
    Result<bool> SoftProbeOffsets() noexcept override { return Ok(false); }
    Result<bool> ValidateLiveOffsets() noexcept override { return Ok(false); }
    Result<bool> IsGameProcessAlive() const noexcept override { return Ok(false); }
    [[nodiscard]] std::string_view GetStatus() const noexcept override { return "Fivem"; }
    [[nodiscard]] SessionState GetState() const noexcept override { return state_.load(); }
    [[nodiscard]] std::string_view GetTerminationReason() const noexcept override { return termination_; }
    void Shutdown() noexcept override { state_ = SessionState::Detached; }
    void SetStateCallback(std::function<void(const SessionEvent&)> callback) noexcept override {
        callback_ = std::move(callback);
    }
private:
    std::atomic<SessionState> state_;
    std::string termination_;
    std::function<void(const SessionEvent&)> callback_;
};
} // namespace

std::unique_ptr<IGameSession> CreateFivemSession(const ServiceContainer& services) {
    return std::make_unique<FivemSession>(services);
}

} // namespace OmniGhost::Platform
