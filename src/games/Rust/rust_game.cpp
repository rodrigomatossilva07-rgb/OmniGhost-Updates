#include "rust_game.h"
#include "rust_esp.h"
#include "rust_offsets.h"

#include "third_party/DMALibrary/Memory/Memory.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_set>

extern Memory mem;

namespace Rust {

Config config{};
Runtime runtime{};
std::atomic_bool ready{false};

namespace {
constexpr const char* kProcessName = "RustClient.exe";
constexpr uintptr_t kMinimumPointer = 0x10000;
constexpr uintptr_t kMaximumUserPointer = 0x00007FFFFFFFFFFFull;
constexpr std::uint32_t kMaximumEntities = 8192;

struct Vec3 { float x{}, y{}, z{}; };
struct Quat { float x{}, y{}, z{}, w{}; };
struct Matrix34 { Vec3 translation; float pad0{}; Quat rotation; Vec3 scale; float pad1{}; };
struct TransformAccess { uintptr_t hierarchy{}; std::int32_t index{-1}; std::int32_t pad{}; };
struct TransformData { uintptr_t matrices{}; uintptr_t parents{}; };

bool IsPointer(uintptr_t value) {
    return value >= kMinimumPointer && value <= kMaximumUserPointer && (value & 7u) == 0;
}

bool IsVector(const Vec3& value) {
    constexpr float limit = 1000000.0f;
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) &&
           std::fabs(value.x) < limit && std::fabs(value.y) < limit && std::fabs(value.z) < limit;
}

bool ReadExact(uintptr_t address, void* output, size_t size) {
    if (!IsPointer(address) || output == nullptr || size == 0) return false;
    size_t bytes = 0;
    return mem.ReadWithCount(address, output, size, bytes) && bytes == size;
}

template <typename T>
bool ReadValue(uintptr_t address, T& output) {
    output = T{};
    return ReadExact(address, &output, sizeof(T));
}

bool ReadPointer(uintptr_t address, uintptr_t& output) {
    return ReadValue(address, output) && IsPointer(output);
}

bool ReadStaticFields(uintptr_t typeInfoRva, uintptr_t& fields) {
    using namespace Offsets::Build_2026_10_22;
    uintptr_t klass = 0;
    return ReadPointer(runtime.game_assembly + typeInfoRva, klass) &&
           ReadPointer(klass + Il2CppClass::staticFields, fields);
}

Vec3 Rotate(const Quat& q, const Vec3& value) {
    const Vec3 u{q.x, q.y, q.z};
    const float dotUV = u.x * value.x + u.y * value.y + u.z * value.z;
    const float dotUU = u.x * u.x + u.y * u.y + u.z * u.z;
    const Vec3 cross{u.y * value.z - u.z * value.y,
                     u.z * value.x - u.x * value.z,
                     u.x * value.y - u.y * value.x};
    return {2.0f * dotUV * u.x + (q.w * q.w - dotUU) * value.x + 2.0f * q.w * cross.x,
            2.0f * dotUV * u.y + (q.w * q.w - dotUU) * value.y + 2.0f * q.w * cross.y,
            2.0f * dotUV * u.z + (q.w * q.w - dotUU) * value.z + 2.0f * q.w * cross.z};
}

bool ReadTransformPosition(uintptr_t transform, Vec3& position) {
    using namespace Offsets::Build_2026_10_22;
    uintptr_t native = 0;
    TransformAccess access{};
    TransformData data{};
    if (!ReadPointer(transform + Object::m_CachedPtr, native) ||
        !ReadValue(native + NativeTransform::access, access) ||
        !IsPointer(access.hierarchy) || access.index < 0 || access.index > 65535 ||
        !ReadValue(access.hierarchy + NativeTransform::hierarchyData, data) ||
        !IsPointer(data.matrices) || !IsPointer(data.parents))
        return false;

    Matrix34 local{};
    if (!ReadValue(data.matrices + static_cast<uintptr_t>(access.index) * sizeof(Matrix34), local) ||
        !IsVector(local.translation)) return false;
    position = local.translation;
    std::int32_t parent = -1;
    if (!ReadValue(data.parents + static_cast<uintptr_t>(access.index) * sizeof(parent), parent)) return false;
    std::unordered_set<std::int32_t> visited;
    for (int depth = 0; parent >= 0 && depth < 128; ++depth) {
        if (parent > 65535 || !visited.insert(parent).second) return false;
        Matrix34 matrix{};
        if (!ReadValue(data.matrices + static_cast<uintptr_t>(parent) * sizeof(Matrix34), matrix) ||
            !IsVector(matrix.translation) || !IsVector(matrix.scale)) return false;
        Vec3 scaled{position.x * matrix.scale.x, position.y * matrix.scale.y,
                    position.z * matrix.scale.z};
        position = Rotate(matrix.rotation, scaled);
        position.x += matrix.translation.x;
        position.y += matrix.translation.y;
        position.z += matrix.translation.z;
        if (!IsVector(position) ||
            !ReadValue(data.parents + static_cast<uintptr_t>(parent) * sizeof(parent), parent)) return false;
    }
    return parent < 0 && IsVector(position);
}

bool ReadCollection(std::vector<uintptr_t>& entities) {
    using namespace Offsets::Build_2026_10_22;
    uintptr_t statics = 0, realm = 0, list = 0, values = 0, array = 0;
    std::int32_t count = 0;
    std::uint64_t capacity = 0;
    if (!ReadStaticFields(BaseNetworkableStatic::TypeInfo, statics) ||
        !ReadPointer(statics + BaseNetworkableStatic::clientEntities, realm) ||
        !ReadPointer(realm + EntityRealm::entityList, list) ||
        !ReadPointer(list + ListDictionary::vals, values) ||
        !ReadPointer(values + BufferList::buffer, array) ||
        !ReadValue(values + BufferList::count, count) ||
        !ReadValue(array + ManagedArray::length, capacity) || count < 0 ||
        static_cast<std::uint64_t>(count) > capacity || capacity > kMaximumEntities) {
        runtime.collection_status = "Cadeia BaseNetworkable/BufferList inválida";
        return false;
    }
    entities.resize(static_cast<size_t>(count));
    if (count != 0 && !ReadExact(array + ManagedArray::data, entities.data(), entities.size() * sizeof(uintptr_t))) {
        runtime.collection_status = "Buffer de entidades ilegível";
        entities.clear();
        return false;
    }
    entities.erase(std::remove_if(entities.begin(), entities.end(), [](uintptr_t p) { return !IsPointer(p); }), entities.end());
    runtime.entity_realm = realm;
    runtime.entity_list = list;
    runtime.entity_count = static_cast<std::uint32_t>(entities.size());
    runtime.collection_status = "Coleção validada";
    return true;
}

void ReadPlayers(const std::vector<uintptr_t>& entities) {
    using namespace Offsets::Build_2026_10_22;
    runtime.players.clear();
    runtime.transform_failures = 0;
    uintptr_t localStatics = 0;
    runtime.local_player = 0;
    if (ReadStaticFields(LocalPlayerStatic::TypeInfo, localStatics))
        ReadPointer(localStatics + LocalPlayerStatic::Entity, runtime.local_player);

    Vec3 localPosition{};
    bool haveLocalPosition = false;
    for (uintptr_t entity : entities) {
        uintptr_t model = 0, root = 0, head = 0;
        if (!ReadPointer(entity + BaseEntity::model, model) ||
            !ReadPointer(model + Model::rootBone, root) ||
            !ReadPointer(model + Model::headBone, head)) continue;
        Vec3 feet{}, headPosition{};
        if (!ReadTransformPosition(root, feet) || !ReadTransformPosition(head, headPosition)) {
            ++runtime.transform_failures;
            continue;
        }
        const float height = std::sqrt((headPosition.x-feet.x)*(headPosition.x-feet.x) +
            (headPosition.y-feet.y)*(headPosition.y-feet.y) + (headPosition.z-feet.z)*(headPosition.z-feet.z));
        if (!std::isfinite(height) || height < 0.5f || height > 4.0f) continue;
        Player player{};
        player.address = entity;
        player.feet[0] = feet.x; player.feet[1] = feet.y; player.feet[2] = feet.z;
        player.head[0] = headPosition.x; player.head[1] = headPosition.y; player.head[2] = headPosition.z;
        ReadValue(entity + BaseCombatEntity::_health, player.health);
        std::uint8_t lifeState = 1;
        ReadValue(entity + BaseCombatEntity::lifestate, lifeState);
        player.alive = std::isfinite(player.health) && player.health > 0.0f &&
                       player.health < 10000.0f && lifeState == 0;
        player.local = entity == runtime.local_player;
        if (player.local) { localPosition = feet; haveLocalPosition = true; }
        runtime.players.push_back(player);
    }
    if (haveLocalPosition) {
        for (Player& player : runtime.players) {
            const float dx = player.feet[0] - localPosition.x;
            const float dy = player.feet[1] - localPosition.y;
            const float dz = player.feet[2] - localPosition.z;
            player.distance = std::sqrt(dx*dx + dy*dy + dz*dz);
        }
    }
    runtime.valid_player_count = static_cast<std::uint32_t>(runtime.players.size());
    runtime.transform_status = runtime.players.empty() ? "Sem transforms de jogador válidos" : "Hierarquias validadas";
}

int MatrixScore(const float* matrix) {
    int score = 0;
    for (const Player& player : runtime.players) {
        if (!player.alive) continue;
        const float* p = player.head;
        const float w = matrix[3]*p[0] + matrix[7]*p[1] + matrix[11]*p[2] + matrix[15];
        if (!std::isfinite(w) || w <= 0.001f) continue;
        const float x = (matrix[0]*p[0] + matrix[4]*p[1] + matrix[8]*p[2] + matrix[12]) / w;
        const float y = (matrix[1]*p[0] + matrix[5]*p[1] + matrix[9]*p[2] + matrix[13]) / w;
        if (std::isfinite(x) && std::isfinite(y) && std::fabs(x) < 1.5f && std::fabs(y) < 1.5f) ++score;
    }
    return score;
}

bool ReadCamera() {
    using namespace Offsets::Build_2026_10_22;
    uintptr_t statics = 0;
    if (!ReadStaticFields(MainCamera::TypeInfo, statics)) {
        runtime.camera_status = "MainCamera TypeInfo inválido"; return false;
    }
    uintptr_t wrapper = 0;
    for (uintptr_t offset = 0; offset <= 0x80 && !wrapper; offset += sizeof(uintptr_t)) {
        uintptr_t owner = 0, candidate = 0, native = 0;
        if (!ReadPointer(statics + offset, owner)) continue;
        if (ReadPointer(owner + MainCamera::mainCamera, candidate) &&
            ReadPointer(candidate + Object::m_CachedPtr, native)) wrapper = candidate;
    }
    if (!wrapper) { runtime.camera_status = "Instância MainCamera não validada"; return false; }
    uintptr_t native = 0;
    if (!ReadPointer(wrapper + Object::m_CachedPtr, native)) return false;
    if (runtime.camera == wrapper && runtime.camera_matrix_offset != 0) {
        float cached[16]{};
        if (ReadExact(native + runtime.camera_matrix_offset, cached, sizeof(cached)) &&
            MatrixScore(cached) > 0) {
            std::memcpy(runtime.view_matrix, cached, sizeof(cached));
            runtime.camera_status = "Matriz validada por projeção";
            runtime.last_camera_sample_ms = GetTickCount64();
            return true;
        }
        runtime.camera_matrix_offset = 0;
    }
    std::array<std::byte, 0x1000> block{};
    if (!ReadExact(native, block.data(), block.size())) {
        runtime.camera_status = "Objeto nativo Camera ilegível"; return false;
    }
    int bestScore = 0;
    uintptr_t bestOffset = 0;
    float best[16]{};
    for (uintptr_t offset = 0; offset + sizeof(best) <= block.size(); offset += 4) {
        float candidate[16]{};
        std::memcpy(candidate, block.data() + offset, sizeof(candidate));
        bool finite = true;
        for (float value : candidate) finite &= std::isfinite(value) && std::fabs(value) < 1000000.0f;
        if (!finite) continue;
        const int score = MatrixScore(candidate);
        if (score > bestScore) { bestScore = score; bestOffset = offset; std::memcpy(best, candidate, sizeof(best)); }
    }
    if (bestScore == 0) { runtime.camera_status = "Nenhuma matriz passou a projeção"; return false; }
    runtime.camera = wrapper;
    runtime.camera_matrix_offset = bestOffset;
    std::memcpy(runtime.view_matrix, best, sizeof(best));
    runtime.camera_status = "Matriz validada por projeção";
    runtime.last_camera_sample_ms = GetTickCount64();
    return true;
}

bool HasSuppliedCriticalOffsets() {
    using namespace Offsets::Build_2026_10_22;
    return Rvas::TypeManagerInstance != 0 && Rvas::TypeToObjectSet != 0 &&
           BaseNetworkableStatic::clientEntities != 0 && EntityRealm::entityList != 0 &&
           BasePlayer::_displayName != 0 && BaseCombatEntity::_health != 0;
}

void UpdateAvailability(bool refreshProcess = true) {
    if (refreshProcess) {
        const auto diagnostics = mem.GetDiagnosticsSnapshot();
        runtime.device_ok = diagnostics.deviceOpen;
        runtime.process_ok = diagnostics.processInitialized && mem.GetPidFromName(kProcessName) != 0;
        runtime.modules_ok = runtime.game_assembly > 0x10000 && runtime.unity_player > 0x10000;
    }
    runtime.offset_catalog_ok = HasSuppliedCriticalOffsets();




    std::vector<uintptr_t> entities;
    runtime.entity_source_ok = ReadCollection(entities);
    if (runtime.entity_source_ok) ReadPlayers(entities);
    runtime.camera_ok = runtime.entity_source_ok && !runtime.players.empty() && ReadCamera();
    runtime.acquisition_ok = runtime.entity_source_ok && runtime.camera_ok;
    if (runtime.entity_source_ok) runtime.last_world_sample_ms = GetTickCount64();

    if (!runtime.device_ok) runtime.status = "DMA/FPGA indisponível";
    else if (!runtime.process_ok) runtime.status = "RustClient.exe não encontrado";
    else if (!runtime.modules_ok) runtime.status = "GameAssembly.dll/UnityPlayer.dll indisponíveis";
    else if (!runtime.offset_catalog_ok) runtime.status = "Catálogo de offsets Rust inválido";
    else if (!runtime.entity_source_ok) runtime.status = runtime.collection_status;
    else if (runtime.players.empty()) runtime.status = runtime.transform_status;
    else if (!runtime.camera_ok) runtime.status = runtime.camera_status;
    else runtime.status = "Aquisição Rust ativa";
}
}

bool Attach() {
    Shutdown();
    runtime.status = "A abrir DMA e a procurar Rust...";
    if (!mem.Init(std::string(), true, false) && !mem.GetDiagnosticsSnapshot().deviceOpen) {
        UpdateAvailability();
        return false;
    }
    runtime.pid = mem.GetPidFromName(kProcessName);
    if (!runtime.pid) {
        UpdateAvailability();
        return false;
    }
    if (!mem.Init(kProcessName, false, false)) {
        UpdateAvailability();
        return false;
    }
    runtime.game_assembly = mem.GetBaseDaddy("GameAssembly.dll");
    runtime.unity_player = mem.GetBaseDaddy("UnityPlayer.dll");
    UpdateAvailability();


    ready.store(runtime.process_ok && runtime.modules_ok, std::memory_order_release);
    return ready.load(std::memory_order_acquire);
}

void Shutdown() noexcept {
    ready.store(false, std::memory_order_release);
    runtime = Runtime{};
}

void Tick() {
    if (!ready.load(std::memory_order_acquire)) return;
    const std::uint64_t now = GetTickCount64();
    if (now - runtime.last_probe_ms >= 1000u) {
        runtime.last_probe_ms = now;
        UpdateAvailability(true);
        if (!runtime.process_ok)
            ready.store(false, std::memory_order_release);
    }
    else if (now - runtime.last_acquisition_attempt_ms >= 7u) {
        // ~144 Hz acquisition attempt. The successful camera path uses the
        // previously validated offset and does not rescan the native object.
        runtime.last_acquisition_attempt_ms = now;
        UpdateAvailability(false);
    }
    ++runtime.frames;
    ESP::Draw(runtime, config);
}

bool IsGameProcessAlive() {
    return mem.GetPidFromName(kProcessName) != 0;
}

bool ValidateLiveOffsets() {
    UpdateAvailability();
    return runtime.modules_ok && runtime.offset_catalog_ok;
}

const char* StatusLine() {
    return runtime.status.c_str();
}

}
