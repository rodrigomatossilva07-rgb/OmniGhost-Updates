#pragma once

#include "interfaces.h"
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace OmniGhost::Platform {

// ============================================================
// Event Bus - Decoupled communication between subsystems
// ============================================================
class EventBus final {
public:
    // Event types are identified by type_index
    using EventType = std::type_index;
    using Handler = std::function<void(const std::any&)>;

    struct Subscription {
        std::uint64_t id;
        EventType eventType;
        Handler handler;
        bool once = false;
    };

    EventBus() = default;
    ~EventBus() { Clear(); }

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    // Subscribe to an event type
    template <typename Event>
    [[nodiscard]] std::uint64_t Subscribe(std::function<void(const Event&)> handler) {
        static_assert(!std::is_reference_v<Event>, "Event type cannot be a reference");
        const EventType type = std::type_index(typeid(Event));
        std::unique_lock lock(mutex_);
        
        const std::uint64_t id = nextSubscriptionId_++;
        subscriptions_[type].push_back({
            id, type,
            [handler = std::move(handler)](const std::any& event) {
                try {
                    handler(std::any_cast<const Event&>(event));
                } catch (const std::bad_any_cast&) {
                    // Type mismatch - silently ignore
                }
            },
            false
        });
        return id;
    }

    // Subscribe once (auto-unsubscribe after first event)
    template <typename Event>
    [[nodiscard]] std::uint64_t SubscribeOnce(std::function<void(const Event&)> handler) {
        static_assert(!std::is_reference_v<Event>, "Event type cannot be a reference");
        const EventType type = std::type_index(typeid(Event));
        std::unique_lock lock(mutex_);
        
        const std::uint64_t id = nextSubscriptionId_++;
        subscriptions_[type].push_back({
            id, type,
            [handler = std::move(handler)](const std::any& event) {
                try {
                    handler(std::any_cast<const Event&>(event));
                } catch (const std::bad_any_cast&) {
                    // Type mismatch - silently ignore
                }
            },
            true
        });
        return id;
    }

    // Unsubscribe by ID
    void Unsubscribe(std::uint64_t subscriptionId) noexcept {
        std::unique_lock lock(mutex_);
        for (auto& [type, subs] : subscriptions_) {
            auto it = std::remove_if(subs.begin(), subs.end(),
                [subscriptionId](const Subscription& s) { return s.id == subscriptionId; });
            if (it != subs.end()) {
                subs.erase(it, subs.end());
                break;
            }
        }
    }

    // Unsubscribe all handlers for an event type
    template <typename Event>
    void UnsubscribeAll() noexcept {
        const EventType type = std::type_index(typeid(Event));
        std::unique_lock lock(mutex_);
        subscriptions_.erase(type);
    }

    // Publish an event (synchronous - calls handlers immediately)
    template <typename Event>
    void Publish(const Event& event) {
        const EventType type = std::type_index(typeid(Event));
        
        // Copy handlers to avoid deadlock if handler subscribes/unsubscribes
        std::vector<Subscription> handlers;
        {
            std::shared_lock lock(mutex_);
            auto it = subscriptions_.find(type);
            if (it != subscriptions_.end()) {
                handlers = it->second;
            }
        }

        for (auto& sub : handlers) {
            try {
                sub.handler(event);
            } catch (...) {
                // Swallow exceptions from handlers to not crash the bus
            }
        }

        // Remove one-time subscriptions
        if (!handlers.empty()) {
            std::unique_lock lock(mutex_);
            auto it = subscriptions_.find(type);
            if (it != subscriptions_.end()) {
                auto& subs = it->second;
                subs.erase(std::remove_if(subs.begin(), subs.end(),
                    [](const Subscription& s) { return s.once; }), subs.end());
            }
        }
    }

    // Publish event by value (move)
    template <typename Event>
    void Publish(Event&& event) {
        const EventType type = std::type_index(typeid(Event));
        
        std::vector<Subscription> handlers;
        {
            std::shared_lock lock(mutex_);
            auto it = subscriptions_.find(type);
            if (it != subscriptions_.end()) {
                handlers = it->second;
            }
        }

        for (auto& sub : handlers) {
            try {
                sub.handler(event);
            } catch (...) {
                // Swallow exceptions
            }
        }

        if (!handlers.empty()) {
            std::unique_lock lock(mutex_);
            auto it = subscriptions_.find(type);
            if (it != subscriptions_.end()) {
                auto& subs = it->second;
                subs.erase(std::remove_if(subs.begin(), subs.end(),
                    [](const Subscription& s) { return s.once; }), subs.end());
            }
        }
    }

    // Queue event for async processing (processed on dedicated thread)
    template <typename Event>
    void Post(Event&& event) {
        std::unique_lock lock(queueMutex_);
        eventQueue_.emplace(std::make_any<Event>(std::forward<Event>(event)));
        queueCond_.notify_one();
    }

    // Start async event processing thread
    void StartAsyncProcessing() {
        if (asyncRunning_.load(std::memory_order_acquire)) return;
        asyncRunning_.store(true, std::memory_order_release);
        asyncThread_ = std::thread([this] { AsyncProcessingLoop(); });
    }

    // Stop async event processing
    void StopAsyncProcessing() {
        if (!asyncRunning_.load(std::memory_order_acquire)) return;
        asyncRunning_.store(false, std::memory_order_release);
        {
            std::unique_lock lock(queueMutex_);
            queueCond_.notify_all();
        }
        if (asyncThread_.joinable()) {
            asyncThread_.join();
        }
    }

    // Get subscription count for debugging
    [[nodiscard]] std::size_t GetSubscriptionCount() const noexcept {
        std::shared_lock lock(mutex_);
        std::size_t count = 0;
        for (const auto& [_, subs] : subscriptions_) {
            count += subs.size();
        }
        return count;
    }

    // Clear all subscriptions
    void Clear() noexcept {
        std::unique_lock lock(mutex_);
        subscriptions_.clear();
    }

private:
    void AsyncProcessingLoop() {
        while (asyncRunning_.load(std::memory_order_acquire)) {
            std::any event;
            {
                std::unique_lock lock(queueMutex_);
                queueCond_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                    return !eventQueue_.empty() || !asyncRunning_.load(std::memory_order_acquire);
                });
                if (!asyncRunning_.load(std::memory_order_acquire)) break;
                if (eventQueue_.empty()) continue;
                event = std::move(eventQueue_.front());
                eventQueue_.pop();
            }

            if (event.has_value()) {
                const EventType type = event.type();
                std::vector<Subscription> handlers;
                {
                    std::shared_lock lock(mutex_);
                    auto it = subscriptions_.find(type);
                    if (it != subscriptions_.end()) {
                        handlers = it->second;
                    }
                }

                for (auto& sub : handlers) {
                    try {
                        sub.handler(event);
                    } catch (...) {
                        // Swallow exceptions
                    }
                }

                if (!handlers.empty()) {
                    std::unique_lock lock(mutex_);
                    auto it = subscriptions_.find(type);
                    if (it != subscriptions_.end()) {
                        auto& subs = it->second;
                        subs.erase(std::remove_if(subs.begin(), subs.end(),
                            [](const Subscription& s) { return s.once; }), subs.end());
                    }
                }
            }
        }
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<EventType, std::vector<Subscription>> subscriptions_;
    std::atomic<std::uint64_t> nextSubscriptionId_{1};

    std::atomic<bool> asyncRunning_{false};
    std::thread asyncThread_;
    std::mutex queueMutex_;
    std::condition_variable queueCond_;
    std::queue<std::any> eventQueue_;
};

// ============================================================
// Common Event Types
// ============================================================

// Session lifecycle events
struct SessionStartedEvent {
    std::string gameId;
    std::chrono::steady_clock::time_point timestamp;
};

struct SessionEndedEvent {
    std::string gameId;
    std::string reason;
    std::chrono::steady_clock::time_point timestamp;
};

struct SessionErrorEvent {
    std::string gameId;
    std::string error;
    std::chrono::steady_clock::time_point timestamp;
};

// Hardware events
struct HardwareConnectedEvent {
    std::string deviceType;
    std::string identifier;
    std::chrono::steady_clock::time_point timestamp;
};

struct HardwareDisconnectedEvent {
    std::string deviceType;
    std::string identifier;
    std::string reason;
    std::chrono::steady_clock::time_point timestamp;
};

struct HardwareErrorEvent {
    std::string deviceType;
    std::string error;
    std::chrono::steady_clock::time_point timestamp;
};

// Offset events
struct OffsetsLoadedEvent {
    std::string gameId;
    std::string source;  // "embedded", "json", "auto"
    std::chrono::steady_clock::time_point timestamp;
};

struct OffsetsRefreshRequestedEvent {
    std::string gameId;
    std::chrono::steady_clock::time_point timestamp;
};

struct OffsetsRefreshedEvent {
    std::string gameId;
    bool success;
    std::string details;
    std::chrono::steady_clock::time_point timestamp;
};

struct OffoutsValidationFailedEvent {
    std::string gameId;
    int consecutiveFailures;
    std::chrono::steady_clock::time_point timestamp;
};

// Configuration events
struct ConfigChangedEvent {
    std::string configPath;
    std::string key;
    std::string oldValue;
    std::string newValue;
    std::chrono::steady_clock::time_point timestamp;
};

struct ConfigSavedEvent {
    std::string configPath;
    std::chrono::steady_clock::time_point timestamp;
};

struct ConfigLoadedEvent {
    std::string configPath;
    std::chrono::steady_clock::time_point timestamp;
};

// UI events
struct UIStateChangedEvent {
    std::string component;
    std::string state;
    std::chrono::steady_clock::time_point timestamp;
};

struct NotificationEvent {
    enum class Type { Info, Success, Warning, Error } type;
    std::string message;
    float durationSeconds = 3.0f;
    std::chrono::steady_clock::time_point timestamp;
};

// Performance events
struct FrameStatsEvent {
    float frameTimeMs;
    float cpuTimeMs;
    float gpuTimeMs;
    std::uint32_t entityCount;
    std::chrono::steady_clock::time_point timestamp;
};

struct DmaStatsEvent {
    std::uint64_t readsPerSecond;
    std::uint64_t bytesPerSecond;
    float avgLatencyMs;
    std::chrono::steady_clock::time_point timestamp;
};

// Input events
struct InputDeviceChangedEvent {
    std::string deviceType;
    bool connected;
    std::chrono::steady_clock::time_point timestamp;
};

struct KeyBindEvent {
    int virtualKey;
    bool down;
    std::chrono::steady_clock::time_point timestamp;
};

// Global accessor
inline EventBus& GetEventBus() noexcept {
    static EventBus instance;
    return instance;
}

// Helper macros for event publishing
#define PUBLISH_EVENT(event) OmniGhost::Platform::GetEventBus().Publish(event)
#define POST_EVENT(event) OmniGhost::Platform::GetEventBus().Post(event)

} // namespace OmniGhost::Platform