#include "hardware_monitor.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>

namespace HardwareMonitor {

    namespace {
        constexpr int MAX_HISTORY_SAMPLES = 600; // 10 minutes at 10Hz
        constexpr int MAX_LATENCY_SAMPLES = 300; // 5 minutes at 1Hz

        std::array<DeviceStatus, static_cast<int>(DeviceType::Count)> g_devices;
        SystemMetrics g_metrics;
        std::vector<LatencySample> g_latency_history;
        std::vector<float> g_fps_history;
        std::vector<int> g_entity_history;
        std::mutex g_mutex;
        bool g_initialized = false;
        std::chrono::steady_clock::time_point g_start_time;

        void InitDevice(int idx, const char* name) {
            g_devices[idx].name = name;
            g_devices[idx].connected = false;
            g_devices[idx].enabled = false;
            g_devices[idx].last_update = std::chrono::steady_clock::now();
        }
    }

    void Initialize() {
        if (g_initialized) return;
        
        InitDevice(static_cast<int>(DeviceType::DMA), "DMA");
        InitDevice(static_cast<int>(DeviceType::Makcu), "Makcu");
        InitDevice(static_cast<int>(DeviceType::KMBoxNet), "KMBox-Net");
        InitDevice(static_cast<int>(DeviceType::Ferrum), "Ferrum");
        
        g_metrics = {};
        g_metrics.last_update = std::chrono::steady_clock::now();
        g_start_time = std::chrono::steady_clock::now();
        
        g_latency_history.reserve(MAX_LATENCY_SAMPLES);
        g_fps_history.reserve(MAX_HISTORY_SAMPLES);
        g_entity_history.reserve(MAX_HISTORY_SAMPLES);
        
        g_initialized = true;
    }

    void Shutdown() {
        g_initialized = false;
    }

    void Update() {
        if (!g_initialized) return;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        auto now = std::chrono::steady_clock::now();
        g_metrics.last_update = now;
        
        // Trim history to max size
        if (g_fps_history.size() > MAX_HISTORY_SAMPLES) {
            g_fps_history.erase(g_fps_history.begin(), g_fps_history.end() - MAX_HISTORY_SAMPLES);
        }
        if (g_entity_history.size() > MAX_HISTORY_SAMPLES) {
            g_entity_history.erase(g_entity_history.begin(), g_entity_history.end() - MAX_HISTORY_SAMPLES);
        }
        if (g_latency_history.size() > MAX_LATENCY_SAMPLES) {
            g_latency_history.erase(g_latency_history.begin(), g_latency_history.end() - MAX_LATENCY_SAMPLES);
        }
    }

    void RecordDMALatency(float read_ms, float write_ms) {
        if (!g_initialized) return;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        g_metrics.dma_read_latency_ms = read_ms;
        g_metrics.dma_write_latency_ms = write_ms;
        
        LatencySample sample;
        sample.latency_ms = read_ms;
        sample.timestamp = std::chrono::steady_clock::now();
        g_latency_history.push_back(sample);
        
        if (g_latency_history.size() > MAX_LATENCY_SAMPLES) {
            g_latency_history.erase(g_latency_history.begin());
        }
    }

    void RecordEntityCount(int players, int vehicles) {
        if (!g_initialized) return;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        g_metrics.player_count = players;
        g_metrics.vehicle_count = vehicles;
        g_metrics.entity_count = players + vehicles;
        g_entity_history.push_back(g_metrics.entity_count);
        
        if (g_entity_history.size() > MAX_HISTORY_SAMPLES) {
            g_entity_history.erase(g_entity_history.begin());
        }
    }

    void RecordMemoryOp(bool is_read, bool success, float /*latency_ms*/) {
        if (!g_initialized) return;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        if (is_read) {
            g_metrics.memory_reads++;
            if (!success) g_metrics.memory_errors++;
        } else {
            g_metrics.memory_writes++;
            if (!success) g_metrics.memory_errors++;
        }
    }

    void RecordFPS(float fps, float frame_time_ms) {
        if (!g_initialized) return;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        g_metrics.fps = fps;
        g_metrics.frame_time_ms = frame_time_ms;
        g_fps_history.push_back(fps);
        
        if (g_fps_history.size() > MAX_HISTORY_SAMPLES) {
            g_fps_history.erase(g_fps_history.begin());
        }
    }

    void UpdateDeviceStatus(DeviceType type, bool connected, const std::string& port, const std::string& version, const std::string& error) {
        if (!g_initialized) return;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        int idx = static_cast<int>(type);
        if (idx >= 0 && idx < static_cast<int>(DeviceType::Count)) {
            g_devices[idx].connected = connected;
            g_devices[idx].last_update = std::chrono::steady_clock::now();
            if (!port.empty()) g_devices[idx].port = port;
            if (!version.empty()) g_devices[idx].version = version;
            if (!error.empty()) g_devices[idx].last_error = error;
        }
    }

    void SetDeviceEnabled(DeviceType type, bool enabled) {
        if (!g_initialized) return;
        
        std::lock_guard<std::mutex> lock(g_mutex);
        int idx = static_cast<int>(type);
        if (idx >= 0 && idx < static_cast<int>(DeviceType::Count)) {
            g_devices[idx].enabled = enabled;
        }
    }

    const DeviceStatus& GetDeviceStatus(DeviceType type) {
        static DeviceStatus empty;
        int idx = static_cast<int>(type);
        if (idx >= 0 && idx < static_cast<int>(DeviceType::Count)) {
            return g_devices[idx];
        }
        return empty;
    }

    const SystemMetrics& GetSystemMetrics() {
        return g_metrics;
    }

    const std::vector<LatencySample>& GetLatencyHistory() {
        return g_latency_history;
    }

    const std::vector<float>& GetFPSHistory() {
        return g_fps_history;
    }

    const std::vector<int>& GetEntityHistory() {
        return g_entity_history;
    }

    float GetAverageLatency(DeviceType /*type*/, int window_seconds) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - std::chrono::seconds(window_seconds);
        
        float sum = 0.0f;
        int count = 0;
        for (const auto& sample : g_latency_history) {
            if (sample.timestamp >= cutoff) {
                sum += sample.latency_ms;
                count++;
            }
        }
        return count > 0 ? sum / count : 0.0f;
    }

    float GetMaxLatency(DeviceType /*type*/, int window_seconds) {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto now = std::chrono::steady_clock::now();
        auto cutoff = now - std::chrono::seconds(window_seconds);
        
        float max_lat = 0.0f;
        for (const auto& sample : g_latency_history) {
            if (sample.timestamp >= cutoff) {
                max_lat = std::max(max_lat, sample.latency_ms);
            }
        }
        return max_lat;
    }

    bool IsDeviceHealthy(DeviceType type) {
        const auto& status = GetDeviceStatus(type);
        if (!status.enabled) return true; // Not enabled = not a problem
        if (!status.connected) return false;
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - status.last_update).count();
        return elapsed < 10; // Consider stale after 10 seconds
    }

    std::string GetDeviceHealthString(DeviceType type) {
        const auto& status = GetDeviceStatus(type);
        if (!status.enabled) return "Disabled";
        if (!status.connected) return "Disconnected";
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - status.last_update).count();
        if (elapsed > 10) return "Stale";
        
        float avg_lat = GetAverageLatency(type, 5);
        if (avg_lat > 50.0f) return "High Latency";
        if (avg_lat > 20.0f) return "Elevated Latency";
        return "Healthy";
    }

    void ExportMetrics(const char* path) {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::ofstream file(path);
        if (!file) return;
        
        file << "{\n";
        file << "  \"timestamp\": \"" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\",\n";
        file << "  \"metrics\": {\n";
        file << "    \"fps\": " << g_metrics.fps << ",\n";
        file << "    \"frame_time_ms\": " << g_metrics.frame_time_ms << ",\n";
        file << "    \"dma_read_latency_ms\": " << g_metrics.dma_read_latency_ms << ",\n";
        file << "    \"dma_write_latency_ms\": " << g_metrics.dma_write_latency_ms << ",\n";
        file << "    \"entity_count\": " << g_metrics.entity_count << ",\n";
        file << "    \"player_count\": " << g_metrics.player_count << ",\n";
        file << "    \"vehicle_count\": " << g_metrics.vehicle_count << ",\n";
        file << "    \"memory_reads\": " << g_metrics.memory_reads << ",\n";
        file << "    \"memory_writes\": " << g_metrics.memory_writes << ",\n";
        file << "    \"memory_errors\": " << g_metrics.memory_errors << "\n";
        file << "  },\n";
        file << "  \"devices\": [\n";
        for (int i = 0; i < static_cast<int>(DeviceType::Count); ++i) {
            const auto& d = g_devices[i];
            file << "    {\n";
            file << "      \"name\": \"" << d.name << "\",\n";
            file << "      \"connected\": " << (d.connected ? "true" : "false") << ",\n";
            file << "      \"enabled\": " << (d.enabled ? "true" : "false") << ",\n";
            file << "      \"port\": \"" << d.port << "\",\n";
            file << "      \"version\": \"" << d.version << "\",\n";
            file << "      \"last_error\": \"" << d.last_error << "\",\n";
            file << "      \"avg_latency_ms\": " << GetAverageLatency(static_cast<DeviceType>(i)) << ",\n";
            file << "      \"max_latency_ms\": " << GetMaxLatency(static_cast<DeviceType>(i)) << "\n";
            file << "    }" << (i < static_cast<int>(DeviceType::Count) - 1 ? "," : "") << "\n";
        }
        file << "  ]\n";
        file << "}\n";
    }

    void ResetMetrics() {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_metrics = {};
        g_metrics.last_update = std::chrono::steady_clock::now();
        g_latency_history.clear();
        g_fps_history.clear();
        g_entity_history.clear();
    }

} // namespace HardwareMonitor