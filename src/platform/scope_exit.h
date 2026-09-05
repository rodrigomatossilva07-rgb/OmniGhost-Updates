#pragma once

#include <type_traits>
#include <utility>

namespace OmniGhost::Platform {

template <typename F>
class ScopeExit final {
public:
    explicit ScopeExit(F&& fn) noexcept(std::is_nothrow_move_constructible_v<F>)
        : fn_(std::forward<F>(fn)) {}

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

    ScopeExit(ScopeExit&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
        : fn_(std::move(other.fn_)), active_(std::exchange(other.active_, false)) {}

    ScopeExit& operator=(ScopeExit&&) = delete;

    ~ScopeExit() noexcept {
        if (active_) {
            try {
                fn_();
            } catch (...) {
                // Scope-exit cleanup must never propagate from a destructor.
            }
        }
    }

    void Release() noexcept { active_ = false; }

private:
    F fn_;
    bool active_{true};
};

template <typename F>
[[nodiscard]] auto MakeScopeExit(F&& fn) {
    return ScopeExit<std::decay_t<F>>(std::forward<F>(fn));
}

} // namespace OmniGhost::Platform
