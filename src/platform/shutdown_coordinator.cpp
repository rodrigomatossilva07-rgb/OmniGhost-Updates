#include "shutdown_coordinator.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace OmniGhost::Platform {

const char* ShutdownComponentName(ShutdownComponent component) noexcept {
    switch (component) {
    case ShutdownComponent::Adapter: return "adapter";
    case ShutdownComponent::Radar: return "radar";
    case ShutdownComponent::Updater: return "updater";
    case ShutdownComponent::Changelog: return "changelog";
    case ShutdownComponent::Hardware: return "hardware";
    case ShutdownComponent::Input: return "input";
    case ShutdownComponent::Ui: return "ui";
    }
    return "unknown";
}

bool ShutdownCoordinator::Register(ShutdownComponent component, int priority,
                                   Callback callback) {
    if (!callback)
        return false;
    std::scoped_lock lock(mutex_);
    if (state_ != ShutdownState::Running)
        return false;
    const auto existing = std::find_if(registrations_.begin(), registrations_.end(),
        [component](const Registration& value) { return value.component == component; });
    if (existing != registrations_.end()) {
        existing->priority = priority;
        existing->callback = std::move(callback);
    } else {
        registrations_.push_back({component, priority, std::move(callback)});
    }
    return true;
}

ShutdownState ShutdownCoordinator::State() const noexcept {
    std::scoped_lock lock(mutex_);
    return state_;
}

bool ShutdownCoordinator::ShutdownAll(std::string_view reason) noexcept {
    std::vector<Registration> callbacks;
    const std::string reasonCopy(reason);
    {
        std::unique_lock lock(mutex_);
        // Idempotent: if already complete or shutting down, wait for completion
        if (state_ == ShutdownState::Complete)
            return true;
        if (state_ == ShutdownState::ShuttingDown) {
            // Wait for the in-progress shutdown to complete
            complete_.wait(lock, [this] { return state_ == ShutdownState::Complete; });
            return true;
        }
        state_ = ShutdownState::ShutdownRequested;
        if (sessionActive_ && sessionCallback_) {
            callbacks.push_back({ShutdownComponent::Adapter, 1000,
                                 std::move(sessionCallback_)});
            sessionActive_ = false;
        }
        for (Registration& registration : registrations_)
            callbacks.push_back({registration.component, registration.priority,
                                 std::move(registration.callback)});
        registrations_.clear();
        state_ = ShutdownState::ShuttingDown;
    }

    std::stable_sort(callbacks.begin(), callbacks.end(),
        [](const Registration& left, const Registration& right) {
            return left.priority > right.priority;
        });
    std::clog << "[SHUTDOWN] scope=process state=RUNNING reason=" << reasonCopy << '\n';
    bool success = true;
    for (Registration& registration : callbacks) {
        std::clog << "[SHUTDOWN] component="
                  << ShutdownComponentName(registration.component)
                  << " state=RUNNING\n";
        try {
            if (registration.callback)
                registration.callback();
            std::clog << "[SHUTDOWN] component="
                      << ShutdownComponentName(registration.component)
                      << " state=COMPLETE\n";
        } catch (...) {
            success = false;
            std::cerr << "[SHUTDOWN] component="
                      << ShutdownComponentName(registration.component)
                      << " state=FAILED exception=UNKNOWN\n";
        }
    }
    {
        std::scoped_lock lock(mutex_);
        state_ = ShutdownState::Complete;
    }
    complete_.notify_all();
    std::clog << "[SHUTDOWN] scope=process state=COMPLETE result="
              << (success ? "PASS" : "PARTIAL") << '\n';
    return success;
}

std::uint64_t ShutdownCoordinator::BeginSession(Callback callback) {
    std::scoped_lock lock(mutex_);
    if (state_ != ShutdownState::Running)
        throw std::logic_error("Cannot start a session after shutdown was requested.");
    if (sessionActive_)
        throw std::logic_error("A shutdown session is already active.");
    ++generation_;
    sessionActive_ = true;
    sessionCallback_ = std::move(callback);
    std::clog << "[SHUTDOWN] session_generation=" << generation_ << " state=ARMED\n";
    return generation_;
}

bool ShutdownCoordinator::EndSession(std::uint64_t generation,
                                     std::string_view reason) noexcept {
    Callback callback;
    {
        std::scoped_lock lock(mutex_);
        if (!sessionActive_ || generation != generation_)
            return false;
        sessionActive_ = false;
        callback = std::move(sessionCallback_);
    }
    std::clog << "[SHUTDOWN] session_generation=" << generation
              << " state=RUNNING reason=" << reason << '\n';
    try {
        if (callback)
            callback();
    } catch (...) {
        std::cerr << "[SHUTDOWN] session_generation=" << generation
                  << " state=FAILED exception=UNKNOWN\n";
        return false;
    }
    std::clog << "[SHUTDOWN] session_generation=" << generation
              << " state=COMPLETE\n";
    return true;
}

void ShutdownCoordinator::CancelSessionRegistration(std::uint64_t generation) noexcept {
    std::scoped_lock lock(mutex_);
    if (sessionActive_ && generation == generation_) {
        sessionActive_ = false;
        sessionCallback_ = {};
    }
}

} // namespace OmniGhost::Platform
