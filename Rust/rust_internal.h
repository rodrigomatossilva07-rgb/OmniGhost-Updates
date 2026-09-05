#pragma once
// Internal declarations shared by the Rust adapter translation units
// (rust_game.cpp, rust_entities.cpp, rust_probe.cpp). Not for UI/other games.
#include "rust_game.h"
#include "../DMALibrary/Memory/Memory.h"

#include <cstdint>
#include <cstring>

namespace Rust {
namespace detail {

constexpr const char* kProcessName = "RustClient.exe";
constexpr const char* kGameAssembly = "GameAssembly.dll";

// Unity/il2cpp layout constants that are stable across Rust builds.
constexpr uintptr_t kIl2CppClassNamePtr = 0x10;      // Il2CppClass::name
constexpr uintptr_t kUnityStringLength = 0x10;       // System.String length (int32)
constexpr uintptr_t kUnityStringChars = 0x14;        // System.String first UTF-16 char
constexpr uintptr_t kUnityArrayCount = 0x18;         // T[] length
constexpr uintptr_t kUnityArrayItems = 0x20;         // T[] first element
constexpr uintptr_t kCachedNativePtr = 0x10;         // UnityEngine.Object::m_CachedPtr
constexpr uintptr_t kNativeComponentGameObject = 0x30;
constexpr uintptr_t kNativeGameObjectComponents = 0x30;
constexpr uintptr_t kNativeComponentEntryPtr = 0x8;
constexpr uintptr_t kTransformHierarchy = 0x38;      // TransformAccess.hierarchy
constexpr uintptr_t kTransformIndex = 0x40;          // TransformAccess.index
constexpr uintptr_t kHierarchyLocalTransforms = 0x18;
constexpr uintptr_t kHierarchyParentIndices = 0x20;
constexpr uintptr_t kTransformDataStride = 0x30;     // {Vector3 pos, Quaternion rot, Vector3 scale}

inline bool IsUserPtr(uintptr_t p) noexcept {
    return p >= 0x10000ull && p < 0x00007FFFFFFFFFFFull && (p & 0x7) == 0;
}

inline bool ReadU64(uintptr_t addr, uintptr_t& out) {
    return mem.Read(addr, &out, sizeof(out));
}

template <typename T>
inline bool R(uintptr_t addr, T& out) {
    return mem.Read(addr, &out, sizeof(T));
}

uint64_t NowMs();

// Process-bound runtime state (matrix/list/local) must never survive a new
// PID or VMM session generation.
void InvalidateProcessContext(const char* why);
void SyncGenerationsOrInvalidate();

// Backend reason shared between the lifecycle worker and RunFrame.
void SetBackendReason(BackendReason reason);
BackendReason CurrentBackendReason();
const char* UserFacingReason(BackendReason reason);

// Entity / camera resolution (rust_entities.cpp)
uintptr_t ResolveLocalPlayer();
bool ResolveViewMatrix(float out[16]);
uintptr_t ResolvePlayerListBuffer(uint32_t& out_size);
void ReadPlayerName(uintptr_t displayNamePtr, char* out, size_t outn);
void ReadClassName(uintptr_t object, char* out, size_t outn);
bool ReadTransformPos(uintptr_t transform, float out[3]);
void CachePlayers();
void UpdatePositions();
void CacheWorldEntities();
void TryMiscWrites();

} // namespace detail
} // namespace Rust
