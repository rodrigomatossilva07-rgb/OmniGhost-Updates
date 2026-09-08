#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <optional>
#include <mutex>
#include <atomic>

namespace Launcher::Analytics {

enum class EventType : uint8_t {
    AppStart = 0,
    AppExit = 1,
    GameLaunch = 2,
    GameExit = 3,
    FeatureUse = 4,
    SettingChanged = 5,
    Error = 6,
    Crash = 7,
    Performance = 8,
    DeviceConnect = 9,
    DeviceDisconnect = 10,
    DMAAttach = 11,
    DMADetach = 12,
    OffsetLoad = 13,
    OffsetRefresh = 14,
    ConfigLoad = 15,
    ConfigSave = 16,
    ThemeChange = 16,
    LanguageChange = 17,
    WizardStart = 18,
    WizardComplete = 19,
    WizardSkip = 20,
    DeviceTest = 21,
    DMAValidation = 22,
    OffsetLoadEmbedded = 23,
    OffsetLoadFile = 24,
    OffsetRefreshComplete = 25,
    CrashDump = 26,
    UpdateCheck = 27,
    UpdateAvailable = 28,
    UpdateDownload = 29,
    UpdateInstall = 30,
    MarketplaceView = 30,
    MarketplaceSearch = 31,
    MarketplaceDownload = 32,
    MarketplaceInstall = 33,
    MarketplaceRate = 33,
    MarketplaceFavorite = 34,
    WizardStartStep = 35,
    WizardCompleteStep = 36,
    DeviceDetect = 36,
    DeviceTest = 37,
    WizardCompleteWizard = 37
};

struct EventData {
    EventType type;
    std::chrono::system_clock::time_point timestamp;
    std::string gameId;
    std::string sessionId;
    std::unordered_map<std::string, std::string> properties;
    std::unordered_map<std::string, double> metrics;
    std::string errorMessage;
    int severity = 0; // 0=info, 1=warning, 2=error, 3=critical
};

struct SessionInfo {
    std::string sessionId;
    std::chrono::system_clock::time_point startTime;
    std::string version;
    std::string buildId;
    std::string channel;
    std::string osVersion;
    std::string hardwareId;
    bool isFirstRun = false;
    bool hasDMA = false;
    std::string dmaDeviceType;
    std::string inputDeviceType;
};

struct PerformanceMetrics {
    double cpuUsage = 0.0;
    double memoryUsageMB = 0.0;
    double gpuUsage = 0.0;
    double fps = 0.0;
    double frameTimeMs = 0.0;
    double dmaLatencyMs = 0.0;
    double dmaReadLatencyMs = 0.0;
    double dmaWriteLatencyMs = 0.0;
    int activeThreads = 0;
    int openHandles = 0;
    int committedMemoryMB = 0;
};

struct GameSessionData {
    std::string gameId;
    std::chrono::system_clock::time_point launchTime;
    std::chrono::system_clock::time_point exitTime;
    std::string exitReason;
    bool dmaAttached = false;
    std::string dmaDeviceType;
    int offsetLoadTimeMs = 0;
    int attachTimeMs = 0;
    int peakMemoryMB = 0;
    int crashCount = 0;
    std::vector<std::string> errors;
};

class AnalyticsCollector {
public:
    virtual ~AnalyticsCollector() = default;

    virtual void Initialize(const SessionInfo& session) = 0;
    virtual void Shutdown() = 0;
    virtual void TrackEvent(const EventData& event) = 0;
    virtual void TrackPerformance(const PerformanceMetrics& metrics) = 0;
    virtual void TrackGameSession(const GameSessionData& session) = 0;
    virtual void Flush() = 0;
    virtual bool IsEnabled() const = 0;
    virtual void SetEnabled(bool enabled) = 0;
    virtual void SetSamplingRate(double rate) = 0;
    virtual void SetBatchSize(size_t size) = 0;
    virtual void SetFlushInterval(std::chrono::seconds interval) = 0;
};

struct AnalyticsConfig {
    bool enabled = true;
    bool optIn = false;
    double samplingRate = 1.0;
    size_t batchSize = 100;
    std::chrono::seconds flushInterval = std::chrono::seconds(30);
    std::string endpoint;
    std::string apiKey;
    bool trackPerformance = true;
    bool trackErrors = true;
    bool trackCrashes = true;
    bool trackGameSessions = true;
    bool trackPerformanceMetrics = true;
    std::vector<std::string> excludedEvents;
    size_t maxQueueSize = 10000;
    std::chrono::seconds maxEventAge = std::chrono::hours(24);
    bool compressEvents = true;
    bool encryptEvents = false;
};

class LocalAnalyticsStore {
public:
    static LocalAnalyticsStore& Instance();

    void Initialize(const std::string& storePath);
    void Shutdown();

    void WriteEvent(const EventData& event);
    std::vector<EventData> ReadEvents(size_t limit = 1000);
    std::vector<EventData> ReadEventsSince(std::chrono::system_clock::time_point since);
    void DeleteEventsBefore(std::chrono::system_clock::time_point before);
    void Vacuum();

    size_t GetEventCount() const;
    size_t GetStorageSizeBytes() const;
    void SetMaxStorageSize(size_t bytes);
    void SetMaxEventAge(std::chrono::hours hours);

private:
    LocalAnalyticsStore() = default;
    struct StoredEvent {
        uint64_t id;
        EventData event;
    };
    std::vector<StoredEvent> events_;
    std::string dbPath_;
    size_t maxStorageBytes_ = 50 * 1024 * 1024; // 50 MB
    std::chrono::hours maxEventAge_ = std::chrono::hours(24 * 30); // 30 days
    std::mutex mutex_;
    uint64_t nextId_ = 1;
};

class EventQueue {
public:
    EventQueue();
    ~EventQueue();

    void Enqueue(const EventData& event);
    bool Dequeue(EventData& event);
    bool DequeueBatch(std::vector<EventData>& batch, size_t maxSize);
    size_t Size() const;
    void Clear();
    void Shutdown();

private:
    std::queue<EventData> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> shutdown_ = false;
};

class AnalyticsManager {
public:
    static AnalyticsManager& Instance();

    void Initialize(const AnalyticsConfig& config, const SessionInfo& session);
    void Shutdown();

    void TrackEvent(EventType type,
                   const std::string& gameId = "",
                   const std::unordered_map<std::string, std::string>& properties = {},
                   const std::unordered_map<std::string, double>& metrics = {},
                   const std::string& errorMessage = "",
                   int severity = 0);

    void TrackPerformance(const PerformanceMetrics& metrics);
    void TrackGameSession(const GameSessionData& session);
    void TrackFeatureUse(const std::string& feature, const std::string& gameId = "");
    void TrackSettingChange(const std::string& setting, const std::string& oldValue, const std::string& newValue);
    void TrackError(const std::string& error, const std::string& context, int severity = 2);
    void TrackCrash(const std::string& crashInfo, const std::string& context);
    void TrackDMAAttach(const std::string& gameId, const std::string& deviceType, int attachTimeMs, bool success);
    void TrackDMADetach(const std::string& gameId, const std::string& reason);
    void TrackOffsetLoad(const std::string& gameId, const std::string& source, int loadTimeMs, bool success);
    void TrackOffsetRefresh(const std::string& gameId, bool success, int durationMs);
    void TrackDeviceEvent(const std::string& deviceId, const std::string& deviceType, bool connected);
    void TrackWizardStep(WizardStep step, bool completed, bool skipped = false);
    void TrackDeviceTest(int deviceIndex, bool passed, const std::string& error = "");
    void TrackMarketplaceEvent(const std::string& action, const std::string& itemId = "");

    void Flush();
    void FlushAsync();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetSamplingRate(double rate);
    void SetBatchSize(size_t size);
    void SetFlushInterval(std::chrono::seconds interval);

    const SessionInfo& GetSessionInfo() const;
    const AnalyticsConfig& GetConfig() const;

    void SetOptIn(bool optIn);
    bool IsOptIn() const;

    void SetEndpoint(const std::string& endpoint, const std::string& apiKey);
    void SetBatchSize(size_t size);
    void SetFlushInterval(std::chrono::seconds interval);

    void AddExcludedEvent(EventType type);
    void RemoveExcludedEvent(EventType type);

    void ExportEvents(const std::string& filePath, std::chrono::system_clock::time_point since);
    void ImportEvents(const std::string& filePath);

    std::vector<EventData> GetRecentEvents(size_t limit = 100);
    std::vector<GameSessionData> GetGameSessions(size_t limit = 50);

private:
    AnalyticsManager() = default;
    AnalyticsConfig config_;
    SessionInfo session_;
    std::unique_ptr<AnalyticsCollector> collector_;
    std::unique_ptr<LocalAnalyticsStore> store_;
    std::unique_ptr<EventQueue> queue_;
    std::thread workerThread_;
    std::atomic<bool> shutdown_ = false;
    std::mutex configMutex_;
    std::string sessionId_;

    void GenerateSessionId();
    void WorkerLoop();
    void ProcessQueue();
    void SendBatch(const std::vector<EventData>& batch);
    void WriteToStore(const std::vector<EventData>& batch);
    bool ShouldSample(EventType type) const;
    bool IsExcluded(EventType type) const;
};

class LocalAnalyticsCollector : public AnalyticsCollector {
public:
    explicit LocalAnalyticsCollector(LocalAnalyticsStore* store) : store_(store) {}

    void Initialize(const SessionInfo& session) override {
        session_ = session;
    }

    void Shutdown() override {
        // Flush any pending events
    }

    void TrackEvent(const EventData& event) override {
        if (store_) store_->WriteEvent(event);
    }

    void TrackPerformance(const PerformanceMetrics& metrics) override {
        EventData event;
        event.type = EventType::Performance;
        event.timestamp = std::chrono::system_clock::now();
        event.metrics["cpu"] = metrics.cpuUsage;
        event.metrics["memory_mb"] = metrics.memoryUsageMB;
        event.metrics["gpu"] = metrics.gpuUsage;
        event.metrics["fps"] = metrics.fps;
        event.metrics["frame_time"] = metrics.frameTimeMs;
        event.metrics["dma_latency"] = metrics.dmaLatencyMs;
        event.metrics["dma_read_latency"] = metrics.dmaReadLatencyMs;
        event.metrics["dma_write_latency"] = metrics.dmaWriteLatencyMs;
        event.metrics["threads"] = metrics.activeThreads;
        event.metrics["handles"] = metrics.openHandles;
        event.metrics["committed_mb"] = metrics.committedMemoryMB;
        if (store_) store_->WriteEvent(event);
    }

    void TrackGameSession(const GameSessionData& session) override {
        EventData event;
        event.type = EventType::GameExit;
        event.timestamp = session.exitTime;
        event.gameId = session.gameId;
        event.properties["launch_time"] = std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                session.launchTime.time_since_epoch()).count());
        event.properties["exit_time"] = std::to_string(
            std::chrono::duration_cast<std::chrono::seconds>(
                session.exitTime.time_since_epoch()).count());
        event.properties["exit_reason"] = session.exitReason;
        event.properties["dma_attached"] = session.dmaAttached ? "1" : "0";
        event.properties["dma_device"] = session.dmaDeviceType;
        event.metrics["offset_load_ms"] = session.offsetLoadTimeMs;
        event.metrics["attach_ms"] = session.attachTimeMs;
        event.metrics["peak_memory_mb"] = session.peakMemoryMB;
        event.metrics["crash_count"] = session.crashCount;
        for (const auto& error : session.errors) {
            event.properties["error_" + std::to_string(event.properties.size())] = error;
        }
        if (store_) store_->WriteEvent(event);
    }

    void Flush() override {
        // Local store is synchronous
    }

    bool IsEnabled() const override { return true; }
    void SetEnabled(bool enabled) override {}
    void SetSamplingRate(double rate) override {}
    void SetBatchSize(size_t size) override {}
    void SetFlushInterval(std::chrono::seconds interval) override {}

private:
    SessionInfo session_;
    LocalAnalyticsStore* store_ = nullptr;
};

} // namespace Launcher::Analytics