#pragma once
#include <cstdint>


namespace Rust::Offsets::Build_2026_10_22 {
namespace Rvas {
    inline static constexpr uintptr_t TypeManagerInstance = 0x221D020;
    inline static constexpr uintptr_t TypeToObjectSet     = 0x221CFF8;
    inline static constexpr uintptr_t m_Instance          = 0x21818E8;
    inline static constexpr uintptr_t rigidActors         = 0x23D8;
}
namespace AttackEntity {
    inline static constexpr uintptr_t animationDelay  = 0x2F8;
    inline static constexpr uintptr_t deployDelay     = 0x2F0;
    inline static constexpr uintptr_t effectiveRange  = 0x2FC;
    inline static constexpr uintptr_t lastTickTime    = 0x350;
    inline static constexpr uintptr_t nextAttackTime  = 0x348;
    inline static constexpr uintptr_t nextTickTime    = 0x358;
    inline static constexpr uintptr_t noHeadshots     = 0x346;
    inline static constexpr uintptr_t repeatDelay     = 0x2F4;
    inline static constexpr uintptr_t timeSinceDeploy = 0x360;
}
namespace BaseCombatEntity {
    inline static constexpr uintptr_t _health             = 0x2BC;
    inline static constexpr uintptr_t _maxHealth          = 0x2C0;
    inline static constexpr uintptr_t baseProtection      = 0x240;
    inline static constexpr uintptr_t lifestate           = 0x2B0;
    inline static constexpr uintptr_t markAttackerHostile = 0x2B6;
    inline static constexpr uintptr_t skeletonProperties  = 0x238;
    inline static constexpr uintptr_t startHealth         = 0x248;
}
namespace BaseCorpse {
    inline static constexpr uintptr_t blockDamageIfNotGather = 0x2F8;
    inline static constexpr uintptr_t parentEnt              = 0x2E0;
}
namespace BaseEntity {
    inline static constexpr uintptr_t SendSignalBroadcast = 0x37D2540;
    inline static constexpr uintptr_t ServerRPC           = 0x385AFE0;
    inline static constexpr uintptr_t bounds              = 0x194;
    inline static constexpr uintptr_t flags               = 0x1C8;
    inline static constexpr uintptr_t model               = 0x1C0;
    inline static constexpr uintptr_t positionLerp        = 0xE0;
    inline static constexpr uintptr_t triggers            = 0xB0;
}
namespace BaseMelee {
    inline static constexpr uintptr_t attackRadius         = 0x3AC;
    inline static constexpr uintptr_t blockSprintOnAttack  = 0x3B1;
    inline static constexpr uintptr_t canThrowAsProjectile = 0x388;
    inline static constexpr uintptr_t damageProperties     = 0x390;
    inline static constexpr uintptr_t gathering            = 0x3E8;
    inline static constexpr uintptr_t maxDistance          = 0x3A8;
}
namespace BaseMovement {
    inline static constexpr uintptr_t Crawling           = 0x58;
    inline static constexpr uintptr_t Ducking            = 0x54;
    inline static constexpr uintptr_t Grounded           = 0x5C;
    inline static constexpr uintptr_t InheritedVelocity  = 0x38;
    inline static constexpr uintptr_t Owner              = 0x30;
    inline static constexpr uintptr_t Running            = 0x50;
    inline static constexpr uintptr_t TargetMovement     = 0x44;
    inline static constexpr uintptr_t lastTeleportedTime = 0x60;
}
namespace BaseNetworkable {
    inline static constexpr uintptr_t children     = 0x70;
    inline static constexpr uintptr_t parentEntity = 0x38;
    inline static constexpr uintptr_t prefabID     = 0x54;
}
namespace BaseNetworkableStatic {
    inline static constexpr uintptr_t clientEntities = 0x28;
    inline static constexpr uintptr_t TypeInfo       = 0x111F0768;
}
namespace BasePlayer {
    inline static constexpr uintptr_t OnViewModeChanged          = 0x3E90C40;
    inline static constexpr uintptr_t _displayName               = 0x6F8;
    inline static constexpr uintptr_t _lookingAt                 = 0x508;
    inline static constexpr uintptr_t clActiveItem               = 0x590;
    inline static constexpr uintptr_t clothingMoveSpeedReduction = 0x7D0;
    inline static constexpr uintptr_t clothingWaterSpeedBonus    = 0x7D4;
    inline static constexpr uintptr_t eyes                       = 0x3C8;
    inline static constexpr uintptr_t input                      = 0x440;
    inline static constexpr uintptr_t inventory                  = 0x5E0;
    inline static constexpr uintptr_t lastSentTick               = 0x358;
    inline static constexpr uintptr_t lastSentTickTime           = 0x6A0;
    inline static constexpr uintptr_t modelState                 = 0x398;
    inline static constexpr uintptr_t modifiers                  = 0x718;
    inline static constexpr uintptr_t mounted                    = 0x5E8;
    inline static constexpr uintptr_t movement                   = 0x550;
    inline static constexpr uintptr_t playerFlags                = 0x6E0;
    inline static constexpr uintptr_t playerModel                = 0x748;
    inline static constexpr uintptr_t weaponMoveSpeedScale       = 0x7C8;
}
namespace BaseProjectile {
    inline static constexpr uintptr_t LaunchProjectile        = 0x2500F20;
    inline static constexpr uintptr_t UpdateAmmoDisplay       = 0x24E1AF0;
    inline static constexpr uintptr_t aimCone                 = 0x420;
    inline static constexpr uintptr_t aimConePenaltyMax       = 0x42C;
    inline static constexpr uintptr_t aimSway                 = 0x408;
    inline static constexpr uintptr_t aimSwaySpeed            = 0x40C;
    inline static constexpr uintptr_t aimconePenalty          = 0x474;
    inline static constexpr uintptr_t aimconePenaltyPerShot   = 0x428;
    inline static constexpr uintptr_t automatic               = 0x3A0;
    inline static constexpr uintptr_t canChangeFireModes      = 0x448;
    inline static constexpr uintptr_t currentBurst            = 0x4C0;
    inline static constexpr uintptr_t fractionalReload        = 0x3F0;
    inline static constexpr uintptr_t hipAimCone              = 0x424;
    inline static constexpr uintptr_t isBurstWeapon           = 0x447;
    inline static constexpr uintptr_t isReloading             = 0x48C;
    inline static constexpr uintptr_t nextReloadTime          = 0x460;
    inline static constexpr uintptr_t numShotsFired           = 0x45C;
    inline static constexpr uintptr_t primaryMagazine         = 0x3E8;
    inline static constexpr uintptr_t projectileVelocityScale = 0x39C;
    inline static constexpr uintptr_t recoil                  = 0x410;
    inline static constexpr uintptr_t reloadEndDuration       = 0x3FC;
    inline static constexpr uintptr_t reloadFractionDuration  = 0x3F8;
    inline static constexpr uintptr_t reloadStartDuration     = 0x3F4;
    inline static constexpr uintptr_t reloadTime              = 0x3E0;
    inline static constexpr uintptr_t stancePenalty           = 0x470;
    inline static constexpr uintptr_t stancePenaltyScale      = 0x438;
    inline static constexpr uintptr_t startReloadTime         = 0x468;
}
namespace BaseViewModel {
    inline static constexpr uintptr_t animator           = 0xC8;
    inline static constexpr uintptr_t aspectOffset       = 0x90;
    inline static constexpr uintptr_t bob                = 0xF0;
    inline static constexpr uintptr_t lower              = 0x98;
    inline static constexpr uintptr_t punch              = 0x88;
    inline static constexpr uintptr_t sway               = 0x108;
    inline static constexpr uintptr_t useViewModelCamera = 0x40;
}
namespace Behaviour {
    inline static constexpr uintptr_t set_enabled = 0xE4E5EC0;
}
namespace BowWeapon {
    inline static constexpr uintptr_t arrowBack   = 0x4E4;
    inline static constexpr uintptr_t attackReady = 0x4E0;
    inline static constexpr uintptr_t wasAiming   = 0x4E8;
}
namespace BuildingBlock {
    inline static constexpr uintptr_t apartmentList = 0x20;
}
namespace CameraUpdateHookStatic {
    inline static constexpr uintptr_t action = 0x48;
}
namespace Component {
    inline static constexpr uintptr_t get_gameObject = 0xE4E6BA0;
}
namespace CompoundBowWeapon {
    inline static constexpr uintptr_t conditionLossHeldDelay     = 0x508;
    inline static constexpr uintptr_t conditionLossPerSecondHeld = 0x504;
    inline static constexpr uintptr_t movementPenalty            = 0x538;
    inline static constexpr uintptr_t movementPenaltyRampUpTime  = 0x500;
    inline static constexpr uintptr_t stringBonusDamage          = 0x4F4;
    inline static constexpr uintptr_t stringBonusDistance        = 0x4F8;
    inline static constexpr uintptr_t stringBonusVelocity        = 0x4FC;
    inline static constexpr uintptr_t stringHoldDurationMax      = 0x4F0;
    inline static constexpr uintptr_t stringHoldTimeStart        = 0x544;
}
namespace ConvarClientStatic {
    inline static constexpr uintptr_t camlerp  = 0x8B4;
    inline static constexpr uintptr_t camspeed = 0x818;
}
namespace ConvarGraphicsStatic {
    inline static constexpr uintptr_t _fov = 0x318;
}
namespace ConvarPlayerStatic {
    inline static constexpr uintptr_t clientTickInterval = 0x4C;
}
namespace EntityRealm {
    inline static constexpr uintptr_t entityList = 0x18;
}
namespace FlintStrikeWeapon {
    inline static constexpr uintptr_t _didSparkThisFrame = 0x4D8;
    inline static constexpr uintptr_t _isStriking        = 0x4D9;
    inline static constexpr uintptr_t strikes            = 0x4DC;
    inline static constexpr uintptr_t successFraction    = 0x4C8;
    inline static constexpr uintptr_t successIncrease    = 0x4CC;
}
namespace GameManager {
    inline static constexpr uintptr_t CreatePrefab = 0x2414E20;
}
namespace GameManagerStatic {
    inline static constexpr uintptr_t client = 0x28;
}
namespace GameObject {
    inline static constexpr uintptr_t SetActive = 0xE4EC070;
}
namespace HeldEntity {
    inline static constexpr uintptr_t viewModel = 0x258;
}
namespace HumanNPC {
    inline static constexpr uintptr_t AdditionalLosBlockingLayer = 0x858;
    inline static constexpr uintptr_t aimConeScale               = 0x868;
    inline static constexpr uintptr_t lastDismountTime           = 0x86C;
}
namespace InputMessage {
    inline static constexpr uintptr_t aimAngles  = 0x24;
    inline static constexpr uintptr_t buttons    = 0x1C;
    inline static constexpr uintptr_t mouseDelta = 0x10;
}
namespace InputState {
    inline static constexpr uintptr_t current  = 0x20;
    inline static constexpr uintptr_t previous = 0x10;
}
namespace Interpolator {
    inline static constexpr uintptr_t last = 0x10;
    inline static constexpr uintptr_t list = 0x30;
}
namespace Item {
    inline static constexpr uintptr_t amount     = 0x48;
    inline static constexpr uintptr_t heldEntity = 0x30;
    inline static constexpr uintptr_t info       = 0x50;
    inline static constexpr uintptr_t position   = 0x14;
    inline static constexpr uintptr_t uid        = 0xC0;
    inline static constexpr uintptr_t worldEnt   = 0xC8;
}
namespace ItemContainer {
    inline static constexpr uintptr_t flags    = 0x60;
    inline static constexpr uintptr_t itemList = 0x50;
    inline static constexpr uintptr_t uid      = 0x38;
}
namespace ItemDefinition {
    inline static constexpr uintptr_t category    = 0x58;
    inline static constexpr uintptr_t displayName = 0x40;
    inline static constexpr uintptr_t itemid      = 0x20;
    inline static constexpr uintptr_t shortname   = 0x28;
}
namespace ItemModProjectile {
    inline static constexpr uintptr_t projectileObject         = 0x20;
    inline static constexpr uintptr_t projectileSpread         = 0x3C;
    inline static constexpr uintptr_t projectileVelocity       = 0x40;
    inline static constexpr uintptr_t projectileVelocitySpread = 0x44;
}
namespace ListComponent {
    inline static constexpr uintptr_t InstanceList = 0x30;
}
namespace ListComponentProjectile {
    inline static constexpr uintptr_t TypeInfo = 0x11204AF0;
}
namespace ListDictionary {
    inline static constexpr uintptr_t vals = 0x20;
}
namespace BufferList {
    // Managed BufferList<T>: backing T[] plus the number of occupied entries.
    inline static constexpr uintptr_t buffer = 0x10;
    inline static constexpr uintptr_t count  = 0x18;
}
namespace ManagedArray {
    inline static constexpr uintptr_t length = 0x18;
    inline static constexpr uintptr_t data   = 0x20;
}
namespace Il2CppClass {
    // IL2CPP v39 / Unity 6000.3.15f1. Runtime code validates every pointer read
    // through this member before accepting the corresponding source.
    inline static constexpr uintptr_t staticFields = 0xB8;
}
namespace ListHashSet {
    inline static constexpr uintptr_t vals = 0x18;
}
namespace LoadingScreen {
    inline static constexpr uintptr_t panel = 0x30;
}
namespace LocalPlayerStatic {
    inline static constexpr uintptr_t Entity = 0x10;
    inline static constexpr uintptr_t TypeInfo = 0x111E9EE0;
}
namespace LootableCorpse {
    inline static constexpr uintptr_t lootPanelName = 0x320;
}
namespace Magazine {
    inline static constexpr uintptr_t allowAmmoSwitching   = 0x29;
    inline static constexpr uintptr_t allowPlayerReloading = 0x28;
    inline static constexpr uintptr_t ammoType             = 0x20;
    inline static constexpr uintptr_t capacity             = 0x18;
    inline static constexpr uintptr_t contents             = 0x1C;
    inline static constexpr uintptr_t definition           = 0x10;
}
namespace MainCamera {
    inline static constexpr uintptr_t mainCamera = 0xB8;
    inline static constexpr uintptr_t TypeInfo = 0x111C43A0;
}
namespace MixerSnapshotManager {
    inline static constexpr uintptr_t defaultSnapshot = 0x20;
    inline static constexpr uintptr_t loadingSnapshot = 0x30;
}
namespace Model {
    inline static constexpr uintptr_t rootBone       = 0x28;
    inline static constexpr uintptr_t headBone       = 0x30;
    inline static constexpr uintptr_t boneNames      = 0x58;
    inline static constexpr uintptr_t boneTransforms = 0x50;
}
namespace ModelState {
    inline static constexpr uintptr_t flags      = 0x7C;
    inline static constexpr uintptr_t lookDir    = 0x5C;
    inline static constexpr uintptr_t waterLevel = 0x1C;
}
namespace NPCPlayer {
    inline static constexpr uintptr_t LegacyNavigation               = 0x834;
    inline static constexpr uintptr_t MovementTickStartDelay         = 0x7F8;
    inline static constexpr uintptr_t attackLengthMaxShortRangeScale = 0x848;
    inline static constexpr uintptr_t damageScale                    = 0x840;
    inline static constexpr uintptr_t finalDestination               = 0x808;
    inline static constexpr uintptr_t shortRange                     = 0x844;
}
namespace Object {
    inline static constexpr uintptr_t m_CachedPtr = 0x10;
}
namespace NativeTransform {
    // Unity TransformAccessReadOnly layout used by Unity 6000.3.15f1. It is
    // never trusted solely from these constants: rust_game.cpp validates the
    // hierarchy pointer, index, parent chain and all produced vectors.
    inline static constexpr uintptr_t access        = 0x38;
    inline static constexpr uintptr_t hierarchyData = 0x18;
    inline static constexpr uintptr_t matrices      = 0x18;
    inline static constexpr uintptr_t parents       = 0x20;
}
namespace UnityEngineTransform {
    inline static constexpr uintptr_t TypeInfo                       = 0x111B9D30;
    inline static constexpr uintptr_t get_position_Injected          = 0xE509990;
    inline static constexpr uintptr_t get_localToWorldMatrix_Injected = 0xE509F20;
}
namespace UnityEngineCamera {
    inline static constexpr uintptr_t TypeInfo                         = 0x11242210;
    inline static constexpr uintptr_t get_main                         = 0xE463A70;
    inline static constexpr uintptr_t get_worldToCameraMatrix_Injected = 0xE467F00;
    inline static constexpr uintptr_t get_projectionMatrix_Injected    = 0xE467FC0;
}
namespace PlayerCorpse {
    inline static constexpr uintptr_t underwearSkin = 0x350;
}
namespace PlayerEyes {
    inline static constexpr uintptr_t bodyRotation = 0x50;
    inline static constexpr uintptr_t viewOffset   = 0x40;
}
namespace PlayerInput {
    inline static constexpr uintptr_t bodyAngles = 0x44;
    inline static constexpr uintptr_t state      = 0x28;
}
namespace PlayerInventory {
    inline static constexpr uintptr_t containerBelt = 0x38;
    inline static constexpr uintptr_t containerMain = 0x78;
    inline static constexpr uintptr_t containerWear = 0x58;
    inline static constexpr uintptr_t loot          = 0x48;
}
namespace PlayerModel {
    inline static constexpr uintptr_t _multiMesh = 0xB8;
}
namespace PlayerTick {
    inline static constexpr uintptr_t activeItem = 0x50;
    inline static constexpr uintptr_t eyePos     = 0x2C;
    inline static constexpr uintptr_t inputState = 0x10;
    inline static constexpr uintptr_t modelState = 0x40;
    inline static constexpr uintptr_t parentID   = 0x18;
    inline static constexpr uintptr_t position   = 0x20;
}
namespace PlayerWalkMovement {
    inline static constexpr uintptr_t groundAngle               = 0x108;
    inline static constexpr uintptr_t groundAngleNew            = 0x110;
    inline static constexpr uintptr_t groundNormal              = 0x160;
    inline static constexpr uintptr_t groundNormalNew           = 0x170;
    inline static constexpr uintptr_t groundTime                = 0x118;
    inline static constexpr uintptr_t groundVelocity            = 0x180;
    inline static constexpr uintptr_t groundVelocityNew         = 0x190;
    inline static constexpr uintptr_t jumpTime                  = 0x120;
    inline static constexpr uintptr_t landTime                  = 0x128;
    inline static constexpr uintptr_t maxVelocity               = 0x100;
    inline static constexpr uintptr_t previousInheritedVelocity = 0x150;
    inline static constexpr uintptr_t previousPosition          = 0x130;
    inline static constexpr uintptr_t previousVelocity          = 0x140;
}
namespace PositionLerp {
    inline static constexpr uintptr_t interpolator = 0x48;
}
namespace ProgressBar {
    inline static constexpr uintptr_t Instance     = 0x30;
    inline static constexpr uintptr_t leftField    = 0x50;
    inline static constexpr uintptr_t timeCounter  = 0x34;
    inline static constexpr uintptr_t timeFinished = 0x30;
}
namespace Projectile {
    inline static constexpr uintptr_t currentPosition  = 0x170;
    inline static constexpr uintptr_t currentVelocity  = 0x164;
    inline static constexpr uintptr_t drag             = 0x34;
    inline static constexpr uintptr_t gravityModifier  = 0x38;
    inline static constexpr uintptr_t initialDistance  = 0x44;
    inline static constexpr uintptr_t integrity        = 0x13C;
    inline static constexpr uintptr_t thickness        = 0x3C;
    inline static constexpr uintptr_t traveledDistance = 0x17C;
    inline static constexpr uintptr_t traveledTime     = 0x180;
}
namespace ProjectileWeaponMod {
    inline static constexpr uintptr_t isSilencer         = 0x220;
    inline static constexpr uintptr_t projectileVelocity = 0x234;
    inline static constexpr uintptr_t silencerType       = 0x224;
}
namespace RecoilProperties {
    inline static constexpr uintptr_t newRecoilOverride = 0x80;
    inline static constexpr uintptr_t recoilPitchMax    = 0x24;
    inline static constexpr uintptr_t recoilPitchMin    = 0x20;
    inline static constexpr uintptr_t recoilYawMax      = 0x1C;
    inline static constexpr uintptr_t recoilYawMin      = 0x18;
}
namespace ScientistNPC {
    inline static constexpr uintptr_t IdleChatterRepeatRange = 0x890;
    inline static constexpr uintptr_t deathStatName          = 0x888;
    inline static constexpr uintptr_t radioChatterType       = 0x898;
}
namespace ServerAdminUGCEntry {
    inline static constexpr uintptr_t ReceivedDataFromServer = 0x42460C0;
}
namespace SingletonComponentCameraMan {
    inline static constexpr uintptr_t Instance = 0x18;
    inline static constexpr uintptr_t TypeInfo = 0x11263D98;
}
namespace SingletonComponentLoadingScreen {
    inline static constexpr uintptr_t Instance = 0x18;
    inline static constexpr uintptr_t TypeInfo = 0x111AFA80;
}
namespace SingletonComponentMixerSnapshotManager {
    inline static constexpr uintptr_t Instance = 0x18;
    inline static constexpr uintptr_t TypeInfo = 0x11224E80;
}
namespace SkeletonProperties {
    inline static constexpr uintptr_t bones = 0x20;
}
namespace SkeletonPropertiesBoneProperty {
    inline static constexpr uintptr_t area     = 0x20;
    inline static constexpr uintptr_t bone     = 0x10;
    inline static constexpr uintptr_t boneName = 0x18;
}
namespace SkinnedMultiMesh {
    inline static constexpr uintptr_t Renderers = 0x58;
}
namespace StringPool {
    inline static constexpr uintptr_t toNumber = 0x20;
}
namespace TodAmbientParameters {
    inline static constexpr uintptr_t Mode           = 0x10;
    inline static constexpr uintptr_t Saturation     = 0x14;
    inline static constexpr uintptr_t UpdateInterval = 0x18;
}
namespace TodAtmosphereParameters {
    inline static constexpr uintptr_t Brightness         = 0x18;
    inline static constexpr uintptr_t Contrast           = 0x1C;
    inline static constexpr uintptr_t Directionality     = 0x28;
    inline static constexpr uintptr_t Fogginess          = 0x2C;
    inline static constexpr uintptr_t MieMultiplier      = 0x14;
    inline static constexpr uintptr_t NightBrightness    = 0x20;
    inline static constexpr uintptr_t NightContrast      = 0x24;
    inline static constexpr uintptr_t RayleighMultiplier = 0x10;
}
namespace TodCloudParameters {
    inline static constexpr uintptr_t Attenuation = 0x24;
    inline static constexpr uintptr_t Brightness  = 0x30;
    inline static constexpr uintptr_t Coloring    = 0x20;
    inline static constexpr uintptr_t Coverage    = 0x18;
    inline static constexpr uintptr_t Opacity     = 0x14;
    inline static constexpr uintptr_t Saturation  = 0x28;
    inline static constexpr uintptr_t Scattering  = 0x2C;
    inline static constexpr uintptr_t Sharpness   = 0x1C;
    inline static constexpr uintptr_t Size        = 0x10;
}
namespace TodDayParameters {
    inline static constexpr uintptr_t AmbientColor                = 0x40;
    inline static constexpr uintptr_t AmbientMultiplier           = 0x54;
    inline static constexpr uintptr_t CloudColor                  = 0x30;
    inline static constexpr uintptr_t FogColor                    = 0x38;
    inline static constexpr uintptr_t LightColor                  = 0x18;
    inline static constexpr uintptr_t LightIntensity              = 0x48;
    inline static constexpr uintptr_t RayColor                    = 0x20;
    inline static constexpr uintptr_t ReflectionMaxClamp          = 0x64;
    inline static constexpr uintptr_t ReflectionMultiplier        = 0x5C;
    inline static constexpr uintptr_t ShadowStrength              = 0x50;
    inline static constexpr uintptr_t SkyColor                    = 0x28;
    inline static constexpr uintptr_t SunColor                    = 0x10;
    inline static constexpr uintptr_t runtimeAmbientMultiplier    = 0x58;
    inline static constexpr uintptr_t runtimeLightIntensity       = 0x4C;
    inline static constexpr uintptr_t runtimeReflectionMultiplier = 0x60;
}
namespace TodFogParameters {
    inline static constexpr uintptr_t HeightBias = 0x14;
    inline static constexpr uintptr_t Mode       = 0x10;
}
namespace TodLightParameters {
    inline static constexpr uintptr_t MinimumHeight  = 0x14;
    inline static constexpr uintptr_t UpdateInterval = 0x10;
}
namespace TodMoonParameters {
    inline static constexpr uintptr_t HaloBrightness        = 0x28;
    inline static constexpr uintptr_t HaloSize              = 0x24;
    inline static constexpr uintptr_t MeshBrightness        = 0x18;
    inline static constexpr uintptr_t MeshBrightnessDefault = 0x1C;
    inline static constexpr uintptr_t MeshContrast          = 0x20;
    inline static constexpr uintptr_t MeshSize              = 0x10;
    inline static constexpr uintptr_t MeshSizeRed           = 0x14;
    inline static constexpr uintptr_t Position              = 0x2C;
}
namespace TodNightParameters {
    inline static constexpr uintptr_t AmbientColor                = 0x48;
    inline static constexpr uintptr_t AmbientMultiplier           = 0x5C;
    inline static constexpr uintptr_t CloudColor                  = 0x38;
    inline static constexpr uintptr_t FogColor                    = 0x40;
    inline static constexpr uintptr_t LightColor                  = 0x20;
    inline static constexpr uintptr_t LightIntensity              = 0x50;
    inline static constexpr uintptr_t MoonColor                   = 0x10;
    inline static constexpr uintptr_t MoonColorRed                = 0x18;
    inline static constexpr uintptr_t RayColor                    = 0x28;
    inline static constexpr uintptr_t ReflectionMaxClamp          = 0x6C;
    inline static constexpr uintptr_t ReflectionMultiplier        = 0x64;
    inline static constexpr uintptr_t ShadowStrength              = 0x58;
    inline static constexpr uintptr_t SkyColor                    = 0x30;
    inline static constexpr uintptr_t runtimeAmbientMultiplier    = 0x60;
    inline static constexpr uintptr_t runtimeLightIntensity       = 0x54;
    inline static constexpr uintptr_t runtimeReflectionMultiplier = 0x68;
}
namespace TodSky {
    inline static constexpr uintptr_t Ambient    = 0x98;
    inline static constexpr uintptr_t Atmosphere = 0x50;
    inline static constexpr uintptr_t Clouds     = 0x80;
    inline static constexpr uintptr_t Day        = 0x58;
    inline static constexpr uintptr_t Fog        = 0x90;
    inline static constexpr uintptr_t Light      = 0x88;
    inline static constexpr uintptr_t Moon       = 0x70;
    inline static constexpr uintptr_t Night      = 0x60;
    inline static constexpr uintptr_t Stars      = 0x78;
    inline static constexpr uintptr_t Sun        = 0x68;
}
namespace TodSkyStatic {
    inline static constexpr uintptr_t instances = 0x28;
}
namespace TodStarParameters {
    inline static constexpr uintptr_t Brightness = 0x14;
    inline static constexpr uintptr_t ColorScale = 0x1C;
    inline static constexpr uintptr_t Position   = 0x18;
    inline static constexpr uintptr_t Size       = 0x10;
}
namespace TodSunParameters {
    inline static constexpr uintptr_t MeshBrightness        = 0x14;
    inline static constexpr uintptr_t MeshBrightnessDefault = 0x18;
    inline static constexpr uintptr_t MeshContrast          = 0x1C;
    inline static constexpr uintptr_t MeshSize              = 0x10;
}
namespace TranslatePhrase {
    inline static constexpr uintptr_t legacyEnglish = 0x20;
    inline static constexpr uintptr_t token         = 0x10;
}
namespace TriggerLadder {
    inline static constexpr uintptr_t ForceLookAt        = 0x54;
    inline static constexpr uintptr_t RequireJumpToMount = 0x55;
    inline static constexpr uintptr_t Type               = 0x50;
}
namespace TriggerMovement {
    inline static constexpr uintptr_t losEyes        = 0x50;
    inline static constexpr uintptr_t movementModify = 0x58;
    inline static constexpr uintptr_t scale          = 0x5C;
}
namespace UnityComponent {
    inline static constexpr uintptr_t GetGameObject = 0xBEA00;
}
namespace UnityEngineUiText {
    inline static constexpr uintptr_t m_Text = 0xE8;
}
namespace UnityGameObject {
    inline static constexpr uintptr_t m_IsActive = 0x46;
}
namespace ViewModel {
    inline static constexpr uintptr_t instance = 0x28;
}
namespace ViewmodelBob {
    inline static constexpr uintptr_t bobAmountRun  = 0x2C;
    inline static constexpr uintptr_t bobAmountWalk = 0x28;
    inline static constexpr uintptr_t bobSpeedRun   = 0x24;
    inline static constexpr uintptr_t bobSpeedWalk  = 0x20;
    inline static constexpr uintptr_t leftOffsetRun = 0x30;
}
namespace ViewmodelLower {
    inline static constexpr uintptr_t forceLower          = 0x22;
    inline static constexpr uintptr_t lowerOnSprint       = 0x20;
    inline static constexpr uintptr_t lowerScale          = 0x24;
    inline static constexpr uintptr_t lowerWhenCantAttack = 0x21;
    inline static constexpr uintptr_t shouldLower         = 0x28;
}
namespace ViewmodelPunch {
    inline static constexpr uintptr_t punchDuration  = 0x34;
    inline static constexpr uintptr_t punchMagnitude = 0x38;
    inline static constexpr uintptr_t punchStartTime = 0x3C;
}
namespace ViewmodelSway {
    inline static constexpr uintptr_t positionalSwayAmount = 0x24;
    inline static constexpr uintptr_t positionalSwaySpeed  = 0x20;
    inline static constexpr uintptr_t rotateAmountTest     = 0x30;
    inline static constexpr uintptr_t rotationSwayAmount   = 0x2C;
    inline static constexpr uintptr_t rotationSwaySpeed    = 0x28;
}
namespace WorldItem {
    inline static constexpr uintptr_t Item        = 0x210;
    inline static constexpr uintptr_t allowPickup = 0x208;
}

}

