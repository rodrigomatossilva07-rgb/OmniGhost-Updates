#include "analytics.h"
#include "../platform/app_paths.h"
#include "../config/app_settings.h"
#include "../window/localization.h"
#include <Windows.h>
#include <Psapi.h>
#include <fstream>
#include <filesystem>
#include <sqlite3.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

#pragma comment(lib, "Psapi.lib")

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Launcher::Analytics {

namespace {

static size_t CurlWriteCallback(void* contents, size_t size, size_t nmemb, std::string* output) {
    size_t totalSize = size * nmemb;
    output->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string GenerateSessionId() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
    std::stringstream ss;
    ss << std::hex << dis(gen) << std::hex << dis(gen);
    return ss.str();
}

std::string GetHardwareId() {
    // Generate a hardware ID based on system info
    std::string id;
    // CPU info
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    id += std::to_string(sysInfo.dwProcessorType) + "_";
    id += std::to_string(sysInfo.dwNumberOfProcessors) + "_";

    // Volume serial
    DWORD serial = 0;
    GetVolumeInformationA("C:\\", nullptr, 0, &serial, nullptr, nullptr, nullptr, 0);
    id += std::to_string(serial);

    // Hash it
    std::hash<std::string> hasher;
    size_t hash = hasher(id);
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

std::string GetOSVersion() {
    RTL_OSVERSIONINFOEXW osvi = {};
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto RtlGetVersion = reinterpret_cast<NTSTATUS(*)(PRTL_OSVERSIONINFOEXW)>(
            GetProcAddress(ntdll, "RtlGetVersion"));
        if (RtlGetVersion && RtlGetVersion(&osvi) == 0) {
            std::ostringstream ss;
            ss << osvi.dwMajorVersion << "." << osvi.dwMinorVersion << "." << osvi.dwBuildNumber;
            return ss.str();
        }
    }
    return "Windows 10";
}

} // namespace

LocalAnalyticsStore& LocalAnalyticsStore::Instance() {
    static LocalAnalyticsStore instance;
    return instance;
}

void LocalAnalyticsStore::Initialize(const std::string& storePath) {
    dbPath_ = storePath;
    fs::create_directories(fs::path(dbPath_).parent_path());

    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[Analytics] Failed to open database: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            type INTEGER NOT NULL,
            timestamp INTEGER NOT NULL,
            game_id TEXT,
            session_id TEXT,
            properties TEXT,
            metrics TEXT,
            error_message TEXT,
            severity INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp);
        CREATE INDEX IF NOT EXISTS idx_events_type ON events(type);
        CREATE INDEX IF NOT EXISTS idx_events_game ON events(game_id);
    )";

    char* errMsg = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::cerr << "[Analytics] Schema creation failed: " << (errMsg ? errMsg : "unknown") << std::endl;
        sqlite3_free(errMsg);
    }
    sqlite3_close(db);
}

void LocalAnalyticsStore::Shutdown() {
    // Nothing to do, connections are per-operation
}

void LocalAnalyticsStore::WriteEvent(const EventData& event) {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return;

    std::string propertiesJson = "{}";
    std::string metricsJson = "{}";

    // Serialize properties
    nlohmann::json propsJson;
    for (const auto& [k, v] : event.properties) propsJson[k] = v;
    std::string propsStr = propsJson.dump();

    nlohmann::json metricsJson;
    for (const auto& [k, v] : event.metrics) metricsJson[k] = v;
    std::string metricsStr = metricsJson.dump();

    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        event.timestamp.time_since_epoch()).count();

    const char* sql = "INSERT INTO events (type, timestamp, game_id, session_id, properties, metrics, error_message, severity) VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, static_cast<int>(event.type));
        sqlite3_bind_int64(stmt, 2, static_cast<long long>(event.timestamp.time_since_epoch().count()));
        sqlite3_bind_text(stmt, 3, event.gameId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, event.sessionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, propsStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, metricsStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, event.errorMessage.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, event.severity);

        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);

    // Enforce max storage size
    if (maxStorageBytes_ > 0) {
        sqlite3* db = nullptr;
        if (sqlite3_open(dbPath_.c_str(), &db) == SQLITE_OK) {
            // Check size
            sqlite3_stmt* stmt = nullptr;
            sqlite3_prepare_v2(db, "SELECT page_count * page_size FROM pragma_page_count, pragma_page_size", -1, &stmt, nullptr);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                long long size = sqlite3_column_int64(stmt, 0);
                sqlite3_finalize(stmt);
                if (size > static_cast<long long>(maxStorageBytes_)) {
                    // Delete oldest events
                    sqlite3_exec(db, "DELETE FROM events WHERE id IN (SELECT id FROM events ORDER BY timestamp ASC LIMIT 1000)", nullptr, nullptr, nullptr);
                }
            }
            sqlite3_close(db);
        }
    }
}

std::vector<EventData> LocalAnalyticsStore::ReadEvents(size_t limit) {
    std::vector<EventData> events;
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return events;

    std::string sql = "SELECT id, type, timestamp, game_id, session_id, properties, metrics, error_message, severity FROM events ORDER BY timestamp DESC LIMIT ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, static_cast<int>(limit));

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EventData event;
            event.type = static_cast<EventType>(sqlite3_column_int(stmt, 0));
            event.timestamp = std::chrono::system_clock::time_point(
                std::chrono::nanoseconds(sqlite3_column_int64(stmt, 1)));
            event.gameId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) ?: "";
            event.sessionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)) ?: "";
            // Parse properties and metrics JSON
            const char* props = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (props) {
                try {
                    auto j = nlohmann::json::parse(props);
                    for (auto& [k, v] : j.items()) {
                        event.properties[k] = v.get<std::string>();
                    }
                } catch (...) {}
            }
            const char* metrics = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (metrics) {
                try {
                    auto j = nlohmann::json::parse(metrics);
                    for (auto& [k, v] : j.items()) {
                        event.metrics[k] = v.get<double>();
                    }
                } catch (...) {}
            }
            event.errorMessage = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) ?: "";
            event.severity = sqlite3_column_int(stmt, 7);
            events.push_back(std::move(event));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return events;
}

std::vector<EventData> LocalAnalyticsStore::ReadEventsSince(std::chrono::system_clock::time_point since) {
    std::vector<EventData> events;
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return events;

    auto sinceNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        since.time_since_epoch()).count();

    std::string sql = "SELECT id, type, timestamp, game_id, session_id, properties, metrics, error_message, severity FROM events WHERE timestamp > ? ORDER BY timestamp ASC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, static_cast<long long>(sinceNs));

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            EventData event;
            event.type = static_cast<EventType>(sqlite3_column_int(stmt, 0));
            event.timestamp = std::chrono::system_clock::time_point(
                std::chrono::nanoseconds(sqlite3_column_int64(stmt, 1)));
            event.gameId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)) ?: "";
            event.sessionId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)) ?: "";
            const char* props = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            if (props) {
                try {
                    auto j = nlohmann::json::parse(props);
                    for (auto& [k, v] : j.items()) {
                        event.properties[k] = v.get<std::string>();
                    }
                } catch (...) {}
            }
            const char* metrics = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            if (metrics) {
                try {
                    auto j = nlohmann::json::parse(metrics);
                    for (auto& [k, v] : j.items()) {
                        event.metrics[k] = v.get<double>();
                    }
                } catch (...) {}
            }
            event.errorMessage = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6)) ?: "";
            event.severity = sqlite3_column_int(stmt, 7);
            events.push_back(std::move(event));
        }
        sqlite3_finalize(stmt);
    }

    sqlite3_close(db);
    return events;
}

void LocalAnalyticsStore::DeleteEventsBefore(std::chrono::system_clock::time_point before) {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return;

    auto beforeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        before.time_since_epoch()).count();

    const char* sql = "DELETE FROM events WHERE timestamp < ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int64(stmt, 1, static_cast<long long>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                before.time_since_epoch()).count()));
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

void LocalAnalyticsStore::Vacuum() {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) == SQLITE_OK) {
        sqlite3_exec(db, "VACUUM", nullptr, nullptr, nullptr);
        sqlite3_close(db);
    }
}

size_t LocalAnalyticsStore::GetEventCount() const {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return 0;

    int count = 0;
    const char* sql = "SELECT COUNT(*) FROM events";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return count;
}

size_t LocalAnalyticsStore::GetStorageSizeBytes() const {
    sqlite3* db = nullptr;
    if (sqlite3_open(dbPath_.c_str(), &db) != SQLITE_OK) return 0;

    size_t size = 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT page_count * page_size FROM pragma_page_count, pragma_page_size", -1, &stmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            size = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
    return size;
}

void LocalAnalyticsStore::SetMaxStorageSize(size_t bytes) {
    maxStorageBytes_ = bytes;
}

void LocalAnalyticsStore::SetMaxEventAge(std::chrono::hours hours) {
    maxEventAge_ = hours;
}

EventQueue::EventQueue() = default;

EventQueue::~EventQueue() {
    Shutdown();
}

void EventQueue::Enqueue(const EventData& event) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!shutdown_) {
        queue_.push(event);
        cv_.notify_one();
    }
}

bool EventQueue::Dequeue(EventData& event) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || shutdown_; });
    if (shutdown_ && queue_.empty()) return false;
    event = std::move(queue_.front());
    queue_.pop();
    return true;
}

bool EventQueue::DequeueBatch(std::vector<EventData>& batch, size_t maxSize) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty() && shutdown_) return false;

    size_t count = std::min(maxSize, queue_.size());
    batch.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        batch.push_back(std::move(queue_.front()));
        queue_.pop();
    }
    return !batch.empty();
}

size_t EventQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void EventQueue::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!queue_.empty()) queue_.pop();
}

void EventQueue::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
    }
    cv_.notify_all();
}

EventQueue::EventQueue() = default;
EventQueue::~EventQueue() {
    Shutdown();
}

AnalyticsManager& AnalyticsManager::Instance() {
    static AnalyticsManager instance;
    return instance;
}

void AnalyticsManager::Initialize(const AnalyticsConfig& config, const SessionInfo& session) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_ = config;
    session_ = session;

    // Generate session ID
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
    std::stringstream ss;
    ss << std::hex << dis(gen) << std::hex << dis(gen);
    sessionId_ = ss.str();
    session_.sessionId = sessionId_;

    // Initialize store
    fs::path storePath = OmniGhost::Paths::LocalData() / "analytics.db";
    store_ = std::make_unique<LocalAnalyticsStore>();
    store_->Initialize((OmniGhost::Paths::LocalData() / "analytics.db").string());

    // Initialize queue and worker
    queue_ = std::make_unique<EventQueue>();
    shutdown_ = false;
    workerThread_ = std::thread(&AnalyticsManager::WorkerLoop, this);

    std::cout << "[Analytics] Initialized session: " << sessionId_ << std::endl;
}

void AnalyticsManager::Shutdown() {
    shutdown_ = true;
    if (queue_) queue_->Shutdown();
    if (workerThread_.joinable()) workerThread_.join();
    if (queue_) queue_->Shutdown();
    if (store_) store_->Shutdown();
    collector_.reset();
    queue_.reset();
    store_.reset();
    std::cout << "[Analytics] Shutdown complete" << std::endl;
}

void AnalyticsManager::TrackEvent(EventType type,
                                 const std::string& gameId,
                                 const std::unordered_map<std::string, std::string>& properties,
                                 const std::unordered_map<std::string, double>& metrics,
                                 const std::string& errorMessage,
                                 int severity) {
    if (!config_.enabled) return;
    if (IsExcluded(type)) return;
    if (!ShouldSample(type)) return;

    EventData event;
    event.type = type;
    event.timestamp = std::chrono::system_clock::now();
    event.gameId = gameId;
    event.sessionId = sessionId_;
    event.properties = properties;
    event.metrics = metrics;
    event.errorMessage = errorMessage;
    event.severity = severity;

    if (queue_) queue_->Enqueue(event);
}

void AnalyticsManager::TrackPerformance(const PerformanceMetrics& metrics) {
    if (!config_.enabled || !config_.trackPerformanceMetrics) return;

    EventData event;
    event.type = EventType::Performance;
    event.timestamp = std::chrono::system_clock::now();
    event.sessionId = sessionId_;
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

    if (queue_) queue_->Enqueue(EventData{
        EventType::Performance,
        std::chrono::system_clock::now(),
        "", sessionId_, {}, metrics, "", 0
    });
}

void AnalyticsManager::TrackGameSession(const GameSessionData& session) {
    if (!config_.enabled || !config_.trackGameSessions) return;

    EventData event;
    event.type = EventType::GameExit;
    event.timestamp = session.exitTime;
    event.gameId = session.gameId;
    event.sessionId = sessionId_;
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
    for (size_t i = 0; i < session.errors.size(); ++i) {
        event.properties["error_" + std::to_string(i)] = session.errors[i];
    }
    event.metrics["offset_load_ms"] = session.offsetLoadTimeMs;
    event.metrics["attach_ms"] = session.attachTimeMs;
    event.metrics["peak_memory_mb"] = session.peakMemoryMB;
    event.metrics["crash_count"] = session.crashCount;

    if (queue_) queue_->Enqueue(event);
}

void AnalyticsManager::TrackFeatureUse(const std::string& feature, const std::string& gameId) {
    TrackEvent(EventType::FeatureUse, gameId, {{"feature", feature}});
}

void AnalyticsManager::TrackSettingChange(const std::string& setting, const std::string& oldValue, const std::string& newValue) {
    TrackEvent(EventType::SettingChanged, "", {{"setting", setting}, {"old_value", oldValue}, {"new_value", newValue}});
}

void AnalyticsManager::TrackError(const std::string& error, const std::string& context, int severity) {
    if (!config_.trackErrors) return;
    TrackEvent(EventType::Error, "", {{"context", context}}, {}, error, severity);
}

void AnalyticsManager::TrackCrash(const std::string& crashInfo, const std::string& context) {
    if (!config_.trackCrashes) return;
    TrackEvent(EventType::Crash, "", {{"context", context}}, {}, crashInfo, 3);
}

void AnalyticsManager::TrackDMAAttach(const std::string& gameId, const std::string& deviceType, int attachTimeMs, bool success) {
    TrackEvent(EventType::DMAAttach, gameId, {{"device", deviceType}, {"success", success ? "1" : "0"}}, {{"attach_time_ms", static_cast<double>(attachTimeMs)}});
}

void AnalyticsManager::TrackDMADetach(const std::string& gameId, const std::string& reason) {
    TrackEvent(EventType::DMADetach, gameId, {{"reason", reason}});
}

void AnalyticsManager::TrackOffsetLoad(const std::string& gameId, const std::string& source, int loadTimeMs, bool success) {
    TrackEvent(EventType::OffsetLoad, gameId, {{"source", source}, {"success", success ? "1" : "0"}}, {{"load_time_ms", static_cast<double>(loadTimeMs)}});
}

void AnalyticsManager::TrackOffsetRefresh(const std::string& gameId, bool success, int durationMs) {
    TrackEvent(EventType::OffsetRefresh, gameId, {{"success", success ? "1" : "0"}}, {{"duration_ms", static_cast<double>(durationMs)}});
}

void AnalyticsManager::TrackDeviceEvent(const std::string& deviceId, const std::string& deviceType, bool connected) {
    TrackEvent(connected ? EventType::DeviceConnect : EventType::DeviceDisconnect, "", {{"device_id", deviceId}, {"device_type", deviceType}});
}

void AnalyticsManager::TrackWizardStep(WizardStep step, bool completed, bool skipped) {
    TrackEvent(completed ? EventType::WizardCompleteStep : EventType::WizardStartStep, "",
               {{"step", std::to_string(static_cast<int>(step))}, {"skipped", skipped ? "1" : "0"}});
}

void AnalyticsManager::TrackDeviceTest(int deviceIndex, bool passed, const std::string& error) {
    TrackEvent(EventType::DeviceTest, "",
               {{"device_index", std::to_string(deviceIndex)}, {"passed", passed ? "1" : "0"}, {"error", error}});
}

void AnalyticsManager::TrackMarketplaceEvent(const std::string& action, const std::string& itemId) {
    TrackEvent(EventType::MarketplaceView, "", {{"action", action}, {"item_id", itemId}});
}

void AnalyticsManager::Flush() {
    if (queue_) queue_->Shutdown();
    ProcessQueue();
    if (store_) store_->Vacuum();
}

void AnalyticsManager::FlushAsync() {
    // Queue flush is async
}

void AnalyticsManager::SetEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.enabled = enabled;
}

bool AnalyticsManager::IsEnabled() const {
    return config_.enabled;
}

void AnalyticsManager::SetSamplingRate(double rate) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.samplingRate = std::clamp(rate, 0.0, 1.0);
}

void AnalyticsManager::SetBatchSize(size_t size) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.batchSize = size;
}

void AnalyticsManager::SetFlushInterval(std::chrono::seconds interval) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.flushInterval = interval;
}

const SessionInfo& AnalyticsManager::GetSessionInfo() const {
    return session_;
}

const AnalyticsConfig& AnalyticsManager::GetConfig() const {
    return config_;
}

void AnalyticsManager::SetOptIn(bool optIn) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.optIn = optIn;
}

bool AnalyticsManager::IsOptIn() const {
    return config_.optIn;
}

void AnalyticsManager::SetEndpoint(const std::string& endpoint, const std::string& apiKey) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.endpoint = endpoint;
    config_.apiKey = apiKey;
}

void AnalyticsManager::SetBatchSize(size_t size) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.batchSize = size;
}

void AnalyticsManager::SetFlushInterval(std::chrono::seconds interval) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.flushInterval = interval;
}

void AnalyticsManager::AddExcludedEvent(EventType type) {
    std::lock_guard<std::mutex> lock(configMutex_);
    config_.excludedEvents.push_back(std::to_string(static_cast<int>(type)));
}

void AnalyticsManager::RemoveExcludedEvent(EventType type) {
    std::lock_guard<std::mutex> lock(configMutex_);
    auto it = std::find(config_.excludedEvents.begin(), config_.excludedEvents.end(),
                        std::to_string(static_cast<int>(type)));
    if (it != config_.excludedEvents.end()) config_.excludedEvents.erase(it);
}

void AnalyticsManager::ExportEvents(const std::string& filePath, std::chrono::system_clock::time_point since) {
    auto events = store_->ReadEventsSince(since);
    nlohmann::json j;
    for (const auto& event : events) {
        nlohmann::json je;
        je["type"] = static_cast<int>(event.type);
        je["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            event.timestamp.time_since_epoch()).count();
        je["game_id"] = event.gameId;
        je["session_id"] = event.sessionId;
        je["properties"] = event.properties;
        je["metrics"] = event.metrics;
        je["error"] = event.errorMessage;
        je["severity"] = event.severity;
        j.push_back(je);
    }

    std::ofstream file(filePath);
    file << j.dump(2);
}

void AnalyticsManager::ImportEvents(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file) return;

    nlohmann::json j = nlohmann::json::parse(file);
    for (const auto& je : j) {
        EventData event;
        event.type = static_cast<EventType>(je.value("type", 0));
        event.timestamp = std::chrono::system_clock::from_time_t(je.value("timestamp", 0));
        event.gameId = je.value("game_id", "");
        event.sessionId = je.value("session_id", "");
        event.properties = je.value("properties", nlohmann::json::object());
        event.metrics = je.value("metrics", nlohmann::json::object());
        event.errorMessage = je.value("error", "");
        event.severity = je.value("severity", 0);

        if (queue_) queue_->Enqueue(event);
    }
}

std::vector<EventData> AnalyticsManager::GetRecentEvents(size_t limit) {
    return store_->ReadEvents(limit);
}

std::vector<GameSessionData> AnalyticsManager::GetGameSessions(size_t limit) {
    auto events = store_->ReadEvents(limit);
    std::vector<GameSessionData> sessions;
    for (const auto& event : events) {
        if (event.type == EventType::GameExit) {
            GameSessionData session;
            session.gameId = event.gameId;
            session.launchTime = std::chrono::system_clock::from_time_t(
                std::stoll(event.properties["launch_time"]));
            session.exitTime = event.timestamp;
            session.exitReason = event.properties["exit_reason"];
            session.dmaAttached = event.properties["dma_attached"] == "1";
            session.dmaDeviceType = event.properties["dma_device"];
            session.offsetLoadTimeMs = static_cast<int>(event.metrics["offset_load_ms"]);
            session.attachTimeMs = static_cast<int>(event.metrics["attach_ms"]);
            session.peakMemoryMB = static_cast<int>(event.metrics["peak_memory_mb"]);
            session.crashCount = static_cast<int>(event.metrics["crash_count"]);
            for (const auto& [k, v] : event.properties) {
                if (k.rfind("error_", 0) == 0) {
                    session.errors.push_back(v);
                }
            }
            sessions.push_back(std::move(session));
        }
    }
    return sessions;
}

void AnalyticsManager::WorkerLoop() {
    while (!shutdown_) {
        std::vector<EventData> batch;
        if (queue_->DequeueBatch(batch, config_.batchSize)) {
            ProcessQueue();
            std::this_thread::sleep_for(config_.flushInterval);
        } else {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void AnalyticsManager::ProcessQueue() {
    std::vector<EventData> batch;
    if (!queue_->DequeueBatch(batch, config_.batchSize)) return;

    // Write to local store
    if (store_) {
        for (const auto& event : batch) {
            store_->WriteEvent(event);
        }
    }

    // Send to remote endpoint if configured
    if (!config_.endpoint.empty() && !batch.empty()) {
        SendBatch(batch);
    }
}

void AnalyticsManager::SendBatch(const std::vector<EventData>& batch) {
    // TODO: Implement remote sending via HTTP
    // For now, just store locally
}

void AnalyticsManager::WriteToStore(const std::vector<EventData>& batch) {
    if (store_) {
        for (const auto& event : batch) {
            store_->WriteEvent(event);
        }
    }
}

bool AnalyticsManager::ShouldSample(EventType type) const {
    if (config_.samplingRate >= 1.0) return true;
    if (config_.samplingRate <= 0.0) return false;

    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng) < config_.samplingRate;
}

bool AnalyticsManager::IsExcluded(EventType type) const {
    std::string typeStr = std::to_string(static_cast<int>(type));
    return std::find(config_.excludedEvents.begin(), config_.excludedEvents.end(), typeStr) != config_.excludedEvents.end();
}

} // namespace Launcher::Analytics