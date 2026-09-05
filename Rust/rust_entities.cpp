// rust_entities.cpp - camera / local player / player list / world entity caches.
// All reads go through Memory (DMA, read-only). Heavy lists use scatter reads.
#include "rust_internal.h"
#include "rust_decrypt.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

namespace Rust {
namespace detail {

namespace {

constexpr uint32_t kMaxPlayers = 256;
constexpr uint32_t kMaxWorldEntities = 4096;
constexpr uint32_t kMaxBoneTransforms = 128;
constexpr int kMaxHierarchyIndex = 1024;
constexpr int kBonePlayersPerTick = 8;
constexpr float kBonePlayerMaxDistance = 160.f;

struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };
struct Quat { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };
struct TransformData { Vec3 pos; Quat rot; Vec3 scale; }; // 0x30 bytes
static_assert(sizeof(TransformData) == 0x28, "TransformData packs to 40 bytes; stride handled explicitly");

inline bool Finite3(const float* v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}

inline bool PlausibleWorldPos(const float* v) {
    if (!Finite3(v)) return false;
    return std::fabs(v[0]) < 12000.f && std::fabs(v[2]) < 12000.f && v[1] > -1500.f && v[1] < 6000.f;
}

inline float Dist3(const float* a, const float* b) {
    const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline float DistH(const float* a, const float* b) {
    const float dx = a[0] - b[0], dz = a[2] - b[2];
    return std::sqrt(dx * dx + dz * dz);
}

inline Vec3 Rotate(const Quat& q, const Vec3& v) {
    // v' = v + 2*cross(q.xyz, cross(q.xyz, v) + q.w*v)
    const float cx = q.y * v.z - q.z * v.y + q.w * v.x;
    const float cy = q.z * v.x - q.x * v.z + q.w * v.y;
    const float cz = q.x * v.y - q.y * v.x + q.w * v.z;
    return Vec3{
        v.x + 2.f * (q.y * cz - q.z * cy),
        v.y + 2.f * (q.z * cx - q.x * cz),
        v.z + 2.f * (q.x * cy - q.y * cx)
    };
}

// One Unity hierarchy block (shared by every Transform of the same GameObject).
struct HierarchyBlock {
    uintptr_t hierarchy = 0;
    int maxIndex = -1;
    std::vector<uint8_t> transforms; // (maxIndex+1) * kTransformDataStride bytes
    std::vector<int32_t> parents;    // (maxIndex+1) entries

    bool Load(uintptr_t h, int maxIdx) {
        hierarchy = h;
        maxIndex = -1;
        if (!IsUserPtr(h) || maxIdx < 0 || maxIdx > kMaxHierarchyIndex) return false;
        uintptr_t transformsPtr = 0, parentsPtr = 0;
        if (!ReadU64(h + kHierarchyLocalTransforms, transformsPtr) || !IsUserPtr(transformsPtr)) return false;
        if (!ReadU64(h + kHierarchyParentIndices, parentsPtr) || !IsUserPtr(parentsPtr)) return false;
        const size_t count = static_cast<size_t>(maxIdx) + 1;
        transforms.resize(count * kTransformDataStride);
        parents.resize(count);
        if (!mem.Read(transformsPtr, transforms.data(), transforms.size())) return false;
        if (!mem.Read(parentsPtr, parents.data(), parents.size() * sizeof(int32_t))) return false;
        maxIndex = maxIdx;
        return true;
    }

    bool Data(int index, TransformData& out) const {
        if (index < 0 || index > maxIndex) return false;
        const uint8_t* base = transforms.data() + static_cast<size_t>(index) * kTransformDataStride;
        std::memcpy(&out.pos, base + 0x00, sizeof(Vec3));
        std::memcpy(&out.rot, base + 0x10, sizeof(Quat));
        std::memcpy(&out.scale, base + 0x20, sizeof(Vec3));
        return true;
    }

    bool WorldPosition(int index, float out[3]) const {
        TransformData self{};
        if (!Data(index, self)) return false;
        Vec3 result = self.pos;
        int parent = parents[static_cast<size_t>(index)];
        for (int depth = 0; parent >= 0 && depth < 64; ++depth) {
            TransformData p{};
            if (!Data(parent, p)) return false;
            const Vec3 scaled{ result.x * p.scale.x, result.y * p.scale.y, result.z * p.scale.z };
            const Vec3 rotated = Rotate(p.rot, scaled);
            result = Vec3{ rotated.x + p.pos.x, rotated.y + p.pos.y, rotated.z + p.pos.z };
            parent = parents[static_cast<size_t>(parent)];
        }
        out[0] = result.x; out[1] = result.y; out[2] = result.z;
        return Finite3(out);
    }
};

struct BoneAccess { int index = -1; };

struct PlayerBoneCache {
    uintptr_t hierarchy = 0;
    int maxIndex = -1;
    std::vector<BoneAccess> bones;   // one per Transform in boneTransforms (index -1 = unresolved)
};

std::unordered_map<uintptr_t, PlayerBoneCache> g_boneCache; // keyed by BasePlayer address
uint64_t g_boneCacheGeneration = 0;

bool ResolveNativeTransformAccess(uintptr_t nativeTransform, uintptr_t& hierarchy, int& index) {
    if (!IsUserPtr(nativeTransform)) return false;
    if (!ReadU64(nativeTransform + kTransformHierarchy, hierarchy) || !IsUserPtr(hierarchy)) return false;
    if (!R(nativeTransform + kTransformIndex, index) || index < 0 || index > kMaxHierarchyIndex) return false;
    return true;
}

// Managed Transform -> native TransformAccess (hierarchy, index).
bool ResolveManagedTransformAccess(uintptr_t managedTransform, uintptr_t& hierarchy, int& index) {
    uintptr_t native = 0;
    if (!IsUserPtr(managedTransform) || !ReadU64(managedTransform + kCachedNativePtr, native)) return false;
    return ResolveNativeTransformAccess(native, hierarchy, index);
}

// Managed MonoBehaviour/Component -> native Transform of its GameObject.
bool ResolveComponentTransform(uintptr_t managedComponent, uintptr_t& nativeTransform) {
    uintptr_t native = 0, gameObject = 0, components = 0;
    if (!IsUserPtr(managedComponent) || !ReadU64(managedComponent + kCachedNativePtr, native) || !IsUserPtr(native)) return false;
    if (!ReadU64(native + kNativeComponentGameObject, gameObject) || !IsUserPtr(gameObject)) return false;
    if (!ReadU64(gameObject + kNativeGameObjectComponents, components) || !IsUserPtr(components)) return false;
    if (!ReadU64(components + kNativeComponentEntryPtr, nativeTransform) || !IsUserPtr(nativeTransform)) return false;
    return true;
}

void ReadUtf16String(uintptr_t stringObj, char* out, size_t outn) {
    if (!out || !outn) return;
    out[0] = '\0';
    if (!IsUserPtr(stringObj)) return;
    int32_t length = 0;
    if (!R(stringObj + kUnityStringLength, length) || length <= 0 || length > 256) return;
    const size_t chars = (std::min)(static_cast<size_t>(length), outn - 1);
    std::vector<uint16_t> raw(chars);
    if (!mem.Read(stringObj + kUnityStringChars, raw.data(), chars * sizeof(uint16_t))) return;
    size_t o = 0;
    for (size_t i = 0; i < chars && o + 1 < outn; ++i) {
        const uint16_t c = raw[i];
        if (c == 0) break;
        out[o++] = (c >= 0x20 && c < 0x7F) ? static_cast<char>(c) : '?';
    }
    out[o] = '\0';
}

bool ContainsI(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*needle) return false;
    const size_t n = std::strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < n && p[i] && std::tolower(static_cast<unsigned char>(p[i])) == std::tolower(static_cast<unsigned char>(needle[i]))) ++i;
        if (i == n) return true;
    }
    return false;
}

// Maps an il2cpp class name to a world category and checks the per-item filters.
bool WorldItemEnabled(const char* className, WorldKind& kind) {
    const Config& c = config;
    kind = WorldKind::Unknown;
    if (!className || !*className) return false;
    if (ContainsI(className, "BasePlayer") || ContainsI(className, "NPCPlayer") || ContainsI(className, "Scientist"))
        return false; // players handled separately
    if (ContainsI(className, "OreResource") || ContainsI(className, "ResourceEntity")) {
        kind = WorldKind::Ore;
        return c.ore_esp && (c.w_sulfur || c.w_metal_ore || c.w_stone_ore);
    }
    if (ContainsI(className, "HackableLockedCrate")) { kind = WorldKind::Crate; return c.crate_esp && c.w_hackable_crate; }
    if (ContainsI(className, "LockedByEntCrate")) { kind = WorldKind::Crate; return c.crate_esp && c.w_locked_crate; }
    if (ContainsI(className, "LootContainer") || ContainsI(className, "Crate")) {
        kind = WorldKind::Crate;
        return c.crate_esp && (c.w_basiccrate || c.w_elite_crate || c.w_military_crate || c.w_barrel || c.w_oil_barrel);
    }
    if (ContainsI(className, "Stash")) { kind = WorldKind::Stash; return c.stash_esp && c.w_buriedstash; }
    if (ContainsI(className, "BuildingPrivlidge")) { kind = WorldKind::TC; return c.tc_esp && c.w_toolcupboard; }
    if (ContainsI(className, "AutoTurret")) { kind = WorldKind::Turret; return c.turret_esp && c.w_autoturret; }
    if (ContainsI(className, "FlameTurret")) { kind = WorldKind::Turret; return c.turret_esp && c.w_flameturret; }
    if (ContainsI(className, "GunTrap")) { kind = WorldKind::Turret; return c.turret_esp && c.w_shotguntrap; }
    if (ContainsI(className, "Bradley")) { kind = WorldKind::Vehicle; return c.vehicle_esp && c.w_bradley; }
    if (ContainsI(className, "PatrolHelicopter")) { kind = WorldKind::AirDrop; return c.airdrop_esp && c.w_attackheli; }
    if (ContainsI(className, "Minicopter")) { kind = WorldKind::Vehicle; return c.vehicle_esp && c.w_minicopter; }
    if (ContainsI(className, "ScrapTransport")) { kind = WorldKind::Vehicle; return c.vehicle_esp && c.w_scrapheli; }
    if (ContainsI(className, "RHIB")) { kind = WorldKind::Vehicle; return c.vehicle_esp && c.w_rhib; }
    if (ContainsI(className, "MotorRowboat")) { kind = WorldKind::Vehicle; return c.vehicle_esp && c.w_rowboat; }
    if (ContainsI(className, "Bike") || ContainsI(className, "Motorbike")) { kind = WorldKind::Vehicle; return c.vehicle_esp && c.w_bike; }
    if (ContainsI(className, "RidableHorse")) { kind = WorldKind::Vehicle; return c.vehicle_esp && c.w_horse; }
    if (ContainsI(className, "BaseVehicle") || ContainsI(className, "ModularCar") || ContainsI(className, "Submarine") || ContainsI(className, "Snowmobile")) {
        kind = WorldKind::Vehicle; return c.vehicle_esp;
    }
    if (ContainsI(className, "Bear")) { kind = WorldKind::Animal; return c.animal_esp && c.w_bear; }
    if (ContainsI(className, "Boar")) { kind = WorldKind::Animal; return c.animal_esp && c.w_boar; }
    if (ContainsI(className, "Wolf")) { kind = WorldKind::Animal; return c.animal_esp && c.w_wolf; }
    if (ContainsI(className, "Stag")) { kind = WorldKind::Animal; return c.animal_esp && c.w_stag; }
    if (ContainsI(className, "Chicken")) { kind = WorldKind::Animal; return c.animal_esp && c.w_chicken; }
    if (ContainsI(className, "BaseAnimalNPC") || ContainsI(className, "Animal")) { kind = WorldKind::Animal; return c.animal_esp; }
    if (ContainsI(className, "SupplyDrop")) { kind = WorldKind::AirDrop; return c.airdrop_esp && c.w_airdrop; }
    if (ContainsI(className, "CollectibleEntity")) {
        kind = WorldKind::Collectable;
        return c.collectables_esp && (c.w_hemp || c.w_blueberry || c.w_wood_pile || c.w_sulfur || c.w_metal_ore || c.w_stone_ore || c.w_diesel);
    }
    if (ContainsI(className, "DroppedItem") || ContainsI(className, "WorldItem")) { kind = WorldKind::ItemDrop; return c.item_drops_esp && c.w_blueprint; }
    if (ContainsI(className, "PlayerCorpse") || ContainsI(className, "Corpse")) { kind = WorldKind::Corpse; return c.corpse_esp && c.w_bodybag; }
    if (ContainsI(className, "DroppedItemContainer")) { kind = WorldKind::Corpse; return c.corpse_esp && c.w_bodybag; }
    if (ContainsI(className, "Coffin")) { kind = WorldKind::Stash; return c.stash_esp && c.w_coffin; }
    kind = WorldKind::Generic;
    return c.world_esp;
}

// Convert a cloud of bone world positions into the 10-slot layout consumed by
// Rust_ESP (0 head 1 neck 2 chest 3 pelvis 4/5 shoulders 6/7 hips 8/9 feet).
bool BuildSkeletonLayout(const std::vector<std::array<float, 3>>& bones, const float* modelPos, Player& p) {
    if (bones.size() < 8 || !PlausibleWorldPos(modelPos)) return false;
    std::vector<const float*> valid;
    valid.reserve(bones.size());
    for (const auto& b : bones) {
        if (!Finite3(b.data())) continue;
        if (Dist3(b.data(), modelPos) > 3.2f) continue;
        valid.push_back(b.data());
    }
    if (valid.size() < 8) return false;

    auto highest = *std::max_element(valid.begin(), valid.end(), [](const float* a, const float* b) { return a[1] < b[1]; });
    const float baseY = modelPos[1];
    const float height = highest[1] - baseY;
    if (height < 0.6f || height > 2.6f) return false; // crouched/sleeping poses are drawn via fallback

    auto closestTo = [&](float targetY, float maxH, const float* ref) -> const float* {
        const float* best = nullptr;
        float bestScore = 1e9f;
        for (const float* b : valid) {
            const float dy = std::fabs(b[1] - targetY);
            const float dh = ref ? DistH(b, ref) : 0.f;
            if (dh > maxH) continue;
            const float score = dy * 2.f + dh;
            if (score < bestScore) { bestScore = score; best = b; }
        }
        return best;
    };

    const float* neck = closestTo(highest[1] - height * 0.12f, 0.25f, highest);
    const float* chest = closestTo(baseY + height * 0.72f, 0.30f, modelPos);
    const float* pelvis = closestTo(baseY + height * 0.52f, 0.30f, modelPos);
    if (!neck || !chest || !pelvis) return false;

    // Laterally furthest bones at shoulder / hip height on opposite sides.
    auto pair = [&](float targetY, float tol, const float* center, const float*& left, const float*& right) {
        left = right = nullptr;
        float bestL = 0.f, bestR = 0.f;
        // Lateral axis: perpendicular to (chest - pelvis) is unknown; use signed offset along X then Z.
        for (const float* b : valid) {
            if (std::fabs(b[1] - targetY) > tol) continue;
            const float dx = b[0] - center[0], dz = b[2] - center[2];
            const float lateral = std::fabs(dx) >= std::fabs(dz) ? dx : dz;
            const float dist = DistH(b, center);
            if (dist < 0.08f) continue;
            if (lateral < 0.f && dist > bestL) { bestL = dist; left = b; }
            if (lateral > 0.f && dist > bestR) { bestR = dist; right = b; }
        }
        return left && right;
    };
    const float *lsh = nullptr, *rsh = nullptr, *lhip = nullptr, *rhip = nullptr;
    pair(chest[1] + height * 0.04f, height * 0.10f, chest, lsh, rsh);
    pair(pelvis[1], height * 0.08f, pelvis, lhip, rhip);

    // Feet: two lowest bones on opposite sides.
    std::vector<const float*> lows(valid);
    std::sort(lows.begin(), lows.end(), [](const float* a, const float* b) { return a[1] < b[1]; });
    const float* lfoot = lows[0];
    const float* rfoot = nullptr;
    for (size_t i = 1; i < lows.size(); ++i) {
        if (DistH(lows[i], lfoot) > 0.12f) { rfoot = lows[i]; break; }
    }
    if (!rfoot) rfoot = lows[1];
    if (lhip && lfoot && DistH(lhip, rfoot) < DistH(lhip, lfoot)) std::swap(lfoot, rfoot);

    auto put = [&](int slot, const float* b) {
        if (b) { p.bones[slot][0] = b[0]; p.bones[slot][1] = b[1]; p.bones[slot][2] = b[2]; }
        else { p.bones[slot][0] = p.bones[slot][1] = p.bones[slot][2] = 0.f; }
    };
    put(0, highest); put(1, neck); put(2, chest); put(3, pelvis);
    put(4, lsh); put(5, rsh); put(6, lhip); put(7, rhip); put(8, lfoot); put(9, rfoot);
    for (int i = 10; i < 16; ++i) put(i, nullptr);
    p.bone_count = 10;
    p.bones_ok = true;
    return true;
}

// Resolves (once per cache rebuild) the hierarchy/index pairs for a player's
// bone transform array; cheap per-tick refresh then needs only two block reads.
bool DiscoverBoneTransforms(uintptr_t playerAddress, uintptr_t boneArray, PlayerBoneCache& cache) {
    cache = PlayerBoneCache{};
    if (!IsUserPtr(boneArray)) return false;
    int32_t count = 0;
    if (!R(boneArray + kUnityArrayCount, count) || count <= 0) return false;
    const uint32_t n = (std::min)(static_cast<uint32_t>(count), kMaxBoneTransforms);

    std::vector<uintptr_t> managed(n, 0), natives(n, 0);
    if (!mem.Read(boneArray + kUnityArrayItems, managed.data(), n * sizeof(uintptr_t))) return false;

    auto scatter = mem.CreateScopedScatterHandle();
    if (!scatter) return false;
    for (uint32_t i = 0; i < n; ++i)
        if (IsUserPtr(managed[i]))
            mem.AddScatterReadRequest(scatter, managed[i] + kCachedNativePtr, &natives[i], sizeof(uintptr_t));
    mem.ExecuteReadScatter(scatter);

    std::vector<uintptr_t> hierarchies(n, 0);
    std::vector<int32_t> indices(n, -1);
    for (uint32_t i = 0; i < n; ++i) {
        if (!IsUserPtr(natives[i])) continue;
        mem.AddScatterReadRequest(scatter, natives[i] + kTransformHierarchy, &hierarchies[i], sizeof(uintptr_t));
        mem.AddScatterReadRequest(scatter, natives[i] + kTransformIndex, &indices[i], sizeof(int32_t));
    }
    mem.ExecuteReadScatter(scatter);

    // Pick the dominant hierarchy (bones of one skeleton share it).
    std::unordered_map<uintptr_t, int> votes;
    for (uint32_t i = 0; i < n; ++i)
        if (IsUserPtr(hierarchies[i])) ++votes[hierarchies[i]];
    uintptr_t best = 0; int bestVotes = 0;
    for (const auto& [h, v] : votes) if (v > bestVotes) { bestVotes = v; best = h; }
    if (!best || bestVotes < 8) return false;

    cache.hierarchy = best;
    cache.bones.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        if (hierarchies[i] != best || indices[i] < 0 || indices[i] > kMaxHierarchyIndex) continue;
        cache.bones[i].index = indices[i];
        cache.maxIndex = (std::max)(cache.maxIndex, indices[i]);
    }
    (void)playerAddress;
    return cache.maxIndex >= 0;
}

bool FillPlayerBones(Player& p, const PlayerBoneCache& cache) {
    if (!IsUserPtr(cache.hierarchy) || cache.maxIndex < 0) return false;
    HierarchyBlock block;
    if (!block.Load(cache.hierarchy, cache.maxIndex)) return false;
    std::vector<std::array<float, 3>> world;
    world.reserve(cache.bones.size());
    for (const BoneAccess& b : cache.bones) {
        if (b.index < 0) continue;
        std::array<float, 3> w{};
        if (block.WorldPosition(b.index, w.data()))
            world.push_back(w);
    }
    if (!BuildSkeletonLayout(world, p.pos, p)) {
        p.bones_ok = false;
        p.bone_count = 0;
        return false;
    }
    return true;
}

void DeriveAimPoints(Player& p) {
    if (p.bones_ok && p.bone_count >= 3) {
        std::memcpy(p.head, p.bones[0], sizeof(p.head));
        std::memcpy(p.chest, p.bones[2], sizeof(p.chest));
        return;
    }
    const float standing = p.sleeping || p.wounded ? 0.35f : 1.55f;
    p.head[0] = p.pos[0]; p.head[1] = p.pos[1] + standing; p.head[2] = p.pos[2];
    p.chest[0] = p.pos[0]; p.chest[1] = p.pos[1] + (p.sleeping || p.wounded ? 0.25f : 1.15f); p.chest[2] = p.pos[2];
}

// Reads the list buffer items (Unity array layout first, raw pointer array fallback).
uint32_t ReadEntityPointers(uintptr_t buffer, uint32_t count, uint32_t cap, std::vector<uintptr_t>& out) {
    out.clear();
    if (!IsUserPtr(buffer) || !count) return 0;
    const uint32_t n = (std::min)(count, cap);
    out.assign(n, 0);
    if (!mem.Read(buffer + kUnityArrayItems, out.data(), n * sizeof(uintptr_t)) || !IsUserPtr(out[0])) {
        if (!mem.Read(buffer, out.data(), n * sizeof(uintptr_t)) || !IsUserPtr(out[0])) {
            out.clear();
            return 0;
        }
    }
    return n;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

uint64_t NowMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void ReadPlayerName(uintptr_t displayNamePtr, char* out, size_t outn) {
    ReadUtf16String(displayNamePtr, out, outn);
}

void ReadClassName(uintptr_t object, char* out, size_t outn) {
    if (!out || !outn) return;
    out[0] = '\0';
    uintptr_t klass = 0, namePtr = 0;
    if (!IsUserPtr(object) || !ReadU64(object, klass) || !IsUserPtr(klass)) return;
    if (!ReadU64(klass + kIl2CppClassNamePtr, namePtr) || !IsUserPtr(namePtr)) return;
    char buffer[64]{};
    if (!mem.Read(namePtr, buffer, sizeof(buffer) - 1)) return;
    buffer[sizeof(buffer) - 1] = '\0';
    for (char* c = buffer; *c; ++c) {
        if (static_cast<unsigned char>(*c) < 0x20 || static_cast<unsigned char>(*c) > 0x7E) { *c = '\0'; break; }
    }
    std::snprintf(out, outn, "%s", buffer);
}

bool ReadTransformPos(uintptr_t transform, float out[3]) {
    uintptr_t hierarchy = 0;
    int index = -1;
    if (!ResolveManagedTransformAccess(transform, hierarchy, index)) return false;
    HierarchyBlock block;
    if (!block.Load(hierarchy, index)) return false;
    return block.WorldPosition(index, out) && PlausibleWorldPos(out);
}

uintptr_t ResolvePlayerListBuffer(uint32_t& out_size) {
    out_size = 0;
    uint32_t n = 0;
    uintptr_t buf = Decrypt::ResolveVisiblePlayersBuffer(n);
    if (buf && n) { out_size = n; return buf; }
    buf = Decrypt::ResolveNetworkableBuffer(n);
    if (buf && n) { out_size = n; return buf; }
    return 0;
}

bool ResolveViewMatrix(float out[16]) {
    if (!runtime.game_assembly || !offsets.MainCamera_TypeInfo) return false;
    uintptr_t klass = 0, statics = 0, camera = 0, target = 0;
    if (!ReadU64(runtime.game_assembly + offsets.MainCamera_TypeInfo, klass) || !IsUserPtr(klass)) return false;
    if (!ReadU64(klass + offsets.staticFields, statics) || !IsUserPtr(statics)) {
        if (!offsets.staticFieldsAlt || !ReadU64(klass + offsets.staticFieldsAlt, statics) || !IsUserPtr(statics))
            return false;
    }
    if (!ReadU64(statics + offsets.mainCamera, camera) || !IsUserPtr(camera)) return false;
    target = camera;
    if (offsets.cameraGameObject) {
        uintptr_t go = 0;
        if (ReadU64(camera + offsets.cameraGameObject, go) && IsUserPtr(go))
            target = go;
    }
    float tmp[16]{};
    if (!mem.Read(target + offsets.viewMatrix, tmp, sizeof(tmp))) return false;
    int nonZero = 0;
    for (float v : tmp) {
        if (!std::isfinite(v)) return false;
        if (v != 0.f) ++nonZero;
    }
    if (nonZero < 4) return false;
    std::memcpy(out, tmp, sizeof(tmp));
    return true;
}

uintptr_t ResolveLocalPlayer() {
    if (!runtime.game_assembly) return 0;

    // 1) LocalPlayer TypeInfo static (when the dump provides it)
    if (offsets.LocalPlayer_TypeInfo) {
        uintptr_t typeInfo = 0;
        if (ReadU64(runtime.game_assembly + offsets.LocalPlayer_TypeInfo, typeInfo) && IsUserPtr(typeInfo)) {
            uintptr_t statics = 0;
            if (ReadU64(typeInfo + offsets.staticFields, statics) && IsUserPtr(statics)) {
                for (uintptr_t off : { 0x0ull, 0x8ull, 0x10ull, 0x18ull }) {
                    uintptr_t local = 0;
                    if (ReadU64(statics + off, local) && IsUserPtr(local)) {
                        uintptr_t model = 0;
                        if (ReadU64(local + offsets.playerModel, model) && IsUserPtr(model))
                            return local;
                    }
                }
            }
        }
    }

    // 2) MainCamera -> owning entity (works even when BasePlayer TypeInfo is stale)
    if (offsets.MainCamera_TypeInfo) {
        uintptr_t klass = 0;
        if (ReadU64(runtime.game_assembly + offsets.MainCamera_TypeInfo, klass) && IsUserPtr(klass)) {
            for (uintptr_t sfo : { offsets.staticFields, offsets.staticFieldsAlt, 0xB8ull, 0x90ull }) {
                if (!sfo) continue;
                uintptr_t statics = 0;
                if (!ReadU64(klass + sfo, statics) || !IsUserPtr(statics)) continue;
                for (uintptr_t camOff : { offsets.mainCamera, 0x0ull, 0x38ull, 0x10ull, 0x20ull }) {
                    uintptr_t cam = 0;
                    if (!ReadU64(statics + camOff, cam) || !IsUserPtr(cam)) continue;
                    for (uintptr_t entOff : { 0x10ull, 0x18ull, 0x20ull, 0x28ull, 0x30ull }) {
                        uintptr_t ent = 0;
                        if (!ReadU64(cam + entOff, ent) || !IsUserPtr(ent)) continue;
                        uintptr_t model = 0;
                        if (ReadU64(ent + offsets.playerModel, model) && IsUserPtr(model)) {
                            offsets.mainCamera = camOff;
                            return ent;
                        }
                    }
                }
            }
        }
    }

    // 3) Visible players list: the entry whose position matches the camera is
    //    unknown here, so take the first valid BasePlayer as a last resort.
    uint32_t n = 0;
    const uintptr_t buf = Decrypt::ResolveVisiblePlayersBuffer(n);
    if (buf && n) {
        std::vector<uintptr_t> items;
        if (ReadEntityPointers(buf, n, 4, items)) {
            for (uintptr_t candidate : items) {
                uintptr_t model = 0;
                if (IsUserPtr(candidate) && ReadU64(candidate + offsets.playerModel, model) && IsUserPtr(model))
                    return candidate;
            }
        }
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Player cache (two scatter stages) + per-tick position refresh
// ─────────────────────────────────────────────────────────────────────────────

void CachePlayers() {
    uint32_t count = 0;
    const uintptr_t buffer = ResolvePlayerListBuffer(count);
    if (!buffer || !count) {
        runtime.players.clear();
        runtime.player_count = 0;
        runtime.list_ok = false;
        return;
    }
    runtime.list_ok = true;

    std::vector<uintptr_t> entities;
    const uint32_t n = ReadEntityPointers(buffer, count, kMaxPlayers, entities);
    if (!n) {
        runtime.players.clear();
        runtime.player_count = 0;
        return;
    }

    struct Stage1 {
        uintptr_t model = 0;
        uintptr_t namePtr = 0;
        uintptr_t klass = 0;
        uint64_t team = 0;
        uint32_t flags = 0;
        int32_t lifestate = 0;
        float health = 0.f;
        float maxHealth = 0.f;
    };
    struct Stage2 {
        float pos[3]{};
        float vel[3]{};
        uintptr_t boneArray = 0;
        uintptr_t classNamePtr = 0;
        int32_t nameLength = 0;
        uint16_t nameChars[32]{};
    };
    std::vector<Stage1> s1(n);
    std::vector<Stage2> s2(n);

    auto scatter = mem.CreateScopedScatterHandle();
    if (!scatter) return;

    for (uint32_t i = 0; i < n; ++i) {
        const uintptr_t e = entities[i];
        if (!IsUserPtr(e)) continue;
        mem.AddScatterReadRequest(scatter, e, &s1[i].klass, sizeof(uintptr_t));
        mem.AddScatterReadRequest(scatter, e + offsets.playerModel, &s1[i].model, sizeof(uintptr_t));
        mem.AddScatterReadRequest(scatter, e + offsets.displayName, &s1[i].namePtr, sizeof(uintptr_t));
        mem.AddScatterReadRequest(scatter, e + offsets.currentTeam, &s1[i].team, sizeof(uint64_t));
        mem.AddScatterReadRequest(scatter, e + offsets.playerFlags, &s1[i].flags, sizeof(uint32_t));
        mem.AddScatterReadRequest(scatter, e + offsets.lifestate, &s1[i].lifestate, sizeof(int32_t));
        mem.AddScatterReadRequest(scatter, e + offsets._health, &s1[i].health, sizeof(float));
        mem.AddScatterReadRequest(scatter, e + offsets._maxHealth, &s1[i].maxHealth, sizeof(float));
    }
    mem.ExecuteReadScatter(scatter);

    for (uint32_t i = 0; i < n; ++i) {
        if (IsUserPtr(s1[i].model)) {
            mem.AddScatterReadRequest(scatter, s1[i].model + offsets.modelPosition, s2[i].pos, sizeof(s2[i].pos));
            mem.AddScatterReadRequest(scatter, s1[i].model + offsets.newVelocity, s2[i].vel, sizeof(s2[i].vel));
            mem.AddScatterReadRequest(scatter, s1[i].model + offsets.boneTransforms, &s2[i].boneArray, sizeof(uintptr_t));
        }
        if (IsUserPtr(s1[i].namePtr)) {
            mem.AddScatterReadRequest(scatter, s1[i].namePtr + kUnityStringLength, &s2[i].nameLength, sizeof(int32_t));
            mem.AddScatterReadRequest(scatter, s1[i].namePtr + kUnityStringChars, s2[i].nameChars, sizeof(s2[i].nameChars));
        }
        if (IsUserPtr(s1[i].klass))
            mem.AddScatterReadRequest(scatter, s1[i].klass + kIl2CppClassNamePtr, &s2[i].classNamePtr, sizeof(uintptr_t));
    }
    mem.ExecuteReadScatter(scatter);

    std::vector<Player> players;
    players.reserve(n);
    const uint64_t generation = ++g_boneCacheGeneration;
    std::unordered_map<uintptr_t, PlayerBoneCache> keptBones;

    for (uint32_t i = 0; i < n; ++i) {
        const uintptr_t e = entities[i];
        if (!IsUserPtr(e) || !IsUserPtr(s1[i].model)) continue;
        if (!PlausibleWorldPos(s2[i].pos)) continue;

        Player p{};
        p.address = e;
        p.model = s1[i].model;
        p.is_local = (e == runtime.local_player);
        p.flags = s1[i].flags;
        p.team_id = static_cast<int>(s1[i].team & 0x7FFFFFFFull);
        p.health = std::isfinite(s1[i].health) ? s1[i].health : 0.f;
        p.max_health = (std::isfinite(s1[i].maxHealth) && s1[i].maxHealth > 1.f) ? s1[i].maxHealth : 100.f;
        p.destroyed = s1[i].lifestate != 0 || p.health <= 0.f;
        p.sleeping = (p.flags & static_cast<uint32_t>(PlayerFlags::Sleeping)) != 0;
        p.wounded = (p.flags & static_cast<uint32_t>(PlayerFlags::Wounded)) != 0;
        p.aiming = (p.flags & static_cast<uint32_t>(PlayerFlags::Aiming)) != 0;
        p.safezone = (p.flags & static_cast<uint32_t>(PlayerFlags::SafeZone)) != 0;
        std::memcpy(p.pos, s2[i].pos, sizeof(p.pos));
        std::memcpy(p.velocity, Finite3(s2[i].vel) ? s2[i].vel : p.velocity, sizeof(p.velocity));
        p.distance = Dist3(p.pos, runtime.local_pos);
        p.bone_array = s2[i].boneArray;

        // Name (UTF-16 -> ASCII)
        if (s2[i].nameLength > 0 && s2[i].nameLength <= 256) {
            size_t o = 0;
            const int chars = (std::min)(s2[i].nameLength, 31);
            for (int c = 0; c < chars && o + 1 < sizeof(p.name); ++c) {
                const uint16_t ch = s2[i].nameChars[c];
                if (!ch) break;
                p.name[o++] = (ch >= 0x20 && ch < 0x7F) ? static_cast<char>(ch) : '?';
            }
            p.name[o] = '\0';
        }

        // NPC detection via il2cpp class name.
        if (IsUserPtr(s2[i].classNamePtr)) {
            char cls[48]{};
            if (mem.Read(s2[i].classNamePtr, cls, sizeof(cls) - 1)) {
                cls[sizeof(cls) - 1] = '\0';
                p.npc = ContainsI(cls, "NPC") || ContainsI(cls, "Scientist") || ContainsI(cls, "Bandit")
                    || ContainsI(cls, "Scarecrow") || ContainsI(cls, "Tunnel") || ContainsI(cls, "Underwater");
                if (!p.name[0] && cls[0]) std::snprintf(p.name, sizeof(p.name), "%s", cls);
            }
        }

        p.valid = !p.destroyed || p.wounded;
        DeriveAimPoints(p);

        // Keep bone access cache across rebuilds when the model didn't change.
        if (auto found = g_boneCache.find(e); found != g_boneCache.end())
            keptBones.emplace(e, std::move(found->second));
        players.push_back(p);
    }

    g_boneCache.swap(keptBones);
    (void)generation;

    // Bones for the nearest players only (DMA budget).
    std::vector<size_t> order(players.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return players[a].distance < players[b].distance; });
    int boneBudget = kBonePlayersPerTick;
    for (size_t idx : order) {
        Player& p = players[idx];
        if (boneBudget <= 0) break;
        if (p.is_local || p.distance > kBonePlayerMaxDistance || !IsUserPtr(p.bone_array)) continue;
        auto& cache = g_boneCache[p.address];
        if (!IsUserPtr(cache.hierarchy) && !DiscoverBoneTransforms(p.address, p.bone_array, cache)) {
            g_boneCache.erase(p.address);
            continue;
        }
        --boneBudget;
        if (FillPlayerBones(p, cache))
            DeriveAimPoints(p);
    }

    runtime.players.swap(players);
    runtime.player_count = 0;
    for (const auto& p : runtime.players)
        if (p.valid && !p.is_local) ++runtime.player_count;
}

void UpdatePositions() {
    if (runtime.players.empty()) return;
    auto scatter = mem.CreateScopedScatterHandle();
    if (!scatter) return;

    const size_t n = runtime.players.size();
    std::vector<float> pos(n * 3, 0.f), vel(n * 3, 0.f);
    std::vector<float> hp(n, 0.f);
    std::vector<uint32_t> flags(n, 0);
    std::vector<int32_t> life(n, 0);
    for (size_t i = 0; i < n; ++i) {
        const Player& p = runtime.players[i];
        if (!IsUserPtr(p.model) || !IsUserPtr(p.address)) continue;
        mem.AddScatterReadRequest(scatter, p.model + offsets.modelPosition, &pos[i * 3], sizeof(float) * 3);
        mem.AddScatterReadRequest(scatter, p.model + offsets.newVelocity, &vel[i * 3], sizeof(float) * 3);
        mem.AddScatterReadRequest(scatter, p.address + offsets._health, &hp[i], sizeof(float));
        mem.AddScatterReadRequest(scatter, p.address + offsets.playerFlags, &flags[i], sizeof(uint32_t));
        mem.AddScatterReadRequest(scatter, p.address + offsets.lifestate, &life[i], sizeof(int32_t));
    }
    mem.ExecuteReadScatter(scatter);

    int boneBudget = kBonePlayersPerTick;
    for (size_t i = 0; i < n; ++i) {
        Player& p = runtime.players[i];
        if (!IsUserPtr(p.model)) continue;
        if (PlausibleWorldPos(&pos[i * 3])) {
            std::memcpy(p.pos, &pos[i * 3], sizeof(p.pos));
            p.distance = Dist3(p.pos, runtime.local_pos);
        }
        if (Finite3(&vel[i * 3])) std::memcpy(p.velocity, &vel[i * 3], sizeof(p.velocity));
        if (std::isfinite(hp[i])) p.health = hp[i];
        p.flags = flags[i];
        p.sleeping = (p.flags & static_cast<uint32_t>(PlayerFlags::Sleeping)) != 0;
        p.wounded = (p.flags & static_cast<uint32_t>(PlayerFlags::Wounded)) != 0;
        p.aiming = (p.flags & static_cast<uint32_t>(PlayerFlags::Aiming)) != 0;
        p.destroyed = life[i] != 0 || p.health <= 0.f;
        p.valid = !p.destroyed || p.wounded;

        bool refreshed = false;
        if (boneBudget > 0 && !p.is_local && p.distance <= kBonePlayerMaxDistance) {
            if (auto found = g_boneCache.find(p.address); found != g_boneCache.end() && IsUserPtr(found->second.hierarchy)) {
                --boneBudget;
                refreshed = FillPlayerBones(p, found->second);
            }
        }
        if (!refreshed && !p.bones_ok) {
            // no bones this tick: keep aim points anchored to the fresh model position
        }
        DeriveAimPoints(p);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// World entities (BaseNetworkable client list)
// ─────────────────────────────────────────────────────────────────────────────

void CacheWorldEntities() {
    const Config& c = config;
    const bool anyWorld = c.world_esp || c.ore_esp || c.crate_esp || c.stash_esp || c.tc_esp || c.turret_esp
        || c.vehicle_esp || c.animal_esp || c.airdrop_esp || c.item_drops_esp || c.collectables_esp || c.corpse_esp;
    if (!anyWorld) {
        runtime.world_entities.clear();
        runtime.world_count = 0;
        return;
    }

    uint32_t count = 0;
    const uintptr_t buffer = Decrypt::ResolveNetworkableBuffer(count);
    if (!buffer || !count) {
        runtime.world_entities.clear();
        runtime.world_count = 0;
        return;
    }

    std::vector<uintptr_t> entities;
    const uint32_t n = ReadEntityPointers(buffer, count, kMaxWorldEntities, entities);
    if (!n) return;

    // Stage 1: class pointers. Stage 2: class name pointers. Stage 3: names.
    std::vector<uintptr_t> klass(n, 0), namePtr(n, 0);
    auto scatter = mem.CreateScopedScatterHandle();
    if (!scatter) return;
    for (uint32_t i = 0; i < n; ++i)
        if (IsUserPtr(entities[i]))
            mem.AddScatterReadRequest(scatter, entities[i], &klass[i], sizeof(uintptr_t));
    mem.ExecuteReadScatter(scatter);
    for (uint32_t i = 0; i < n; ++i)
        if (IsUserPtr(klass[i]))
            mem.AddScatterReadRequest(scatter, klass[i] + kIl2CppClassNamePtr, &namePtr[i], sizeof(uintptr_t));
    mem.ExecuteReadScatter(scatter);

    // Class names repeat heavily; read each distinct name pointer once.
    std::unordered_map<uintptr_t, std::string> names;
    for (uint32_t i = 0; i < n; ++i)
        if (IsUserPtr(namePtr[i])) names.emplace(namePtr[i], std::string());
    {
        std::vector<std::pair<uintptr_t, std::array<char, 48>>> raw;
        raw.reserve(names.size());
        for (const auto& [ptr, _] : names) raw.emplace_back(ptr, std::array<char, 48>{});
        for (auto& [ptr, buf] : raw)
            mem.AddScatterReadRequest(scatter, ptr, buf.data(), buf.size() - 1);
        mem.ExecuteReadScatter(scatter);
        for (auto& [ptr, buf] : raw) {
            buf[buf.size() - 1] = '\0';
            for (char& ch : buf) if (ch && (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7E)) { ch = '\0'; break; }
            names[ptr] = buf.data();
        }
    }

    // Select interesting entities, resolve their native transform, read positions.
    struct Pending { uintptr_t entity; WorldKind kind; const std::string* name; uintptr_t nativeTransform; float pos[3]; bool ok; };
    std::vector<Pending> pending;
    pending.reserve(512);
    for (uint32_t i = 0; i < n && pending.size() < 1024; ++i) {
        if (!IsUserPtr(entities[i]) || !IsUserPtr(namePtr[i])) continue;
        const std::string& cls = names[namePtr[i]];
        WorldKind kind = WorldKind::Unknown;
        if (!WorldItemEnabled(cls.c_str(), kind)) continue;
        pending.push_back(Pending{ entities[i], kind, &cls, 0, {0.f, 0.f, 0.f}, false });
    }

    std::vector<WorldEntity> world;
    world.reserve(pending.size());
    const float maxDistance = static_cast<float>(c.world_max_distance) + 50.f;
    for (Pending& item : pending) {
        if (!ResolveComponentTransform(item.entity, item.nativeTransform)) continue;
        uintptr_t hierarchy = 0;
        int index = -1;
        if (!ResolveNativeTransformAccess(item.nativeTransform, hierarchy, index)) continue;
        HierarchyBlock block;
        if (!block.Load(hierarchy, index) || !block.WorldPosition(index, item.pos) || !PlausibleWorldPos(item.pos)) continue;
        WorldEntity we{};
        we.address = item.entity;
        we.transform = item.nativeTransform;
        we.kind = item.kind;
        std::memcpy(we.pos, item.pos, sizeof(we.pos));
        we.distance = Dist3(we.pos, runtime.local_pos);
        if (we.distance > maxDistance) continue;
        std::snprintf(we.name, sizeof(we.name), "%s", item.name->c_str());
        we.valid = true;
        world.push_back(we);
    }

    std::sort(world.begin(), world.end(), [](const WorldEntity& a, const WorldEntity& b) { return a.distance < b.distance; });
    if (world.size() > 512) world.resize(512);
    runtime.world_entities.swap(world);
    runtime.world_count = static_cast<int>(runtime.world_entities.size());
}

void TryMiscWrites() {
    // Memory writes (no-recoil, admin flags, time of day...) are intentionally
    // not part of the read-only build. The UI toggles remain persisted but are
    // inert; log once so the absence is explicit in the session log.
    static bool logged = false;
    const bool wanted = config.no_recoil || config.spider_man || config.admin_flag || config.bright_nights
        || config.bright_caves || config.change_time || config.change_fov || config.remove_water || config.instant_eoka;
    if (wanted && !logged) {
        logged = true;
        std::cout << "[Rust] misc writes requested but this build is read-only; ignoring\n";
    }
}

} // namespace detail
} // namespace Rust
