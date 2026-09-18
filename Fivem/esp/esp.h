#pragma once
#include <cstdint>
#include <unordered_map>
#include <chrono>
#include <atomic>
#include <array>
#include "math/math.h"
#include "../game/game.h"
#include "ui/ui_models.h"
#include "../../ImGui/imgui.h"
#include "../playerInfo/PedData.h"

namespace esp {
    // Bone chain (cheatoffsets): ped + bone_matrix → +skel_o1 → matrices @ stride, trans @ +0xC
    const uintptr_t BONE_MATRIX_OFFSET = 0x60;   // CEntity visual matrix (fallback)
    const uintptr_t BONE_FRAG_3258     = 0xFF8;  // frag_inst / bone_matrix on 3258
    const uintptr_t BONE_FRAG_3751     = 0x12B8; // 3751+
    const uintptr_t BONE_SKEL_O1       = 0x78;
    const uintptr_t BONE_SKEL_STRIDE   = 0x40;
    const uintptr_t BONE_SKEL_TRANS    = 0xC;
    const uintptr_t BONE_ARRAY_BASE    = 0x410;  // legacy local array
    const uintptr_t BONE_SIZE          = 0x10;
    const float MAX_ESP_DISTANCE = 200.0f;

    // ESP Mode Configuration - Simple two modes
    enum class ESPMode {
        HEAD_CIRCLE = 0,  // Your existing head circle ESP
        SKELETON_BONES = 1 // Your existing skeleton bones ESP
    };

    // Global ESP configuration
    extern ESPMode current_esp_mode;
    extern ImU32 circle_color;
    extern ImU32 skeleton_color;
    extern float line_thickness;
    extern bool use_cache;
    extern bool use_batch_skeleton;

    extern Config config;

    // Enhanced bone data structure for caching
    struct CachedBoneData {
        Matrix bone_matrix;
        Vec3 head_position;
        std::chrono::steady_clock::time_point last_update;
        bool is_valid;

        CachedBoneData() : bone_matrix{}, head_position{},
            last_update(std::chrono::steady_clock::now()),
            is_valid(false) {
        }
    };

    // NEW: Batch skeleton data structure
    struct BatchSkeletonData {
        uintptr_t ped;
        Matrix bone_matrix;
        std::array<Vector3, 9> bone_offsets{};
        Vec3 origin;
        float health;
        uint16_t bone_mask;
        bool valid;

        BatchSkeletonData() : ped(0), health(0.0f), bone_mask(0), valid(false) {}
    };

    // NEW: Batch head data structure
    struct BatchHeadData {
        uintptr_t ped;
        Matrix bone_matrix;
        Vector3 head_offset;
        float health;
        bool valid;

        BatchHeadData() : ped(0), valid(false) {}
    };

    void DrawDebugLine(const Vec2& from, const Vec2& to, ImU32 color);
    void DrawDebugText(const Vec2& pos, const char* text, ImU32 color);
    void DrawDebugCircle(const Vec2& center, float radius, ImU32 color);

    // Enhanced bone cache class with intelligent caching
    class BoneCache {
    private:
        std::unordered_map<uintptr_t, CachedBoneData> cached_bone_data;
        // Reuse data within one 120 Hz acquisition interval. A zero duration
        // made every lookup a guaranteed miss and defeated the cache entirely.
        static constexpr std::chrono::milliseconds CACHE_VALIDITY_MS{ 8 };
        static constexpr std::chrono::seconds CLEANUP_INTERVAL{ 2 }; // Cleanup every 2 seconds
        std::chrono::steady_clock::time_point last_cleanup;

    public:
        BoneCache() : last_cleanup(std::chrono::steady_clock::now()) {}

        // Get bone matrix with caching
        Matrix get_bone_matrix(uintptr_t ped, bool force_refresh = false);

        // Get bone position with caching
        Vec3 get_bone_position(uintptr_t ped, int bone_position, bool force_refresh = false);

        // Update cache with fresh data (called from slow cache thread)
        void update_bone_data(uintptr_t ped, const Matrix& bone_matrix, const Vec3& head_pos);

        // Cache management
        void clear() { cached_bone_data.clear(); }
        void cleanup_old_entries();
        bool is_data_valid(uintptr_t ped) const;
        size_t get_cache_size() const { return cached_bone_data.size(); }

        
    };

    bool get_skeleton_bones_for_ped(uintptr_t ped, std::vector<Vec3>& bone_positions, bool force_refresh = false);

    
    // Global bone cache instance
    extern BoneCache bone_cache;

    // Core ESP functions - your existing ones
    void draw_skeleton(uintptr_t ped, Matrix viewport, uintptr_t localplayer);
    void DrawWaypointAndBlips(const Matrix& view, uintptr_t localplayer);
    void DrawPlayerRadar(const Matrix& view_matrix, uintptr_t localplayer);
    void draw_skeleton_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data);

    // HEAD CIRCLE ESP functions (based on your existing cached version)
    void draw_head_circle(uintptr_t ped, Matrix viewport, uintptr_t localplayer);
    void draw_head_circle_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data);

    // ESP Mode Management
    void set_esp_mode(ESPMode mode);
    ESPMode get_esp_mode();
    const char* get_esp_mode_name(ESPMode mode);
    std::vector<const char*> get_esp_mode_names();
    void set_use_cache(bool use_cache);
    bool get_use_cache();

    // Main rendering dispatcher - chooses between head circle or skeleton
    void render_esp_for_ped(uintptr_t ped, Matrix viewport, uintptr_t localplayer);
    void render_esp_for_ped_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data);

    // NEW: Batch skeleton functions
    void batch_read_skeleton_data(const std::vector<uintptr_t>& peds,
        std::vector<BatchSkeletonData>& out_data, uint16_t bone_mask = 0x01FFu);
    void prepare_skeleton_frame(const std::vector<uintptr_t>& peds,
        const std::vector<Vec3>& origins, uint16_t bone_mask = 0x01FFu);
    void prepare_esp_frame(const std::vector<uintptr_t>& peds, const std::vector<Vec3>& origins);
    void prepare_entity_frames(const std::vector<uintptr_t>& peds, const std::vector<Vec3>& origins,
                               std::vector<FiveM::ESP::EntityFrame>& out_frames);
    bool has_extra_visuals();
    bool try_get_prepared_bone_position(uintptr_t ped, int bone_index, Vec3& out);
    bool try_get_prepared_origin(uintptr_t ped, Vec3& out);
    bool try_get_prepared_health(uintptr_t ped, float& out);
    bool try_get_prepared_vehicle(uintptr_t ped, uintptr_t& out);
    void render_batch_skeletons(const std::vector<BatchSkeletonData>& skeleton_data, Matrix viewport, uintptr_t localplayer);
    void render_skeleton_esp_batch();


    // NEW: Batch head circle functions
    void batch_read_head_data(const std::vector<uintptr_t>& peds, std::vector<BatchHeadData>& out_data);
    void render_batch_head_circles(const std::vector<BatchHeadData>& head_data, Matrix viewport, uintptr_t localplayer);
    void render_head_circle_esp_batch();

    // Bone position functions (your existing ones)
    Vec3 get_bone_position(uintptr_t ped, int bone_position);
    Vec3 get_bone_position_cached(uintptr_t ped, int bone_position, const PedData& cached_ped_data);

    // Utility functions
    Vec3 find_closest_player(Vec3& localPlayerPosition, Matrix viewmatrix);
    void draw_health_info(const Vec2& position, float health);
    void draw_enhanced_health_info(const Vec2& position, float health); // OLD: Small health display

    // Batch update functions for performance
    void batch_update_bone_cache(const std::vector<uintptr_t>& peds);
    void batch_update_skeleton_cache(const std::vector<uintptr_t>& peds); // NEW: Skeleton cache batch update

    // NEW: Multi-threaded bone reading for skeleton ESP
    struct BoneReadResult {
        uintptr_t ped;
        std::vector<Vec3> bone_positions;
        bool success;

        BoneReadResult() : ped(0), success(false) {}
        BoneReadResult(uintptr_t p) : ped(p), success(false) {
            bone_positions.resize(6); // For the 6 bone connections
        }
    };

    // Configuration setters
    void set_circle_color(ImU32 color);
    void set_skeleton_color(ImU32 color);
    void set_line_thickness(float thickness);
    void set_use_batch_skeleton(bool use_batch);  // NEW

    ImU32 get_circle_color();
    ImU32 get_skeleton_color();
    float get_line_thickness();
    bool get_use_batch_skeleton();  // NEW

    // NEW: Skeleton cache management
    size_t get_skeleton_cache_size();
    void clear_skeleton_cache();

    // Performance monitoring
    struct ESPStats {
        std::atomic<int> cache_hits{0};
        std::atomic<int> cache_misses{0};
        std::atomic<int> memory_reads{0};
        std::atomic<int> batch_reads{0};
        std::chrono::steady_clock::time_point last_reset;

        ESPStats() : last_reset(std::chrono::steady_clock::now()) {}

        void reset() {
            cache_hits.store(0, std::memory_order_relaxed);
            cache_misses.store(0, std::memory_order_relaxed);
            memory_reads.store(0, std::memory_order_relaxed);
            batch_reads.store(0, std::memory_order_relaxed);
            last_reset = std::chrono::steady_clock::now();
        }

        double get_hit_ratio() const {
            const int hits = cache_hits.load(std::memory_order_relaxed);
            const int misses = cache_misses.load(std::memory_order_relaxed);
            const int total = hits + misses;
            return total > 0 ? static_cast<double>(hits) / total : 0.0;
        }
    };

    extern ESPStats esp_stats;
    void print_esp_stats();
}
