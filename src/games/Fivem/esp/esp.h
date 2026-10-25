#pragma once
#include <cstdint>
#include <cstddef>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <array>
#include "math/math.h"
#include "../game/game.h"
#include "ui/ui_models.h"
#include "third_party/ImGui/imgui.h"
#include "../playerInfo/PedData.h"

namespace aimbot { struct Config; }

namespace esp {
    const float MAX_ESP_DISTANCE = 200.0f;



    enum class SkeletonLOD : uint8_t {
        Full = 0,
        High = 1,
        Medium = 2,
        Low = 3
    };


    inline constexpr float LOD_DIST_FULL = 30.0f;
    inline constexpr float LOD_DIST_HIGH = 80.0f;
    inline constexpr float LOD_DIST_MEDIUM = 150.0f;


    uint16_t ComputeSkeletonBoneMask(SkeletonLOD lod);





    SkeletonLOD ComputeSkeletonLOD(float distance,
                                   float fullDist,
                                   float highDist,
                                   float medDist);

    uint16_t ComputeCombinedBoneMask(const std::vector<uintptr_t>& peds,
                                     const std::vector<Vec3>& positions,
                                     const Vec3& localPos,
                                     float fullDist,
                                     float highDist,
                                     float medDist);


    enum class ESPMode {
        HEAD_CIRCLE = 0,
        SKELETON_BONES = 1
    };


    extern ESPMode current_esp_mode;
    extern float line_thickness;

    extern Config config;


    struct BatchSkeletonData {
        uintptr_t ped;
        Matrix bone_matrix;
        std::array<Vector3, 9> bone_offsets{};
        Vec3 origin;
        float health;
        uint16_t bone_mask;
        bool valid;
        std::chrono::steady_clock::time_point pose_sample{};
        std::chrono::steady_clock::time_point origin_sample{};
        uint64_t session_generation = 0;
        uint64_t camera_sample_ms = 0;

        BatchSkeletonData() : ped(0), health(0.0f), bone_mask(0), valid(false) {}
    };


    void DrawDebugLine(const Vec2& from, const Vec2& to, ImU32 color);
    void DrawDebugText(const Vec2& pos, const char* text, ImU32 color);
    void DrawDebugCircle(const Vec2& center, float radius, ImU32 color);


    void draw_skeleton(uintptr_t ped, Matrix viewport, uintptr_t localplayer);
    void DrawWaypointAndBlips(const Matrix& view, uintptr_t localplayer);
    void DrawPlayerRadar(const Matrix& view_matrix, uintptr_t localplayer);
    void DrawActiveFeaturesPanel();
    void draw_skeleton_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data);


    void draw_head_circle(uintptr_t ped, Matrix viewport, uintptr_t localplayer);
    void draw_head_circle_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data);


    void set_esp_mode(ESPMode mode);
    ESPMode get_esp_mode();
    const char* get_esp_mode_name(ESPMode mode);
    std::vector<const char*> get_esp_mode_names();


    void render_esp_for_ped(uintptr_t ped, Matrix viewport, uintptr_t localplayer);
    void render_esp_for_ped_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data);


    void batch_read_skeleton_data(const std::vector<uintptr_t>& peds,
        std::vector<BatchSkeletonData>& out_data, uint16_t bone_mask = 0x01FFu);
    void prepare_skeleton_frame(const std::vector<uintptr_t>& peds,
        const std::vector<Vec3>& origins, uint16_t bone_mask = 0x01FFu);
    void prepare_esp_frame(const std::vector<uintptr_t>& peds, const std::vector<Vec3>& origins,
        const std::vector<float>& coreHealths,
        const Config& frameConfig, const aimbot::Config& aimConfig,
        const std::vector<bool>* visibilityResults = nullptr);
    void PrepareWaypointAndBlips(const Config& frameConfig, const Vec3& localPos);
    void ShutdownWaypointAndBlips();
    void ShutdownPreparedFrames();
    bool has_extra_visuals();
    bool try_get_prepared_bone_position(uintptr_t ped, int bone_index, Vec3& out);
    bool try_get_prepared_origin(uintptr_t ped, Vec3& out);
    bool try_get_prepared_network_id(uintptr_t ped, uint32_t& out);
    bool try_get_prepared_player_name(uintptr_t ped, char* out, size_t outSize);
    bool try_get_prepared_health(uintptr_t ped, float& out);
    bool try_get_prepared_armor(uintptr_t ped, float& out);
    bool try_get_prepared_visibility(uintptr_t ped, bool& visible, bool& known);
    bool try_get_prepared_vehicle(uintptr_t ped, uintptr_t& out);

    void draw_health_info(const Vec2& position, float health);
    void draw_enhanced_health_info(const Vec2& position, float health);


    struct ESPStats {




        std::atomic<uint64_t> cache_combined{0};
        std::atomic<int> memory_reads{0};
        std::atomic<int> batch_reads{0};
        std::chrono::steady_clock::time_point last_reset;

        ESPStats() : last_reset(std::chrono::steady_clock::now()) {}

        void reset() {
            cache_combined.store(0, std::memory_order_relaxed);
            memory_reads.store(0, std::memory_order_relaxed);
            batch_reads.store(0, std::memory_order_relaxed);
            last_reset = std::chrono::steady_clock::now();
        }

        void add_hit() {
            cache_combined.fetch_add(1ull << 32, std::memory_order_relaxed);
        }
        void add_miss() {
            cache_combined.fetch_add(1ull, std::memory_order_relaxed);
        }

        double get_hit_ratio() const {
            const uint64_t combined = cache_combined.load(std::memory_order_relaxed);
            const int hits = static_cast<int>(combined >> 32);
            const int misses = static_cast<int>(combined & 0xFFFFFFFFu);
            const int total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };

    extern ESPStats esp_stats;
}
