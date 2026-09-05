#include "timeout_manager.h"
#include "session_log.h"

namespace OmniGhost::Platform {

TimeoutManager::TimeoutManager(const Config& config) : config_(config) {}

Result<void> TimeoutManager::RunWithTimeout(
    const std::string& operationName,
    std::chrono::milliseconds timeout,
    std::function<Result<void>()> operation,
    std::shared_ptr<std::atomic<bool>> cancellationToken
) {
    std::promise<Result<void>> promise;
    auto future = promise.get_future();

    std::thread worker([&, operation = std::move(operation), cancellationToken = std::move(cancellationToken)]() mutable {
        try {
            auto result = operation();
            promise.set_value(std::move(result));
        } catch (const std::exception& e) {
            promise.set_value(Err<void>(std::string("Exception: ") + e.what()));
        } catch (...) {
            promise.set_value(Err<void>("Unknown exception"));
        }
    });

    std::chrono::milliseconds actualTimeout = timeout.count() > 0 ? timeout : GetDefaultTimeout(operationName);
    std::future_status status = future.wait_for(actualTimeout);

    if (status == std::future_status::timeout) {
        if (cancellationToken) cancellationToken->store(true, std::memory_order_release);
        LogTimeout(operationName, actualTimeout);
        return Err<void>(ErrorCode::Timeout, "Operation timed out: " + operationName);
    }

    auto result = future.get();
    if (result.IsErr()) LogError(operationName, result.UnwrapErr().message);
    return result;
}

std::chrono::milliseconds TimeoutManager::GetDefaultTimeout(const std::string& operationName) const noexcept {
    static const std::unordered_map<std::string, std::chrono::milliseconds> defaults = {
        {"dma_device_open", config_.dmaDeviceOpen},
        {"dma_data_path", config_.dmaDeviceDataPath},
        {"vmm_init", config_.vmmInitialization},
        {"vmm_recovery", config_.vmmSessionRecovery},
        {"plugin_init", config_.pluginInitialization},
        {"process_attach", config_.processAttach},
        {"procinfo_generation", config_.processInfoGeneration},
        {"procinfo_zero_stall", config_.processInfoZeroStall},
        {"procinfo_heartbeat", config_.processInfoHeartbeat},
        {"procinfo_poll", config_.processInfoPoll},
        {"procinfo_settle", config_.processInfoSettle},
        {"dma_operation", config_.dmaOperationTimeout},
        {"network_request", config_.networkRequest},
        {"file_operation", config_.fileOperation},
        {"authentication", config_.authentication},
    };
    auto it = defaults.find(operationName);
    return it != defaults.end() ? it->second : std::chrono::milliseconds(30000);
}

void TimeoutManager::LogTimeout(const std::string& operation, std::chrono::milliseconds timeout) {
    SessionLog::Write(SessionLog::Severity::Warning, SessionLog::Subsystem::Core,
        "Operation timeout", {{"operation", operation}, {"timeout_ms", std::to_string(timeout.count())}});
}

void TimeoutManager::LogError(const std::string& operation, const std::string& error) {
    SessionLog::Write(SessionLog::Severity::Error, SessionLog::Subsystem::Core,
        "Operation failed", {{"operation", operation}, {"error", error}});
}

CancellationToken::CancellationToken() = default;
CancellationToken::CancellationToken(std::shared_ptr<std::atomic<bool>> token) : token_(std::move(token)) {}
CancellationToken::CancellationToken(const CancellationToken&) = default;
CancellationToken& CancellationToken::operator=(const CancellationToken&) = default;
CancellationToken::CancellationToken(CancellationToken&&) = default;
CancellationToken& CancellationToken::operator=(CancellationToken&&) = default;

[[nodiscard]] bool CancellationToken::IsCancelled() const noexcept {
    return token_ && token_->load(std::memory_order_acquire);
}
void CancellationToken::Cancel() noexcept { if (token_) token_->store(true, std::memory_order_release); }
void CancellationToken::Reset() noexcept { if (token_) token_->store(false, std::memory_order_release); }
CancellationToken::operator bool() const noexcept { return static_cast<bool>(token_); }
CancellationToken CancellationToken::Create() { return CancellationToken(std::make_shared<std::atomic<bool>>(false)); }
CancellationToken CancellationToken::None() { return CancellationToken(); }

Watchdog::Watchdog() = default;
Watchdog::~Watchdog() { Stop(); }

void Watchdog::Start(const std::string& operation, std::chrono::milliseconds timeout, std::function<void(const std::string&)> onTimeout) {
    std::lock_guard lock(mutex_);
    WatchInfo info;
    info.operation = operation;
    info.startTime = std::chrono::steady_clock::now();
    info.timeout = timeout;
    info.threadId = std::this_thread::get_id();
    info.onTimeout = std::move(onTimeout);
    watches_.push_back(std::move(info));

    if (!running_) {
        running_ = true;
        watcher_ = std::thread(&Watchdog::WatchLoop, this);
    }
}

void Watchdog::Stop() {
    { std::lock_guard lock(mutex_); running_ = false; cv_.notify_all(); }
    if (watcher_.joinable()) watcher_.join();
}

void Watchdog::CheckIn(const std::string& operation) {
    std::lock_guard lock(mutex_);
    for (auto& w : watches_) if (w.operation == operation) { w.startTime = std::chrono::steady_clock::now(); break; }
}
void Watchdog::Remove(const std::string& operation) {
    std::lock_guard lock(mutex_);
    watches_.erase(std::remove_if(watches_.begin(), watches_.end(),
        [&](const WatchInfo& w) { return w.operation == operation; }), watches_.end());
}

void Watchdog::WatchLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::vector<WatchInfo> timedOut;
        { std::lock_guard lock(mutex_);
            auto now = std::chrono::steady_clock::now();
            for (auto& w : watches_) if (now - w.startTime > w.timeout) timedOut.push_back(w);
        }
        for (const auto& w : timedOut) {
            SessionLog::Write(SessionLog::Severity::Warning, SessionLog::Subsystem::Core,
                "Watchdog timeout", {{"operation", w.operation}, {"timeout_ms", std::to_string(w.timeout.count())}});
            if (w.onTimeout) w.onTimeout(w.operation);
            Remove(w.operation);
        }
        std::unique_lock lock(mutex_);
        cv_.wait_for(lock, std::chrono::milliseconds(100), [this] { return !running_; });
    }
}

CancellationToken::CancellationToken() = default;
CancellationToken::CancellationToken(std::shared_ptr<std::atomic<bool>> token) : token_(std::move(token)) {}
bool CancellationToken::IsCancelled() const noexcept { return token_ && token_->load(std::memory_order_acquire); }
void CancellationToken::Cancel() noexcept { if (token_) token_->store(true, std::memory_order_release); }
void CancellationToken::Reset() noexcept { if (token_) token_->store(false, std::memory_order_release); }
CancellationToken::operator bool() const noexcept { return static_cast<bool>(token_); }
CancellationToken CancellationToken::Create() { return CancellationToken(std::make_shared<std::atomic<bool>>(false)); }
CancellationToken CancellationToken::None() { return CancellationToken(); }

} // namespace OmniGhost::Platform