#pragma once
#include "../globals.h"

#include "game_session.h"
#include "event_bus.h"
#include "service_container.h"
#include "result.h"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace OmniGhost::Platform {

// ============================================================
// ScopedSession - RAII wrapper for guaranteed cleanup
// ============================================================
class ScopedSession final {
public:
    // Configuration for session behavior
    struct Config {
        std::chrono::milliseconds tickInterval{1};        // Tick loop interval
        std::chrono::milliseconds probeInterval{5000};    // Offset probe interval
        std::chrono::milliseconds aliveCheckInterval{750};// Process alive check
        bool enableSoftProbe = true;                       // Enable soft probe
        bool enableAutoReconnect = false;                  // Auto-reconnect hardware
        std::uint32_t maxOffsetProbeFailures = 3;          // Max consecutive failures
        std::uint32_t maxProcessMissFrames = 3;            // Max process miss frames
        std::function<void()> onCleanup;                   // Custom cleanup callback
    };

    // Create a scoped session with the given game session
    explicit ScopedSession(std::unique_ptr<IGameSession> session, Config config = {})
        : session_(std::move(session)), config_(std::move(config)) {
        if (session_) {
            session_->SetStateCallback([this](const SessionEvent& event) {
                OnStateChanged(event);
            });
        }
    }

    // Move-only
    ScopedSession(ScopedSession&& other) noexcept
        : session_(std::move(other.session_))
        , config_(std::move(other.config_))
        , state_(other.state_.load())
        , shouldExit_(other.shouldExit_.load())
        , tickThread_(std::move(other.tickThread_))
        , probeThread_(std::move(other.probeThread_)) {
        other.shouldExit_.store(true);
    }

    ScopedSession& operator=(ScopedSession&& other) noexcept {
        if (this != &other) {
            Shutdown();
            session_ = std::move(other.session_);
            config_ = std::move(other.config_);
            state_ = other.state_.load();
            shouldExit_ = other.shouldExit_.load();
            tickThread_ = std::move(other.tickThread_);
            probeThread_ = std::move(other.probeThread_);
            other.shouldExit_.store(true);
        }
        return *this;
    }

    ScopedSession(const ScopedSession&) = delete;
    ScopedSession& operator=(const ScopedSession&) = delete;

    ~ScopedSession() {
        Shutdown();
    }

    // Start the session (blocks until running or error)
    Result<void> Start() noexcept {
        if (!session_) return Err<void>("No session");
        if (state_.load() != SessionState::Detached) return Err<void>("Already started");

        state_.store(SessionState::Attaching);
        
        // Attach
        auto result = session_->Attach();
        if (!result) {
            state_.store(SessionState::Failed);
            return Err<void>("Attach failed: " + result.error());
        }
        state_.store(SessionState::Attached);

        // Load offsets
        state_.store(SessionState::LoadingOffsets);
        result = session_->LoadOffsets();
        if (!result) {
            state_.store(SessionState::Failed);
            return Err<void>("LoadOffsets failed: " + result.error());
        }
        state_.store(SessionState::OffsetsLoaded);

        // Initialize
        state_.store(SessionState::Initializing);
        result = session_->Initialize();
        if (!result) {
            state_.store(SessionState::Failed);
            return Err<void>("Initialize failed: " + result.error());
        }

        // Start running
        state_.store(SessionState::Running);
        shouldExit_.store(false);
        
        // Start background threads
        tickThread_ = std::jthread([this](std::stop_token token) { TickLoop(token); });
        if (config_.enableSoftProbe) {
            probeThread_ = std::jthread([this](std::stop_token token) { ProbeLoop(token); });
        }

        // Publish session started event
        /* event bus publish disabled */;

        return Ok();
    }

    // Run the session until it ends (blocks)
    void Run() noexcept {
        if (!session_) return;
        
        while (!shouldExit_.load(std::memory_order_acquire)) {
            // Check if game process is alive
            auto aliveResult = session_->IsGameProcessAlive();
            if (!aliveResult || !aliveResult.value()) {
                shouldExit_.store(true);
                break;
            }

            // Check for external shutdown request
            if (state_.load() == SessionState::Terminating || state_.load() == SessionState::Cleanup) {
                shouldExit_.store(true);
                break;
            }

            std::this_thread::sleep_for(config_.aliveCheckInterval);
        }
    }

    // Request graceful shutdown
    void RequestShutdown() noexcept {
        shouldExit_.store(true);
        if (state_.load() == SessionState::Running) {
            state_.store(SessionState::Terminating);
        }
    }

    // Get current session state
    [[nodiscard]] SessionState GetState() const noexcept {
        return state_.load(std::memory_order_acquire);
    }

    // Get session status text
    [[nodiscard]] std::string_view GetStatus() const noexcept {
        return session_ ? session_->GetStatus() : "No session";
    }

    // Get underlying session (for advanced use)
    [[nodiscard]] IGameSession* GetSession() noexcept {
        return session_.get();
    }
    [[nodiscard]] const IGameSession* GetSession() const noexcept {
        return session_.get();
    }

    // Check if session is running
    [[nodiscard]] bool IsRunning() const noexcept {
        return state_.load() == SessionState::Running;
    }

    // Check if session has failed
    [[nodiscard]] bool HasFailed() const noexcept {
        return state_.load() == SessionState::Failed;
    }

    // Wait for session to end
    void Wait() noexcept {
        if (tickThread_.joinable()) tickThread_.join();
        if (probeThread_.joinable()) probeThread_.join();
    }

private:
    void Shutdown() noexcept {
        shouldExit_.store(true);
        
        if (state_.load() != SessionState::Detached && state_.load() != SessionState::Failed) {
            state_.store(SessionState::Cleanup);
        }

        // Wait for background threads
        Wait();

        // Shutdown session
        if (session_) {
            try {
                session_->Shutdown();
            } catch (...) {
                // Swallow exceptions during shutdown
            }
        }

        // Run custom cleanup
        if (config_.onCleanup) {
            try {
                config_.onCleanup();
            } catch (...) {
                // Swallow exceptions
            }
        }

        // Publish session ended event
        if (!gameId_.empty()) {
            /* event bus publish disabled */;
        }

        state_.store(SessionState::Detached);
    }

    void TickLoop(std::stop_token token) {
        while (!token.stop_requested() && !shouldExit_.load(std::memory_order_acquire)) {
            if (session_ && state_.load() == SessionState::Running) {
                try {
                    session_->Tick();
                } catch (const std::exception& e) {
                    (void)e;
                    /* event bus publish disabled */;
                } catch (...) {
                    /* event bus publish disabled */;
                }
            }
            std::this_thread::sleep_for(config_.tickInterval);
        }
    }

    void ProbeLoop(std::stop_token token) {
        std::uint32_t offsetFailures = 0;
        std::uint32_t processMissFrames = 0;

        while (!token.stop_requested() && !shouldExit_.load(std::memory_order_acquire)) {
            if (!session_ || state_.load() != SessionState::Running) {
                std::this_thread::sleep_for(config_.probeInterval);
                continue;
            }

            // Soft probe offsets
            auto probeResult = session_->SoftProbeOffsets();
            if (probeResult) {
                if (probeResult.value()) {
                    offsetFailures = 0;
                    /* event bus publish disabled */;
                } else if (++offsetFailures >= config_.maxOffsetProbeFailures) {
                    offsetFailures = 0;
                    /* event bus publish disabled */;
                }
            }

            // Check process alive
            auto aliveResult = session_->IsGameProcessAlive();
            if (!aliveResult || !aliveResult.value()) {
                if (++processMissFrames >= config_.maxProcessMissFrames) {
                    terminationReason_ = session_->GetTerminationReason();
                    /* event bus publish disabled */;
                    shouldExit_.store(true);
                    break;
                }
            } else {
                processMissFrames = 0;
            }

            std::this_thread::sleep_for(config_.probeInterval);
        }
    }

    void OnStateChanged(const SessionEvent& event) {
        state_.store(event.newState);
        
        if (event.type == SessionEvent::Type::StateChanged) {
            if (event.newState == SessionState::Running) {
                gameId_ = std::string(session_->GetStatus());
            }
        }
    }

    std::unique_ptr<IGameSession> session_;
    Config config_;
    std::atomic<SessionState> state_{SessionState::Detached};
    std::atomic<bool> shouldExit_{true};
    std::jthread tickThread_;
    std::jthread probeThread_;
    std::string gameId_;
    std::string terminationReason_;
};

// ============================================================
// SessionManager - High-level session lifecycle management
// ============================================================
class SessionManager final {
public:
    using SessionFactory = std::function<std::unique_ptr<IGameSession>(const ServiceContainer&)>;

    SessionManager() = default;
    ~SessionManager() { ShutdownCurrent(); }

    // Register a session factory for a game
    void RegisterFactory(ActiveGame game, SessionFactory factory) {
        std::unique_lock lock(mutex_);
        factories_[game] = std::move(factory);
    }

    // Start a session for a game
    Result<std::unique_ptr<ScopedSession>> StartSession(ActiveGame game, const ServiceContainer& services, ScopedSession::Config config = {}) {
        std::unique_lock lock(mutex_);
        
        auto it = factories_.find(game);
        if (it == factories_.end()) {
            return Err<std::unique_ptr<ScopedSession>>("No factory registered for game");
        }

        // Shutdown any existing session
        if (currentSession_) {
            currentSession_->RequestShutdown();
            currentSession_->Wait();
            currentSession_.reset();
        }

        auto session = it->second(services);
        if (!session) {
            return Err<std::unique_ptr<ScopedSession>>("Factory returned null session");
        }

        auto scoped = std::make_unique<ScopedSession>(std::move(session), std::move(config));
        auto startResult = scoped->Start();
        if (!startResult) {
            return Err<std::unique_ptr<ScopedSession>>(startResult.error());
        }

        currentSession_ = std::move(scoped);
        currentGame_ = game;
        return Ok(std::move(currentSession_));
    }

    // Get current session
    [[nodiscard]] ScopedSession* GetCurrentSession() noexcept {
        return currentSession_.get();
    }
    [[nodiscard]] const ScopedSession* GetCurrentSession() const noexcept {
        return currentSession_.get();
    }

    // Get current game
    [[nodiscard]] std::optional<ActiveGame> GetCurrentGame() const noexcept {
        std::shared_lock lock(mutex_);
        return currentGame_;
    }

    // Check if a session is running
    [[nodiscard]] bool HasActiveSession() const noexcept {
        std::shared_lock lock(mutex_);
        return currentSession_ != nullptr && currentSession_->IsRunning();
    }

    // Request shutdown of current session
    void RequestShutdown() noexcept {
        std::unique_lock lock(mutex_);
        if (currentSession_) {
            currentSession_->RequestShutdown();
        }
    }

    // Shutdown current session
    void ShutdownCurrent() noexcept {
        std::unique_lock lock(mutex_);
        if (currentSession_) {
            currentSession_->RequestShutdown();
            currentSession_->Wait();
            currentSession_.reset();
            currentGame_.reset();
        }
    }

    // Check if game has a factory
    [[nodiscard]] bool HasFactory(ActiveGame game) const noexcept {
        std::shared_lock lock(mutex_);
        return factories_.contains(game);
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<ActiveGame, SessionFactory> factories_;
    std::unique_ptr<ScopedSession> currentSession_;
    std::optional<ActiveGame> currentGame_;
};

} // namespace OmniGhost::Platform