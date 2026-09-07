#pragma once

#include "game_session.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>

namespace OmniGhost::Platform {
class ServiceContainer;

class FivemSession final : public IGameSession {
public:
    explicit FivemSession(const ServiceContainer& services);
    ~FivemSession() override;

    Result<void> Attach() noexcept override;
    Result<void> LoadOffsets() noexcept override;
    Result<void> Initialize() noexcept override;
    void Tick() noexcept override;
    Result<bool> SoftProbeOffsets() noexcept override;
    Result<bool> ValidateLiveOffsets() noexcept override;
    Result<bool> IsGameProcessAlive() const noexcept override;
    [[nodiscard]] std::string_view GetStatus() const noexcept override;
    [[nodiscard]] SessionState GetState() const noexcept override;
    [[nodiscard]] std::string_view GetTerminationReason() const noexcept override;
    void Shutdown() noexcept override;
    void SetStateCallback(std::function<void(const SessionEvent&)> callback) noexcept override;

private:
    const ServiceContainer& services_;
    std::function<void(const SessionEvent&)> stateCallback_;
    std::atomic<SessionState> state_{SessionState::Detached};
    std::string status_;
    std::string terminationReason_;
    std::chrono::steady_clock::time_point lastTick_;
    std::uint64_t frameCount_ = 0;
    bool attached_ = false;
    bool offsetsLoaded_ = false;
    bool initialized_ = false;

    void EmitState(SessionState newState, std::string_view message = {});
    bool IsProcessAliveImpl() const noexcept;
};

} // namespace OmniGhost::Platform