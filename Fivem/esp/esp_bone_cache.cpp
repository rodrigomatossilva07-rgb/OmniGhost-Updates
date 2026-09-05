#include "esp.h"
#include "../game/game.h"
#include <Memory/Memory.h>

#include <chrono>

esp::BoneCache esp::bone_cache;

// Bone Cache Implementation (keeping your existing logic)
Matrix esp::BoneCache::get_bone_matrix(uintptr_t ped, bool force_refresh) {
    auto now = std::chrono::steady_clock::now();

    auto it = cached_bone_data.find(ped);
    if (!force_refresh && it != cached_bone_data.end() && it->second.is_valid) {
        auto age = now - it->second.last_update;
        if (age < CACHE_VALIDITY_MS) {
            esp_stats.cache_hits++;
            return it->second.bone_matrix;
        }
    }

    esp_stats.cache_misses++;
    esp_stats.memory_reads++;

    Matrix bone_matrix{};
    // Preferred chain (cheatoffsets): ped + frag(0xFF8/0x12B8) → +skel_o1 → matrix
    // Fall back to CEntity visual matrix at +0x60 if frag path fails.
    bool got = false;
    const uintptr_t frag_offs[] = {
        FiveM::offset::boneMatrix ? FiveM::offset::boneMatrix : BONE_FRAG_3258,
        BONE_FRAG_3258, BONE_FRAG_3751
    };
    for (uintptr_t frag_off : frag_offs) {
        if (!frag_off) continue;
        uintptr_t frag = mem.Read<uintptr_t>(ped + frag_off);
        if (frag < 0x10000ull || frag > 0x7FFFFFFFFFFFULL) continue;
        uintptr_t skel = mem.Read<uintptr_t>(frag + BONE_SKEL_O1);
        if (skel < 0x10000ull || skel > 0x7FFFFFFFFFFFULL) {
            // some builds store matrix block directly at frag
            skel = frag;
        }
        if (mem.Read(skel, &bone_matrix, sizeof(Matrix))) {
            got = true;
            break;
        }
    }
    if (!got) {
        auto handle = mem.CreateScatterHandle();
        mem.AddScatterReadRequest(handle, ped + BONE_MATRIX_OFFSET, &bone_matrix, sizeof(Matrix));
        mem.ExecuteReadScatter(handle);
        mem.CloseScatterHandle(handle);
    }

    auto& cached_data = cached_bone_data[ped];
    cached_data.bone_matrix = bone_matrix;
    cached_data.last_update = now;
    cached_data.is_valid = true;

    return bone_matrix;
}

Vec3 esp::BoneCache::get_bone_position(uintptr_t ped, int bone_position, bool force_refresh) {
    auto now = std::chrono::steady_clock::now();

    if (bone_position == 0) {
        auto it = cached_bone_data.find(ped);
        if (!force_refresh && it != cached_bone_data.end() && it->second.is_valid) {
            auto age = now - it->second.last_update;
            if (age < CACHE_VALIDITY_MS) {
                esp_stats.cache_hits++;
                return it->second.head_position;
            }
        }
    }

    esp_stats.cache_misses++;
    esp_stats.memory_reads++;

    Matrix bone_matrix = get_bone_matrix(ped, force_refresh);

    Vector3 bone_offset;
    auto handle = mem.CreateScatterHandle();
    mem.AddScatterReadRequest(handle, ped + (BONE_ARRAY_BASE + BONE_SIZE * bone_position),
        reinterpret_cast<void*>(&bone_offset), sizeof(Vector3));
    mem.ExecuteReadScatter(handle);
    mem.CloseScatterHandle(handle);

    DirectX::SimpleMath::Vector3 boneVec(bone_offset.x, bone_offset.y, bone_offset.z);
    DirectX::SimpleMath::Vector3 transformedBoneVec = DirectX::XMVector3Transform(boneVec, bone_matrix);
    Vec3 result(transformedBoneVec.x, transformedBoneVec.y, transformedBoneVec.z);

    if (bone_position == 0) {
        auto& cached_data = cached_bone_data[ped];
        cached_data.head_position = result;
        cached_data.last_update = now;
        cached_data.is_valid = true;
    }

    return result;
}

void esp::BoneCache::update_bone_data(uintptr_t ped, const Matrix& bone_matrix, const Vec3& head_pos) {
    auto now = std::chrono::steady_clock::now();
    auto& cached_data = cached_bone_data[ped];

    cached_data.bone_matrix = bone_matrix;
    cached_data.head_position = head_pos;
    cached_data.last_update = now;
    cached_data.is_valid = true;
}

void esp::BoneCache::cleanup_old_entries() {
    auto now = std::chrono::steady_clock::now();

    if (now - last_cleanup < CLEANUP_INTERVAL) {
        return;
    }

    for (auto it = cached_bone_data.begin(); it != cached_bone_data.end(); ) {
        auto age = now - it->second.last_update;
        if (age > std::chrono::seconds(10)) {
            it = cached_bone_data.erase(it);
        }
        else {
            ++it;
        }
    }

    last_cleanup = now;
}

bool esp::BoneCache::is_data_valid(uintptr_t ped) const {
    auto it = cached_bone_data.find(ped);
    if (it == cached_bone_data.end()) return false;

    auto now = std::chrono::steady_clock::now();
    auto age = now - it->second.last_update;
    return it->second.is_valid && age < CACHE_VALIDITY_MS;
}


