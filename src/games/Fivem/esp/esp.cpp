#include "esp.h"
#include "prepared_esp_data.h"
#include "math/math.h"
#include "../game/game.h"
#include "third_party/ImGui/imgui.h"
#include <iostream>
#include "../playerInfo/PedData.h"
#include "../friends/friends.h"
#include "../aimbot/aimbot.h"
#include "config/app_settings.h"
#include "gameplay/esp_core.h"
#include "gameplay/trail_history.h"
#include "gameplay/esp_fx.h"
#include "gameplay/weapon_name_registry.h"
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
#include <shared_mutex>
#include <mutex>
#include <atomic>
#include <cfloat>

#include "gameplay/snapshot_exchange.h"


esp::ESPStats esp::esp_stats;


esp::ESPMode esp::current_esp_mode = esp::ESPMode::HEAD_CIRCLE;
float esp::line_thickness = 2.0f;
esp::Config esp::config;




static constexpr uint16_t kLODMasks[4] = {
    0x01FFu,
    0x00F3u,
    0x0013u,
    0x0001u
};

uint16_t esp::ComputeSkeletonBoneMask(SkeletonLOD lod) {
    return kLODMasks[static_cast<uint8_t>(lod)];
}

esp::SkeletonLOD esp::ComputeSkeletonLOD(float distance,
                                          float fullDist,
                                          float highDist,
                                          float medDist) {




    const float full = (std::isfinite(fullDist) && fullDist > 0.f) ? fullDist : LOD_DIST_FULL;
    const float high = (std::isfinite(highDist) && highDist > 0.f) ? highDist : LOD_DIST_HIGH;
    const float medium = (std::isfinite(medDist) && medDist > 0.f) ? medDist : LOD_DIST_MEDIUM;
    if (distance <= full) return SkeletonLOD::Full;
    if (distance <= high) return SkeletonLOD::High;
    if (distance <= medium) return SkeletonLOD::Medium;
    return SkeletonLOD::Low;
}

uint16_t esp::ComputeCombinedBoneMask(const std::vector<uintptr_t>& peds,
                                      const std::vector<Vec3>& positions,
                                      const Vec3& localPos,
                                      float fullDist,
                                      float highDist,
                                      float medDist) {
    uint16_t combinedMask = 0;
    for (size_t i = 0; i < peds.size() && i < positions.size(); ++i) {
        float dist = positions[i].distance_to(localPos);
        SkeletonLOD lod = ComputeSkeletonLOD(dist, fullDist, highDist, medDist);
        combinedMask |= ComputeSkeletonBoneMask(lod);
    }

    combinedMask |= 0x0001u;
    return combinedMask;
}

static ImU32 EspRGB();
static bool EspPedVisible(uintptr_t ped);
static ImU32 EspPedColor(uintptr_t ped, ImU32 configured, bool visible);




struct ProjCacheEntry { float x, y, z; Vec2 screen; bool ok; };
static uint32_t g_proj_frame = 0;
static std::unordered_map<uint64_t, ProjCacheEntry> g_proj_cache;
static Matrix g_lastViewMatrix{};
static bool g_hasLastViewMatrix = false;

static uint64_t ProjKey(uintptr_t ped, int slot, float x, float y, float z) {







    const auto mix64 = [](uint64_t v) {
        v ^= v >> 33; v *= 0xff51afd7ed558ccdULL;
        v ^= v >> 33; v *= 0xc4ceb9fe1a85ec53ULL;
        v ^= v >> 33; return v;
    };
    const int qx = static_cast<int>(x * 50.f);
    const int qy = static_cast<int>(y * 50.f);
    const int qz = static_cast<int>(z * 50.f);
    uint64_t k = ped;
    k = mix64(k + 0x9e3779b97f4a7c15ULL + (static_cast<uint64_t>(static_cast<uint32_t>(slot)) << 1));
    k = mix64(k ^ static_cast<uint64_t>(static_cast<uint32_t>(qx)));
    k = mix64(k ^ static_cast<uint64_t>(static_cast<uint32_t>(qy)));
    k = mix64(k ^ static_cast<uint64_t>(static_cast<uint32_t>(qz)));
    return k;
}

static bool CachedWorldToScreen(uintptr_t ped, int slot, const Vec3& world, const Matrix& vp, Vec2& out) {

    constexpr float kViewMatrixEpsilon = 1e-4f;
    bool viewMatrixChanged = !g_hasLastViewMatrix;
    if (!viewMatrixChanged) {
        for (int i = 0; i < 16; ++i) {
            if (std::fabs(vp.m[i] - g_lastViewMatrix.m[i]) > kViewMatrixEpsilon) {
                viewMatrixChanged = true;
                break;
            }
        }
    }
    if (viewMatrixChanged) {
        g_proj_cache.clear();
        g_lastViewMatrix = vp;
        g_hasLastViewMatrix = true;
    }

    const uint64_t key = ProjKey(ped, slot, world.x, world.y, world.z);
    auto it = g_proj_cache.find(key);
    if (it != g_proj_cache.end()) {
        out = it->second.screen;
        return it->second.ok;
    }
    ProjCacheEntry e{};
    e.x = world.x; e.y = world.y; e.z = world.z;
    e.ok = world.world_to_screen(vp, e.screen);
    g_proj_cache.emplace(key, e);
    out = e.screen;
    return e.ok;
}


static const esp::BatchSkeletonData* FindPreparedSkeleton(uintptr_t ped);





struct PreparedSkeletonFrame {
    std::vector<esp::BatchSkeletonData> entries;
    std::unordered_map<uintptr_t, size_t> index;
    uint32_t frame = 0;
    uint64_t session_generation = 0;
    std::chrono::steady_clock::time_point timestamp{};
    uint64_t world_sample_ms = 0;
};
static PreparedSkeletonFrame g_prepared_skeleton;
static std::shared_mutex g_prepared_skeleton_mutex;
static std::atomic<uint32_t> g_prepared_sequence{ 0 };

struct PreparedEspFrame {
    std::vector<PreparedEspData> entries;
    std::unordered_map<uintptr_t, size_t> index;
    uint32_t frame = 0;
    uint64_t session_generation = 0;
    std::chrono::steady_clock::time_point timestamp{};
    uint64_t world_sample_ms = 0;

    PreparedRenderConfig renderConfig{};
};
static PreparedEspFrame g_prepared_esp;
static std::shared_mutex g_prepared_esp_mutex;
static std::mutex g_preparedBoneScatterMutex;
static VMMDLL_SCATTER_HANDLE g_preparedBoneScatter = nullptr;
static VMMDLL_SCATTER_HANDLE g_preparedEspScatters[3]{};
static std::chrono::steady_clock::time_point g_lastPrepScatter{};
static std::unordered_map<uintptr_t, PreparedEspData> g_stickyPrep;
struct PreparedNameCacheEntry {
    char name[48]{};
    std::chrono::steady_clock::time_point sampledAt{};
};


static std::unordered_map<uintptr_t, PreparedNameCacheEntry> g_preparedNameCache;
static std::shared_mutex g_preparedNameCacheMutex;
static std::vector<uint8_t> g_nameDueBuffer;

static const esp::BatchSkeletonData* FindPreparedSkeleton(uintptr_t ped) {



    thread_local esp::BatchSkeletonData copy{};
    std::shared_lock lock(g_prepared_skeleton_mutex);


    if (g_prepared_skeleton.timestamp.time_since_epoch().count() == 0 ||
        g_prepared_skeleton.session_generation !=
            FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire) ||
        std::chrono::steady_clock::now() - g_prepared_skeleton.timestamp >
            std::chrono::milliseconds(120))
        return nullptr;
    const auto it = g_prepared_skeleton.index.find(ped);
    if (it == g_prepared_skeleton.index.end() || it->second >= g_prepared_skeleton.entries.size())
        return nullptr;
    const auto& data = g_prepared_skeleton.entries[it->second];
    if (!data.valid) return nullptr;
    copy = data;
    return &copy;
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
    thread_local PreparedEspData copy{};
    std::shared_lock lock(g_prepared_esp_mutex);
    if (g_prepared_esp.timestamp.time_since_epoch().count() == 0 ||
        g_prepared_esp.session_generation !=
            FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire) ||
        std::chrono::steady_clock::now() - g_prepared_esp.timestamp >
            std::chrono::milliseconds(120))
        return nullptr;
    const auto it = g_prepared_esp.index.find(ped);
    if (it == g_prepared_esp.index.end() || it->second >= g_prepared_esp.entries.size())
        return nullptr;
    const auto& data = g_prepared_esp.entries[it->second];
    if (!data.valid) return nullptr;
    copy = data;
    return &copy;
}


static const PreparedRenderConfig* FindPreparedRenderConfig() {
    thread_local PreparedRenderConfig copy{};
    std::shared_lock lock(g_prepared_esp_mutex);
    if (g_prepared_esp.timestamp.time_since_epoch().count() == 0 ||
        g_prepared_esp.session_generation !=
            FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire) ||
        std::chrono::steady_clock::now() - g_prepared_esp.timestamp >
            std::chrono::milliseconds(120))
        return nullptr;
    copy = g_prepared_esp.renderConfig;
    return &copy;
}




static const Matrix* SelectCameraSnapshot(uint64_t world_sample_ms) {
    thread_local Matrix selectedMatrix{};
    auto camHandle = FiveM::ESP::AcquireCameraSnapshot();
    if (!camHandle.valid()) return nullptr;

    if (camHandle.historyCount == 0 || world_sample_ms == 0) {

        selectedMatrix = *camHandle.viewMatrix;
        return &selectedMatrix;
    }

    const uint64_t nowMs = GetTickCount64();
    if (nowMs < camHandle.sampleMs) return nullptr;
    if (nowMs - camHandle.sampleMs > 100u) return nullptr;


    if (camHandle.sampleMs > world_sample_ms + 40u) {




        if (camHandle.historyMatrices && camHandle.historyTimesMs && camHandle.historyCount > 0) {
            const Matrix* best = nullptr;
            uint64_t bestDelta = UINT64_MAX;
            const Matrix* bestM = nullptr;
            for (unsigned i = 0; i < camHandle.historyCount; ++i) {
                const uint64_t sample = (*camHandle.historyTimesMs)[i];
                if (!sample) continue;
                const uint64_t delta = sample > world_sample_ms
                    ? sample - world_sample_ms : world_sample_ms - sample;
                if (delta < bestDelta) {
                    bestDelta = delta;
                    best = &(*camHandle.historyMatrices)[i];
                    bestM = best;
                }
            }
            if (best) {
                selectedMatrix = *best;
                return &selectedMatrix;
            }
        }
    }


    selectedMatrix = *camHandle.viewMatrix;
    return &selectedMatrix;
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

bool esp::try_get_prepared_network_id(uintptr_t ped, uint32_t& out) {
    const PreparedEspData* data = FindPreparedEsp(ped);
    out = data ? data->network_id : 0;
    return out != 0;
}

bool esp::try_get_prepared_player_name(uintptr_t ped, char* out, size_t outSize) {
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    const PreparedEspData* data = FindPreparedEsp(ped);
    if (!data || !data->player_name[0]) return false;
    std::snprintf(out, outSize, "%s", data->player_name);
    return true;
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


void esp::set_esp_mode(ESPMode mode) {
    current_esp_mode = mode;
}

esp::ESPMode esp::get_esp_mode() {
    return current_esp_mode;
}

const char* esp::get_esp_mode_name(ESPMode mode) {
    switch (mode) {
    case ESPMode::HEAD_CIRCLE: return "CÃƒÂ­rculo na cabeÃƒÂ§a";
    case ESPMode::SKELETON_BONES: return "Ossos do esqueleto";
    default: return "Desconhecido";
    }
}

std::vector<const char*> esp::get_esp_mode_names() {
    return {
        "CÃƒÂ­rculo na cabeÃƒÂ§a",
        "Ossos do esqueleto"
    };
}


void esp::batch_read_skeleton_data(const std::vector<uintptr_t>& peds,
                                   std::vector<BatchSkeletonData>& out_data,
                                   uint16_t bone_mask) {
    if (peds.empty()) return;


    out_data.resize(peds.size());


    static constexpr std::array<int, 9> skeleton_bones = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };



    std::scoped_lock scatterLock(g_preparedBoneScatterMutex);
    if (!g_preparedBoneScatter && mem.vHandle)
        g_preparedBoneScatter = mem.CreateScatterHandle();
    auto handle = g_preparedBoneScatter;
    if (!handle) {
        for (auto& data : out_data) data.valid = false;
        return;
    }


    for (size_t i = 0; i < peds.size(); ++i) {
        out_data[i].ped = peds[i];
        out_data[i].valid = false;
        out_data[i].bone_mask = bone_mask;
        out_data[i].bone_matrix = {};
        std::fill(out_data[i].bone_offsets.begin(), out_data[i].bone_offsets.end(), Vector3{});
        mem.AddScatterReadRequest(handle, peds[i] + FiveM::offset::boneMatrix,
            &out_data[i].bone_matrix, sizeof(Matrix));
    }


    for (size_t ped_idx = 0; ped_idx < peds.size(); ++ped_idx) {
        for (size_t bone_idx = 0; bone_idx < skeleton_bones.size(); ++bone_idx) {
            int bone_id = skeleton_bones[bone_idx];
            if ((bone_mask & (uint16_t(1u) << static_cast<unsigned>(bone_id))) == 0)
                continue;
            mem.AddScatterReadRequest(handle,
                peds[ped_idx] + (FiveM::offset::boneArrayBase + FiveM::offset::boneSize * bone_id),
                &out_data[ped_idx].bone_offsets[bone_id],
                sizeof(Vector3));
        }
    }


    const bool readOk = mem.ExecuteReadScatter(handle);



    for (auto& data : out_data) {
        const float matrixEnergy = std::fabs(data.bone_matrix._11) +
            std::fabs(data.bone_matrix._22) + std::fabs(data.bone_matrix._33);
        bool hasBone = false;
        for (int boneId : skeleton_bones) {
            if ((bone_mask & (uint16_t(1u) << static_cast<unsigned>(boneId))) == 0)
                continue;
            const auto& b = data.bone_offsets[boneId];
            if (std::isfinite(b.x) && std::isfinite(b.y) && std::isfinite(b.z) &&
                (std::fabs(b.x) + std::fabs(b.y) + std::fabs(b.z)) > 0.001f)
                hasBone = true;
        }
        data.valid = readOk && std::isfinite(matrixEnergy) && matrixEnergy > 0.001f && hasBone;
    }


    size_t requested_bones = 0;
    for (int bone_id : skeleton_bones)
        if ((bone_mask & (uint16_t(1u) << static_cast<unsigned>(bone_id))) != 0)
            ++requested_bones;
    esp_stats.memory_reads.fetch_add(
        static_cast<int>(peds.size() * (1 + requested_bones)), std::memory_order_relaxed);
    esp_stats.batch_reads.fetch_add(1, std::memory_order_relaxed);
}

void esp::prepare_skeleton_frame(const std::vector<uintptr_t>& peds,
                                 const std::vector<Vec3>& origins,
                                 uint16_t bone_mask) {
    PreparedSkeletonFrame next;

    next.frame = g_prepared_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    next.timestamp = std::chrono::steady_clock::now();




    next.session_generation = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);

    if (peds.empty()) {
        std::unique_lock lock(g_prepared_skeleton_mutex);
        g_prepared_skeleton = std::move(next);
        return;
    }


    next.entries.reserve(peds.size());



    const uint64_t boneBeginMs = GetTickCount64();
    batch_read_skeleton_data(peds, next.entries, bone_mask);
    const uint64_t boneEndMs = GetTickCount64();
    next.world_sample_ms = boneBeginMs + (boneEndMs - boneBeginMs) / 2u;

    next.index.reserve(peds.size() * 2);
    const auto sampleNow = next.timestamp;
    for (size_t i = 0; i < next.entries.size(); ++i) {
        next.entries[i].origin = i < origins.size() ? origins[i] : Vec3{};
        next.entries[i].pose_sample = sampleNow;
        next.entries[i].origin_sample = sampleNow;
        next.entries[i].session_generation = next.session_generation;

        next.index[next.entries[i].ped] = i;
    }
    std::unique_lock lock(g_prepared_skeleton_mutex);
    g_prepared_skeleton = std::move(next);
}

static bool IsPlausiblePlayerName(const char* s) {
    if (!s || !s[0]) return false;
    size_t len = 0;
    int visible = 0;
    while (s[len] && len < 48) {
        const unsigned char c = static_cast<unsigned char>(s[len]);
        if (c < 0x20 || c == 0x7F) return false;
        if (c < 0x80) {
            if (c != ' ') ++visible;
            ++len;
            continue;
        }



        int continuation = 0;
        if ((c & 0xE0) == 0xC0 && c >= 0xC2) continuation = 1;
        else if ((c & 0xF0) == 0xE0) continuation = 2;
        else if ((c & 0xF8) == 0xF0 && c <= 0xF4) continuation = 3;
        else return false;
        if (len + static_cast<size_t>(continuation) >= 48) return false;
        for (int n = 1; n <= continuation; ++n) {
            const unsigned char next = static_cast<unsigned char>(s[len + n]);
            if ((next & 0xC0) != 0x80) return false;
        }
        len += static_cast<size_t>(continuation) + 1;
        ++visible;
    }
    return len >= 1 && len <= 47 && visible > 0 && s[len] == '\0';
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

static void DrawTextOutlinedScaled(ImDrawList* dl, ImVec2 pos, ImU32 col,
                                   const char* text, float scale) {
    if (!dl || !text || !*text) return;
    ImFont* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize() * std::clamp(scale, .65f, 1.6f);
    const ImU32 shadow = IM_COL32(0, 0, 0, 210);
    dl->AddText(font, size, ImVec2(pos.x - 1, pos.y), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x + 1, pos.y), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x, pos.y - 1), shadow, text);
    dl->AddText(font, size, ImVec2(pos.x, pos.y + 1), shadow, text);
    dl->AddText(font, size, pos, col, text);
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
    if (const auto* prepared = FindPreparedEsp(ped); prepared && prepared->visibility_known)
        return prepared->visible;
    return FiveM::Visibility::IsPedVisible(ped);
}
static bool EspPedVisibilityKnown(uintptr_t ped) {
    const auto* prepared = FindPreparedEsp(ped);
    return (prepared && prepared->visibility_known) || FiveM::Visibility::IsPedVisibilityKnown(ped);
}

static bool EspPedPassesVisibility(uintptr_t ped) {



    return !esp::config.visible_check || !EspPedVisibilityKnown(ped) || EspPedVisible(ped);
}


struct PedFrameFlags {
    uint32_t frame = 0;
    uint64_t sessionGeneration = 0;
    std::chrono::steady_clock::time_point lastAccess;
    FiveM::PedHealthState health_state = FiveM::PedHealthState::Unknown;
};

static std::unordered_map<uintptr_t, PedFrameFlags> g_pedFrameFlags;
static std::shared_mutex g_pedFrameFlagsMutex;




static PedFrameFlags PedFlags(uintptr_t ped) {
    const uint32_t f = static_cast<uint32_t>(ImGui::GetFrameCount());
    const uint64_t currentGen = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);

    PedFrameFlags result{};
    {
        std::unique_lock lock(g_pedFrameFlagsMutex);
        auto& e = g_pedFrameFlags[ped];
        if (e.sessionGeneration != currentGen || e.frame != f) {
            e = PedFrameFlags{};
            e.frame = f;
            e.sessionGeneration = currentGen;
        }
        e.lastAccess = std::chrono::steady_clock::now();
        result = e;

        static std::chrono::steady_clock::time_point s_lastPrune{};
        constexpr auto kPruneTTL = std::chrono::seconds(30);
        constexpr size_t kPruneChunk = 32;
        const auto now = std::chrono::steady_clock::now();

        if (g_pedFrameFlags.size() > 8 &&
            (s_lastPrune == std::chrono::steady_clock::time_point{} ||
             now - s_lastPrune >= std::chrono::seconds(5))) {
            s_lastPrune = now;
            size_t examined = 0;
            for (auto it = g_pedFrameFlags.begin();
                 it != g_pedFrameFlags.end() && examined < kPruneChunk; ++examined) {
                if (now - it->second.lastAccess > kPruneTTL)
                    it = g_pedFrameFlags.erase(it);
                else
                    ++it;
            }
        }
    }
    return result;
}


static void SetPedHealthState(uintptr_t ped, FiveM::PedHealthState state) {
    std::unique_lock lock(g_pedFrameFlagsMutex);
    auto it = g_pedFrameFlags.find(ped);
    if (it != g_pedFrameFlags.end())
        it->second.health_state = state;
}







struct LivenessFrameCache {
    const FiveM::ESP::LivenessSample* players = nullptr;
    uint32_t count = 0;
    uint64_t sampleMs = 0;
    uint64_t generation = 0;
    uint32_t frame = 0;
    bool valid = false;
    std::unordered_map<uintptr_t, uint32_t> byPed;
};
static LivenessFrameCache g_livenessFrame;

static const FiveM::ESP::LivenessSample* FindLivenessSample(uintptr_t ped) {
    const uint32_t f = static_cast<uint32_t>(ImGui::GetFrameCount());
    const uint64_t currentGen = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);
    if (!g_livenessFrame.valid || g_livenessFrame.frame != f ||
        g_livenessFrame.generation != currentGen) {
        g_livenessFrame.frame = f;
        g_livenessFrame.generation = currentGen;
        g_livenessFrame.byPed.clear();
        auto liveness = FiveM::ESP::AcquireLivenessSnapshot();
        g_livenessFrame.valid = liveness.valid();
        g_livenessFrame.players = liveness.players;
        g_livenessFrame.count = liveness.count;
        g_livenessFrame.sampleMs = liveness.timestamp_ms;
        if (liveness.valid()) {
            g_livenessFrame.byPed.reserve(static_cast<size_t>(liveness.count) * 2);
            for (uint32_t i = 0; i < liveness.count; ++i) {
                if (liveness.players[i].generation == currentGen)
                    g_livenessFrame.byPed.emplace(liveness.players[i].ped, i);
            }
        }
    }
    if (!g_livenessFrame.valid) return nullptr;
    const auto it = g_livenessFrame.byPed.find(ped);
    if (it == g_livenessFrame.byPed.end()) return nullptr;
    return &g_livenessFrame.players[it->second];
}

static FiveM::PedHealthState EspPedHealthState(uintptr_t ped, float* outHealth = nullptr) {
    const PedFrameFlags flags = PedFlags(ped);
    if (flags.health_state != FiveM::PedHealthState::Unknown && !outHealth)
        return flags.health_state;

    float health = -1.f;
    FiveM::PedHealthState state = FiveM::PedHealthState::Unknown;

    if (const auto* sample = FindLivenessSample(ped)) {
        health = sample->health;
        state = (sample->health > 0.f && sample->knocked)
            ? FiveM::PedHealthState::Knocked
            : FiveM::ClassifyPedHealth(health);
        if (outHealth) *outHealth = health;
        SetPedHealthState(ped, state);
        return state;
    }


    {
        const PreparedEspData* prepared = FindPreparedEsp(ped);
        if (prepared && prepared->health_valid) {
            health = prepared->health;
        } else {
            if (outHealth) *outHealth = -1.f;
            SetPedHealthState(ped, FiveM::PedHealthState::Unknown);
            return FiveM::PedHealthState::Unknown;
        }
    }

    if (outHealth) *outHealth = health;
    state = FiveM::ClassifyPedHealth(health);
    SetPedHealthState(ped, state);
    return state;
}

static bool EspPedMayDraw(uintptr_t ped, bool showDead, bool showKnocked) {
    const auto state = EspPedHealthState(ped);
    return state == FiveM::PedHealthState::Alive ||
        (showDead && state == FiveM::PedHealthState::Dead) ||
        (showKnocked && state == FiveM::PedHealthState::Knocked);
}

static bool EspPedIsDead(uintptr_t ped) {
    return EspPedHealthState(ped) == FiveM::PedHealthState::Dead;
}

static bool EspPedIsKnocked(uintptr_t ped) {
    return EspPedHealthState(ped) == FiveM::PedHealthState::Knocked;
}

static bool EspPedIsFriend(uintptr_t ped) {
    const PreparedEspData* prepared = FindPreparedEsp(ped);
    if (prepared)
        return prepared->network_id != 0 && friends::IsFriend(prepared->network_id);
    return false;
}

static ImU32 EspPedColor(uintptr_t ped, ImU32 configured, bool visible) {


    if (esp::config.visibility_colors) {
        if (!EspPedVisibilityKnown(ped))
            return IM_COL32(150, 156, 166, 255);
        return visible ? esp::config.color_visible : esp::config.color_invisible;
    }
    if (esp::config.rgb_mode)
        return EspRGB();
    if (esp::config.show_knocked && EspPedIsKnocked(ped))
        return esp::config.color_knocked ? esp::config.color_knocked : IM_COL32(255, 165, 0, 255);
    if (EspPedIsDead(ped))
        return esp::config.color_dead ? esp::config.color_dead : IM_COL32(255, 50, 50, 255);


    if (EspPedIsFriend(ped))
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

    return bone.distance_to(origin) <= 8.0f;
}

static ImU32 MultiplyAlpha(ImU32 color, float factor) {
    ImVec4 value = ImGui::ColorConvertU32ToFloat4(color);
    value.w *= std::clamp(factor, 0.f, 1.f);
    return ImGui::ColorConvertFloat4ToU32(value);
}

static int FxSegCount(float distM, int nearSegs, int farSegs) {
    if (distM > 90.f) return (std::max)(3, farSegs / 2);
    if (distM > 50.f) return (std::max)(4, (nearSegs + farSegs) / 2);
    return nearSegs;
}

static void DrawMotionVisuals(uintptr_t ped, Matrix viewport, const PedData* cached) {
    const auto& cfg = esp::config;
    if (!EspPedMayDraw(ped, cfg.show_dead, cfg.show_knocked))
        return;
    if (!cfg.trails && !cfg.head_halo && !cfg.look_direction && !cfg.chinese_hat &&
        !cfg.angel_wings && !cfg.devil_horns && !cfg.floating_crown)
        return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (!draw) return;

    const esp::BatchSkeletonData* skeleton = FindPreparedSkeleton(ped);
    const PreparedEspData* prepared = FindPreparedEsp(ped);
    Vec3 origin = skeleton ? skeleton->origin : Vec3{};
    if (origin.IsZero() && prepared) origin = prepared->origin;
    if (origin.IsZero() && cached) origin = cached->position_origin;
    if (origin.IsZero()) return;


    float motionDist = 0.f;
    if (FiveM::ESP::FrameCacheValid()) {
        const Vec3& lp = FiveM::ESP::GetFrameLocalPos();
        if (!lp.IsZero()) motionDist = origin.distance_to(lp);
    }
    const bool farOrnament = motionDist > 70.f;
    const bool veryFar = motionDist > 120.f;

    const bool visible = EspPedVisible(ped);
    const double now = ImGui::GetTime();

    static std::unordered_map<uintptr_t, OmniGhost::Gameplay::FixedTrailHistory<18>> trails;
    static uint64_t trailsSessionGen = 0;
    const uint64_t currentSessionGen = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);
    if (trailsSessionGen != currentSessionGen) {
        trails.clear();
        trailsSessionGen = currentSessionGen;
    }
    static int cleanup_frame = -1;

    if (cfg.trails && !veryFar) {
        auto& history = trails[ped];
        const float trailX = origin.x;
        const float trailY = origin.y;
        const float trailZ = origin.z + .58f;
        if (history.Discontinuous(trailX, trailY, trailZ, now, 4.0f, .35))
            history.Clear();
        history.Push(trailX, trailY, trailZ, now, 0.10f, 0.035);
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
            const ImU32 col = cfg.rainbow_trails
                ? OmniGhost::Gameplay::EspFx::RainbowFade(static_cast<float>(b.time) * 0.35f, fade > 0.f ? 1.f - fade : 1.f)
                : MultiplyAlpha(base, fade * fade);
            draw->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y),
                col, std::clamp(cfg.trail_thickness, 1.f, 8.f));
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


    const float fxScale = std::clamp(cfg.fun_effects_scale, .5f, 2.5f);
    const float hatScale = std::clamp(cfg.chinese_hat_scale, 0.4f, 2.5f);
    Vec3 forward(1.f, 0.f, 0.f);
    if (skeleton) {
        forward = Vec3(skeleton->bone_matrix._21, skeleton->bone_matrix._22, 0.f);
        const float len = std::sqrt(forward.x * forward.x + forward.y * forward.y);
        if (len > .001f) { forward.x /= len; forward.y /= len; }
        else forward = Vec3(1.f, 0.f, 0.f);
    }
    const Vec3 side(-forward.y, forward.x, 0.f);
    auto fxColor = [&](float phase, float alpha = 1.f) -> ImU32 {
        return cfg.fun_effects_rainbow
            ? OmniGhost::Gameplay::EspFx::Hsv(static_cast<float>(now) * .16f + phase, .88f, 1.f, alpha)
            : MultiplyAlpha(EspPedColor(ped, cfg.color_fun_effects, visible), alpha);
    };
    auto worldLine = [&](const Vec3& a, const Vec3& b, ImU32 color, float thickness) {
        Vec2 sa{}, sb{};
        if (a.world_to_screen(viewport, sa) && b.world_to_screen(viewport, sb))
            draw->AddLine(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), color, thickness);
    };
    auto worldPoly = [&](const Vec3* pts, int n, ImU32 color, float thickness, bool closed) {
        for (int i = 0; i < n - 1; ++i)
            worldLine(pts[i], pts[i + 1], color, thickness);
        if (closed && n > 2)
            worldLine(pts[n - 1], pts[0], color, thickness);
    };

    if (cfg.head_halo) {

        const ImU32 color = EspPedColor(ped, cfg.color_halo, visible);
        const ImU32 glow = MultiplyAlpha(color, 0.35f);
        constexpr int segments = 32;
        const float rx = 0.22f;
        const float ry = 0.11f;
        const float z = head.z + 0.16f;
        Vec2 prev{}, first{};
        bool prev_ok = false, first_ok = false;
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) * 6.28318530718f / segments;
            Vec3 point(
                head.x + std::cos(a) * rx,
                head.y + std::sin(a) * ry,
                z);
            Vec2 screen{};
            const bool ok = point.world_to_screen(viewport, screen);
            if (ok && prev_ok) {
                draw->AddLine(ImVec2(prev.x, prev.y), ImVec2(screen.x, screen.y), color, 2.0f);

                Vec3 gout(
                    head.x + std::cos(a) * rx * 1.18f,
                    head.y + std::sin(a) * ry * 1.18f,
                    z);
                Vec2 gs{};
                if (gout.world_to_screen(viewport, gs) && i > 0) {

                }
            }
            if (i == 0) { first = screen; first_ok = ok; }
            prev = screen;
            prev_ok = ok;
        }

        prev_ok = false;
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) * 6.28318530718f / segments;
            Vec3 point(
                head.x + std::cos(a) * rx * 1.2f,
                head.y + std::sin(a) * ry * 1.2f,
                z);
            Vec2 screen{};
            const bool ok = point.world_to_screen(viewport, screen);
            if (ok && prev_ok)
                draw->AddLine(ImVec2(prev.x, prev.y), ImVec2(screen.x, screen.y), glow, 1.2f);
            prev = screen;
            prev_ok = ok;
        }
    }

    if (cfg.chinese_hat && !farOrnament) {
        auto project = [&](float wx, float wy, float wz, float& sx, float& sy) -> bool {
            Vec2 screen{};
            if (!Vec3(wx, wy, wz).world_to_screen(viewport, screen)) return false;
            sx = screen.x; sy = screen.y;
            return true;
        };
        OmniGhost::Gameplay::EspFx::DrawChineseHat(
            draw, head.x, head.y, head.z, project, static_cast<float>(now), hatScale, true);
    }

    if (cfg.angel_wings && !farOrnament) {

        const float flap = std::sin(static_cast<float>(now) * 3.0f) * 0.06f * fxScale;
        const Vec3 shoulder(
            origin.x - forward.x * 0.06f,
            origin.y - forward.y * 0.06f,
            origin.z + 0.62f);
        for (int wingSide = -1; wingSide <= 1; wingSide += 2) {
            const float s = static_cast<float>(wingSide);
            const Vec3 root(
                shoulder.x + side.x * s * 0.12f * fxScale,
                shoulder.y + side.y * s * 0.12f * fxScale,
                shoulder.z);

            Vec3 upper[7]{};
            for (int i = 0; i <= 6; ++i) {
                const float t = static_cast<float>(i) / 6.f;
                const float out = (0.15f + t * 0.70f) * fxScale;
                const float back = (0.02f + t * 0.16f) * fxScale;
                const float up = (0.05f + std::sin(t * 1.4f) * 0.38f) * fxScale + flap * (1.f - t * 0.4f);
                upper[i] = Vec3(
                    root.x + side.x * s * out - forward.x * back,
                    root.y + side.y * s * out - forward.y * back,
                    root.z + up);
            }

            Vec3 lower[5]{};
            for (int i = 0; i <= 4; ++i) {
                const float t = static_cast<float>(i) / 4.f;
                const float out = (0.18f + t * 0.55f) * fxScale;
                const float back = (0.04f + t * 0.12f) * fxScale;
                const float up = (0.02f - t * 0.28f) * fxScale + flap * 0.5f;
                lower[i] = Vec3(
                    root.x + side.x * s * out - forward.x * back,
                    root.y + side.y * s * out - forward.y * back,
                    root.z + up);
            }
            worldPoly(upper, 7, fxColor(wingSide > 0 ? 0.08f : 0.55f), 2.0f, false);
            worldPoly(lower, 5, fxColor(wingSide > 0 ? 0.18f : 0.65f), 1.7f, false);
            worldLine(upper[6], lower[4], fxColor(wingSide > 0 ? 0.22f : 0.70f), 1.6f);
            worldLine(root, upper[0], fxColor(wingSide > 0 ? 0.05f : 0.50f), 1.8f);

            for (int f = 0; f < 4; ++f) {
                const float t = 0.2f + f * 0.18f;
                const int ui = std::clamp(static_cast<int>(t * 6.f), 0, 6);
                const Vec3 base = upper[ui];
                const float featherOut = (0.10f + f * 0.04f) * fxScale;
                const float featherDown = (0.14f + f * 0.06f) * fxScale;
                const Vec3 tip(
                    base.x + side.x * s * featherOut - forward.x * 0.04f * fxScale,
                    base.y + side.y * s * featherOut - forward.y * 0.04f * fxScale,
                    base.z - featherDown);
                worldLine(base, tip, fxColor(0.12f * f + (wingSide > 0 ? 0.f : 0.5f), 0.9f), 1.4f);
            }
        }
    }

    if (cfg.devil_horns && !farOrnament) {

        for (int hornSide = -1; hornSide <= 1; hornSide += 2) {
            const float s = static_cast<float>(hornSide);
            Vec3 pts[7]{};
            for (int i = 0; i <= 6; ++i) {
                const float t = static_cast<float>(i) / 6.f;

                const float lateral = (0.10f + 0.14f * t * (1.f - t * 0.85f)) * fxScale;
                const float forwardAmt = (0.02f + 0.10f * t * t) * fxScale;
                const float up = (0.04f + 0.28f * t) * fxScale;
                pts[i] = Vec3(
                    head.x + side.x * s * lateral + forward.x * forwardAmt,
                    head.y + side.y * s * lateral + forward.y * forwardAmt,
                    head.z + up);
            }
            for (int i = 0; i < 6; ++i) {
                const float th = 2.6f - i * 0.28f;
                worldLine(pts[i], pts[i + 1], fxColor(hornSide > 0 ? 0.05f + i * 0.03f : 0.55f + i * 0.03f), th);
            }
        }
    }

    if (cfg.floating_crown && !farOrnament) {

        const float bob = std::sin(static_cast<float>(now) * 2.2f) * 0.02f;
        const float zBase = head.z + (0.20f + bob) * fxScale;
        const float halfW = 0.16f * fxScale;
        const float baseH = 0.04f * fxScale;

        const Vec3 bl(
            head.x - side.x * halfW - forward.x * 0.02f * fxScale,
            head.y - side.y * halfW - forward.y * 0.02f * fxScale,
            zBase);
        const Vec3 br(
            head.x + side.x * halfW - forward.x * 0.02f * fxScale,
            head.y + side.y * halfW - forward.y * 0.02f * fxScale,
            zBase);
        const Vec3 tl(
            head.x - side.x * halfW * 0.92f - forward.x * 0.02f * fxScale,
            head.y - side.y * halfW * 0.92f - forward.y * 0.02f * fxScale,
            zBase + baseH);
        const Vec3 tr(
            head.x + side.x * halfW * 0.92f - forward.x * 0.02f * fxScale,
            head.y + side.y * halfW * 0.92f - forward.y * 0.02f * fxScale,
            zBase + baseH);
        worldLine(bl, br, fxColor(0.1f), 2.0f);
        worldLine(bl, tl, fxColor(0.15f), 1.8f);
        worldLine(br, tr, fxColor(0.15f), 1.8f);
        worldLine(tl, tr, fxColor(0.2f), 1.8f);


        const float peaksX[5] = { -0.85f, -0.42f, 0.f, 0.42f, 0.85f };
        const float peaksH[5] = { 0.08f, 0.14f, 0.22f, 0.14f, 0.08f };
        Vec3 peakPts[5]{};
        Vec3 basePts[5]{};
        for (int i = 0; i < 5; ++i) {
            const float px = peaksX[i] * halfW * 0.95f;
            basePts[i] = Vec3(
                head.x + side.x * px - forward.x * 0.02f * fxScale,
                head.y + side.y * px - forward.y * 0.02f * fxScale,
                zBase + baseH);
            peakPts[i] = Vec3(
                basePts[i].x,
                basePts[i].y,
                zBase + baseH + peaksH[i] * fxScale);
        }

        worldLine(tl, basePts[0], fxColor(0.25f), 1.7f);
        for (int i = 0; i < 5; ++i) {
            worldLine(basePts[i], peakPts[i], fxColor(0.3f + i * 0.05f), 1.8f);
            if (i < 4)
                worldLine(peakPts[i], basePts[i + 1], fxColor(0.35f + i * 0.05f), 1.8f);
        }
        worldLine(basePts[4], tr, fxColor(0.25f), 1.7f);
    }

    if (cfg.look_direction && skeleton) {
        Vec3 lookForward(skeleton->bone_matrix._21, skeleton->bone_matrix._22, 0.f);
        const float length = std::sqrt(lookForward.x * lookForward.x + lookForward.y * lookForward.y);
        if (length > 0.001f) {
            lookForward.x /= length;
            lookForward.y /= length;
            const float line_length = std::clamp(cfg.look_direction_length, 0.5f, 5.f);
            Vec3 end(head.x + lookForward.x * line_length,
                     head.y + lookForward.y * line_length,
                     head.z);
            Vec2 from{}, to{};
            if (head.world_to_screen(viewport, from) && end.world_to_screen(viewport, to)) {
                const ImU32 color = EspPedColor(ped, cfg.color_look_direction, visible);
                draw->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y), color, std::clamp(cfg.eye_line_thickness, 1.f, 6.f));
                draw->AddCircleFilled(ImVec2(to.x, to.y), 2.2f, color, 8);
            }
        }
    }
}


static bool AnyEspExtrasEnabled(const esp::Config& c) {
    return c.box_2d || c.corner_box || c.snaplines || c.health_bar || c.armor_bar
        || c.weapon_name || c.distance || c.player_name || c.player_id || c.hit_marker
        || c.offscreen_arrows;
}

static bool AnyEspExtrasEnabled() { return AnyEspExtrasEnabled(esp::config); }

bool esp::has_extra_visuals() {
    return AnyEspExtrasEnabled();
}

void esp::prepare_esp_frame(const std::vector<uintptr_t>& peds,
                            const std::vector<Vec3>& origins,
                            const std::vector<float>& coreHealths,
                            const Config& frameConfig,
                            const aimbot::Config& aimConfig,
                            const std::vector<bool>* visibilityResults) {
    PreparedEspFrame next;

    next.frame = g_prepared_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    next.timestamp = std::chrono::steady_clock::now();

    next.session_generation = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);


    next.renderConfig.max_esp_distance = frameConfig.max_esp_distance;
    next.renderConfig.show_dead = frameConfig.show_dead;
    next.renderConfig.show_knocked = frameConfig.show_knocked;
    next.renderConfig.visible_check = frameConfig.visible_check;
    next.renderConfig.visibility_colors = frameConfig.visibility_colors;
    next.renderConfig.box_2d = frameConfig.box_2d;
    next.renderConfig.corner_box = frameConfig.corner_box;
    next.renderConfig.snaplines = frameConfig.snaplines;
    next.renderConfig.health_bar = frameConfig.health_bar;
    next.renderConfig.armor_bar = frameConfig.armor_bar;
    next.renderConfig.skeleton = frameConfig.skeleton;
    next.renderConfig.head_circle = frameConfig.head_circle;
    next.renderConfig.chinese_hat = frameConfig.chinese_hat;
    next.renderConfig.angel_wings = frameConfig.angel_wings;
    next.renderConfig.devil_horns = frameConfig.devil_horns;
    next.renderConfig.floating_crown = frameConfig.floating_crown;
    next.renderConfig.head_halo = frameConfig.head_halo;
    next.renderConfig.look_direction = frameConfig.look_direction;
    next.renderConfig.team_check = frameConfig.team_check;
    next.renderConfig.npc_esp = frameConfig.npc_esp;
    next.renderConfig.player_name = frameConfig.player_name;
    next.renderConfig.player_id = frameConfig.player_id;
    next.renderConfig.distance = frameConfig.distance;
    next.renderConfig.weapon_name = frameConfig.weapon_name;
    next.renderConfig.radar_enabled = frameConfig.radar_enabled;
    next.renderConfig.triangle_radar = frameConfig.triangle_radar;
    next.renderConfig.trails = frameConfig.trails;
    next.renderConfig.trail_duration = frameConfig.trail_duration;
    next.renderConfig.trail_thickness = frameConfig.trail_thickness;
    next.renderConfig.rainbow_trails = frameConfig.rainbow_trails;
    next.renderConfig.color_visible = frameConfig.color_visible;
    next.renderConfig.color_invisible = frameConfig.color_invisible;
    next.renderConfig.color_dead = frameConfig.color_dead;
    next.renderConfig.color_team = frameConfig.color_team;
    next.renderConfig.color_npc = frameConfig.color_npc;
    next.renderConfig.color_box_2d = frameConfig.color_box_2d;
    next.renderConfig.color_corner_box = frameConfig.color_corner_box;
    next.renderConfig.color_snaplines = frameConfig.color_snaplines;
    next.renderConfig.color_health = frameConfig.color_health;
    next.renderConfig.color_armor = frameConfig.color_armor;
    next.renderConfig.color_skeleton = frameConfig.color_skeleton;
    next.renderConfig.color_name = frameConfig.color_name;
    next.renderConfig.color_id = frameConfig.color_id;
    next.renderConfig.color_distance = frameConfig.color_distance;
    next.renderConfig.color_weapon = frameConfig.color_weapon;
    next.renderConfig.color_skeleton_points = frameConfig.color_skeleton_points;
    next.renderConfig.joints = frameConfig.joints;
    next.renderConfig.skeleton_thickness = frameConfig.skeleton_thickness;
    next.renderConfig.circle_type = frameConfig.circle_type;
    next.renderConfig.head_circle_thickness = frameConfig.head_circle_thickness;
    next.renderConfig.box_thickness = frameConfig.box_thickness;
    next.renderConfig.snapline_thickness = frameConfig.snapline_thickness;
    next.renderConfig.snapline_pos = frameConfig.snapline_pos;
    next.renderConfig.filled_box = frameConfig.filled_box;
    next.renderConfig.box_rounding = frameConfig.box_rounding;




    next.renderConfig.hit_marker = frameConfig.hit_marker;
    next.renderConfig.offscreen_arrows = frameConfig.offscreen_arrows;
    next.renderConfig.offscreen_arrow_size = frameConfig.offscreen_arrow_size;
    next.renderConfig.offscreen_arrow_margin = frameConfig.offscreen_arrow_margin;
    next.renderConfig.offscreen_arrow_max_distance = frameConfig.offscreen_arrow_max_distance;
    next.renderConfig.offscreen_arrow_opacity = frameConfig.offscreen_arrow_opacity;
    next.renderConfig.offscreen_arrow_distance = frameConfig.offscreen_arrow_distance;
    next.renderConfig.offscreen_arrow_name = frameConfig.offscreen_arrow_name;
    next.renderConfig.offscreen_arrow_style = frameConfig.offscreen_arrow_style;
    next.renderConfig.color_offscreen_arrow = frameConfig.color_offscreen_arrow;
    next.renderConfig.layout_name = static_cast<int>(frameConfig.layout_name);
    next.renderConfig.layout_health = static_cast<int>(frameConfig.layout_health);
    next.renderConfig.layout_armor = static_cast<int>(frameConfig.layout_armor);
    next.renderConfig.layout_weapon = static_cast<int>(frameConfig.layout_weapon);
    next.renderConfig.layout_distance = static_cast<int>(frameConfig.layout_distance);
    next.renderConfig.layout_spacing = frameConfig.layout_spacing;
    next.renderConfig.layout_text_scale = frameConfig.layout_text_scale;
    next.renderConfig.skeleton_lod = frameConfig.skeleton_lod;

    const auto publish = [&](PreparedEspFrame&& frame) {


        if (visibilityResults && visibilityResults->size() == frame.entries.size()) {
            for (size_t i = 0; i < frame.entries.size(); ++i) {
                auto& entry = frame.entries[i];
                if (!entry.ped) continue;
                entry.visibility_known = true;
                entry.visible = (*visibilityResults)[i];
                entry.visibility_flag = entry.visible ? 1 : 0;
            }
        } else {

            for (auto& entry : frame.entries) {
                PedData pedData;
                if (!g_pedCacheManager.getPedData(entry.ped, pedData)) continue;
                entry.visibility_known = pedData.visibility_known &&
                    std::chrono::steady_clock::now() - pedData.visibility_updated <
                        std::chrono::milliseconds(80);
                entry.visible = entry.visibility_known && pedData.visible;
                entry.visibility_flag = pedData.visibility_flag;
            }
        }
        std::unique_lock lock(g_prepared_esp_mutex);
        g_prepared_esp = std::move(frame);
    };

    const auto nowPrep = std::chrono::steady_clock::now();
    const bool doScatter = g_lastPrepScatter.time_since_epoch().count() == 0 ||
        (nowPrep - g_lastPrepScatter) >= std::chrono::milliseconds(25);

    const bool needs_motion_origin = frameConfig.trails || frameConfig.head_halo || frameConfig.look_direction ||
        frameConfig.chinese_hat || frameConfig.angel_wings || frameConfig.devil_horns || frameConfig.floating_crown;



    const bool identityNeeded = frameConfig.team_check || friends::HasFriends();
    const bool radarNeedsIdentity = frameConfig.radar_enabled &&
        (frameConfig.team_check || friends::HasFriends());
    if (peds.empty() || (!AnyEspExtrasEnabled(frameConfig) && !needs_motion_origin &&
        !radarNeedsIdentity && !identityNeeded &&
        !aimConfig.aimbot_enabled && !aimConfig.trigger_enabled)) {
        publish(std::move(next));
        return;
    }

    next.entries.resize(peds.size());
    next.index.reserve(peds.size() * 2);
    for (size_t i = 0; i < peds.size(); ++i) {
        next.entries[i] = {};
        next.entries[i].ped = peds[i];
        next.entries[i].origin = i < origins.size() ? origins[i] : Vec3{};
        if (i < coreHealths.size() &&
            FiveM::ClassifyPedHealth(coreHealths[i]) != FiveM::PedHealthState::Unknown) {
            next.entries[i].health = coreHealths[i];
            next.entries[i].health_valid = true;
        }
        next.entries[i].valid = peds[i] != 0 && !next.entries[i].origin.IsZero();
        next.index[peds[i]] = i;
    }




    if (!AnyEspExtrasEnabled(frameConfig) && !radarNeedsIdentity && !identityNeeded &&
        !aimConfig.aimbot_enabled &&
        !aimConfig.trigger_enabled) {
        publish(std::move(next));
        return;
    }


    if (!doScatter) {
        for (auto& data : next.entries) {
            auto it = g_stickyPrep.find(data.ped);
            if (it == g_stickyPrep.end()) continue;
            const Vec3 originKeep = data.origin;
            const uintptr_t pedKeep = data.ped;
            data = it->second;
            data.origin = originKeep;
            data.ped = pedKeep;
            data.valid = pedKeep != 0 && !originKeep.IsZero();
        }
        publish(std::move(next));
        return;
    }
    g_lastPrepScatter = nowPrep;

    using namespace FiveM;
    OmniGhost::Gameplay::EspCore::FeatureSet requested{};
    requested.box = frameConfig.box_2d;
    requested.corner_box = frameConfig.corner_box;
    requested.skeleton = frameConfig.skeleton;
    requested.head = frameConfig.head_circle;
    requested.health = frameConfig.health_bar;
    requested.armor = frameConfig.armor_bar;
    requested.snapline = frameConfig.snaplines;
    requested.name = frameConfig.player_name || frameConfig.player_id || frameConfig.npc_esp ||
        frameConfig.team_check || friends::HasFriends();
    requested.weapon = frameConfig.weapon_name;
    requested.distance = frameConfig.distance;

    requested.visibility = aimConfig.visible_check ||
        frameConfig.visible_check || frameConfig.visibility_colors;
    requested.aim = aimConfig.aimbot_enabled || aimConfig.trigger_enabled;
    requested.prediction = aimConfig.velocity_prediction;
    requested.trail = frameConfig.trails;
    requested.halo = frameConfig.head_halo;
    requested.look_direction = frameConfig.look_direction;
    const auto fields = requested.RequiredFields();

    const bool need_armor = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Armor);
    const bool need_identity = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Name);
    const bool need_weapon = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Weapon);
    const bool need_vehicle = OmniGhost::Gameplay::EspCore::Has(
        fields, OmniGhost::Gameplay::EspCore::DataField::Visibility);

    if (!g_preparedEspScatters[0] && mem.vHandle)
        g_preparedEspScatters[0] = mem.CreateScatterHandle();
    auto first = g_preparedEspScatters[0];
    if (!first) {
        next.frame = 0;
        publish(std::move(next));
        return;
    }
    for (size_t i = 0; i < next.entries.size(); ++i) {
        auto& data = next.entries[i];
        if (!data.ped) continue;
        if (i >= coreHealths.size() || !std::isfinite(coreHealths[i]) ||
            coreHealths[i] < 0.f || coreHealths[i] >= 1000.f)
            mem.AddScatterReadRequest(first, data.ped + offset::playerHealth,
                &data.health, sizeof(data.health));
        mem.AddScatterReadRequest(first, data.ped + offset::playerMaxHealth,
            &data.max_health, sizeof(data.max_health));
        if (need_armor) {
            mem.AddScatterReadRequest(first, data.ped + offset::playerArmor,
                &data.armor, sizeof(data.armor));
            mem.AddScatterReadRequest(first, data.ped + offset::playerArmorAlt1,
                &data.armor_alt_1, sizeof(data.armor_alt_1));
            mem.AddScatterReadRequest(first, data.ped + offset::playerArmorAlt2,
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

        if (need_identity && frameConfig.player_name)
            mem.AddScatterReadRequest(first, data.ped + offset::playerInfo + offset::playerInfo_name,
                &data.name_ptr, sizeof(data.name_ptr));








    }
    mem.SetDmaCallTag("FiveM.EspCore");
    const uint64_t boneBeginMs = GetTickCount64();
    const bool firstReadOk = mem.ExecuteReadScatter(first);
    const uint64_t boneEndMs = GetTickCount64();
    const uint64_t boneSampleMs = boneBeginMs + (boneEndMs - boneBeginMs) / 2u;

    for (auto& data : next.entries) {
        if (!data.health_valid && firstReadOk)
            data.health_valid = FiveM::ClassifyPedHealth(data.health) != FiveM::PedHealthState::Unknown;
    }





    if (need_identity || need_weapon) {
        if (!g_preparedEspScatters[1] && mem.vHandle)
            g_preparedEspScatters[1] = mem.CreateScatterHandle();
        auto second = g_preparedEspScatters[1];
        if (second) {
            bool queued = false;
            auto& nameDue = g_nameDueBuffer;
            nameDue.assign(next.entries.size(), 0);
            for (size_t i = 0; i < next.entries.size(); ++i) {
                auto& data = next.entries[i];
                if (need_identity && data.player_info) {
                    mem.AddScatterReadRequest(second,
                        data.player_info + offset::playerInfo_netId,
                        &data.network_id, sizeof(data.network_id));
                    queued = true;
                    if (frameConfig.player_name) {
                        std::shared_lock nameLock(g_preparedNameCacheMutex);
                        const auto cached = g_preparedNameCache.find(data.player_info);
                        if (cached != g_preparedNameCache.end() &&
                            nowPrep - cached->second.sampledAt < std::chrono::seconds(1)) {
                            std::memcpy(data.player_name, cached->second.name,
                                        sizeof(data.player_name));
                        } else {

                            mem.AddScatterReadRequest(second,
                                data.player_info + offset::playerInfo_name,
                                data.player_name, sizeof(data.player_name) - 1);
                            nameDue[i] = 1;
                        }
                    }
                }
                if (need_weapon && data.weapon_manager) {
                    mem.AddScatterReadRequest(second,
                        data.weapon_manager + offset::weaponMgr_currentWeapon,
                        &data.weapon_info, sizeof(data.weapon_info));
                    queued = true;
                }
            }
            if (queued) {
                mem.SetDmaCallTag("FiveM.EspMetadataPointers");
                mem.ExecuteReadScatter(second);
                for (size_t i = 0; i < next.entries.size(); ++i) {
                    if (!nameDue[i]) continue;
                    auto& data = next.entries[i];
                    if (!IsPlausiblePlayerName(data.player_name))
                        data.player_name[0] = '\0';
                    PreparedNameCacheEntry cached{};
                    std::memcpy(cached.name, data.player_name, sizeof(cached.name));
                    cached.sampledAt = nowPrep;
                    {
                        std::unique_lock nameLock(g_preparedNameCacheMutex);
                        g_preparedNameCache[data.player_info] = cached;
                        if (g_preparedNameCache.size() > 512) {



                            std::vector<std::pair<
                                std::chrono::steady_clock::time_point, uintptr_t>> byAge;
                            byAge.reserve(g_preparedNameCache.size());
                            for (const auto& [playerInfo, entry] : g_preparedNameCache)
                                byAge.emplace_back(entry.sampledAt, playerInfo);
                            const size_t toErase = g_preparedNameCache.size() / 4;
                            std::partial_sort(byAge.begin(),
                                byAge.begin() + static_cast<std::ptrdiff_t>(toErase),
                                byAge.end(),
                                [](const auto& a, const auto& b) { return a.first < b.first; });
                            for (size_t n = 0; n < toErase; ++n)
                                g_preparedNameCache.erase(byAge[n].second);
                        }
                    }
                }
            }
        }
    }


    if (need_identity && frameConfig.player_name) {
        bool needNamePtrResolve = false;
        for (auto& data : next.entries) {
            if (!data.player_info || !data.name_ptr || IsPlausiblePlayerName(data.player_name))
                continue;
            needNamePtrResolve = true;
            break;
        }
        if (needNamePtrResolve) {
            if (!g_preparedEspScatters[2] && mem.vHandle)
                g_preparedEspScatters[2] = mem.CreateScatterHandle();
            auto third = g_preparedEspScatters[2];
            if (third) {
                bool queued = false;
                for (auto& data : next.entries) {
                    if (!data.player_info || !data.name_ptr || IsPlausiblePlayerName(data.player_name))
                        continue;
                    mem.AddScatterReadRequest(third,
                        data.name_ptr,
                        data.player_name, sizeof(data.player_name) - 1);
                    queued = true;
                }
                if (queued) {
                    mem.SetDmaCallTag("FiveM.EspPointedNames");
                    mem.ExecuteReadScatter(third);
                    for (auto& data : next.entries) {
                        if (!data.player_info || !data.name_ptr || IsPlausiblePlayerName(data.player_name))
                            continue;
                        if (!IsPlausiblePlayerName(data.player_name))
                            data.player_name[0] = '\0';
                        PreparedNameCacheEntry cached{};
                        std::memcpy(cached.name, data.player_name, sizeof(cached.name));
                        cached.sampledAt = nowPrep;
                        {
                            std::unique_lock nameLock(g_preparedNameCacheMutex);
                            g_preparedNameCache[data.player_info] = cached;
                        }
                    }
                }
            }
        }
    }

    if (need_weapon) {
        if (!g_preparedEspScatters[2] && mem.vHandle)
            g_preparedEspScatters[2] = mem.CreateScatterHandle();
        auto third = g_preparedEspScatters[2];
        if (third) {
            bool queued = false;
            for (auto& data : next.entries) {
                if (data.weapon_info) {
                    mem.AddScatterReadRequest(third,
                        data.weapon_info + offset::weaponInfo_hash,
                        &data.weapon_hash, sizeof(data.weapon_hash));
                    queued = true;
                }
            }
            if (queued) {
                mem.SetDmaCallTag("FiveM.EspWeaponHashes");
                mem.ExecuteReadScatter(third);
            }
        }
    }


    g_stickyPrep.clear();
    for (const auto& data : next.entries)
        if (data.ped) g_stickyPrep[data.ped] = data;
    next.world_sample_ms = boneSampleMs;


    for (auto& data : next.entries) {
        if (!data.ped) continue;
        if (const Matrix* cam = SelectCameraSnapshot(next.world_sample_ms)) {
            data.camera_sample_ms = GetTickCount64();
        }
    }
    publish(std::move(next));
}

static bool DrawOffscreenArrow(uintptr_t ped, const Vec3& world, const Matrix& viewport,
                               float distance, const PreparedEspData& prepared,
                               const PreparedRenderConfig& cfg) {
    if (!cfg.offscreen_arrows || distance > cfg.offscreen_arrow_max_distance)
        return false;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 2.f || display.y < 2.f)
        return false;

    Vec2 projected{};
    const bool projectedOk = world.world_to_screen(viewport, projected);
    const bool onScreen = projectedOk && projected.x >= 0.f && projected.y >= 0.f &&
        projected.x <= display.x && projected.y <= display.y;
    if (onScreen)
        return false;

    const Matrix v = viewport.Transpose();
    const Vector4 rowX(v._21, v._22, v._23, v._24);
    const Vector4 rowY(v._31, v._32, v._33, v._34);
    const Vector4 rowW(v._41, v._42, v._43, v._44);
    float x = rowX.x * world.x + rowX.y * world.y + rowX.z * world.z + rowX.w;
    float y = rowY.x * world.x + rowY.y * world.y + rowY.z * world.z + rowY.w;
    const float w = rowW.x * world.x + rowW.y * world.y + rowW.z * world.z + rowW.w;
    if (w < 0.f) { x = -x; y = -y; }
    if (!std::isfinite(x) || !std::isfinite(y) || (fabsf(x) + fabsf(y)) < .0001f)
        return false;

    const float length = sqrtf(x * x + y * y);
    x /= length;
    y /= length;
    y = -y;

    const float margin = std::clamp(cfg.offscreen_arrow_margin, 12.f,
        (std::min)(display.x, display.y) * .30f);
    const ImVec2 center(display.x * .5f, display.y * .5f);
    const float halfW = (std::max)(20.f, center.x - margin);
    const float halfH = (std::max)(20.f, center.y - margin);
    const float scale = (std::min)(halfW / (std::max)(fabsf(x), .0001f),
                                   halfH / (std::max)(fabsf(y), .0001f));
    const ImVec2 anchor(center.x + x * scale, center.y + y * scale);
    const float size = std::clamp(cfg.offscreen_arrow_size, 6.f, 32.f);
    const ImVec2 normal(x, y);
    const ImVec2 tangent(-y, x);
    const ImVec2 tip(anchor.x + normal.x * size, anchor.y + normal.y * size);
    const ImVec2 left(anchor.x - normal.x * size * .65f + tangent.x * size * .72f,
                      anchor.y - normal.y * size * .65f + tangent.y * size * .72f);
    const ImVec2 right(anchor.x - normal.x * size * .65f - tangent.x * size * .72f,
                       anchor.y - normal.y * size * .65f - tangent.y * size * .72f);
    ImVec4 color = ImGui::ColorConvertU32ToFloat4(cfg.color_offscreen_arrow);
    color.w *= std::clamp(cfg.offscreen_arrow_opacity, 0.f, 1.f);
    const ImU32 arrowColor = ImGui::ColorConvertFloat4ToU32(color);
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    if (cfg.offscreen_arrow_style == 1)
        draw->AddTriangle(tip, left, right, arrowColor, 2.f);
    else {
        draw->AddTriangleFilled(tip, left, right, arrowColor);
        if (cfg.offscreen_arrow_style == 2)
            draw->AddTriangle(tip, left, right, IM_COL32(0, 0, 0, 190), 1.5f);
    }

    char label[96]{};
    if (cfg.offscreen_arrow_name && IsPlausiblePlayerName(prepared.player_name)) {
        if (cfg.offscreen_arrow_distance)
            std::snprintf(label, sizeof(label), "%s  %.0fm", prepared.player_name, distance);
        else
            std::snprintf(label, sizeof(label), "%s", prepared.player_name);
    } else if (cfg.offscreen_arrow_distance) {
        std::snprintf(label, sizeof(label), "%.0fm", distance);
    }
    if (label[0]) {
        const ImVec2 textSize = ImGui::CalcTextSize(label);
        DrawTextOutlined(draw, ImVec2(anchor.x - textSize.x * .5f,
            anchor.y + size + 4.f), arrowColor, label);
    }
    (void)ped;
    return true;
}

static void DrawEspExtras(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData* cached) {
    (void)localplayer;

    const PreparedRenderConfig* rcfg = FindPreparedRenderConfig();
    if (!rcfg) return;
    if (!rcfg->box_2d && !rcfg->corner_box && !rcfg->skeleton && !rcfg->health_bar && !rcfg->armor_bar &&
        !rcfg->player_name && !rcfg->distance && !rcfg->head_circle &&
        !rcfg->weapon_name && !rcfg->snaplines && !rcfg->trails && !rcfg->head_halo &&
        !rcfg->look_direction && !rcfg->chinese_hat && !rcfg->angel_wings && !rcfg->devil_horns &&
        !rcfg->floating_crown && !rcfg->footstep_esp)
        return;
    using namespace FiveM;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    const PreparedEspData* prepared_esp = FindPreparedEsp(ped);
    const esp::BatchSkeletonData* prepared_bones = FindPreparedSkeleton(ped);




    if (!prepared_esp)
        return;
    if (!EspPedMayDraw(ped, rcfg->show_dead, rcfg->show_knocked)) return;

    Vec3 origin = prepared_esp->origin;
    if (origin.IsZero() && cached && !cached->position_origin.IsZero())
        origin = cached->position_origin;
    if (origin.IsZero()) return;

    Vec3 localPos = FiveM::ESP::FrameCacheValid()
        ? FiveM::ESP::GetFrameLocalPos() : Vec3{};
    if (localPos.IsZero()) return;

    float dist = origin.distance_to(localPos);
    const float maxDist = rcfg->max_esp_distance;
    if (maxDist <= 0.f || dist > maxDist) return;

    float health = prepared_esp->health;


    if (!prepared_esp->health_valid) return;
    const float maxHealthProbe = prepared_esp->max_health;
    if (FiveM::ClassifyPedHealth(health) == FiveM::PedHealthState::Dead && !rcfg->show_dead)
        return;

    const bool visible = EspPedVisible(ped);
    if (rcfg->visible_check && !EspPedPassesVisibility(ped))
        return;

    Vec2 originProjection{};
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool originProjected = origin.world_to_screen(viewport, originProjection);
    const bool originOnScreen = originProjected && originProjection.x >= 0.f &&
        originProjection.y >= 0.f && originProjection.x <= display.x &&
        originProjection.y <= display.y;
    if (!originOnScreen) {
        DrawOffscreenArrow(ped, origin, viewport, dist, *prepared_esp, *rcfg);
        return;
    }


    const bool needBones = rcfg->box_2d || rcfg->corner_box
        || rcfg->snaplines || rcfg->health_bar || rcfg->armor_bar;
    Vec3 head, footL, footR;
    if (needBones) {
        head = PreparedBonePosition(prepared_bones, 0);
        footL = PreparedBonePosition(prepared_bones, 1);
        footR = PreparedBonePosition(prepared_bones, 2);


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


    if (head.z < feet.z + 0.3f)
        head.z = feet.z + 1.0f;

    if (rcfg->hit_marker) {
        struct HitMarkerState { float health = -1.f; double expires = 0.0; Vec3 world{}; };
        static std::unordered_map<uintptr_t, HitMarkerState> hitMarkers;
        static uint64_t hitMarkersGeneration = 0;
        static double s_lastMarkerPrune = 0.0;
        const uint64_t markersGen =
            FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);
        if (markersGen != hitMarkersGeneration) {
            hitMarkers.clear();
            hitMarkersGeneration = markersGen;
        }
        const double markerNow = ImGui::GetTime();
        if (markerNow - s_lastMarkerPrune > 10.0) {
            s_lastMarkerPrune = markerNow;
            for (auto it = hitMarkers.begin(); it != hitMarkers.end();) {
                if (it->second.expires + 30.0 < markerNow)
                    it = hitMarkers.erase(it);
                else
                    ++it;
            }
        }
        auto& marker = hitMarkers[ped];
        if (marker.health >= 0.f && health > 0.f && health < marker.health &&
            aimbot::current_target.valid && aimbot::current_target.ped == ped) {
            marker.world = aimbot::current_target.world_pos.IsZero()
                ? head : aimbot::current_target.world_pos;
            marker.expires = markerNow + .5;
        }
        marker.health = health;
        if (marker.expires > markerNow) {
            Vec2 hitScreen{};
            if (marker.world.world_to_screen(viewport, hitScreen)) {
                const float life = std::clamp(
                    static_cast<float>((marker.expires - markerNow) / .5), 0.f, 1.f);
                const float arm = 5.f + 3.f * (1.f - life);
                const ImU32 color = IM_COL32(255, 255, 255,
                    static_cast<int>(255.f * life));
                const ImVec2 h(hitScreen.x, hitScreen.y);
                dl->AddLine(ImVec2(h.x-arm,h.y-arm), ImVec2(h.x-2,h.y-2), color, 2.f);
                dl->AddLine(ImVec2(h.x+arm,h.y-arm), ImVec2(h.x+2,h.y-2), color, 2.f);
                dl->AddLine(ImVec2(h.x-arm,h.y+arm), ImVec2(h.x-2,h.y+2), color, 2.f);
                dl->AddLine(ImVec2(h.x+arm,h.y+arm), ImVec2(h.x+2,h.y+2), color, 2.f);
            }
        }
    }

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
        return;
    }

    ImU32 colBox = EspPedColor(ped, rcfg->color_box_2d, visible);
    if (health <= 0.f) colBox = rcfg->color_dead;
    ImU32 colCorner = EspPedColor(ped, rcfg->color_corner_box, visible);
    ImU32 colSnap = EspPedColor(ped, rcfg->color_snaplines, visible);

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

    if (rcfg->box_2d) {

        if (rcfg->filled_box) {
            dl->AddRectFilled(boxMin, boxMax, IM_COL32(0, 0, 0, 80));
        }

        dl->AddRect(ImVec2(boxMin.x - 1.f, boxMin.y - 1.f),
                    ImVec2(boxMax.x + 1.f, boxMax.y + 1.f),
                    IM_COL32(0, 0, 0, 160), 0.f, 0, 2.0f);
        dl->AddRect(boxMin, boxMax, colBox, 0.f, 0, 1.6f);
    }

    if (rcfg->corner_box)
        OmniGhost::Gameplay::EspCore::DrawCornerBox(dl, boxMin, boxMax, colCorner,
            std::clamp(rcfg->box_thickness, 0.5f, 8.f));

    if (rcfg->snaplines) {
        ImVec2 scr = ImGui::GetIO().DisplaySize;
        ImVec2 start;
        if (rcfg->snapline_pos == 0) start = ImVec2(scr.x * 0.5f, 0.f);
        else if (rcfg->snapline_pos == 1) start = ImVec2(scr.x * 0.5f, scr.y * 0.5f);
        else start = ImVec2(scr.x * 0.5f, scr.y);
        dl->AddLine(start, ImVec2((boxMin.x + boxMax.x) * 0.5f, boxMin.y), colSnap,
            std::clamp(rcfg->snapline_thickness, 0.5f, 8.f));
    }

    float maxHealth = maxHealthProbe;
    if (maxHealth < 1.f) maxHealth = 200.f;
    float hp100 = (health / maxHealth) * 100.f;
    if (hp100 < 0.f) hp100 = 0.f;
    if (hp100 > 100.f) hp100 = 100.f;
    float hpFrac = hp100 / 100.f;


    if (rcfg->health_bar) {
        const float bw = 4.f;
        const ImU32 healthColor = EspPedColor(ped, rcfg->color_health, visible);
        const int anchor = std::clamp(rcfg->layout_health, 0, 3);
        if (anchor == 2 || anchor == 3) {
            const float x = anchor == 2 ? boxMin.x - bw - rcfg->layout_spacing
                                        : boxMax.x + rcfg->layout_spacing;
            OmniGhost::Gameplay::EspCore::DrawVerticalBar(dl, ImVec2(x, boxMin.y),
                ImVec2(x + bw, boxMax.y), hpFrac, healthColor, Darker(healthColor));
        } else {
            const float y = anchor == 0 ? boxMin.y - bw - rcfg->layout_spacing
                                        : boxMax.y + rcfg->layout_spacing;
            const ImVec2 barMin(boxMin.x, y), barMax(boxMax.x, y + bw);
            dl->AddRectFilled(ImVec2(barMin.x - 1.f, barMin.y - 1.f),
                ImVec2(barMax.x + 1.f, barMax.y + 1.f), IM_COL32(0, 0, 0, 210), 1.f);
            dl->AddRectFilled(barMin, barMax, IM_COL32(18, 18, 22, 225));
            dl->AddRectFilled(barMin, ImVec2(barMin.x + (barMax.x - barMin.x) * hpFrac,
                barMax.y), healthColor);
        }
    }


    if (rcfg->armor_bar) {
        float armor = prepared_esp->armor;

        if (armor < 0.f || armor > 200.f) {
            float a2 = prepared_esp->armor_alt_1;
            if (a2 >= 0.f && a2 <= 200.f) armor = a2;
            else {
                a2 = prepared_esp->armor_alt_2;
                if (a2 >= 0.f && a2 <= 200.f) armor = a2;
            }
        }
        if (armor > 0.5f && armor <= 200.f) {
            float ar100 = armor;
            if (ar100 > 100.f) ar100 = 100.f;
            float arFrac = ar100 / 100.f;
            const float bw = 5.f;
            const ImU32 armorColor = EspPedColor(ped, rcfg->color_armor, visible);
            const int anchor = std::clamp(rcfg->layout_armor, 0, 3);
            if (anchor == 2 || anchor == 3) {
                const float x = anchor == 2 ? boxMin.x - bw - rcfg->layout_spacing - 6.f
                                            : boxMax.x + rcfg->layout_spacing;
                OmniGhost::Gameplay::EspCore::DrawVerticalBar(dl, ImVec2(x, boxMin.y),
                    ImVec2(x + bw, boxMax.y), arFrac, armorColor, Darker(armorColor));
            } else {
                const float y = anchor == 0 ? boxMin.y - bw - rcfg->layout_spacing - 6.f
                                            : boxMax.y + rcfg->layout_spacing + 6.f;
                const ImVec2 barMin(boxMin.x, y), barMax(boxMax.x, y + bw);
                dl->AddRectFilled(ImVec2(barMin.x - 1.f, barMin.y - 1.f),
                    ImVec2(barMax.x + 1.f, barMax.y + 1.f), IM_COL32(0, 0, 0, 210), 1.f);
                dl->AddRectFilled(barMin, barMax, IM_COL32(18, 18, 22, 225));
                dl->AddRectFilled(barMin, ImVec2(barMin.x + (barMax.x - barMin.x) * arFrac,
                    barMax.y), armorColor);
            }
        }
    }


    {
        const float cx = (boxMin.x + boxMax.x) * 0.5f;
        float sideOffsets[4] = { rcfg->layout_spacing, rcfg->layout_spacing,
                                 rcfg->layout_spacing, rcfg->layout_spacing };
        auto drawLayoutText = [&](const char* text, ImU32 color, int rawAnchor) {
            if (!text || !*text) return;
            const int anchor = std::clamp(rawAnchor, 0, 3);
            const float scale = std::clamp(rcfg->layout_text_scale, .65f, 1.6f);
            ImFont* font = ImGui::GetFont();
            const float fontSize = ImGui::GetFontSize() * scale;
            const ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, text);
            ImVec2 pos{};
            if (anchor == 0) {
                pos = ImVec2(cx - ts.x * .5f, boxMin.y - sideOffsets[0] - ts.y);
            } else if (anchor == 1) {
                pos = ImVec2(cx - ts.x * .5f, boxMax.y + sideOffsets[1]);
            } else if (anchor == 2) {
                pos = ImVec2(boxMin.x - sideOffsets[2] - ts.x, boxMin.y + sideOffsets[2]);
            } else {
                pos = ImVec2(boxMax.x + sideOffsets[3], boxMin.y + sideOffsets[3]);
            }
            DrawTextOutlinedScaled(dl, pos, color, text, scale);
            sideOffsets[anchor] += ts.y + 2.f;
        };


        if (rcfg->player_name || rcfg->player_id) {
            char nbuf[64]{};
            const uintptr_t pinfo = prepared_esp->player_info;
            (void)pinfo;
            const uint32_t netId = prepared_esp->network_id;
            if (rcfg->player_name && IsPlausiblePlayerName(prepared_esp->player_name))
                snprintf(nbuf, sizeof(nbuf), "%s", prepared_esp->player_name);
            else if (rcfg->player_id && netId)
                snprintf(nbuf, sizeof(nbuf), "ID %u", netId);
            else
                snprintf(nbuf, sizeof(nbuf), "Jogador");
            char line[96]{};
            if (rcfg->player_name && rcfg->player_id && netId)
                snprintf(line, sizeof(line), "%s [%u]", nbuf, netId);
            else if (rcfg->player_id && netId)
                snprintf(line, sizeof(line), "ID %u", netId);
            else
                snprintf(line, sizeof(line), "%s", nbuf);
            drawLayoutText(line, EspPedColor(ped,
                (rcfg->player_name ? rcfg->color_name : rcfg->color_id), visible),
                rcfg->layout_name);
        }

        if (rcfg->weapon_name) {
            uintptr_t wpnMgr = prepared_esp->weapon_manager;
            const char* wname = "Desarmado";
            char wbuf[48];
            if (wpnMgr) {
                uintptr_t wpnInfo = prepared_esp->weapon_info;
                if (wpnInfo) {
                    uint32_t hash = prepared_esp->weapon_hash;
                    switch (hash) {

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

                    case 0x13532244u: wname = "Micro SMG"; break;
                    case 0x2BE6766Bu: wname = "SMG"; break;
                    case 0x78A97CD0u: wname = "SMG MK2"; break;
                    case 0xEFE7E2DFu: wname = "Assault SMG"; break;
                    case 0x0A3D4D34u: wname = "Combat PDW"; break;
                    case 0xDB1AA450u: wname = "Machine Pistol"; break;
                    case 0xBD248B55u: wname = "Mini SMG"; break;
                    case 0x476BF155u: wname = "Unholy Hellbringer"; break;

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

                    case 0x9D07F764u: wname = "MG"; break;
                    case 0x7FD62962u: wname = "Combat MG"; break;
                    case 0xDBBD7280u: wname = "Combat MG MK2"; break;
                    case 0x61012683u: wname = "Gusenberg"; break;

                    case 0x05FC3C11u: wname = "Sniper Rifle"; break;
                    case 0x0C472FE2u: wname = "Heavy Sniper"; break;
                    case 0x0A914799u: wname = "Heavy Sniper MK2"; break;
                    case 0xC734385Au: wname = "Marksman Rifle"; break;
                    case 0x6A6C02E0u: wname = "Marksman Rifle MK2"; break;
                    case 0x6E7DDDECu: wname = "Precision Rifle"; break;

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

                    case 0x34A67B97u: wname = "Jerry Can"; break;
                    case 0xFBAB5776u: wname = "Parachute"; break;
                    case 0x060EC506u: wname = "Fire Extinguisher"; break;
                    case 0xBA536372u: wname = "Hazard Can"; break;
                    case 0x1B574AFEu: wname = "Fertilizer Can"; break;
                    case 0x184140A1u: wname = "Fertilizer Can"; break;
                    default:
                        if (hash != 0) {


                            const std::string registered =
                                OmniGhost::Gameplay::WeaponNameRegistry::Instance()
                                    .GetWeaponName(hash);
                            snprintf(wbuf, sizeof(wbuf), "%s",
                                registered.empty() ? "Arma" : registered.c_str());
                            wname = wbuf;
                        }
                        break;
                    }
                }
            }
            drawLayoutText(wname, EspPedColor(ped, rcfg->color_weapon, visible),
                rcfg->layout_weapon);
        }

        if (rcfg->distance) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fm", dist);
            drawLayoutText(buf, EspPedColor(ped, rcfg->color_distance, visible),
                rcfg->layout_distance);
        }
    }
}



static uintptr_t ResolveLocalPlayerPointer(uintptr_t fallback) {
    using namespace FiveM;


    return offset::localplayer ? offset::localplayer : fallback;
}

static bool IsLocalPlayerPed(uintptr_t ped, uintptr_t localplayer) {
    if (!ped) return false;
    uintptr_t lp = ResolveLocalPlayerPointer(localplayer);
    return lp != 0 && ped == lp;
}


void esp::render_esp_for_ped(uintptr_t ped, Matrix viewport, uintptr_t localplayer) {
    if (!config.enabled) return;
    if (!config.self_esp && IsLocalPlayerPed(ped, localplayer)) return;

    if (config.team_check && EspPedIsFriend(ped))
        return;

    if (!EspPedMayDraw(ped, config.show_dead, config.show_knocked))
        return;

    if (config.visible_check && !EspPedPassesVisibility(ped))
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
    if (config.team_check && EspPedIsFriend(ped))
        return;
    if (!EspPedMayDraw(ped, config.show_dead, config.show_knocked))
        return;
    if (config.visible_check && !EspPedPassesVisibility(ped))
        return;

    if (config.head_circle)
        draw_head_circle_cached(ped, viewport, localplayer, cached_ped_data);
    if (config.skeleton)
        draw_skeleton(ped, viewport, localplayer);

    DrawMotionVisuals(ped, viewport, &cached_ped_data);

    if (AnyEspExtrasEnabled()) DrawEspExtras(ped, viewport, localplayer, &cached_ped_data);
}

void esp::DrawActiveFeaturesPanel() {
    if (!config.active_features_panel)
        return;
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    if (display.x < 2.f || display.y < 2.f)
        return;

    config.active_panel_x = std::clamp(config.active_panel_x, 0.f, .95f);
    config.active_panel_y = std::clamp(config.active_panel_y, 0.f, .95f);
    ImGui::SetNextWindowPos(ImVec2(display.x * config.active_panel_x,
                                  display.y * config.active_panel_y), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(std::clamp(config.active_panel_opacity, .15f, 1.f));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (!app_settings::menu_open)
        flags |= ImGuiWindowFlags_NoInputs;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 8, 8, 245));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(212, 175, 55, 105));
    if (ImGui::Begin("##fivem_active_features", nullptr, flags)) {
        ImGui::SetWindowFontScale(std::clamp(config.active_panel_scale, .65f, 1.6f));
        ImGui::TextColored(ImVec4(.83f, .69f, .22f, 1.f), "FiveM DMA");
        if (!config.active_panel_compact)
            ImGui::Separator();
        auto row = [](const char* name, bool active) {
            if (!active) return;
            ImGui::TextUnformatted(name);
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(.20f, .90f, .48f, 1.f), "ON");
        };
        if (config.active_panel_show_aim) row("Aimbot", aimbot::config.aimbot_enabled);
        if (config.active_panel_show_trigger) row("Triggerbot", aimbot::config.trigger_enabled);
        if (config.active_panel_show_esp) row("Player ESP", config.enabled);
        if (config.active_panel_show_radar)
            row("Radar", config.radar_enabled || config.triangle_radar || config.square_radar);
        row("Dynamic FOV", aimbot::config.aimbot_enabled &&
            aimbot::config.dynamic_fov_mode != aimbot::DynamicFovMode::Static);
        row("Offscreen Arrows", config.offscreen_arrows);

        if (app_settings::menu_open && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            const ImVec2 pos = ImGui::GetWindowPos();
            config.active_panel_x = std::clamp(pos.x / display.x, 0.f, .95f);
            config.active_panel_y = std::clamp(pos.y / display.y, 0.f, .95f);
        }
        ImGui::SetWindowFontScale(1.f);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar();
}


static void DrawHeadCircleAt(ImDrawList* draw_list, const Vec2& screen, ImU32 col, float distance_m = 25.f) {

    float r = 220.0f / (distance_m + 12.0f);
    if (r < 2.0f) r = 2.0f;
    if (r > 9.0f) r = 9.0f;
    float th = std::clamp(esp::config.head_circle_thickness, 0.5f, 6.f);
    if (r <= 4.f) th = (std::min)(th, 1.6f);

    const int segs = FxSegCount(distance_m, 20, 10);
    if (esp::config.circle_type == 1) {
        draw_list->AddCircleFilled(ImVec2(screen.x, screen.y), r, col, segs);
    } else if (esp::config.circle_type == 2) {
        draw_list->AddLine(ImVec2(screen.x - r - 2, screen.y), ImVec2(screen.x + r + 2, screen.y), col, th);
        draw_list->AddLine(ImVec2(screen.x, screen.y - r - 2), ImVec2(screen.x, screen.y + r + 2), col, th);
    } else {
        draw_list->AddCircle(ImVec2(screen.x, screen.y), r, col, segs, th);
    }
}

void esp::draw_head_circle(uintptr_t ped, Matrix viewport, uintptr_t localplayer) {
    (void)localplayer;
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    if (!draw_list) return;

    const BatchSkeletonData* prepared = FindPreparedSkeleton(ped);
    const PreparedEspData* core = FindPreparedEsp(ped);
    Vec3 origin = prepared ? prepared->origin : (core ? core->origin : Vec3{});
    if (origin.IsZero()) return;

    Vec3 head_world_pos = prepared
        ? PreparedBonePosition(prepared, 0)
        : Vec3{};
    if (head_world_pos.IsZero() || head_world_pos.distance_to(origin) > 3.5f) {
        head_world_pos = origin;
        head_world_pos.z += 0.9f;
    }

    Vec2 head_screen_pos;
    if (!CachedWorldToScreen(ped, 0, head_world_pos, viewport, head_screen_pos))
        return;

    float dist = 25.f;
    if (localplayer) {
        Vec3 lp = FiveM::ESP::FrameCacheValid()
            ? FiveM::ESP::GetFrameLocalPos() : Vec3{};
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
    if (origin.IsZero()) return;

    Vec3 head_world_pos = prepared
        ? PreparedBonePosition(prepared, 0)
        : Vec3{};
    if (head_world_pos.IsZero() || head_world_pos.distance_to(origin) > 3.5f) {
        head_world_pos = origin;
        head_world_pos.z += 0.9f;
    }

    Vec2 head_screen_pos;
    if (!head_world_pos.world_to_screen(viewport, head_screen_pos))
        return;

    const bool visible = EspPedVisible(ped);
    ImU32 color = EspPedColor(ped, config.color_head_circle, visible);
    if (!config.rgb_mode && !config.visibility_colors && cached_ped_data.health < 50.0f)
        color = IM_COL32(255, 255, 0, 255);
    if (!config.rgb_mode && !config.visibility_colors && cached_ped_data.health < 25.0f)
        color = IM_COL32(255, 0, 0, 255);
    if (!config.rgb_mode && cached_ped_data.health <= 0.0f)
        color = IM_COL32(100, 100, 100, 255);

    float dist = 25.f;
    if (localplayer) {
        Vec3 lp = FiveM::ESP::FrameCacheValid()
            ? FiveM::ESP::GetFrameLocalPos() : Vec3{};
        if (!lp.IsZero())
            dist = lp.distance_to(head_world_pos);
    }
    DrawHeadCircleAt(draw_list, head_screen_pos, color, dist);
}


void esp::draw_skeleton_cached(uintptr_t ped, Matrix viewport, uintptr_t localplayer, const PedData& cached_ped_data) {
    (void)cached_ped_data;

    draw_skeleton(ped, viewport, localplayer);
}

void esp::draw_skeleton(uintptr_t ped, Matrix viewport, uintptr_t localplayer) {
    if (!EspPedMayDraw(ped, esp::config.show_dead, esp::config.show_knocked)) return;
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    if (!draw_list) return;

    if (esp::config.skeleton_lod && FiveM::ESP::FrameCacheValid()) {

        if (const auto* pe = FindPreparedEsp(ped)) {
            const Vec3& lp = FiveM::ESP::GetFrameLocalPos();
            if (!lp.IsZero() && !pe->origin.IsZero() &&
                (esp::config.max_esp_distance <= 0.f ||
                 pe->origin.distance_to(lp) > esp::config.max_esp_distance))
                return;
        }
    }


    struct SkelSmooth {
        Vec3 head{}, neck{}, hip{}, lfoot{}, rfoot{}, lhand{}, rhand{}, origin{};
        int lod = 0;
        int swapHands = 0;
        bool init = false;
        int missFrames = 0;
        uint32_t lastFrame = 0;
    };
    static std::unordered_map<uintptr_t, SkelSmooth> s_smooth;
    static uint64_t s_smoothGeneration = 0;
    static uint32_t s_frameId = 0;
    static uint32_t s_lastBump = 0;
    {
        const uint64_t gen = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);
        if (gen != s_smoothGeneration) {
            s_smooth.clear();
            s_smoothGeneration = gen;
        }
    }

    {
        uint32_t f = (uint32_t)ImGui::GetFrameCount();
        if (f != s_lastBump) { s_lastBump = f; s_frameId = f; }

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
        return;

    float lodDist = 0.f;
    if (localplayer) {
        Vec3 lp{};
        if (FiveM::ESP::FrameCacheValid())
            lp = FiveM::ESP::GetFrameLocalPos();
        if (!lp.IsZero()) {
            lodDist = origin.distance_to(lp);
        }
    }
    {
        const float maxDist = config.max_esp_distance;
        if (maxDist <= 0.f || lodDist > maxDist)
            return;
    }


    sm.lod = 0;

    Matrix bone_matrix{};
    Vector3 localBones[9]{};
    if (prepared && prepared->bone_offsets.size() >= 9) {
        bone_matrix = prepared->bone_matrix;
        for (int i = 0; i < 9; ++i)
            localBones[i] = prepared->bone_offsets[static_cast<size_t>(i)];
    } else return;

    auto readBone = [&](int idx) -> Vec3 {
        if (idx < 0 || idx > 8) return {};

        if (localBones[idx].x == 0.f && localBones[idx].y == 0.f && localBones[idx].z == 0.f)
            return {};
        DirectX::SimpleMath::Vector3 boneVec(localBones[idx].x, localBones[idx].y, localBones[idx].z);
        DirectX::SimpleMath::Vector3 tf = DirectX::XMVector3Transform(boneVec, bone_matrix);
        Vec3 b(tf.x, tf.y, tf.z);
        if (b.IsZero()) return {};

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


    auto limbOk = [&](const Vec3& limb, const Vec3& root, float maxDist) -> bool {
        if (limb.IsZero() || root.IsZero()) return false;
        float d = limb.distance_to(root);
        return d > 0.05f && d < maxDist;
    };

    if (!lfoot.IsZero() && head.IsZero() == false) {
        if (lfoot.z > head.z + 0.2f || !limbOk(lfoot, origin, 2.4f)) lfoot = {};
    }
    if (!rfoot.IsZero() && head.IsZero() == false) {
        if (rfoot.z > head.z + 0.2f || !limbOk(rfoot, origin, 2.4f)) rfoot = {};
    }
    if (!lhand.IsZero() && !origin.IsZero() && !limbOk(lhand, origin, 2.6f)) lhand = {};
    if (!rhand.IsZero() && !origin.IsZero() && !limbOk(rhand, origin, 2.6f)) rhand = {};

    if (!lfoot.IsZero() && !rfoot.IsZero() && lfoot.distance_to(rfoot) > 1.8f) {

        if (lfoot.distance_to(origin) > rfoot.distance_to(origin)) lfoot = {};
        else rfoot = {};
    }


    int realHits = 0;
    if (!head.IsZero()) realHits++;
    if (!lfoot.IsZero()) realHits++;
    if (!rfoot.IsZero()) realHits++;
    if (!neck.IsZero()) realHits++;
    if (!hip.IsZero()) realHits++;
    if (!lhand.IsZero()) realHits++;
    if (!rhand.IsZero()) realHits++;


    if (realHits < 2) {
        sm.missFrames++;
        if (sm.missFrames > 1) {
            s_smooth.erase(ped);
            return;
        }

        if (!sm.init) return;
        head = sm.head; neck = sm.neck; hip = sm.hip;
        lfoot = sm.lfoot; rfoot = sm.rfoot;
        lhand = sm.lhand; rhand = sm.rhand;
    } else {
        sm.missFrames = 0;
    }


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


    auto smoothTo = [](Vec3& prev, const Vec3& cur, float a, bool inited) {
        if (!inited || prev.IsZero()) { prev = cur; return; }
        float dx = cur.x - prev.x, dy = cur.y - prev.y, dz = cur.z - prev.z;
        float dist2 = dx*dx + dy*dy + dz*dz;

        if (dist2 > 1.8f * 1.8f) { prev = cur; return; }
        prev.x = prev.x + (cur.x - prev.x) * a;
        prev.y = prev.y + (cur.y - prev.y) * a;
        prev.z = prev.z + (cur.z - prev.z) * a;
    };

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



    Vec3 spineDir = vnorm(Vec3(neck.x - hip.x, neck.y - hip.y, neck.z - hip.z));
    Vec3 side;
    {
        Vec3 handSpan(rhand.x - lhand.x, rhand.y - lhand.y, rhand.z - lhand.z);

        float along = handSpan.x * spineDir.x + handSpan.y * spineDir.y + handSpan.z * spineDir.z;
        handSpan.x -= spineDir.x * along;
        handSpan.y -= spineDir.y * along;
        handSpan.z -= spineDir.z * along;
        if (vlen(handSpan) > 0.12f) {
            side = vnorm(handSpan);
        } else {

            side = vnorm(vcross(spineDir, Vec3(0.f, 0.f, 1.f)));
            if (vlen(side) < 0.1f)
                side = vnorm(vcross(spineDir, Vec3(0.f, 1.f, 0.f)));
        }
    }


    const float shW  = 0.20f;
    const float clav = 0.10f;
    const float hipW = 0.11f;


    Vec3 spineU = lerp3(neck, hip, 0.18f);
    Vec3 chest  = lerp3(neck, hip, 0.34f);
    Vec3 spineM = lerp3(neck, hip, 0.55f);
    Vec3 spineL = lerp3(neck, hip, 0.78f);


    Vec3 lclav = vadd(neck, vscale(side, -clav)); lclav.z -= 0.02f;
    Vec3 rclav = vadd(neck, vscale(side,  clav)); rclav.z -= 0.02f;
    Vec3 lsh   = vadd(neck, vscale(side, -shW));  lsh.z   -= 0.04f;
    Vec3 rsh   = vadd(neck, vscale(side,  shW));  rsh.z   -= 0.04f;


    lsh = lerp3(lsh, lhand, 0.08f);
    rsh = lerp3(rsh, rhand, 0.08f);



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


    Vec3 lhip = vadd(hip, vscale(side, -hipW));
    Vec3 rhip = vadd(hip, vscale(side,  hipW));
    Vec3 lknee = lerp3(lhip, lfoot, 0.48f);
    Vec3 rknee = lerp3(rhip, rfoot, 0.48f);

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
    float th = std::clamp(config.skeleton_thickness > 0.1f ? config.skeleton_thickness : line_thickness, 0.5f, 8.f);
    if (bodyPx < 70.f) th = (std::max)(th, 1.7f);
    if (bodyPx < 35.f) th = (std::max)(th, 2.1f);

    auto link = [&](int a, int b) {
        if (a < 0 || b < 0 || !J[a].on || !J[b].on) return;
        float dx = J[a].s.x - J[b].s.x;
        float dy = J[a].s.y - J[b].s.y;
        float len2 = dx * dx + dy * dy;

        float maxSeg = bodyPx * 0.95f;
        if (maxSeg < 22.f) maxSeg = 22.f;
        if (maxSeg > 260.f) maxSeg = 260.f;
        if (len2 > maxSeg * maxSeg || len2 < 0.8f) return;
        draw_list->AddLine(ImVec2(J[a].s.x, J[a].s.y),
                           ImVec2(J[b].s.x, J[b].s.y), col, th);
    };


    link(iHead, iNeck);
    link(iNeck, iSpU);
    link(iSpU, iChest);
    link(iChest, iSpM);
    link(iSpM, iSpL);
    link(iSpL, iHip);


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

void esp::DrawPlayerRadar(const Matrix& , uintptr_t localplayer) {
    if (!localplayer) return;



    if (config.square_radar && config.radar_enabled) {
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        if (!dl) return;
        ImVec2 scr = ImGui::GetIO().DisplaySize;
        float cx = scr.x * config.radar_pos_x;
        float cy = scr.y * config.radar_pos_y;
        float R = config.radar_size;
        float range = (std::max)(20.f, config.radar_range);

        static bool draggingRadar = false;
        ImGuiIO& io = ImGui::GetIO();
        const bool mouseInside = io.MousePos.x >= cx - R && io.MousePos.x <= cx + R &&
                                 io.MousePos.y >= cy - R && io.MousePos.y <= cy + R;
        if (app_settings::menu_open && mouseInside && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            draggingRadar = true;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            draggingRadar = false;
        if (app_settings::menu_open && draggingRadar) {
            cx = std::clamp(cx + io.MouseDelta.x, R + 8.f, scr.x - R - 8.f);
            cy = std::clamp(cy + io.MouseDelta.y, R + 8.f, scr.y - R - 8.f);
            config.radar_pos_x = cx / (std::max)(1.f, scr.x);
            config.radar_pos_y = cy / (std::max)(1.f, scr.y);
        }

        const ImVec2 radarMin(cx - R, cy - R);
        const ImVec2 radarMax(cx + R, cy + R);
        dl->AddRectFilled(ImVec2(radarMin.x - 4.f, radarMin.y - 4.f),
            ImVec2(radarMax.x + 4.f, radarMax.y + 4.f), IM_COL32(8, 8, 12, 205), 9.f);
        dl->AddRect(radarMin, radarMax, IM_COL32(212, 175, 55, 165), 7.f, 0, 1.5f);
        dl->AddLine(ImVec2(radarMin.x, cy), ImVec2(radarMax.x, cy), IM_COL32(212, 175, 55, 45), 1.f);
        dl->AddLine(ImVec2(cx, radarMin.y), ImVec2(cx, radarMax.y), IM_COL32(212, 175, 55, 45), 1.f);
        dl->AddCircleFilled(ImVec2(cx, cy), 3.5f, IM_COL32(0, 255, 120, 255), 12);
        if (!FiveM::ESP::FrameCacheValid()) return;
        const Vec3 localPos = FiveM::ESP::GetFrameLocalPos();
        if (localPos.IsZero()) return;
        auto snapshot = FiveM::ESP::GetValidPedsSnapshot();
        if (snapshot.validPeds.empty()) return;







    float fx = 0.f, fy = 1.f;
    {
        const Matrix& vm = FiveM::ESP::GetFrameViewMatrix();
        const float rightX = vm._12;
        const float rightY = vm._22;
        const float len = sqrtf(rightX * rightX + rightY * rightY);
        if (len > 1e-4f) {
            fx = -rightY / len;
            fy = rightX / len;
        }
    }
    float rx = fy, ry = -fx;
    for (size_t i = 0; i < snapshot.validPeds.size(); ++i) {
        uintptr_t ped = snapshot.validPeds[i];
        if (!ped || ped == localplayer) continue;
        const bool visible = EspPedVisible(ped);
        if (config.visible_check && !EspPedPassesVisibility(ped)) continue;
        Vec3 pos = (i < snapshot.positions.size()) ? snapshot.positions[i] : Vec3{};
        if (pos.IsZero()) continue;
        float dx = pos.x - localPos.x, dy = pos.y - localPos.y;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist > range || dist < 0.5f) continue;
        float localX = dx * rx + dy * ry;
        float localY = dx * fx + dy * fy;
        float nx = (localX / range) * R;
        float ny = (-localY / range) * R;
        nx = std::clamp(nx, -R + 5.f, R - 5.f);
        ny = std::clamp(ny, -R + 5.f, R - 5.f);
        ImU32 col = EspPedColor(ped, config.color_box_2d, visible);
        if (!config.visibility_colors && friends::config.show_friend_esp && EspPedIsFriend(ped))
            col = friends::config.friend_color;
        dl->AddCircleFilled(ImVec2(cx + nx, cy + ny), 3.0f, col, 10);
    }
    }


    if (!config.triangle_radar) return;
    if (FiveM::ESP::validPeds.empty()) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;

    ImVec2 scr = ImGui::GetIO().DisplaySize;
    ImVec2 center(scr.x * 0.5f, scr.y * 0.5f);


    float radius = config.triangle_radar_radius;








    radius = (std::max)(40.f, config.triangle_radar_radius);

    if (!FiveM::ESP::FrameCacheValid()) return;
    auto snapshot = FiveM::ESP::GetValidPedsSnapshot();
    if (snapshot.validPeds.empty()) return;

    Vec3 localPos = FiveM::ESP::FrameCacheValid()
        ? FiveM::ESP::GetFrameLocalPos() : Vec3{};
    if (localPos.IsZero()) return;


    Matrix view = FiveM::ESP::GetFrameViewMatrix();




    float tfx = 0.f, tfy = 1.f;
    {
        const float rightX = view._12;
        const float rightY = view._22;
        const float len = sqrtf(rightX * rightX + rightY * rightY);
        if (len > 1e-4f) {
            tfx = -rightY / len;
            tfy = rightX / len;
        }
    }
    const float tRx = tfy, tRy = -tfx;

    struct TriSm { float ix, iy; bool init; uint32_t lastF; };
    static std::unordered_map<uintptr_t, TriSm> s_tri;
    static uint64_t s_triGeneration = 0;
    {
        const uint64_t gen = FiveM::ESP::GetSessionGeneration().load(std::memory_order_acquire);
        if (gen != s_triGeneration) {
            s_tri.clear();
            s_triGeneration = gen;
        }
    }
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

    const float maxDist = config.max_esp_distance;
    if (maxDist <= 0.f) return;

    for (size_t i = 0; i < snapshot.validPeds.size(); ++i) {
        uintptr_t ped = snapshot.validPeds[i];
        if (!ped || ped == localplayer) continue;
        if (config.team_check && EspPedIsFriend(ped)) continue;
        const bool visible = EspPedVisible(ped);
        if (config.visible_check && !EspPedPassesVisibility(ped)) continue;

        Vec3 pos = (i < snapshot.positions.size()) ? snapshot.positions[i] : Vec3{};
        if (pos.IsZero()) continue;

        float dist = localPos.distance_to(pos);
        if (dist > maxDist) continue;
        if (dist < 0.8f) continue;

        Vec2 sp;
        bool onScreen = pos.world_to_screen(view, sp);
        if (!onScreen) {
            float wdx = pos.x - localPos.x;
            float wdy = pos.y - localPos.y;


            float localX = wdx * tRx + wdy * tRy;
            float localY = wdx * tfx + wdy * tfy;
            float ang = atan2f(localX, localY);
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

            ts.ix += (rawIx - ts.ix) * 0.45f;
            ts.iy += (rawIy - ts.iy) * 0.45f;
        }
        float ix = ts.ix, iy = ts.iy;

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

        ImU32 col = EspPedColor(ped, config.color_box_2d, visible);
        if (!config.visibility_colors && friends::config.show_friend_esp && EspPedIsFriend(ped))
            col = friends::config.friend_color;

        dl->AddTriangleFilled(tip, a, b, col);
        dl->AddTriangle(tip, a, b, IM_COL32(0, 0, 0, 160), 1.0f);
    }
}




void esp::draw_enhanced_health_info(const Vec2& position, float health) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();


    float health_ratio = health / 100.0f;
    if (health_ratio < 0.0f) health_ratio = 0.0f;
    if (health_ratio > 1.0f) health_ratio = 1.0f;

    float bar_width = 40.0f;
    float bar_height = 5.0f;


    draw_list->AddRectFilled(
        ImVec2(position.x - bar_width / 2, position.y - 25),
        ImVec2(position.x + bar_width / 2, position.y - 25 + bar_height),
        IM_COL32(0, 0, 0, 180)
    );


    ImU32 health_color;
    if (health > 75.0f) {
        health_color = IM_COL32(0, 255, 0, 255);
    }
    else if (health > 50.0f) {
        health_color = IM_COL32(255, 255, 0, 255);
    }
    else if (health > 25.0f) {
        health_color = IM_COL32(255, 165, 0, 255);
    }
    else {
        health_color = IM_COL32(255, 0, 0, 255);
    }

    draw_list->AddRectFilled(
        ImVec2(position.x - bar_width / 2, position.y - 25),
        ImVec2(position.x - bar_width / 2 + bar_width * health_ratio, position.y - 25 + bar_height),
        health_color
    );


    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.0f", health);
    draw_list->AddText(
        ImVec2(position.x - 10, position.y - 18),
        IM_COL32(255, 255, 255, 255),
        buffer
    );
}


void esp::draw_health_info(const Vec2& position, float health) {
    ImDrawList* draw_list = ImGui::GetForegroundDrawList();
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.1f", health);


    ImU32 text_color = IM_COL32(255, 255, 255, 255);
    if (health < 50.0f) text_color = IM_COL32(255, 255, 0, 255);
    if (health < 25.0f) text_color = IM_COL32(255, 0, 0, 255);

    draw_list->AddText(ImVec2(position.x, position.y - 20), text_color, buffer);
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



namespace {
struct WaypointBlipSnapshot {
    Vec3 localPos{};
    float waypointX = 0.f;
    float waypointY = 0.f;
    bool waypointValid = false;
    std::array<Vec3, 64> blips{};
    int blipCount = 0;
    std::chrono::steady_clock::time_point sampledAt{};
};
OmniGhost::Gameplay::SnapshotExchange<WaypointBlipSnapshot> g_waypointBlips;
VMMDLL_SCATTER_HANDLE g_waypointScatter = nullptr;
std::chrono::steady_clock::time_point g_lastWaypointRead{};
}

void esp::PrepareWaypointAndBlips(const Config& frameConfig, const Vec3& localPos) {
    const auto now = std::chrono::steady_clock::now();
    if (g_lastWaypointRead.time_since_epoch().count() != 0 &&
        now - g_lastWaypointRead < std::chrono::milliseconds(200)) return;
    g_lastWaypointRead = now;
    WaypointBlipSnapshot next{};
    next.localPos = localPos;
    if (frameConfig.radar_enabled &&
        (frameConfig.waypoint_line || frameConfig.blip_esp) && !localPos.IsZero()) {
        if (!g_waypointScatter && mem.vHandle)
            g_waypointScatter = mem.CreateScatterHandle();
        if (g_waypointScatter) {
            float waypoint[2]{};
            std::array<uintptr_t, 64> entries{};
            bool queued = false;
            if (frameConfig.waypoint_line && FiveM::offset::waypoint) {
                mem.AddScatterReadRequest(g_waypointScatter, FiveM::offset::waypoint,
                    waypoint, sizeof(waypoint));
                queued = true;
            }
            if (frameConfig.blip_esp && FiveM::offset::blip_list) {
                for (size_t i = 0; i < entries.size(); ++i) {
                    mem.AddScatterReadRequest(g_waypointScatter,
                        FiveM::offset::blip_list + i * sizeof(uintptr_t),
                        &entries[i], sizeof(uintptr_t));
                }
                queued = true;
            }
            if (queued) mem.ExecuteReadScatter(g_waypointScatter);
            next.waypointX = waypoint[0];
            next.waypointY = waypoint[1];
            next.waypointValid = frameConfig.waypoint_line && FiveM::offset::waypoint &&
                std::isfinite(waypoint[0]) && std::isfinite(waypoint[1]) &&
                waypoint[0] > -5000.f && waypoint[0] < 9000.f &&
                waypoint[1] > -5000.f && waypoint[1] < 9000.f;
            std::array<Vec3, 64> primary{}, fallback{};
            bool queuedPositions = false;
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i] <= 0x10000u || entries[i] >= 0x7FFFFFFFFFFFULL) continue;
                mem.AddScatterReadRequest(g_waypointScatter, entries[i] + FiveM::offset::waypointBlipPosition,
                    &primary[i], sizeof(Vec3));
                mem.AddScatterReadRequest(g_waypointScatter, entries[i] + FiveM::offset::waypointBlipSprite,
                    &fallback[i], sizeof(Vec3));
                queuedPositions = true;
            }
            if (queuedPositions) mem.ExecuteReadScatter(g_waypointScatter);
            for (size_t i = 0; i < entries.size(); ++i) {
                const Vec3& pos = primary[i].IsZero() ? fallback[i] : primary[i];
                if (pos.IsZero() || !std::isfinite(pos.x) || !std::isfinite(pos.y) ||
                    !std::isfinite(pos.z) || pos.x < -5000.f || pos.x > 9000.f) continue;
                next.blips[static_cast<size_t>(next.blipCount++)] = pos;
            }
        }
    }
    next.sampledAt = std::chrono::steady_clock::now();
    if (auto slot = g_waypointBlips.TryBeginWrite()) {
        *slot.value = next;
        g_waypointBlips.Publish(slot.index);
    }
}

void esp::ShutdownWaypointAndBlips() {
    if (g_waypointScatter) {
        mem.CloseScatterHandle(g_waypointScatter);
        g_waypointScatter = nullptr;
    }
    g_lastWaypointRead = {};
    if (auto slot = g_waypointBlips.TryBeginWrite()) {
        *slot.value = WaypointBlipSnapshot{};
        g_waypointBlips.Publish(slot.index);
    }
}

void esp::ShutdownPreparedFrames() {
    g_stickyPrep.clear();
    {
        std::unique_lock nameLock(g_preparedNameCacheMutex);
        g_preparedNameCache.clear();
    }
    g_nameDueBuffer.clear();
    g_proj_cache.clear();
    g_lastPrepScatter = {};

    {
        std::scoped_lock lock(g_preparedBoneScatterMutex);
        if (g_preparedBoneScatter) {
            mem.CloseScatterHandle(g_preparedBoneScatter);
            g_preparedBoneScatter = nullptr;
        }
    }
    for (auto& handle : g_preparedEspScatters) {
        if (handle) {
            mem.CloseScatterHandle(handle);
            handle = nullptr;
        }
    }
    {
        std::unique_lock lock(g_prepared_skeleton_mutex);
        g_prepared_skeleton = PreparedSkeletonFrame{};
    }
    {
        std::unique_lock lock(g_prepared_esp_mutex);
        g_prepared_esp = PreparedEspFrame{};
    }
}

void esp::DrawWaypointAndBlips(const Matrix& view, uintptr_t localplayer) {
    if (!localplayer) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (!dl) return;
    const auto snapshot = g_waypointBlips.Acquire();
    if (!snapshot || snapshot->sampledAt.time_since_epoch().count() == 0 ||
        std::chrono::steady_clock::now() - snapshot->sampledAt > std::chrono::milliseconds(500))
        return;
    const Vec3 localPos = snapshot->localPos;


    if (config.waypoint_line && snapshot->waypointValid) {
        const float wx = snapshot->waypointX;
        const float wy = snapshot->waypointY;


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


    if (config.blip_esp) {
        for (int i = 0; i < snapshot->blipCount; ++i) {
            const Vec3& pos = snapshot->blips[static_cast<size_t>(i)];
            Vec2 sp;
            if (!pos.world_to_screen(view, sp)) continue;
            dl->AddCircleFilled(ImVec2(sp.x, sp.y), 3.f, IM_COL32(100, 180, 255, 200), 8);
        }
    }
}
