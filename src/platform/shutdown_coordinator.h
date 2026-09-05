#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string_view>
#include <vector>

namespace OmniGhost::Platform {

enum class ShutdownComponent : std::uint8_t {
    Adapter, Radar, Updater, Changelog, Hardware, Input, Ui
};

enum class ShutdownState : std::uint8_t {
    Running, ShutdownRequested, ShuttingDown, Complete
};

// Single process-wide authority for teardown ordering. Registrations are
// component keyed, run at most once, and execute without holding the mutex.
class ShutdownCoordinator final {
public:
    using Callback = std::function<void()>;

    [[nodiscard]] bool Register(ShutdownComponent component, int priority,
                                Callback callback);
    [[nodiscard]] bool ShutdownAll(std::string_view reason) noexcept;
    [[nodiscard]] ShutdownState State() const noexcept;

    [[nodiscard]] std::uint64_t BeginSession(Callback callback);
    [[nodiscard]] bool EndSession(std::uint64_t generation,
                                  std::string_view reason) noexcept;
    void CancelSessionRegistration(std::uint64_t generation) noexcept;

private:
    struct Registration {
        ShutdownComponent component{};
        int priority{};
        Callback callback;
    };

    mutable std::mutex mutex_;
    std::condition_variable complete_;
    std::vector<Registration> registrations_;
    ShutdownState state_{ShutdownState::Running};
    std::uint64_t generation_ = 0;
    bool sessionActive_ = false;
    Callback sessionCallback_;
};

const char* ShutdownComponentName(ShutdownComponent component) noexcept;

} // namespace OmniGhost::Platform
