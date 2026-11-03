#pragma once
#include <cstdint>

namespace Rust::Offsets::Build_25231061 {
inline constexpr const char* BuildId = "25231061";
namespace Rvas { inline constexpr std::uintptr_t BaseNetworkable = 0x10BE8EC8; inline constexpr std::uintptr_t Camera = 0x10C62B28; }
namespace BaseNetworkable {
inline constexpr std::uintptr_t staticFields=0xB8, clientEntities=0x28, entityList=0x10, objectDictionary=0x10;
inline constexpr std::uintptr_t entityContent=0x10, entitySize=0x18, bufferList=0x10, prefabId=0x54;
}
namespace BaseCamera {
inline constexpr std::uintptr_t staticFields=0xB8, wrapperClass=0x78, parentStaticFields=0x10;
inline constexpr std::uintptr_t matrix=0x30C, viewMatrix=0x80, projectionMatrix=0xC0, position=0x454;
}
namespace BaseCombatEntity { inline constexpr std::uintptr_t lifeState=0x2A8, model=0x1B8, health=0x2B0, maxHealth=0x240; }
namespace BasePlayer {
inline constexpr std::uintptr_t playerFlags=0x6D8, modelState=0x4B0, displayName=0x520, playerModel=0x2F0;
inline constexpr std::uintptr_t currentTeam=0x558, input=0x6F0, eyes=0x718;
}
namespace PlayerModel {
inline constexpr std::uintptr_t headBone=0xF8, boneTransforms=0x120, position=0x1F8, velocity=0x21C;
inline constexpr std::uintptr_t rotation=0x228, isVisible=0x160, isNpc=0x3DD;
}
namespace Model { inline constexpr std::uintptr_t boneTransforms=0x50, rootBone=0x28, headBone=0x30; }
namespace ManagedArray { inline constexpr std::uintptr_t length=0x18, data=0x20; }
}

