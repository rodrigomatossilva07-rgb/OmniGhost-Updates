#pragma once

#include <cstdint>

namespace OmniGhost::Startup {

enum class State : std::uint8_t {
    Boot,
    CheckingInstance,
    InitializingRuntime,
    Authenticating,
    CheckingHardware,
    Ready,
    StartingGame,
    Running,
    Failed
};

class StateMachine final {
public:
    [[nodiscard]] State Current() const noexcept { return state_; }
    [[nodiscard]] bool Transition(State expected, State next) noexcept {
        if (state_ != expected || state_ == State::Failed)
            return false;
        state_ = next;
        return true;
    }
    void Fail() noexcept { state_ = State::Failed; }

private:
    State state_{State::Boot};
};

} // namespace OmniGhost::Startup
