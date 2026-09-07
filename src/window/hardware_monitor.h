#pragma once
#include <string>
#include <chrono>
#include <array>
#include <atomic>
#include <mutex>
#include <vector>

namespace HardwareMonitor {

    enum class DeviceType {
        DMA,
        Makcu,
        KMBoxNet,
        Ferrum,
        Count
    };

    struct DeviceStatus {
        bool connected = false;
        bool enabled = false;
        std::string name;
        std::string port;
        std::string version;
        std::string last_error;
        std::chrono::steady_clock::time_point last_update;
        uint64_t bytes_read = 0;
        uint64_t bytes_written = 0;
        uint64_t read_errors = 0;
        uint64_t write_errors = 0;
        float avg_latency_ms = 0.0f;
        float max_latency_ms = 0.0f;
    };

    struct SystemMetrics {
        float fps = 0.0f;
        float frame_time_ms = 0.0f;
        float dma_read_latency_ms = 0.0f;
        float dma_write_latency_ms = 0.0f;
        int entity_count = 0;
        int player_count = 0;
        int vehicle_count = 0;
        uint64_t memory_reads = 0;
        uint64_t memory_writes = 0;
        uint64_t memory_errors = 0;
        std::chrono::steady_clock::time_point last_update;
    };

    struct LatencySample {
        float latency_ms;
        std::chrono::steady_clock::time_point timestamp;
    };

    void Initialize();
    void Shutdown();
    void Update();

    void RecordDMALatency(float read_ms, float write_ms);
    void RecordEntityCount(int players, int vehicles);
    void RecordMemoryOp(bool is_read, bool success, float latency_ms);
    void RecordFPS(float fps, float frame_time_ms);
    void UpdateDeviceStatus(DeviceType type, bool connected, const std::string& port = "", const std::string& version = "", const std::string& error = "");
    void SetDeviceEnabled(DeviceType type, bool enabled);

    const DeviceStatus& GetDeviceStatus(DeviceType type);
    const SystemMetrics& GetSystemMetrics();
    const std::vector<LatencySample>& GetLatencyHistory();
    const std::vector<float>& GetFPSHistory();
    const std::vector<int>& GetEntityHistory();

    float GetAverageLatency(DeviceType type, int window_seconds = 5);
    float GetMaxLatency(DeviceType type, int window_seconds = 5);
    bool IsDeviceHealthy(DeviceType type);
    std::string GetDeviceHealthString(DeviceType type);

    void ExportMetrics(const char* path);
    void ResetMetrics();

} // namespace HardwareMonitor