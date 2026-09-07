#include "esp.h"
#include "math/math.h"
#include "../game/game.h"
#include "../../ImGui/imgui.h"
#include <iostream>
#include "../playerInfo/PedData.h"
#include "../friends/friends.h"
#include "../aimbot/aimbot.h"
#include "config/app_settings.h"
#include "gameplay/esp_core.h"
#include "gameplay/trail_history.h"
#include "../game/visibility.h"
#include "../game/esp_manager.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <string>
#include <cctype>
#include <Memory/Memory.h>
#include <cstdio>
#include <cstring>
#include <chrono>

#include "gameplay/esp_optimizer.h"

// Initialize global instances
esp::ESPStats esp::esp_stats;

// ESP Configuration globals
esp::ESPMode esp::current_esp_mode = esp::ESPMode::HEAD_CIRCLE;
ImU32 esp::circle_color = IM_COL32(0, 255, 0, 255);
ImU32 esp::skeleton_color = IM_COL32(255, 0, 0, 255);
float esp::line_thickness = 2.0f;
bool esp::use_cache = true;
bool esp::use_batch_skeleton = true; // per-ped path respects all toggles
esp::Config esp::config;

static ImU32 EspRGB();
static bool EspPedVisible(uintptr_t ped);
static ImU32 EspPedColor(uintptr_t ped, ImU32 configured, bool visible);

// Enhanced skeleton data structure for caching
struct CachedSkeletonData {
    std::vector<Vec3> bone_positions;  // Cache all bone positions
    std::chrono::steady_clock::time_point last_update;
    bool is_valid;

    CachedSkeletonData() : last_update(std::chrono::steady_clock::now()), is_valid(false) {
        bone_positions.resize(9); // Pre-allocate for bones 0-8
    }
};

// Enhanced bone cache for skeleton
class EnhancedBoneCache {
public:  // Made public for batch updates
    std::unordered_map<uintptr_t, CachedSkeletonData> skeleton_cache;

private:
    static constexpr std::chrono::milliseconds SKELETON_CACHE_VALIDITY_MS{ 50 }; // ~20Hz bone refresh
    std::chrono::steady_clock::time_point last_cleanup;

public:
    EnhancedBoneCache() : last_cleanup(std::chrono::steady_clock::now()) {}

    // Get all bone positions for skeleton at once
    bool get_skeleton_bones(uintptr_t ped, std::vector<Vec3>& bone_positions, bool force_refresh = false) {
        auto now = std::chrono::steady_clock::now();

        auto it = skeleton_cache.find(ped);
        if (!force_refresh && it != skeleton_cache.end() && it->second.is_valid) {
            auto age = now - it->second.last_update;
            if (age < SKELETON_CACHE_VALIDITY_MS) {
                bone_positions = it->second.bone_positions;
                esp::esp_stats.cache_hits++;
                return true;
            }
        }

        esp::esp_stats.cache_misses++;

        // Read all skeleton bones in one batch operation
        Matrix bone_matrix = esp::bone_cache.get_bone_matrix(ped, force_refresh);

        // Read all bone offsets we need for skeleton
        std::vector<int> bone_indices = { 0, 1, 2, 3, 4, 5, 6, 7, 8 }; // head, spine, limbs
        std::vector<Vector3> bone_offsets(bone_indices.size());

        auto handle = mem.CreateScatterHandle();
        for (size_t i = 0; i < bone_indices.size(); ++i) {
            mem.AddScatterReadRequest(handle, ped + (esp::BONE_ARRAY_BASE + esp::BONE_SIZE * bone_indices[i]),
                &bone_offsets[i], sizeof(Vector3));
        }
        mem.ExecuteReadScatter(handle);
        mem.CloseScatterHandle(handle);

        // Transform all bones at once
        bone_positions.resize(9); // Ensure proper size
        for (size_t i = 0; i < bone_indices.size(); ++i) {
            DirectX::SimpleMath::Vector3 boneVec(bone_offsets[i].x, bone_offsets[i].y, bone_offsets[i].z);
            DirectX::SimpleMath::Vector3 transformedBoneVec = DirectX::XMVector3Transform(boneVec, bone_matrix);
            bone_positions[bone_indices[i]] = Vec3(transformedBoneVec.x, transformedBoneVec.y, transformedBoneVec.z);
        }

        // Cache the results
        auto& cached_data = skeleton_cache[ped];
        cached_data.bone_positions = bone_positions;
        cached_data.last_update = now;
        cached_data.is_valid = true;

        esp::esp_stats.memory_reads += static_cast<int>(bone_indices.size());
        return true;
    }

    void cleanup_skeleton_cache() {
        auto now = std::chrono::steady_clock::now();

        if (now - last_cleanup < std::chrono::seconds(3)) {
            return;
        }

        for (auto it = skeleton_cache.begin(); it != skeleton_cache.end(); ) {
            auto age = now - it->second.last_update;
            if (age > std::chrono::seconds(15)) {
                it = skeleton_cache.erase(it);
            }
            else {
                ++it;
            }
        }

        last_cleanup = now;
    }

    size_t get_skeleton_cache_size() const { return skeleton_cache.size(); }
    void clear_skeleton_cache() { skeleton_cache.clear(); }
};

// Global enhanced bone cache instance
static EnhancedBoneCache enhanced_bone_cache;

// One DMA scatter for every visible ped, shared by the whole render frame.
// The former path created and executed one scatter per player, then repeated
// several of those reads in DrawEspExtras. With 13 players that dominated the
// frame time even though only nine real anchor bones were required.
static std::vector<esp::BatchSkeletonData> g_prepared_skeletons;
static std::unordered_map<uintptr_t, size_t> g_prepared_skeleton_index;
static uint32_t g_prepared_skeleton_frame = 0;

struct PreparedEspData {
    uintptr_t ped = 0;
    Vec3 origin{};
    float health = 0.0f;
    float max_health = 200.0f;
    float armor = 0.0f;
    float armor_alt_1 = 0.0f;
    float armor_alt_2 = 0.0f;
    uintptr_t player_info = 0;
    uint32_t network_id = 0;
    uintptr_t weapon_manager = 0;
    uintptr_t weapon_info = 0;
    uint32_t weapon_hash = 0;
    uintptr_t vehicle = 0;
    bool valid = false;
};

static std::vector<PreparedEspData> g_prepared_esp;
static std::unordered_map<uintptr_t, size_t> g_prepared_esp_index;
static uint32_t g_prepared_esp_frame = 0;

static const esp::BatchSkeletonData* FindPreparedSkeleton(uintptr_t ped) {
    if (g_prepared_skeleton_frame != static_cast<uint32_t>(ImGui::GetFrameCount()))
        return nullptr;
    const auto it = g_prepared_skeleton_index.find(ped);
    if (it == g_prepared_skeleton_index.end() || it->second >= g_prepared_skeletons.size())
        return nullptr;
    const auto& data = g_prepared_skeletons[it->second];
    return data.valid ? &data : nullptr;
}

static Vec3 PreparedBonePosition(const esp::BatchSkeletonData* data, int index) {
    if (!data || index < 0 || index >= static_cast<int>(data->bone_offsets.size()))
        return {};
    if ((data->bone_mask & (uint16_t(1u) << static_cast<unsigned>(index))) == 0)
        return {};
    const Vector3& local = data->bone_offsets[static_cast<size_t>(index)];
    DirectX::SimpleMath::Vector3 value(local.x, local.y, local.z);
    const DirectX::SimpleMath::Vector3 transformed =
        DirectX::XMVector3Transform(value, data->bone_matrix);
    return Vec3(transformed.x, transformed.y, transformed.z);
}

static const PreparedEspData* FindPreparedEsp(uintptr_t ped) {
    if (g_prepared_esp_frame != static_cast<uint32_t>(ImGui::GetFrameCount()))
        return nullptr;
    const auto it = g_prepared_esp_index.find(ped);
    if (it == g_prepared_esp_index.end() || it->second >= g_prepared_esp.size())
        return nullptr;
    const auto& data = g_prepared_esp[it->second];
    return data.valid ? &data : nullptr;
}

bool esp::try_get_prepared_bone_position(uintptr_t ped, int bone_index, Vec3& out) {
    out = PreparedBonePosition(FindPreparedSkeleton(ped), bone_index);
    return !out.IsZero();
}

bool esp::try_get_prepared_origin(uintptr_t ped, Vec3& out) {
    const BatchSkeletonData* skeleton = FindPreparedSkeleton(ped);
    if (skeleton && !skeleton->origin.IsZero()) {
        out = skeleton->origin;
        return true;
    }
    const PreparedEspData* data = FindPreparedEsp(ped);
    if (data && !data->origin.IsZero()) {
        out = data->origin;
        return true;
    }
    out = {};
    return false;
}

bool esp::try_get_prepared_health(uintptr_t ped, float& out) {
    const PreparedEspData* data = FindPreparedEsp(ped);
    if (!data) {
        out = 0.0f;
        return false;
    }
    out = data->health;
    return data->health > 0.0f && data->health < 1000.0f;
}

bool esp::try_get_prepared_vehicle(uintptr_t ped, uintptr_t& out) {
    const PreparedEspData* data = FindPreparedEsp(ped);
    if (!data) {
        out = 0;
        return false;
    }
    out = data->vehicle;
    return true;
}

// ESP Mode Management
void esp::set_esp_mode(ESPMode mode) {
    current_esp_mode = mode;
}

esp::ESPMode esp::get_esp_mode() {
    return current_esp_mode;
}

const char* esp::get_esp_mode_name(ESPMode mode) {
    switch (mode) {
    case ESPMode::HEAD_CIRCLE: return "Círculo na cabeça";
    case ESPMode::SKELETON_BONES: return "Ossos do esqueleto";
    default: return "Desconhecido";
    }
}

std::vector<const char*> esp::get_esp_mode_names() {
    return {
        "Círculo na cabeça",
        "Ossos do esqueleto"
    };
}

// NEW: Batch skeleton reading implementation
void esp::batch_read_skeleton_data(const std::vector<uintptr_t>& peds,
                                   std::vector<BatchSkeletonData>& out_data,
                                   uint16_t bone_mask) {
    if (peds.empty()) return;

    // Keep the nine-element bone vectors allocated between frames.
    out_data.resize(peds.size());

    // Define which bones we need for skeleton
    const std::vector<int> skeleton_bones = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };

    // Create single scatter handle for ALL reads
    auto handle = mem.CreateScatterHandle();
    if (!handle) {
        for (auto& data : out_data) data.valid = false;
        return;
    }

    // First pass: Add all bone matrix read requests
    for (size_t i = 0; i < peds.size(); ++i) {
        out_data[i].ped = peds[i];
        out_data[i].valid = false;
        out_data[i].bone_mask = bone_mask;
        out_data[i].bone_matrix = {};
        if (out_data[i].bone_offsets.size() != 9)
            out_data[i].bone_offsets.resize(9);
        else
            std::fill(out_data[i].bone_offsets.begin(), out_data[i].bone_offsets.end(), Vector3{});
        mem.AddScatterReadRequest(handle, peds[i] + BONE_MATRIX_OFFSET,
            &out_data[i].bone_matrix, sizeof(Matrix));
    }

    // Second pass: Add all bone offset read requests
    for (size_t ped_idx = 0; ped_idx < peds.size(); ++ped_idx) {
        for (size_t bone_idx = 0; bone_idx < skeleton_bones.size(); ++bone_idx) {
            int bone_id = skeleton_bones[bone_idx];
            if ((bone_mask & (uint16_t(1u) << static_cast<unsigned>(bone_id))) == 0)
                continue;
            mem.AddScatterReadRequest(handle,
                peds[ped_idx] + (BONE_ARRAY_BASE + BONE_SIZE * bone_id),
                &out_data[ped_idx].bone_offsets[bone_id],
                sizeof(Vector3));
        }
    }

    // Execute ALL reads in one operation
    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    // Mark valid entries
    for (auto& data : out_data) {
        data.valid = true;
    }

    // Update stats
    size_t requested_bones = 0;
    for (int bone_id : skeleton_bones)
        if ((bone_mask & (uint16_t(1u) << static_cast<unsigned>(bone_id))) != 0)
            ++requested_bones;
    esp_stats.memory_reads += static_cast<int>(peds.size() * (1 + requested_bones));
    esp_stats.batch_reads++;
}

void esp::prepare_skeleton_frame(const std::vector<uintptr_t>& peds,
                                 const std::vector<Vec3>& origins,
                                 uint16_t bone_mask) {
    g_prepared_skeleton_index.clear();
    g_prepared_skeleton_frame = static_cast<uint32_t>(ImGui::GetFrameCount());
    if (peds.empty()) {
        g_prepared_skeletons.clear();
        return;
    }

    batch_read_skeleton_data(peds, g_prepared_skeletons, bone_mask);
    g_prepared_skeleton_index.reserve(peds.size() * 2);
    for (size_t i = 0; i < g_prepared_skeletons.size(); ++i) {
        g_prepared_skeletons[i].origin = i < origins.size() ? origins[i] : Vec3{};
        g_prepared_skeleton_index[g_prepared_skeletons[i].ped] = i;
    }
}

// NEW: Batch skeleton rendering
void esp::render_batch_skeletons(const std::vector<BatchSkeletonData>& skeleton_data,
    Matrix viewport, uintptr_t localplayer) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    // Bone connections for skeleton
    static const int bone_connections[][2] = {
        { 0, 7 }, { 7, 1 }, { 1, 2 }, { 2, 8 },
        { 7, 5 }, { 5, 6 },  // left arm chain
        { 7, 6 },            // fallback
        { 8, 3 }, { 3, 4 },  // left leg-ish
        { 8, 4 },            // right leg-ish  
        { 7, 8 }             // torso
    };

    // Pre-calculate local player position for distance calculations
    Vec3 local_pos = mem.Read<Vec3>(localplayer + 0x90);

    for (const auto& data : skeleton_data) {
        if (!data.valid) continue;
        if (esp::config.team_check && friends::IsFriendPed(data.ped))
            continue;

        // Transform all bones for this ped
        std::vector<Vec3> world_positions(9);
        std::vector<Vec2> screen_positions(9);
        std::vector<bool> on_screen(9, false);

        // Batch transform bones to world space
        for (int bone_id : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
            DirectX::SimpleMath::Vector3 boneVec(
                data.bone_offsets[bone_id].x,
                data.bone_offsets[bone_id].y,
                data.bone_offsets[bone_id].z
            );
            DirectX::SimpleMath::Vector3 transformedBoneVec =
                DirectX::XMVector3Transform(boneVec, data.bone_matrix);

            world_positions[bone_id] = Vec3(
                transformedBoneVec.x,
                transformedBoneVec.y,
                transformedBoneVec.z
            );

            // Convert to screen space
            on_screen[bone_id] = world_positions[bone_id].world_to_screen(
                viewport, screen_positions[bone_id]
            );
        }

        // Get health for coloring (from cache if available)
        float health = 100.0f;
        PedData cached_ped_data;
        if (g_pedCacheManager.getPedData(data.ped, cached_ped_data)) {
            health = cached_ped_data.health;
        }

        // Determine skeleton color based on health
        const bool visible = EspPedVisible(data.ped);
        ImU32 current_skeleton_color = EspPedColor(
            data.ped, config.color_skeleton, visible);
        if (!config.visibility_colors && health < 50.0f) {
            current_skeleton_color = IM_COL32(255, 255, 0, 255);
        }
        if (!config.visibility_colors && health < 25.0f) {
            current_skeleton_color = IM_COL32(255, 100, 0, 255);
        }
        if (health <= 0.0f) {
            current_skeleton_color = IM_COL32(100, 100, 100, 255);
        }

        // Draw skeleton connections
        for (const auto& connection : bone_connections) {
            int bone1 = connection[0];
            int bone2 = connection[1];

            if (on_screen[bone1] && on_screen[bone2]) {
                draw_list->AddLine(
                    ImVec2(screen_positions[bone1].x, screen_positions[bone1].y),
                    ImVec2(screen_positions[bone2].x, screen_positions[bone2].y),
                    current_skeleton_color,
                    line_thickness
                );

                // Joint dots (reference style — green points)
                if (config.joints && health > 0.0f) {
                    const ImU32 configured = config.color_skeleton_points
                        ? config.color_skeleton_points
                        : IM_COL32(80, 220, 90, 255);
                    const ImU32 jc = EspPedColor(data.ped, configured, visible);
                    const float joint_radius = 2.6f;
                    auto joint = [&](int id) {
                        if (!on_screen[id]) return;
                        ImVec2 pt(screen_positions[id].x, screen_positions[id].y);
                        draw_list->AddCircleFilled(pt, joint_radius, jc, 10);
                        draw_list->AddCircle(pt, joint_radius, IM_COL32(0, 0, 0, 180), 10, 1.0f);
                    };
                    joint(bone1);
                    joint(bone2);
                }
            }
        }

        // Draw head indicator
        if (on_screen[0]) {
            float distance = local_pos.distance_to(world_positions[0]);
            float distance_factor = 50.0f / distance;
            if (distance_factor < 0.2f) distance_factor = 0.2f;
            if (distance_factor > 2.0f) distance_factor = 2.0f;

            float head_radius = 4.0f * distance_factor;
            if (head_radius < 1.0f) head_radius = 1.0f;
            if (head_radius > 8.0f) head_radius = 8.0f;

            draw_list->AddCircle(
                ImVec2(screen_positions[0].x, screen_positions[0].y),
                head_radius,
                current_skeleton_color,
                12,
                head_radius * 0.25f
            );
        }
    }

    // Update cache with transformed data
    for (const auto& data : skeleton_data) {
        if (data.valid) {
            std::vector<Vec3> bone_positions(9);
            for (int bone_id : {0, 1, 2, 3, 4, 5, 6, 7, 8}) {
                DirectX::SimpleMath::Vector3 boneVec(
                    data.bone_offsets[bone_id].x,
                    data.bone_offsets[bone_id].y,
                    data.bone_offsets[bone_id].z
                );
                DirectX::SimpleMath::Vector3 transformedBoneVec =
                    DirectX::XMVector3Transform(boneVec, data.bone_matrix);
                bone_positions[bone_id] = Vec3(
                    transformedBoneVec.x,
                    transformedBoneVec.y,
                    transformedBoneVec.z
                );
            }

            // Update enhanced bone cache
            auto& cached_data = enhanced_bone_cache.skeleton_cache[data.ped];
            cached_data.bone_positions = bone_positions;
            cached_data.last_update = std::chrono::steady_clock::now();
            cached_data.is_valid = true;
        }
    }
}

// NEW: Batch skeleton ESP rendering function
void esp::render_skeleton_esp_batch() {
    // Get all valid peds
    std::vector<uintptr_t> valid_peds = g_pedCacheManager.getValidPedIds();
    if (valid_peds.empty()) return;

    // Get viewport matrix once
    Matrix view_matrix;
    auto handle = mem.CreateScatterHandle();
    mem.AddScatterReadRequest(handle, FiveM::offset::viewport + 0x24C,
        &view_matrix, sizeof(Matrix));
    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    // Batch read all skeleton data
    std::vector<BatchSkeletonData> skeleton_data;
    batch_read_skeleton_data(valid_peds, skeleton_data);

    // Render all skeletons in one pass
    render_batch_skeletons(skeleton_data, view_matrix, FiveM::offset::localplayer);

    // Clean up old cache entries
    enhanced_bone_cache.cleanup_skeleton_cache();
}

// NEW: Batch head circle reading implementation
void esp::batch_read_head_data(const std::vector<uintptr_t>& peds, std::vector<BatchHeadData>& out_data) {
    if (peds.empty()) return;

    // Pre-allocate output data
    out_data.clear();
    out_data.resize(peds.size());

    // Create single scatter handle for ALL reads
    auto handle = mem.CreateScatterHandle();

    // Add all read requests
    for (size_t i = 0; i < peds.size(); ++i) {
        out_data[i].ped = peds[i];

        // Read bone matrix
        mem.AddScatterReadRequest(handle, peds[i] + BONE_MATRIX_OFFSET,
            &out_data[i].bone_matrix, sizeof(Matrix));

        // Read head offset (bone 0)
        mem.AddScatterReadRequest(handle, peds[i] + (BONE_ARRAY_BASE + BONE_SIZE * 0),
            &out_data[i].head_offset, sizeof(Vector3));
    }

    // Execute ALL reads in one operation
    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    // Mark valid entries
    for (auto& data : out_data) {
        data.valid = true;
    }

    // Update stats
    esp_stats.memory_reads += static_cast<int>(peds.size() * 2);
    esp_stats.batch_reads++;
}

// NEW: Batch head circle rendering
void esp::render_batch_head_circles(const std::vector<BatchHeadData>& head_data,
    Matrix viewport, uintptr_t localplayer) {
    (void)localplayer;
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    for (const auto& data : head_data) {
        if (!data.valid) continue;
        if (esp::config.team_check && friends::IsFriendPed(data.ped))
            continue;

        // Transform head bone to world space
        DirectX::SimpleMath::Vector3 boneVec(
            data.head_offset.x,
            data.head_offset.y,
            data.head_offset.z
        );
        DirectX::SimpleMath::Vector3 transformedBoneVec =
            DirectX::XMVector3Transform(boneVec, data.bone_matrix);

        Vec3 head_world_pos(
            transformedBoneVec.x,
            transformedBoneVec.y,
            transformedBoneVec.z
        );

        // Convert to screen space
        Vec2 head_screen_pos;
        if (head_world_pos.world_to_screen(viewport, head_screen_pos)) {
            // Get health for coloring (from cache if available)
            float health = 100.0f;
            PedData cached_ped_data;
            if (g_pedCacheManager.getPedData(data.ped, cached_ped_data)) {
                health = cached_ped_data.health;
            }

            // Determine color based on health
            ImU32 color = circle_color;
            if (health < 50.0f) {
                color = IM_COL32(255, 255, 0, 255);
            }
            if (health < 25.0f) {
                color = IM_COL32(255, 0, 0, 255);
            }
            if (health <= 0.0f) {
                color = IM_COL32(100, 100, 100, 255);
            }

            // Draw circle
            draw_list->AddCircle(
                ImVec2(head_screen_pos.x, head_screen_pos.y),
                4.0f,
                color,
                20,
                2.0f
            );
        }

        // Update cache
        bone_cache.update_bone_data(data.ped, data.bone_matrix, head_world_pos);
    }
}

// NEW: Batch head circle ESP rendering function
void esp::render_head_circle_esp_batch() {
    // Get all valid peds
    std::vector<uintptr_t> valid_peds = g_pedCacheManager.getValidPedIds();
    if (valid_peds.empty()) return;

    // Get viewport matrix once
    Matrix view_matrix;
    auto handle = mem.CreateScatterHandle();
    mem.AddScatterReadRequest(handle, FiveM::offset::viewport + 0x24C,
        &view_matrix, sizeof(Matrix));
    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    // Batch read all head data
    std::vector<BatchHeadData> head_data;
    batch_read_head_data(valid_peds, head_data);

    // Render all head circles in one pass
    render_batch_head_circles(head_data, view_matrix, FiveM::offset::localplayer);

    // Clean up old cache entries
    bone_cache.cleanup_old_entries();
}


static bool IsPlausiblePlayerName(const char* s) {
    if (!s || !s[0]) return false;
    int letters = 0, digits = 0, len = 0, hexish = 0;
    for (int i = 0; s[i] && i < 48; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c < 32 || c > 126) return false;
        ++len;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) ++letters;
        else if (c >= '0' && c <= '9') ++digits;
        else if (c == '_' || c == '-' || c == ' ' || c == '.') continue;
        else return false; // weird punctuation
        char lc = (char)tolower(c);
        if ((lc >= 'a' && lc <= 'f') || (lc >= '0' && lc <= '9')) ++hexish;
    }
    if (len < 3 || len > 24) return false;
    if (letters < 2) return false; // need real letters
    // reject pure/mostly hex garbage like "R0c5636c0"
    if (hexish >= len - 1 && digits >= 3) return false;
    if (digits > letters + 2) return false;
    return true;
}

// Name cache by netId — TTL + hard cap
static std::unordered_map<uint32_t, std::pair<std::string, std::chrono::steady_clock::time_point>> g_nameCache;
static constexpr size_t kNameCacheMax = 256;
static constexpr auto kNameCacheTtl = std::chrono::seconds(30);

static bool ReadPlayerDisplayName(uintptr_t ped, uintptr_t pinfo, uint32_t netId,
                                  bool allow_live_fallback, char* out, size_t outN) {
    if (!out || outN < 4 || !ped) return false;
    out[0] = 0;
    if (!esp::config.player_name) { snprintf(out, outN, "Jogador"); return false; }
    using namespace FiveM;
    if (!pinfo && allow_live_fallback)
        pinfo = mem.Read<uintptr_t>(ped + offset::playerInfo);
    if (!pinfo) {
        snprintf(out, outN, "Jogador");
        return false;
    }
    if (!netId && allow_live_fallback) {
        netId = mem.Read<uint32_t>(pinfo + offset::playerInfo_netId);
        if (!netId)
            netId = mem.Read<uint32_t>(pinfo + offset::playerInfo_netId_alt);
    }

    auto it = g_nameCache.find(netId);
    if (netId && it != g_nameCache.end() && !it->second.first.empty()) {
        if (std::chrono::steady_clock::now() - it->second.second < kNameCacheTtl) {
            // Don't cache "ID N" forever as a real name
            if (it->second.first.rfind("ID ", 0) != 0) {
                snprintf(out, outN, "%s", it->second.first.c_str());
                return true;
            }
        }
        g_nameCache.erase(it);
    }

    char buf[64]{};
    bool ok = false;

    // 1) Inline string at CPlayerInfo+name (0x100 cheatoffsets)
    mem.Read(pinfo + offset::playerInfo_name, buf, 31);
    buf[31] = 0;
    ok = IsPlausiblePlayerName(buf);

    // 2) Pointer at same offset
    if (!ok) {
        uintptr_t sp = mem.Read<uintptr_t>(pinfo + offset::playerInfo_name);
        if (sp > 0x10000 && sp < 0x7FFFFFFFFFFFULL) {
            memset(buf, 0, sizeof(buf));
            mem.Read(sp, buf, 31);
            buf[31] = 0;
            ok = IsPlausiblePlayerName(buf);
        }
    }

    // 3) Other common CPlayerInfo name slots
    if (!ok) {
        for (uintptr_t off : { 0x100u, 0xFCu, 0x7Cu, 0xA0u, 0x84u, 0xBC0u, 0xE0u }) {
            memset(buf, 0, sizeof(buf));
            mem.Read(pinfo + off, buf, 31);
            buf[31] = 0;
            if (IsPlausiblePlayerName(buf)) { ok = true; break; }
            uintptr_t sp = mem.Read<uintptr_t>(pinfo + off);
            if (sp > 0x10000 && sp < 0x7FFFFFFFFFFFULL) {
                memset(buf, 0, sizeof(buf));
                mem.Read(sp, buf, 31);
                buf[31] = 0;
                if (IsPlausiblePlayerName(buf)) { ok = true; break; }
            }
        }
    }

    // 4) PlayerNames global table (module RVA) indexed by netId when available
    if (!ok && netId && offset::base) {
        // b3258 PlayerNames = 0x1E63C68; also stored as network_player_mgr on some builds
        uintptr_t names_rva = offset::b3258_networkPlayerMgr; // 0x1E63C68 in offsets.h
        if (names_rva) {
            uintptr_t table = mem.Read<uintptr_t>(offset::base + names_rva);
            if (table > 0x10000 && table < 0x7FFFFFFFFFFFULL) {
                // Common layout: entry = table + netId * stride, name at +0
                for (uintptr_t stride : { 0x10ull, 0x18ull, 0x20ull }) {
                    uintptr_t entry = table + (uintptr_t)netId * stride;
                    memset(buf, 0, sizeof(buf));
                    mem.Read(entry, buf, 31);
                    buf[31] = 0;
                    if (IsPlausiblePlayerName(buf)) { ok = true; break; }
                    uintptr_t sp = mem.Read<uintptr_t>(entry);
                    if (sp > 0x10000 && sp < 0x7FFFFFFFFFFFULL) {
                        memset(buf, 0, sizeof(buf));
                        mem.Read(sp, buf, 31);
                        buf[31] = 0;
                        if (IsPlausiblePlayerName(buf)) { ok = true; break; }
                    }
                }
            }
        }
    }

    if (ok) {
        snprintf(out, outN, "%s", buf);
        if (netId) {
            if (g_nameCache.size() >= kNameCacheMax) g_nameCache.clear();
            g_nameCache[netId] = { buf, std::chrono::steady_clock::now() };
        }
        return true;
    }
    // Real name not found — show ID only when player_id is on; otherwise "Player"
    if (esp::config.player_id && netId)
        snprintf(out, outN, "ID %u", netId);
    else
        snprintf(out, outN, "Jogador");
    return false;
}

static void DrawTextOutlined(ImDrawList* dl, ImVec2 pos, ImU32 col, const char* text) {
    if (!dl || !text) return;
    const ImU32 shadow = IM_COL32(0, 0, 0, 200);
    dl->AddText(ImVec2(pos.x - 1, pos.y), shadow, text);
    dl->AddText(ImVec2(pos.x + 1, pos.y), shadow, text);
    dl->AddText(ImVec2(pos.x, pos.y - 1), shadow, text);
    dl->AddText(ImVec2(pos.x, pos.y + 1), shadow, text);
    dl->AddText(pos, col, text);
}

static ImU32 EspRGB() {
    float t = (float)ImGui::GetTime();
    return IM_COL32(
        (int)(sinf(t * 2.0f) * 127 + 128),
        (int)(sinf(t * 2.0f + 2.094f) * 127 + 128),
        (int)(sinf(t * 2.0f + 4.188f) * 127 + 128), 255);
}

static bool EspPedVisible(uintptr_t ped) {
    if (!ped || (!esp::config.visibility_colors && !esp::config.visible_check))
        return true;
    return FiveM::Visibility::IsPedVisible(ped);
}

static bool EspPedIsFriend(uintptr_t ped, bool allowDirectRead) {
    const PreparedEspData* prepared = FindPreparedEsp(ped);
    if (prepared)
        return prepared->network_id != 0 && friends::IsFriend(prepared->network_id);
    return allowDirectRead && friends::IsFriendPed(ped);
}

static ImU32 EspPedColor(uintptr_t ped, ImU32 configured, bool visible) {
    if (esp::config.rgb_mode)
        return EspRGB();
    if (esp::config.visibility_colors)
        return visible ? esp::config.color_visible : esp::config.color_invisible;
    // Colour rendering can call this several times for the same entity. Never
    // perform a fresh DMA read here; reuse the player-info batch for the frame.
    if (EspPedIsFriend(ped, false))
        return esp::config.color_team;
    if (esp::config.npc_esp) {
        const PreparedEspData* prepared = FindPreparedEsp(ped);
        if (prepared && prepared->player_info == 0)
            return esp::config.color_npc;
    }
    return configured;
}

static ImU32 Darker(ImU32 color, float factor = 0.48f) {
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.x *= factor;
    value.y *= factor;
    value.z *= factor;
    return ImGui::ColorConvertFloat4ToU32(value);
}

static bool BoneLooksValid(const Vec3& bone, const Vec3& origin) {
    if (bone.IsZero()) return false;
    // Reject garbage transforms far from the ped (allow tall / ragdoll poses)
    return bone.distance_to(origin) <= 8.0f;
}

static ImU32 MultiplyAlpha(ImU32 color, float factor) {
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.w *= std::clamp(factor, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32(value);
}

static void DrawMotionVisuals(uintptr_t ped, Matrix viewport, const PedData* cached) {
    const auto& cfg = esp::config;
    if (!cfg.trails && !cfg.head_halo && !cfg.look_direction)
        return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw) return;

    const esp::BatchSkeletonData* skeleton = FindPreparedSkeleton(ped);
    const PreparedEspData* prepared = FindPreparedEsp(ped);
    Vec3 origin = skeleton ? skeleton->origin : Vec3{};
    if (origin.IsZero() && prepared) origin = prepared->origin;
    if (origin.IsZero() && cached) origin = cached->position_origin;
    if (origin.IsZero()) return;

    const bool visible = EspPedVisible(ped);
    const double now = ImGui::GetTime();
    static std::unordered_map<uintptr_t, OmniGhost::Gameplay::FixedTrailHistory<18>> trails;
    static int cleanup_frame = -1;

    if (cfg.trails) {
        auto& history = trails[ped];
        history.Push(origin.x, origin.y, origin.z + 0.04f, now, 0.10f, 0.035);
        const double duration = std::clamp(static_cast<double>(cfg.trail_duration), 0.20, 2.50);
        const ImU32 base = EspPedColor(ped, cfg.color_trail, visible);
        for (std::size_t i = 1; i < history.Size(); ++i) {
            const auto& a = history.At(i - 1);
            const auto& b = history.At(i);
            const double age = now - b.time;
            if (age < 0.0 || age > duration) continue;
            Vec2 sa{}, sb{};
            if (!Vec3(a.x, a.y, a.z).world_to_screen(viewport, sa) ||
                !Vec3(b.x, b.y, b.z).world_to_screen(viewport, sb))
                continue;
            const float fade = static_cast<float>(1.0 - age / duration);
            draw->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y),
                MultiplyAlpha(base, fade * fade),
                std::clamp(cfg.trail_thickness, 1.f, 4.f));
        }
    }

    const int frame = ImGui::GetFrameCount();
    if (frame != cleanup_frame && (frame % 120) == 0) {
        cleanup_frame = frame;
        for (auto it = trails.begin(); it != trails.end();) {
            if (it->second.Stale(now, 3.0)) it = trails.erase(it);
            else ++it;
        }
    }

    Vec3 head = skeleton ? PreparedBonePosition(skeleton, 0) : Vec3{};
    if (!BoneLooksValid(head, origin)) {
        head = origin;
        head.z += 0.92f;
    }

    if (cfg.head_halo) {
        const ImU32 color = EspPedColor(ped, cfg.color_halo, visible);
        constexpr float radius = 0.18f;
        constexpr int segments = 14;
        Vec2 previous{};
        bool previous_ok = false;
        for (int i = 0; i <= segments; ++i) {
            const float angle = static_cast<float>(i) * 6.28318530718f / segments;
            Vec3 point(head.x + std::cos(angle) * radius,
                       head.y + std::sin(angle) * radius,
                       head.z + 0.12f);
            Vec2 screen{};
            const bool ok = point.world_to_screen(viewport, screen);
            if (ok && previous_ok)
                draw->AddLine(ImVec2(previous.x, previous.y), ImVec2(screen.x, screen.y), color, 1.6f);
            previous = screen;
            previous_ok = ok;
        }
    }

    if (cfg.look_direction && skeleton) {
        Vec3 forward(skeleton->bone_matrix._21, skeleton->bone_matrix._22, 0.f);
        const float length = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        if (length > 0.001f) {
            forward.x /= length;
            forward.y /= length;
            const float line_length = std::clamp(cfg.look_direction_length, 0.5f, 5.f);
            Vec3 end(head.x + forward.x * line_length,
                     head.y + forward.y * line_length,
                     head.z);
            Vec2 from{}, to{};
            if (head.world_to_screen(viewport, from) && end.world_to_screen(viewport, to)) {
                const ImU32 color = EspPedColor(ped, cfg.color_look_direction, visible);
                draw->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), color, 1.6f);
                draw->AddCircleFilled(ImVec2(to.x, to.y), 2.2f, color, 8);
            }
        }
    }
}


static bool AnyEspExtrasEnabled() {
    const auto& c = esp::config;
    return c.box_2d || c.corner_box || c.snaplines || c.health_bar || c.armor_bar
        || c.weapon_name || c.distance || c.player_name || c.player_id;
}

bool esp::has_extra_visuals() {
    return AnyEspExtrasEnabled();
}

void esp::prepare_esp_frame(const std::vector<uintptr_t>& peds,
                            const std::vector<Vec3>& origins) {
    g_prepared_esp_frame = static_cast<uint32_t>(ImGui::GetFrameCount());
    g_prepared_esp_index.clear();
    const bool needs_motion_origin = config.trails || config.head_halo || config.look_direction;
    if (peds.empty() || (!AnyEspExtrasEnabled() && !needs_motion_origin &&
        !aimbot::config.aimbot_enabled && !aimbot::config.trigger_enabled)) {
        g_prepared_esp.clear();
        return;
    }

    g_prepared_esp.resize(peds.size());
    g_prepared_esp_index.reserve(peds.size() * 2);
    for (size_t i = 0; i < peds.size(); ++i) {
        g_prepared_esp[i] = {};
        g_prepared_esp[i].ped = peds[i];
        g_prepared_esp[i].origin = i < origins.size() ? origins[i] : Vec3{};
        g_prepared_esp[i].valid = peds[i] != 0 && !g_prepared_esp[i].origin.IsZero();
        g_prepared_esp_index[peds[i]] = i;
    }

    // Trails only need the positions already collected by the game manager.
    // Stop here instead of issuing a health/identity DMA batch for a cosmetic
    // feature that does not consume those fields.
    if (!AnyEspExtrasEnabled() && !aimbot::config.aimbot_enabled &&
        !aimbot::config.trigger_enabled)
        return;

    using namespace FiveM;
    OmniGhost::Gameplay::EspCore::FeatureSet requested{};
    requested.box = config.box_2d;
    requested.corner_box = config.corner_box;
    requested.skeleton = config.skeleton;
    requested.head = config.head_circle;
    requested.health = config.health_bar;
    requested.armor = config.armor_bar;
    requested.snapline = config.snaplines;
    requested.name = config.player_name || config.player_id || config.npc_esp ||
        config.team_check || friends::HasFriends();
    requested.weapon = config.weapon_name;
    requested.distance = config.distance;
    // Vehicle-exception field for aim; ESP colour/filter uses Visibility::Batch.
    requested.visibility = aimbot::config.visible_check ||
        esp::config.visible_check || esp::config.visibility_colors;
    requested.aim = aimbot::config.aimbot_enabled || aimbot::config.trigger_enabled;
    requested.prediction = aimbot::config.velocity_prediction;
    requested.trail = config.trails;
    requested.halo = config.head_halo;
    requested.look_direction = config.look_direction;
    const auto fields = requested.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Armor);
    const bool need_identity = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Name);
    const bool need_weapon = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Weapon);
    const bool need_vehicle = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Visibility);

    auto first = mem.CreateScatterHandle();
    if (!first) {
        g_prepared_esp_frame = 0;
        return;
    }
    for (auto& data : g_prepared_esp) {
        if (!data.ped) continue;
        mem.AddScatterReadRequest(first, data.ped + offset::playerHealth,
            &data.health, sizeof(data.health));
        mem.AddScatterReadRequest(first, data.ped + 0x284,
            &data.max_health, sizeof(data.max_health));
        if (need_armor) {
            mem.AddScatterReadRequest(first, data.ped + offset::playerArmor,
                &data.armor, sizeof(data.armor));
            mem.AddScatterReadRequest(first, data.ped + 0x14E0,
                &data.armor_alt_1, sizeof(data.armor_alt_1));
            mem.AddScatterReadRequest(first, data.ped + 0x1530,
                &data.armor_alt_2, sizeof(data.armor_alt_2));
        }
        if (need_identity)
            mem.AddScatterReadRequest(first, data.ped + offset::playerInfo,
                &data.player_info, sizeof(data.player_info));
        if (need_weapon)
            mem.AddScatterReadRequest(first, data.ped + offset::weaponManager,
                &data.weapon_manager, sizeof(data.weapon_manager));
        if (need_vehicle)
            mem.AddScatterReadRequest(first, data.ped + offset::pedVehicle,
                &data.vehicle, sizeof(data.vehicle));
    }
    mem.ExecuteReadScatter(first);
    mem.CloseScatterHandle(first);

    if (need_identity || need_weapon) {
        auto second = mem.CreateScatterHandle();
        if (second) {
            for (auto& data : g_prepared_esp) {
                if (need_identity && data.player_info)
                    mem.AddScatterReadRequest(second,
                        data.player_info + offset::playerInfo_netId,
                        &data.network_id, sizeof(data.network_id));
                if (need_weapon && data.weapon_manager)
                    mem.AddScatterReadRequest(second,
                        data.weapon_manager + offset::weaponMgr_currentWeapon,
                        &data.weapon_info, sizeof(data.weapon_info));
            }
            mem.ExecuteReadScatter(second);
            mem.CloseScatterHandle(second);
        }
    }

    if (need_weapon) {
        auto third = mem.CreateScatterHandle();
        if (third) {
            for (auto& data : g_prepared_esp) {
                if (data.weapon_info)
                    mem.AddScatterReadRequest(third,
                        data.weapon_info + offset::weaponInfo_hash,
                        &data.weapon_hash, sizeof(data.weapon_hash));
            }
            mem.ExecuteReadScatter(third);
            mem.CloseScatterHandle(third);
        }
    }
}

static void DrawEspExtras(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData* cached) {
    if (!AnyEspExtrasEnabled()) return;
    using namespace FiveM;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    const PreparedEspData* prepared_esp = FindPreparedEsp(ped);
    const esp::BatchSkeletonData* prepared_bones = FindPreparedSkeleton(ped);

    Vec3 origin = prepared_esp ? prepared_esp->origin : Vec3{};
    if (origin.IsZero() && cached && !cached->position_origin.IsZero())
        origin = cached->position_origin;
    if (origin.IsZero())
        origin = mem.Read<Vec3>(ped + offset::playerPosition);
    if (origin.IsZero())
        origin = mem.Read<Vec3>(ped + 0x90);
    if (origin.IsZero()) return;

    Vec3 localPos = FiveM::ESP::FrameCacheValid()
        ? FiveM::ESP::GetFrameLocalPos() : Vec3{};
    if (localPos.IsZero() && localplayer)
        localPos = mem.Read<Vec3>(localplayer + offset::playerPosition);
    if (localPos.IsZero())
        localPos = mem.Read<Vec3>(localplayer + 0x90);

    float dist = origin.distance_to(localPos);
    const float maxDist = (esp::config.max_esp_distance > 1.f) ? esp::config.max_esp_distance : 150.f;
    if (dist > maxDist) return;

    float health = prepared_esp ? prepared_esp->health : (cached ? cached->health : 0.f);
    if (!prepared_esp && health <= 0.f)
        health = mem.Read<float>(ped + offset::playerHealth);
    if (!prepared_esp && health <= 0.f)
        health = mem.Read<float>(ped + 0x280);

    // Only treat as dead when health is clearly <= 0 AND we successfully read a
    // plausible max-health (avoids "everyone invisible" when the offset is wrong).
    float maxHealthProbe = prepared_esp ? prepared_esp->max_health : mem.Read<float>(ped + 0x284);
    const bool healthLooksValid = (maxHealthProbe > 50.f && maxHealthProbe < 1000.f);
    if (healthLooksValid && health <= 0.f && !esp::config.show_dead)
        return;
    if (!healthLooksValid && health <= 0.f)
        health = 100.f; // assume alive when health chain is unreliable

    const bool visible = EspPedVisible(ped);
    if (esp::config.visible_check && !visible)
        return;

    // Skip expensive bone reads when only name/distance/weapon text is on
    const bool needBones = esp::config.box_2d || esp::config.corner_box
        || esp::config.snaplines || esp::config.health_bar || esp::config.armor_bar;
    Vec3 head, footL, footR;
    if (needBones) {
        head = PreparedBonePosition(prepared_bones, 0);
        footL = PreparedBonePosition(prepared_bones, 1);
        footR = PreparedBonePosition(prepared_bones, 2);
        if (!prepared_bones) {
            Matrix bone_matrix{};
            Vector3 loc0{}, loc1{}, loc2{};
            auto bh = mem.CreateScatterHandle();
            if (bh) {
                mem.AddScatterReadRequest(bh, ped + 0x60, &bone_matrix, sizeof(Matrix));
                mem.AddScatterReadRequest(bh, ped + (0x410 + 0x10 * 0), &loc0, sizeof(Vector3));
                mem.AddScatterReadRequest(bh, ped + (0x410 + 0x10 * 1), &loc1, sizeof(Vector3));
                mem.AddScatterReadRequest(bh, ped + (0x410 + 0x10 * 2), &loc2, sizeof(Vector3));
                mem.ExecuteReadScatter(bh);
                mem.CloseScatterHandle(bh);
                auto xform = [&](const Vector3& l) -> Vec3 {
                    DirectX::SimpleMath::Vector3 v(l.x, l.y, l.z);
                    DirectX::SimpleMath::Vector3 tf = DirectX::XMVector3Transform(v, bone_matrix);
                    return Vec3(tf.x, tf.y, tf.z);
                };
                head = xform(loc0);
                footL = xform(loc1);
                footR = xform(loc2);
            }
        }
        if (!BoneLooksValid(head, origin)) {
            head = origin; head.z += 0.95f;
        } else {
            head.z += 0.05f;
        }
    } else {
        head = origin; head.z += 0.95f;
        footL = origin; footR = origin;
    }
    Vec3 feet = origin;
    if (BoneLooksValid(footL, origin) && BoneLooksValid(footR, origin)) {
        feet.x = (footL.x + footR.x) * 0.5f;
        feet.y = (footL.y + footR.y) * 0.5f;
        feet.z = (footL.z + footR.z) * 0.5f;
    } else if (BoneLooksValid(footL, origin)) {
        feet = footL;
    } else if (BoneLooksValid(footR, origin)) {
        feet = footR;
    } else {
        feet.z -= 0.05f;
    }

    // Ensure head is above feet in world space
    if (head.z < feet.z + 0.3f)
        head.z = feet.z + 1.0f;

    Vec2 headS{}, feetS{}, originS{};
    const bool okHead = head.world_to_screen(viewport, headS);
    const bool okFeet = feet.world_to_screen(viewport, feetS);
    const bool okOrigin = origin.world_to_screen(viewport, originS);

    ImVec2 boxMin{}, boxMax{};
    if (okHead && okFeet) {
        float h = fabsf(feetS.y - headS.y);
        if (h < 12.f) h = 12.f;
        float w = h * 0.42f;
        boxMin = ImVec2(headS.x - w * 0.5f, (std::min)(headS.y, feetS.y));
        boxMax = ImVec2(headS.x + w * 0.5f, (std::max)(headS.y, feetS.y));
    } else if (okHead) {
        float h = 60.f;
        float w = h * 0.42f;
        boxMin = ImVec2(headS.x - w * 0.5f, headS.y);
        boxMax = ImVec2(headS.x + w * 0.5f, headS.y + h);
    } else if (okOrigin) {
        float h = 70.f;
        float w = h * 0.42f;
        boxMin = ImVec2(originS.x - w * 0.5f, originS.y - h);
        boxMax = ImVec2(originS.x + w * 0.5f, originS.y);
        headS = Vec2(originS.x, originS.y - h);
    } else {
        return; // fully off-screen
    }

    ImU32 colBox = EspPedColor(ped, esp::config.color_box_2d, visible);
    if (health <= 0.f) colBox = esp::config.color_dead;
    ImU32 colCorner = EspPedColor(ped, esp::config.color_corner_box, visible);
    ImU32 colSnap = EspPedColor(ped, esp::config.color_snaplines, visible);

    {
        bool flying = false;
        if (!localPos.IsZero() && (origin.z - localPos.z) > 8.f)
            flying = true;
        if (flying) {
            const char* tag = "FLY";
            ImVec2 ts = ImGui::CalcTextSize(tag);
            DrawTextOutlined(dl, ImVec2((boxMin.x + boxMax.x) * 0.5f - ts.x * 0.5f, boxMin.y - ts.y - 14.f),
                             IM_COL32(255, 80, 80, 255), tag);
        }
    }

    if (esp::config.box_2d) {
        // filled_box removed from product
        // Outer dark outline + main stroke for a more solid box
        dl->AddRect(ImVec2(boxMin.x - 1.f, boxMin.y - 1.f),
                    ImVec2(boxMax.x + 1.f, boxMax.y + 1.f),
                    IM_COL32(0, 0, 0, 160), 0.f, 0, 2.0f);
        dl->AddRect(boxMin, boxMax, colBox, 0.f, 0, 1.6f);
    }

    if (esp::config.corner_box)
        OmniGhost::Gameplay::EspCore::DrawCornerBox(dl, boxMin, boxMax, colCorner, 1.6f);

    if (esp::config.snaplines) {
        ImVec2 scr = ImGui::GetIO().DisplaySize;
        ImVec2 start;
        if (esp::config.snapline_pos == 0) start = ImVec2(scr.x * 0.5f, 0.f);
        else if (esp::config.snapline_pos == 1) start = ImVec2(scr.x * 0.5f, scr.y * 0.5f);
        else start = ImVec2(scr.x * 0.5f, scr.y);
        dl->AddLine(start, ImVec2((boxMin.x + boxMax.x) * 0.5f, boxMin.y), colSnap, 1.2f);
    }

    float maxHealth = maxHealthProbe;
    if (maxHealth < 1.f) maxHealth = 200.f;
    float hp100 = (health / maxHealth) * 100.f;
    if (hp100 < 0.f) hp100 = 0.f;
    if (hp100 > 100.f) hp100 = 100.f;
    float hpFrac = hp100 / 100.f;

    // ── Health bar (continuous gradient green→yellow→red, HP text on top) ──
    if (esp::config.health_bar) {
        const float bw = 4.f;
        ImVec2 barMin(boxMin.x - bw - 3.f, boxMin.y);
        ImVec2 barMax(boxMin.x - 3.f, boxMax.y);
        const ImU32 healthColor = EspPedColor(ped, esp::config.color_health, visible);
        OmniGhost::Gameplay::EspCore::DrawVerticalBar(
            dl, barMin, barMax, hpFrac, healthColor, Darker(healthColor));
        char hpBuf[16];
        snprintf(hpBuf, sizeof(hpBuf), "%.0f", hp100);
        ImVec2 ts = ImGui::CalcTextSize(hpBuf);
        DrawTextOutlined(dl, ImVec2(barMin.x + (bw - ts.x) * 0.5f, barMin.y - ts.y - 2.f),
                    healthColor, hpBuf);
    }

    // ── Armor bar matching health style (right of box) ──
    if (esp::config.armor_bar) {
        float armor = prepared_esp ? prepared_esp->armor
            : mem.Read<float>(ped + offset::playerArmor);
        // Only probe fallback offsets when value looks invalid (not when truly 0 armor)
        if (armor < 0.f || armor > 200.f) {
            float a2 = prepared_esp ? prepared_esp->armor_alt_1
                : mem.Read<float>(ped + 0x14E0);
            if (a2 >= 0.f && a2 <= 200.f) armor = a2;
            else {
                a2 = prepared_esp ? prepared_esp->armor_alt_2
                    : mem.Read<float>(ped + 0x1530);
                if (a2 >= 0.f && a2 <= 200.f) armor = a2;
            }
        }
        if (armor > 0.5f && armor <= 200.f) {
            float ar100 = armor;
            if (ar100 > 100.f) ar100 = 100.f;
            float arFrac = ar100 / 100.f;
            const float bw = 5.f;
            ImVec2 barMin(boxMax.x + 4.f, boxMin.y);
            ImVec2 barMax(boxMax.x + 4.f + bw, boxMax.y);
            const float filledY = barMax.y - (barMax.y - barMin.y) * arFrac;
            const ImU32 armorColor = EspPedColor(ped, esp::config.color_armor, visible);
            OmniGhost::Gameplay::EspCore::DrawVerticalBar(
                dl, barMin, barMax, arFrac, armorColor, Darker(armorColor));
            dl->AddLine(ImVec2(barMin.x, filledY), ImVec2(barMax.x, filledY),
                        IM_COL32(255, 255, 255, 60), 1.f);
            char arBuf[16];
            snprintf(arBuf, sizeof(arBuf), "%.0f", ar100);
            dl->AddText(ImVec2(barMax.x + 3.f, barMin.y - 1.f),
                        armorColor, arBuf);
        }
    }

    // ── Centered text under box: name, weapon, distance ──
    {
        const float cx = (boxMin.x + boxMax.x) * 0.5f;
        float textY = boxMax.y + 3.f;

        // Name above box
        if (esp::config.player_name || esp::config.player_id) {
            char nbuf[64]{};
            const uintptr_t pinfo = prepared_esp ? prepared_esp->player_info
                : mem.Read<uintptr_t>(ped + offset::playerInfo);
            const uint32_t netId = prepared_esp ? prepared_esp->network_id
                : (pinfo ? mem.Read<uint32_t>(pinfo + offset::playerInfo_netId) : 0);
            ReadPlayerDisplayName(ped, pinfo, netId, prepared_esp == nullptr,
                nbuf, sizeof(nbuf));
            char line[96]{};
            if (esp::config.player_name && esp::config.player_id && netId)
                snprintf(line, sizeof(line), "%s [%u]", nbuf, netId);
            else if (esp::config.player_id && netId)
                snprintf(line, sizeof(line), "ID %u", netId);
            else
                snprintf(line, sizeof(line), "%s", nbuf);
            ImVec2 ts = ImGui::CalcTextSize(line);
            DrawTextOutlined(dl, ImVec2(cx - ts.x * 0.5f, boxMin.y - ts.y - 3.f),
                        EspPedColor(ped,
                            (esp::config.player_name ? esp::config.color_name : esp::config.color_id),
                            visible), line);
        }

        if (esp::config.weapon_name) {
            uintptr_t wpnMgr = prepared_esp ? prepared_esp->weapon_manager
                : mem.Read<uintptr_t>(ped + offset::weaponManager);
            const char* wname = "Desarmado";
            char wbuf[48];
            if (wpnMgr) {
                uintptr_t wpnInfo = prepared_esp ? prepared_esp->weapon_info
                    : mem.Read<uintptr_t>(wpnMgr + offset::weaponMgr_currentWeapon);
                if (wpnInfo) {
                    uint32_t hash = prepared_esp ? prepared_esp->weapon_hash
                        : mem.Read<uint32_t>(wpnInfo + offset::weaponInfo_hash);
                    switch (hash) {
                    // Melee
                    case 0xA2719263u: wname = "Soco"; break;
                    case 0x92A27487u: wname = "Adaga"; break;
                    case 0x958A4A8Fu: wname = "Bastao"; break;
                    case 0xF9E6AA4Bu: wname = "Garrafa"; break;
                    case 0x84BD7BFDu: wname = "Crowbar"; break;
                    case 0x8BB05FD7u: wname = "Lanterna"; break;
                    case 0x440E4788u: wname = "Golf"; break;
                    case 0x4E875F73u: wname = "Martelo"; break;
                    case 0xF9DCBF2Du: wname = "Machado"; break;
                    case 0xD8DF3C3Cu: wname = "Soco Ingles"; break;
                    case 0x99B507EAu: wname = "Faca"; break;
                    case 0xDD5DF8D9u: wname = "Machete"; break;
                    case 0xDFE37640u: wname = "Canivete"; break;
                    case 0x678B81B1u: wname = "Taco"; break;
                    case 0x19044EE0u: wname = "Chave Inglesa"; break;
                    case 0xCD274149u: wname = "Battle Axe"; break;
                    case 0x94117305u: wname = "Pool Cue"; break;
                    case 0x3813BA38u: wname = "Stone Hatchet"; break;
                    // Pistols
                    case 0x1B06D571u: wname = "Pistola"; break;
                    case 0xBFE256D4u: wname = "Pistola MK2"; break;
                    case 0x5EF9FEC4u: wname = "Combat Pistol"; break;
                    case 0x22D8FE39u: wname = "AP Pistol"; break;
                    case 0x3656C8C1u: wname = "Stun Gun"; break;
                    case 0x99AEEB3Bu: wname = "Pistol .50"; break;
                    case 0xBFD21232u: wname = "SNS Pistol"; break;
                    case 0x88374054u: wname = "SNS Pistol MK2"; break;
                    case 0xD205520Eu: wname = "Heavy Pistol"; break;
                    case 0x083839C4u: wname = "Vintage Pistol"; break;
                    case 0x47757124u: wname = "Flare Gun"; break;
                    case 0xDC4DB296u: wname = "Marksman Pistol"; break;
                    case 0xC1B3C3D1u: wname = "Revolver"; break;
                    case 0xCB96392Fu: wname = "Revolver MK2"; break;
                    case 0x97EA20B8u: wname = "Double Action"; break;
                    case 0xAF3696A1u: wname = "Up-n-Atomizer"; break;
                    case 0x2B5EF5ECu: wname = "Ceramic Pistol"; break;
                    case 0x917F6C8Cu: wname = "Navy Revolver"; break;
                    case 0x57A4368Cu: wname = "Perico Pistol"; break;
                    case 0x1BC4FDB9u: wname = "WM 29"; break;
                    // SMG
                    case 0x13532244u: wname = "Micro SMG"; break;
                    case 0x2BE6766Bu: wname = "SMG"; break;
                    case 0x78A97CD0u: wname = "SMG MK2"; break;
                    case 0xEFE7E2DFu: wname = "Assault SMG"; break;
                    case 0x0A3D4D34u: wname = "Combat PDW"; break;
                    case 0xDB1AA450u: wname = "Machine Pistol"; break;
                    case 0xBD248B55u: wname = "Mini SMG"; break;
                    case 0x476BF155u: wname = "Unholy Hellbringer"; break;
                    // Shotguns
                    case 0x1D073A89u: wname = "Pump Shotgun"; break;
                    case 0x555AF99Au: wname = "Pump Shotgun MK2"; break;
                    case 0x7846A318u: wname = "Sawed-Off"; break;
                    case 0xE284C527u: wname = "Assault Shotgun"; break;
                    case 0x9D61E50Fu: wname = "Bullpup Shotgun"; break;
                    case 0xA89CB99Eu: wname = "Musket"; break;
                    case 0xEF951FBBu: wname = "Heavy Shotgun"; break;
                    case 0x12E82D3Du: wname = "Double Barrel"; break;
                    case 0x05A96BA4u: wname = "Sweeper Shotgun"; break;
                    case 0x5FC3FC38u: wname = "Combat Shotgun"; break;
                    // Rifles
                    case 0xBFEFFF6Du: wname = "Assault Rifle"; break;
                    case 0x394F415Cu: wname = "Assault Rifle MK2"; break;
                    case 0x83BF0278u: wname = "Carbine Rifle"; break;
                    case 0xFAD1F1C9u: wname = "Carbine Rifle MK2"; break;
                    case 0xAF113F99u: wname = "Advanced Rifle"; break;
                    case 0xC0A3098Du: wname = "Special Carbine"; break;
                    case 0x969C3D67u: wname = "Special Carbine MK2"; break;
                    case 0x7F229F94u: wname = "Bullpup Rifle"; break;
                    case 0x84D6FAFDu: wname = "Bullpup Rifle MK2"; break;
                    case 0x624FE830u: wname = "Compact Rifle"; break;
                    case 0x9D1F17E6u: wname = "Military Rifle"; break;
                    case 0xC78D71B4u: wname = "Heavy Rifle"; break;
                    case 0xD1D5F52Bu: wname = "Tactical Rifle"; break;
                    // MG
                    case 0x9D07F764u: wname = "MG"; break;
                    case 0x7FD62962u: wname = "Combat MG"; break;
                    case 0xDBBD7280u: wname = "Combat MG MK2"; break;
                    case 0x61012683u: wname = "Gusenberg"; break;
                    // Sniper
                    case 0x05FC3C11u: wname = "Sniper Rifle"; break;
                    case 0x0C472FE2u: wname = "Heavy Sniper"; break;
                    case 0x0A914799u: wname = "Heavy Sniper MK2"; break;
                    case 0xC734385Au: wname = "Marksman Rifle"; break;
                    case 0x6A6C02E0u: wname = "Marksman Rifle MK2"; break;
                    case 0x6E7DDDECu: wname = "Precision Rifle"; break;
                    // Heavy
                    case 0xB1CA77B1u: wname = "RPG"; break;
                    case 0xA284510Bu: wname = "Grenade Launcher"; break;
                    case 0x4DD2DC56u: wname = "Smoke Launcher"; break;
                    case 0x42BF8A85u: wname = "Minigun"; break;
                    case 0x7F7497E5u: wname = "Firework"; break;
                    case 0x6D544C99u: wname = "Railgun"; break;
                    case 0x63AB0442u: wname = "Homing Launcher"; break;
                    case 0x0781FE4Au: wname = "Compact Launcher"; break;
                    case 0xB62D1F67u: wname = "Widowmaker"; break;
                    case 0xDB2678E3u: wname = "Compact EMP"; break;
                    // Throwables
                    case 0x93E220BDu: wname = "Granada"; break;
                    case 0xA0973D5Eu: wname = "BZ Gas"; break;
                    case 0x24B17070u: wname = "Molotov"; break;
                    case 0x2C3731D9u: wname = "Sticky Bomb"; break;
                    case 0xAB564B93u: wname = "Proximity Mine"; break;
                    case 0x0787F0BBu: wname = "Snowball"; break;
                    case 0xBA45E8B8u: wname = "Pipe Bomb"; break;
                    case 0x23C9F95Cu: wname = "Ball"; break;
                    case 0xFDBC8A50u: wname = "Smoke Grenade"; break;
                    case 0x497FACC3u: wname = "Flare"; break;
                    // Misc
                    case 0x34A67B97u: wname = "Jerry Can"; break;
                    case 0xFBAB5776u: wname = "Parachute"; break;
                    case 0x060EC506u: wname = "Fire Extinguisher"; break;
                    case 0xBA536372u: wname = "Hazard Can"; break;
                    case 0x1B574AFEu: wname = "Fertilizer Can"; break;
                    case 0x184140A1u: wname = "Fertilizer Can"; break;
                    default:
                        if (hash != 0) {
                            snprintf(wbuf, sizeof(wbuf), "Arma");
                            wname = wbuf;
                        }
                        break;
                    }
                }
            }
            ImVec2 ts = ImGui::CalcTextSize(wname);
            DrawTextOutlined(dl, ImVec2(cx - ts.x * 0.5f, textY),
                        EspPedColor(ped, esp::config.color_weapon, visible), wname);
            textY += ts.y + 2.f;
        }

        if (esp::config.distance) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fm", dist);
            ImVec2 ts = ImGui::CalcTextSize(buf);
            DrawTextOutlined(dl, ImVec2(cx - ts.x * 0.5f, textY),
                        EspPedColor(ped, esp::config.color_distance, visible), buf);
        }
    }
}

// Resolve local player ONLY via world→localplayer pointer (no distance heuristics).
// CPed* at World+0x8 is the authoritative local player entity in GTA/FiveM.
static uintptr_t ResolveLocalPlayerPointer(uintptr_t fallback) {
    using namespace FiveM;
    // ESP manager refreshes World+0x8 once at the start of every frame.
    // Re-reading it here used to add one synchronous DMA request per player.
    return offset::localplayer ? offset::localplayer : fallback;
}

static bool IsLocalPlayerPed(uintptr_t ped, uintptr_t localplayer) {
    if (!ped) return false;
    uintptr_t lp = ResolveLocalPlayerPointer(localplayer);
    return lp != 0 && ped == lp;
}

// Main rendering dispatcher
void esp::render_esp_for_ped(uintptr_t ped, Matrix viewport, uintptr_t localplayer) {
    if (!config.enabled) return;
    if (!config.self_esp && IsLocalPlayerPed(ped, localplayer)) return;
    // Team check = hide friends from ESP when enabled
    if (config.team_check && EspPedIsFriend(ped, true))
        return;
    // Visibility: when ON, skip entities that are not visible
    if (config.visible_check && !EspPedVisible(ped))
        return;

    if (config.head_circle)
        draw_head_circle(ped, viewport, localplayer);
    if (config.skeleton)
        draw_skeleton(ped, viewport, localplayer);

    DrawMotionVisuals(ped, viewport, nullptr);

    if (AnyEspExtrasEnabled())
        DrawEspExtras(ped, viewport, localplayer, nullptr);
}

void esp::render_esp_for_ped_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data) {
    if (!config.enabled) return;
    if (!config.self_esp && IsLocalPlayerPed(ped, localplayer)) return;
    if (config.team_check && EspPedIsFriend(ped, true))
        return;
    if (config.visible_check && !EspPedVisible(ped))
        return;

    if (config.head_circle)
        draw_head_circle_cached(ped, viewport, localplayer, cached_ped_data);
    if (config.skeleton)
        draw_skeleton(ped, viewport, localplayer);

    DrawMotionVisuals(ped, viewport, &cached_ped_data);

    if (AnyEspExtrasEnabled()) DrawEspExtras(ped, viewport, localplayer, &cached_ped_data);
}

// HEAD CIRCLE ESP (modified to use new health bar)
static void DrawHeadCircleAt(ImDrawList* draw_list, const Vec2& screen, ImU32 col, float distance_m = 25.f) {
    // Scale with distance: close ≈ 8px, far ≈ 2px (never a giant blob at range)
    float r = 220.0f / (distance_m + 12.0f);
    if (r < 2.0f) r = 2.0f;
    if (r > 9.0f) r = 9.0f;
    const float th = (r > 4.f) ? 2.0f : 1.4f;
    // LOD segments: far = fewer verts (cheaper), near = smoother
    const int segs = (distance_m > 80.f) ? 10 : (distance_m > 40.f) ? 14 : 20;
    if (esp::config.circle_type == 1)
        draw_list->AddCircleFilled(ImVec2(screen.x, screen.y), r, col, segs);
    else
        draw_list->AddCircle(ImVec2(screen.x, screen.y), r, col, segs, th);
    if (esp::config.circle_type == 2) {
        draw_list->AddLine(ImVec2(screen.x - r - 2, screen.y), ImVec2(screen.x + r + 2, screen.y), col, th);
        draw_list->AddLine(ImVec2(screen.x, screen.y - r - 2), ImVec2(screen.x, screen.y + r + 2), col, th);
    }
}

void esp::draw_head_circle(uintptr_t ped, Matrix viewport, uintptr_t localplayer) {
    (void)localplayer;
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    if (!draw_list) return;

    const BatchSkeletonData* prepared = FindPreparedSkeleton(ped);
    Vec3 origin = prepared ? prepared->origin : Vec3{};
    if (origin.IsZero())
        origin = mem.Read<Vec3>(ped + FiveM::offset::playerPosition);

    Vec3 head_world_pos = prepared
        ? PreparedBonePosition(prepared, 0)
        : esp::get_bone_position(ped, 0);
    if (head_world_pos.IsZero() || head_world_pos.distance_to(origin) > 3.5f) {
        head_world_pos = origin;
        head_world_pos.z += 0.9f;
    }

    Vec2 head_screen_pos;
    if (!head_world_pos.world_to_screen(viewport, head_screen_pos))
        return;

    float dist = 25.f;
    if (localplayer) {
        Vec3 lp = FiveM::ESP::FrameCacheValid()
            ? FiveM::ESP::GetFrameLocalPos()
            : mem.Read<Vec3>(localplayer + FiveM::offset::playerPosition);
        if (!lp.IsZero() && !head_world_pos.IsZero())
            dist = lp.distance_to(head_world_pos);
    }
    const bool visible = EspPedVisible(ped);
    ImU32 col = EspPedColor(ped, config.color_head_circle, visible);
    DrawHeadCircleAt(draw_list, head_screen_pos, col, dist);
}

void esp::draw_head_circle_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data) {
    (void)localplayer;
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    if (!draw_list) return;

    const BatchSkeletonData* prepared = FindPreparedSkeleton(ped);
    Vec3 origin = prepared ? prepared->origin : cached_ped_data.position_origin;
    if (origin.IsZero())
        origin = mem.Read<Vec3>(ped + 0x90);

    Vec3 head_world_pos = prepared
        ? PreparedBonePosition(prepared, 0)
        : esp::get_bone_position(ped, 0);
    if (head_world_pos.IsZero() || head_world_pos.distance_to(origin) > 3.5f) {
        head_world_pos = origin;
        head_world_pos.z += 0.9f;
    }

    Vec2 head_screen_pos;
    if (!head_world_pos.world_to_screen(viewport, head_screen_pos))
        return;

    const bool visible = EspPedVisible(ped);
    ImU32 color = EspPedColor(ped, config.color_head_circle, visible);
    if (!config.visibility_colors && cached_ped_data.health < 50.0f)
        color = IM_COL32(255, 255, 0, 255);
    if (!config.visibility_colors && cached_ped_data.health < 25.0f)
        color = IM_COL32(255, 0, 0, 255);
    if (cached_ped_data.health <= 0.0f) color = IM_COL32(100, 100, 100, 255);

    float dist = 25.f;
    if (localplayer) {
        Vec3 lp = FiveM::ESP::FrameCacheValid()
            ? FiveM::ESP::GetFrameLocalPos()
            : mem.Read<Vec3>(localplayer + FiveM::offset::playerPosition);
        if (!lp.IsZero())
            dist = lp.distance_to(head_world_pos);
    }
    DrawHeadCircleAt(draw_list, head_screen_pos, color, dist);
}

// SKELETON ESP (modified to use new health bar)
void esp::draw_skeleton_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data) {
    (void)cached_ped_data;
    // Same path as live skeleton so preview and game never diverge
    draw_skeleton(ped, viewport, localplayer);
}

void esp::draw_skeleton(uintptr_t ped, Matrix viewport, uintptr_t localplayer) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    if (!draw_list) return;

    // Per-ped temporal smooth state (stops flicker)
    struct SkelSmooth {
        Vec3 head{}, neck{}, hip{}, lfoot{}, rfoot{}, lhand{}, rhand{}, origin{};
        int lod = 0;
        int swapHands = 0;
        bool init = false;
        int missFrames = 0; // consecutive bad reads
        uint32_t lastFrame = 0;
    };
    static std::unordered_map<uintptr_t, SkelSmooth> s_smooth;
    static uint32_t s_frameId = 0;
    static uint32_t s_lastBump = 0;
    // Bump frame id once per ImGui frame
    {
        uint32_t f = (uint32_t)ImGui::GetFrameCount();
        if (f != s_lastBump) { s_lastBump = f; s_frameId = f; }
        // Erase smooth entries not touched for 2+ frames (ped left / recycled)
        if ((f & 15) == 0) {
            for (auto it = s_smooth.begin(); it != s_smooth.end(); ) {
                if (s_frameId > it->second.lastFrame + 2)
                    it = s_smooth.erase(it);
                else
                    ++it;
            }
        }
    }
    SkelSmooth& sm = s_smooth[ped];
    sm.lastFrame = s_frameId;

    enum {
        B_HEAD = 0, B_LFOOT = 1, B_RFOOT = 2,
        B_LHAND_A = 5, B_RHAND_A = 6,
        B_LHAND_B = 3, B_RHAND_B = 4,
        B_NECK = 7, B_HIP = 8
    };

    const BatchSkeletonData* prepared = FindPreparedSkeleton(ped);
    Vec3 origin = prepared ? prepared->origin : Vec3{};
    if (origin.IsZero())
        origin = mem.Read<Vec3>(ped + FiveM::offset::playerPosition);
    if (origin.IsZero())
        origin = mem.Read<Vec3>(ped + 0x90);
    if (origin.IsZero())
        return;

    float lodDist = 0.f;
    if (localplayer) { // LOD always on with skeleton
        Vec3 lp{};
        if (FiveM::ESP::FrameCacheValid())
            lp = FiveM::ESP::GetFrameLocalPos();
        if (lp.IsZero()) {
            lp = mem.Read<Vec3>(localplayer + 0x90);
            if (lp.IsZero())
                lp = mem.Read<Vec3>(localplayer + FiveM::offset::playerPosition);
        }
        if (!lp.IsZero()) {
            lodDist = origin.distance_to(lp);
        }
    }
    {
        const float maxDist = (config.max_esp_distance > 1.f) ? config.max_esp_distance : 150.f;
        if (lodDist > maxDist)
            return;
    }

    // Preserve the complete anatomical skeleton at every supported distance.
    sm.lod = 0;

    Matrix bone_matrix{};
    Vector3 localBones[9]{};
    if (prepared && prepared->bone_offsets.size() >= 9) {
        bone_matrix = prepared->bone_matrix;
        for (int i = 0; i < 9; ++i)
            localBones[i] = prepared->bone_offsets[static_cast<size_t>(i)];
    } else {
        auto h = mem.CreateScatterHandle();
        if (!h) return;
        mem.AddScatterReadRequest(h, ped + 0x60, &bone_matrix, sizeof(Matrix));
        for (int i = 0; i < 9; ++i)
            mem.AddScatterReadRequest(h, ped + (0x410 + 0x10 * i), &localBones[i], sizeof(Vector3));
        mem.ExecuteReadScatter(h);
        mem.CloseScatterHandle(h);
    }

    auto readBone = [&](int idx) -> Vec3 {
        if (idx < 0 || idx > 8) return {};
        // Zero local offset often = unread/invalid slot
        if (localBones[idx].x == 0.f && localBones[idx].y == 0.f && localBones[idx].z == 0.f)
            return {};
        DirectX::SimpleMath::Vector3 boneVec(localBones[idx].x, localBones[idx].y, localBones[idx].z);
        DirectX::SimpleMath::Vector3 tf = DirectX::XMVector3Transform(boneVec, bone_matrix);
        Vec3 b(tf.x, tf.y, tf.z);
        if (b.IsZero()) return {};
        // Tighter radius around entity origin (stops wild legs)
        if (b.distance_to(origin) > 3.2f) return {};
        return b;
    };

    Vec3 head  = readBone(B_HEAD);
    Vec3 neck  = readBone(B_NECK);
    Vec3 hip   = readBone(B_HIP);
    Vec3 lfoot = readBone(B_LFOOT);
    Vec3 rfoot = readBone(B_RFOOT);
    Vec3 lhand = readBone(B_LHAND_A);
    Vec3 rhand = readBone(B_RHAND_A);
    if (lhand.IsZero()) lhand = readBone(B_LHAND_B);
    if (rhand.IsZero()) rhand = readBone(B_RHAND_B);

    // Sanitize limbs vs body: reject feet/hands that are anatomically impossible
    auto limbOk = [&](const Vec3& limb, const Vec3& root, float maxDist) -> bool {
        if (limb.IsZero() || root.IsZero()) return false;
        float d = limb.distance_to(root);
        return d > 0.05f && d < maxDist;
    };
    // Feet must be near origin/hip and mostly below head
    if (!lfoot.IsZero() && head.IsZero() == false) {
        if (lfoot.z > head.z + 0.2f || !limbOk(lfoot, origin, 2.4f)) lfoot = {};
    }
    if (!rfoot.IsZero() && head.IsZero() == false) {
        if (rfoot.z > head.z + 0.2f || !limbOk(rfoot, origin, 2.4f)) rfoot = {};
    }
    if (!lhand.IsZero() && !origin.IsZero() && !limbOk(lhand, origin, 2.6f)) lhand = {};
    if (!rhand.IsZero() && !origin.IsZero() && !limbOk(rhand, origin, 2.6f)) rhand = {};
    // Feet should be relatively close to each other (not one left 20m behind)
    if (!lfoot.IsZero() && !rfoot.IsZero() && lfoot.distance_to(rfoot) > 1.8f) {
        // Keep the foot closer to origin, drop the outlier
        if (lfoot.distance_to(origin) > rfoot.distance_to(origin)) lfoot = {};
        else rfoot = {};
    }

    // Count real bone hits this frame (not fabricated)
    int realHits = 0;
    if (!head.IsZero()) realHits++;
    if (!lfoot.IsZero()) realHits++;
    if (!rfoot.IsZero()) realHits++;
    if (!neck.IsZero()) realHits++;
    if (!hip.IsZero()) realHits++;
    if (!lhand.IsZero()) realHits++;
    if (!rhand.IsZero()) realHits++;

    // Ped pointer recycled or entity gone → don't draw ghost
    if (realHits < 2) {
        sm.missFrames++;
        if (sm.missFrames > 1) {
            s_smooth.erase(ped);
            return;
        }
        // one frame grace: use previous smooth only if init
        if (!sm.init) return;
        head = sm.head; neck = sm.neck; hip = sm.hip;
        lfoot = sm.lfoot; rfoot = sm.rfoot;
        lhand = sm.lhand; rhand = sm.rhand;
    } else {
        sm.missFrames = 0;
    }

    // Required anchors — synthetic feet stay under the body (no lag limb)
    if (head.IsZero())  { head = origin; head.z += 0.95f; }
    if (lfoot.IsZero()) { lfoot = origin; lfoot.x -= 0.12f; lfoot.z = origin.z; }
    if (rfoot.IsZero()) { rfoot = origin; rfoot.x += 0.12f; rfoot.z = origin.z; }
    if (neck.IsZero())  { neck = head; neck.z -= 0.14f; }
    if (hip.IsZero())   {
        hip = origin;
        hip.z += 0.15f;
        hip.x = (head.x + origin.x) * 0.5f;
        hip.y = (head.y + origin.y) * 0.5f;
    }

    // Stabilize L/R with hysteresis (prevents flip every frame = "pistar")
    auto horiz = [](const Vec3& a, const Vec3& b) {
        return Vec3(b.x - a.x, b.y - a.y, 0.f);
    };
    if (!lhand.IsZero() && !rhand.IsZero() && !lfoot.IsZero() && !rfoot.IsZero()) {
        Vec3 fdir = horiz(lfoot, rfoot);
        Vec3 hdir = horiz(lhand, rhand);
        float dot = fdir.x * hdir.x + fdir.y * hdir.y;
        if (sm.swapHands == 0) {
            if (dot < -0.15f) sm.swapHands = 1;
        } else if (sm.swapHands == 1) {
            if (dot > 0.15f) sm.swapHands = 0;
        } else {
            sm.swapHands = (dot < 0.f) ? 1 : 0;
        }
        if (sm.swapHands == 1)
            std::swap(lhand, rhand);
    }
    if (lhand.IsZero()) { lhand = neck; lhand.x -= 0.35f; lhand.z -= 0.35f; }
    if (rhand.IsZero()) { rhand = neck; rhand.x += 0.35f; rhand.z -= 0.35f; }

    // Translate the complete previous pose by the ped's current movement before
    // filtering limb animation. This removes the visible trail while running.
    if (sm.init && !sm.origin.IsZero()) {
        const Vec3 delta(origin.x - sm.origin.x, origin.y - sm.origin.y, origin.z - sm.origin.z);
        auto translate = [&](Vec3& value) {
            value.x += delta.x;
            value.y += delta.y;
            value.z += delta.z;
        };
        translate(sm.head); translate(sm.neck); translate(sm.hip);
        translate(sm.lfoot); translate(sm.rfoot);
        translate(sm.lhand); translate(sm.rhand);
    }
    sm.origin = origin;

    // Exponential smooth anchors in world space (solid, not jittery)
    auto smoothTo = [](Vec3& prev, const Vec3& cur, float a, bool inited) {
        if (!inited || prev.IsZero()) { prev = cur; return; }
        float dx = cur.x - prev.x, dy = cur.y - prev.y, dz = cur.z - prev.z;
        float dist2 = dx*dx + dy*dy + dz*dz;
        // Big teleport / entity reuse → snap, don't drag ghost
        if (dist2 > 1.8f * 1.8f) { prev = cur; return; }
        prev.x = prev.x + (cur.x - prev.x) * a;
        prev.y = prev.y + (cur.y - prev.y) * a;
        prev.z = prev.z + (cur.z - prev.z) * a;
    };
    // Higher alpha = more solid tracking, less smear behind player
    const float alpha = app_settings::config.performance_mode ? 0.86f : 0.78f;
    smoothTo(sm.head, head, alpha, sm.init);
    smoothTo(sm.neck, neck, alpha, sm.init);
    smoothTo(sm.hip, hip, alpha, sm.init);
    smoothTo(sm.lfoot, lfoot, alpha, sm.init);
    smoothTo(sm.rfoot, rfoot, alpha, sm.init);
    smoothTo(sm.lhand, lhand, alpha, sm.init);
    smoothTo(sm.rhand, rhand, alpha, sm.init);
    sm.init = true;
    head = sm.head; neck = sm.neck; hip = sm.hip;
    lfoot = sm.lfoot; rfoot = sm.rfoot;
    lhand = sm.lhand; rhand = sm.rhand;

    // === Full anatomical skeleton (photo-style) — side axis from spine×hands, never feet ===
    auto lerp3 = [](const Vec3& a, const Vec3& b, float t) -> Vec3 {
        return Vec3(a.x + (b.x - a.x) * t,
                    a.y + (b.y - a.y) * t,
                    a.z + (b.z - a.z) * t);
    };
    auto vlen = [](const Vec3& v) -> float {
        return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    };
    auto vnorm = [&](Vec3 v) -> Vec3 {
        float L = vlen(v);
        if (L < 1e-4f) return Vec3(1.f, 0.f, 0.f);
        return Vec3(v.x / L, v.y / L, v.z / L);
    };
    auto vcross = [](const Vec3& a, const Vec3& b) -> Vec3 {
        return Vec3(a.y * b.z - a.z * b.y,
                    a.z * b.x - a.x * b.z,
                    a.x * b.y - a.y * b.x);
    };
    auto vadd = [](const Vec3& a, const Vec3& b) -> Vec3 {
        return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
    };
    auto vscale = [](const Vec3& a, float s) -> Vec3 {
        return Vec3(a.x * s, a.y * s, a.z * s);
    };

    // Spine direction (hip → neck). Side axis from hands projected orthogonal to spine
    // (feet swap while walking — never use feet for lateral axis).
    Vec3 spineDir = vnorm(Vec3(neck.x - hip.x, neck.y - hip.y, neck.z - hip.z));
    Vec3 side;
    {
        Vec3 handSpan(rhand.x - lhand.x, rhand.y - lhand.y, rhand.z - lhand.z);
        // Remove component along spine so shoulders stay level with torso
        float along = handSpan.x * spineDir.x + handSpan.y * spineDir.y + handSpan.z * spineDir.z;
        handSpan.x -= spineDir.x * along;
        handSpan.y -= spineDir.y * along;
        handSpan.z -= spineDir.z * along;
        if (vlen(handSpan) > 0.12f) {
            side = vnorm(handSpan);
        } else {
            // Fallback: cross spine with world-up
            side = vnorm(vcross(spineDir, Vec3(0.f, 0.f, 1.f)));
            if (vlen(side) < 0.1f)
                side = vnorm(vcross(spineDir, Vec3(0.f, 1.f, 0.f)));
        }
    }

    // Body proportions (metres) — tuned to look like photo skeletons
    const float shW  = 0.20f;   // shoulder half-width from neck
    const float clav = 0.10f;   // clavicle inset
    const float hipW = 0.11f;   // hip joint outward

    // ── Spine (multi-segment like real bone ESP) ──
    Vec3 spineU = lerp3(neck, hip, 0.18f);
    Vec3 chest  = lerp3(neck, hip, 0.34f);
    Vec3 spineM = lerp3(neck, hip, 0.55f);
    Vec3 spineL = lerp3(neck, hip, 0.78f);

    // ── Shoulders / clavicles (out from neck along stable side axis) ──
    Vec3 lclav = vadd(neck, vscale(side, -clav)); lclav.z -= 0.02f;
    Vec3 rclav = vadd(neck, vscale(side,  clav)); rclav.z -= 0.02f;
    Vec3 lsh   = vadd(neck, vscale(side, -shW));  lsh.z   -= 0.04f;
    Vec3 rsh   = vadd(neck, vscale(side,  shW));  rsh.z   -= 0.04f;

    // Pull shoulders slightly toward real hands so arms track pose
    lsh = lerp3(lsh, lhand, 0.08f);
    rsh = lerp3(rsh, rhand, 0.08f);

    // ── Arms: upper → elbow → forearm → wrist → hand ──
    // Elbow is 50% but pushed slightly outward for natural bend (photo look)
    Vec3 lelb = lerp3(lsh, lhand, 0.48f);
    Vec3 relb = lerp3(rsh, rhand, 0.48f);
    lelb = vadd(lelb, vscale(side, -0.03f));
    relb = vadd(relb, vscale(side,  0.03f));
    Vec3 lupper = lerp3(lsh,  lelb,  0.50f);
    Vec3 rupper = lerp3(rsh,  relb,  0.50f);
    Vec3 lfore  = lerp3(lelb, lhand, 0.45f);
    Vec3 rfore  = lerp3(relb, rhand, 0.45f);
    Vec3 lwrist = lerp3(lelb, lhand, 0.82f);
    Vec3 rwrist = lerp3(relb, rhand, 0.82f);

    // ── Hips / legs ──
    Vec3 lhip = vadd(hip, vscale(side, -hipW));
    Vec3 rhip = vadd(hip, vscale(side,  hipW));
    Vec3 lknee = lerp3(lhip, lfoot, 0.48f);
    Vec3 rknee = lerp3(rhip, rfoot, 0.48f);
    // Slight outward knee for natural stance
    lknee = vadd(lknee, vscale(side, -0.02f));
    rknee = vadd(rknee, vscale(side,  0.02f));
    Vec3 lthigh = lerp3(lhip,  lknee, 0.50f);
    Vec3 rthigh = lerp3(rhip,  rknee, 0.50f);
    Vec3 lshin  = lerp3(lknee, lfoot, 0.45f);
    Vec3 rshin  = lerp3(rknee, rfoot, 0.45f);
    Vec3 lankle = lerp3(lknee, lfoot, 0.82f);
    Vec3 rankle = lerp3(rknee, rfoot, 0.82f);

    struct Jnt { Vec3 w; Vec2 s; bool on; };
    Jnt J[36]{};
    int n = 0;
    auto add = [&](const Vec3& w) -> int {
        if (n >= 36) return -1;
        int i = n++;
        J[i].w = w;
        J[i].on = w.world_to_screen(viewport, J[i].s);
        return i;
    };

    const int iHead   = add(head);
    const int iNeck   = add(neck);
    const int iSpU    = add(spineU);
    const int iChest  = add(chest);
    const int iSpM    = add(spineM);
    const int iSpL    = add(spineL);
    const int iHip    = add(hip);

    const int iLclav  = add(lclav);
    const int iLsh    = add(lsh);
    const int iLupper = add(lupper);
    const int iLelb   = add(lelb);
    const int iLfore  = add(lfore);
    const int iLwrist = add(lwrist);
    const int iLhand  = add(lhand);

    const int iRclav  = add(rclav);
    const int iRsh    = add(rsh);
    const int iRupper = add(rupper);
    const int iRelb   = add(relb);
    const int iRfore  = add(rfore);
    const int iRwrist = add(rwrist);
    const int iRhand  = add(rhand);

    const int iLhip   = add(lhip);
    const int iLthigh = add(lthigh);
    const int iLknee  = add(lknee);
    const int iLshin  = add(lshin);
    const int iLankle = add(lankle);
    const int iLfoot  = add(lfoot);

    const int iRhip   = add(rhip);
    const int iRthigh = add(rthigh);
    const int iRknee  = add(rknee);
    const int iRshin  = add(rshin);
    const int iRankle = add(rankle);
    const int iRfoot  = add(rfoot);

    const bool visible = EspPedVisible(ped);
    ImU32 col = EspPedColor(ped, config.color_skeleton, visible);
    ImU32 jointCol = EspPedColor(ped, config.color_skeleton_points, visible);

    float bodyPx = 50.f;
    if (iHead >= 0 && iHip >= 0 && J[iHead].on && J[iHip].on)
        bodyPx = fabsf(J[iHip].s.y - J[iHead].s.y);
    float th = line_thickness;
    if (bodyPx < 70.f) th = (std::max)(th, 1.7f);
    if (bodyPx < 35.f) th = (std::max)(th, 2.1f);

    auto link = [&](int a, int b) {
        if (a < 0 || b < 0 || !J[a].on || !J[b].on) return;
        float dx = J[a].s.x - J[b].s.x;
        float dy = J[a].s.y - J[b].s.y;
        float len2 = dx * dx + dy * dy;
        // Allow longer segments for full arms/legs (~ body height)
        float maxSeg = bodyPx * 0.95f;
        if (maxSeg < 22.f) maxSeg = 22.f;
        if (maxSeg > 260.f) maxSeg = 260.f;
        if (len2 > maxSeg * maxSeg || len2 < 0.8f) return;
        draw_list->AddLine(ImVec2(J[a].s.x, J[a].s.y),
                           ImVec2(J[b].s.x, J[b].s.y), col, th);
    };

    // Spine column
    link(iHead, iNeck);
    link(iNeck, iSpU);
    link(iSpU, iChest);
    link(iChest, iSpM);
    link(iSpM, iSpL);
    link(iSpL, iHip);

    // Full detail at every supported distance.
    link(iNeck, iLclav); link(iLclav, iLsh);
    link(iLsh, iLupper); link(iLupper, iLelb);
    link(iLelb, iLfore); link(iLfore, iLwrist); link(iLwrist, iLhand);

    link(iNeck, iRclav); link(iRclav, iRsh);
    link(iRsh, iRupper); link(iRupper, iRelb);
    link(iRelb, iRfore); link(iRfore, iRwrist); link(iRwrist, iRhand);

    link(iHip, iLhip);
    link(iLhip, iLthigh); link(iLthigh, iLknee);
    link(iLknee, iLshin); link(iLshin, iLankle); link(iLankle, iLfoot);

    link(iHip, iRhip);
    link(iRhip, iRthigh); link(iRthigh, iRknee);
    link(iRknee, iRshin); link(iRshin, iRankle); link(iRankle, iRfoot);

    // Joint dots for the complete anatomical skeleton.
    if (config.joints) {
        const float jointR = (std::max)(2.2f, th * 1.15f);
        const ImU32 jc = jointCol;
        static const int kClose[] = {
            iHead, iNeck, iSpU, iChest, iSpM, iSpL, iHip,
            iLclav, iLsh, iLupper, iLelb, iLfore, iLwrist, iLhand,
            iRclav, iRsh, iRupper, iRelb, iRfore, iRwrist, iRhand,
            iLhip, iLthigh, iLknee, iLshin, iLankle, iLfoot,
            iRhip, iRthigh, iRknee, iRshin, iRankle, iRfoot
        };
        const int* list = kClose;
        const int count = static_cast<int>(sizeof(kClose) / sizeof(kClose[0]));
        for (int i = 0; i < count; ++i) {
            const int idx = list[i];
            if (idx < 0 || !J[idx].on) continue;
            draw_list->AddCircleFilled(ImVec2(J[idx].s.x, J[idx].s.y), jointR, jc, 12);
            draw_list->AddCircle(ImVec2(J[idx].s.x, J[idx].s.y), jointR,
                IM_COL32(0, 0, 0, 200), 12, 1.0f);
        }
    }
}

void esp::DrawPlayerRadar(const Matrix& /*view_matrix*/, uintptr_t localplayer) {
    if (!localplayer) return;

    // Square corner minimap (legacy) — only if explicitly enabled
    if (config.square_radar && config.radar_enabled && !FiveM::ESP::validPeds.empty()) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;
        ImVec2 scr = ImGui::GetIO().DisplaySize;
        float cx = scr.x * config.radar_pos_x;
        float cy = scr.y * config.radar_pos_y;
        float R = config.radar_size;
        float range = (std::max)(20.f, config.radar_range);
        dl->AddCircleFilled(ImVec2(cx, cy), R + 4.f, IM_COL32(8, 8, 12, 180), 64);
        dl->AddCircle(ImVec2(cx, cy), R, IM_COL32(40, 40, 50, 255), 64, 1.0f);
        dl->AddCircleFilled(ImVec2(cx, cy), 3.5f, IM_COL32(0, 255, 120, 255), 12);
        Vec3 localPos = mem.Read<Vec3>(localplayer + FiveM::offset::playerPosition);
        if (localPos.IsZero()) localPos = mem.Read<Vec3>(localplayer + 0x90);
        Matrix lm = mem.Read<Matrix>(localplayer + 0x60);
        float fx = lm._21, fy = lm._22;
        float fl = sqrtf(fx * fx + fy * fy);
        if (fl > 1e-3f) { fx /= fl; fy /= fl; } else { fx = 0.f; fy = 1.f; }
        float rx = fy, ry = -fx;
        for (size_t i = 0; i < FiveM::ESP::validPeds.size(); ++i) {
            uintptr_t ped = FiveM::ESP::validPeds[i];
            if (!ped || ped == localplayer) continue;
            const bool visible = EspPedVisible(ped);
            if (config.visible_check && !visible) continue;
            Vec3 pos = (i < FiveM::ESP::positions.size()) ? FiveM::ESP::positions[i] : Vec3{};
            if (pos.IsZero()) continue;
            float dx = pos.x - localPos.x, dy = pos.y - localPos.y;
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > range || dist < 0.5f) continue;
            float localX = dx * rx + dy * ry;
            float localY = dx * fx + dy * fy;
            float nx = (localX / range) * R;
            float ny = (-localY / range) * R;
            float pr = sqrtf(nx * nx + ny * ny);
            if (pr > R && pr > 1e-3f) { nx = nx / pr * R; ny = ny / pr * R; }
            ImU32 col = EspPedColor(ped, config.color_visible, visible);
            if (friends::IsFriendPed(ped)) col = friends::config.friend_color;
            dl->AddCircleFilled(ImVec2(cx + nx, cy + ny), 3.0f, col, 10);
        }
    }

    // Triangle radar around crosshair (default) — tracks aim FOV rings
    if (!config.triangle_radar) return;
    if (FiveM::ESP::validPeds.empty()) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    ImVec2 scr = ImGui::GetIO().DisplaySize;
    ImVec2 center(scr.x * 0.5f, scr.y * 0.5f);

    // Radius follows the largest active aim FOV
    float radius = config.triangle_radar_radius;
    // Read aim FOVs via include
    // (aimbot::config is in aimbot namespace)
    // We'll use a soft link: include was already possible from esp_manager path
    // Direct use of aimbot::config requires including aimbot.h — added below in file includes.
    // aimbot config linked at call site prefers dynamic radius:
    // radius = max(base, active_fov * 0.55)
    // Implemented with weak default; esp_manager can set via config.triangle_radar_radius before call.

    radius = (std::max)(40.f, config.triangle_radar_radius);

    Vec3 localPos = mem.Read<Vec3>(localplayer + FiveM::offset::playerPosition);
    if (localPos.IsZero()) localPos = mem.Read<Vec3>(localplayer + 0x90);
    if (localPos.IsZero()) return;

    // Use same view as ESP when possible — avoid extra read jitter
    Matrix view = mem.Read<Matrix>(FiveM::offset::viewport + 0x24C);

    // Smooth triangle ring positions (stops flicker)
    struct TriSm { float ix, iy; bool init; uint32_t lastF; };
    static std::unordered_map<uintptr_t, TriSm> s_tri;
    static uint32_t s_triBump = 0;
    const uint32_t frame = (uint32_t)ImGui::GetFrameCount();
    if (frame != s_triBump) {
        s_triBump = frame;
        if ((frame & 31) == 0) {
            for (auto it = s_tri.begin(); it != s_tri.end(); ) {
                if (frame > it->second.lastF + 3) it = s_tri.erase(it);
                else ++it;
            }
        }
    }

    const float maxDist = (config.max_esp_distance > 1.f) ? config.max_esp_distance : 150.f;

    for (size_t i = 0; i < FiveM::ESP::validPeds.size(); ++i) {
        uintptr_t ped = FiveM::ESP::validPeds[i];
        if (!ped || ped == localplayer) continue;
        if (config.team_check && friends::IsFriendPed(ped)) continue;
        const bool visible = EspPedVisible(ped);
        if (config.visible_check && !visible) continue;

        Vec3 pos = (i < FiveM::ESP::positions.size()) ? FiveM::ESP::positions[i] : Vec3{};
        if (pos.IsZero()) continue;

        float dist = localPos.distance_to(pos);
        if (dist > maxDist) continue;
        if (dist < 0.8f) continue;

        Vec2 sp;
        bool onScreen = pos.world_to_screen(view, sp);
        if (!onScreen) {
            float wdx = pos.x - localPos.x;
            float wdy = pos.y - localPos.y;
            float ang = atan2f(wdx, wdy);
            sp.x = center.x + sinf(ang) * radius;
            sp.y = center.y - cosf(ang) * radius;
        }

        float dx = sp.x - center.x;
        float dy = sp.y - center.y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len < 1.f) continue;

        float nx = dx / len;
        float ny = dy / len;
        float rawIx = center.x + nx * radius;
        float rawIy = center.y + ny * radius;

        TriSm& ts = s_tri[ped];
        ts.lastF = frame;
        if (!ts.init) {
            ts.ix = rawIx; ts.iy = rawIy; ts.init = true;
        } else {
            // Soft lerp — solid triangles, no blink
            ts.ix += (rawIx - ts.ix) * 0.45f;
            ts.iy += (rawIy - ts.iy) * 0.45f;
        }
        float ix = ts.ix, iy = ts.iy;
        // Recompute direction from smoothed point
        float sdx = ix - center.x, sdy = iy - center.y;
        float sl = sqrtf(sdx * sdx + sdy * sdy);
        if (sl < 1.f) continue;
        nx = sdx / sl; ny = sdy / sl;
        ix = center.x + nx * radius;
        iy = center.y + ny * radius;

        float tx = -ny, ty = nx;
        float size = 7.f;
        ImVec2 tip(ix + nx * size, iy + ny * size);
        ImVec2 a(ix - nx * size * 0.6f + tx * size * 0.7f, iy - ny * size * 0.6f + ty * size * 0.7f);
        ImVec2 b(ix - nx * size * 0.6f - tx * size * 0.7f, iy - ny * size * 0.6f - ty * size * 0.7f);

        ImU32 col = EspPedColor(ped, config.color_visible, visible);
        if (friends::IsFriendPed(ped)) col = friends::config.friend_color;

        dl->AddTriangleFilled(tip, a, b, col);
        dl->AddTriangle(tip, a, b, IM_COL32(0, 0, 0, 160), 1.0f);
    }
}




void esp::draw_enhanced_health_info(const Vec2& position, float health) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();

    // Health bar
    float health_ratio = health / 100.0f;
    if (health_ratio < 0.0f) health_ratio = 0.0f;
    if (health_ratio > 1.0f) health_ratio = 1.0f;

    float bar_width = 40.0f;
    float bar_height = 5.0f;

    // Background bar
    draw_list->AddRectFilled(
        ImVec2(position.x - bar_width / 2, position.y - 25),
        ImVec2(position.x + bar_width / 2, position.y - 25 + bar_height),
        IM_COL32(0, 0, 0, 180)
    );

    // Health bar with gradient coloring
    ImU32 health_color;
    if (health > 75.0f) {
        health_color = IM_COL32(0, 255, 0, 255);     // Green
    }
    else if (health > 50.0f) {
        health_color = IM_COL32(255, 255, 0, 255);   // Yellow
    }
    else if (health > 25.0f) {
        health_color = IM_COL32(255, 165, 0, 255);   // Orange
    }
    else {
        health_color = IM_COL32(255, 0, 0, 255);     // Red
    }

    draw_list->AddRectFilled(
        ImVec2(position.x - bar_width / 2, position.y - 25),
        ImVec2(position.x - bar_width / 2 + bar_width * health_ratio, position.y - 25 + bar_height),
        health_color
    );

    // Health text
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.0f", health);
    draw_list->AddText(
        ImVec2(position.x - 10, position.y - 18),
        IM_COL32(255, 255, 255, 255),
        buffer
    );
}

// Utility functions
void esp::draw_health_info(const Vec2& position, float health) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.1f", health);

    // Color based on health
    ImU32 text_color = IM_COL32(255, 255, 255, 255); // White
    if (health < 50.0f) text_color = IM_COL32(255, 255, 0, 255); // Yellow
    if (health < 25.0f) text_color = IM_COL32(255, 0, 0, 255); // Red

    draw_list->AddText(ImVec2(position.x, position.y - 20), text_color, buffer);
}

// Your existing functions (keeping them exactly as they are)
Vec3 esp::get_bone_position(uintptr_t ped, int bone_position)
{
    Matrix bone_matrix = mem.Read<Matrix>(ped + 0x60);
    Vector3 Head = mem.Read<Vector3>(ped + (0x410 + 0x10 * bone_position));
    DirectX::SimpleMath::Vector3 boneVec(Head.x, Head.y, Head.z);
    DirectX::SimpleMath::Vector3 transformedBoneVec = DirectX::XMVector3Transform(boneVec, bone_matrix);
    return Vec3(transformedBoneVec.x, transformedBoneVec.y, transformedBoneVec.z);
}

Vec3 esp::find_closest_player(Vec3& localPlayerPosition, Matrix viewmatrix) {
    (void)viewmatrix;
    float minDistance = 1000.0f;
    Vec3 closestPedPosition = Vec3(1.0f, 1.0f, 1.0f);

    // Get all valid ped IDs
    std::vector<uintptr_t> validPedIds = g_pedCacheManager.getValidPedIds();

    for (uintptr_t pedPointer : validPedIds) {
        PedData data;
        if (g_pedCacheManager.getPedData(pedPointer, data)) {
            float dx = localPlayerPosition.x - data.position_origin.x;
            float dy = localPlayerPosition.y - data.position_origin.y;
            float distance = std::sqrt(dx * dx + dy * dy);

            if (distance < minDistance) {
                minDistance = distance;
                closestPedPosition = data.position_origin;
            }
        }
    }

    return closestPedPosition;
}

// Configuration functions
void esp::set_circle_color(ImU32 color) { circle_color = color; config.color_head_circle = color; }
void esp::set_skeleton_color(ImU32 color) { skeleton_color = color; config.color_skeleton = color; }
void esp::set_line_thickness(float thickness) { line_thickness = thickness; }
void esp::set_use_batch_skeleton(bool use_batch) { use_batch_skeleton = use_batch; }

ImU32 esp::get_circle_color() { return circle_color; }
ImU32 esp::get_skeleton_color() { return skeleton_color; }
float esp::get_line_thickness() { return line_thickness; }
bool esp::get_use_batch_skeleton() { return use_batch_skeleton; }

// Batch update and other functions (keeping your existing implementations)
void esp::batch_update_bone_cache(const std::vector<uintptr_t>& peds) {
    if (peds.empty()) return;

    auto handle = mem.CreateScatterHandle();
    std::vector<Matrix> bone_matrices(peds.size());
    std::vector<Vector3> head_offsets(peds.size());

    for (size_t i = 0; i < peds.size(); ++i) {
        mem.AddScatterReadRequest(handle, peds[i] + BONE_MATRIX_OFFSET,
            &bone_matrices[i], sizeof(Matrix));
        mem.AddScatterReadRequest(handle, peds[i] + (BONE_ARRAY_BASE + BONE_SIZE * 0),
            &head_offsets[i], sizeof(Vector3));
    }

    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    for (size_t i = 0; i < peds.size(); ++i) {
        DirectX::SimpleMath::Vector3 boneVec(head_offsets[i].x, head_offsets[i].y, head_offsets[i].z);
        DirectX::SimpleMath::Vector3 transformedBoneVec = DirectX::XMVector3Transform(boneVec, bone_matrices[i]);
        Vec3 head_pos(transformedBoneVec.x, transformedBoneVec.y, transformedBoneVec.z);

        bone_cache.update_bone_data(peds[i], bone_matrices[i], head_pos);
    }

    esp_stats.memory_reads += static_cast<int>(peds.size() * 2);
}

// Batch skeleton update for multiple peds (performance optimization)
void esp::batch_update_skeleton_cache(const std::vector<uintptr_t>& peds) {
    if (peds.empty()) return;

    // Batch read all skeleton data
    for (uintptr_t ped : peds) {
        std::vector<Vec3> bone_positions;
        enhanced_bone_cache.get_skeleton_bones(ped, bone_positions, false); // Use cache when possible
    }
}

Vec3 esp::get_bone_position_cached(uintptr_t ped, int bone_position, const PedData& cached_ped_data) {
    (void)cached_ped_data;
    if (bone_position == 0 && bone_cache.is_data_valid(ped)) {
        return bone_cache.get_bone_position(ped, 0, false);
    }
    return bone_cache.get_bone_position(ped, bone_position, true);
}

void esp::print_esp_stats() {
    // silent — avoid console spam
}

void esp::DrawDebugLine(const Vec2& from, const Vec2& to, ImU32 color) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), color, 1.0f);
}

void esp::DrawDebugText(const Vec2& pos, const char* text, ImU32 color) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->AddText(ImVec2(pos.x, pos.y), color, text);
}

void esp::DrawDebugCircle(const Vec2& center, float radius, ImU32 color) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    draw_list->AddCircle(ImVec2(center.x, center.y), radius, color, 12, 1.0f);
}

// Cache management functions
size_t esp::get_skeleton_cache_size() {
    return enhanced_bone_cache.get_skeleton_cache_size();
}

void esp::clear_skeleton_cache() {
    enhanced_bone_cache.clear_skeleton_cache();
}

void esp::set_use_cache(bool use) { use_cache = use; }
bool esp::get_use_cache() { return use_cache; }



void esp::DrawWaypointAndBlips(const Matrix& view, uintptr_t localplayer) {
    if (!localplayer) return;
    using namespace FiveM;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    Vec3 localPos = mem.Read<Vec3>(localplayer + offset::playerPosition);
    if (localPos.IsZero()) localPos = mem.Read<Vec3>(localplayer + 0x90);

    // Waypoint (module static -> Vec3-ish or blip coords). Common layout: waypoint pointer holds coords.
    if (config.waypoint_line && offset::waypoint) {
        // Try reading a Vec3 directly from resolved waypoint address (build-dependent)
        float wx = mem.Read<float>(offset::waypoint + 0x0);
        float wy = mem.Read<float>(offset::waypoint + 0x4);
        // Z intentionally follows localPos.z; only waypoint X/Y are consumed.
        // Sanity: GTA map bounds roughly -4000..8000
        if (wx > -5000.f && wx < 9000.f && wy > -5000.f && wy < 9000.f) {
            Vec3 wp{wx, wy, localPos.z};
            Vec2 sp;
            if (wp.world_to_screen(view, sp)) {
                ImVec2 scr = ImGui::GetIO().DisplaySize;
                ImVec2 center(scr.x * 0.5f, scr.y * 0.5f);
                dl->AddLine(center, ImVec2(sp.x, sp.y), IM_COL32(255, 200, 50, 160), 1.5f);
                dl->AddCircleFilled(ImVec2(sp.x, sp.y), 5.f, IM_COL32(255, 200, 50, 220), 12);
                char buf[32];
                float dist = localPos.distance_to(wp);
                snprintf(buf, sizeof(buf), "WP %.0fm", dist);
                dl->AddText(ImVec2(sp.x + 8.f, sp.y - 8.f), IM_COL32(255, 220, 100, 255), buf);
            }
        }
    }

    // Blip list: many builds store array of blip ptrs. Soft attempt — skip if invalid.
    if (offset::blip_list && config.blip_esp) {
        // Read first N candidate blip entries (soft). Structure varies; we only draw if W2S succeeds.
        for (int i = 0; i < 64; ++i) {
            uintptr_t entry = mem.Read<uintptr_t>(offset::blip_list + (uintptr_t)i * 8);
            if (!entry || entry < 0x10000) continue;
            // Common: position at +0x10 or +0x8 as Vec3
            Vec3 pos = mem.Read<Vec3>(entry + 0x10);
            if (pos.IsZero()) pos = mem.Read<Vec3>(entry + 0x8);
            if (pos.IsZero()) continue;
            if (pos.x < -5000.f || pos.x > 9000.f) continue;
            Vec2 sp;
            if (!pos.world_to_screen(view, sp)) continue;
            dl->AddCircleFilled(ImVec2(sp.x, sp.y), 3.f, IM_COL32(100, 180, 255, 200), 8);
        }
    }
}
