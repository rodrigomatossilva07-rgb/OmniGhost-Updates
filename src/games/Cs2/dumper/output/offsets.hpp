// Generated using https://github.com/a2x/cs2-dumper
// 2026-10-31 22:03:44.491654800 UTC

#pragma once

#include <cstddef>
#include <cstdint>

namespace cs2_dumper {
    namespace offsets {
        // Module: animationsystem.dll
        namespace animationsystem_dll {
            constexpr std::ptrdiff_t pAnimationSystemUtils = 0x83F6B8;
        }
        // Module: client.dll
        namespace client_dll {
            constexpr std::ptrdiff_t C_CSPlayerPawn_GetAimPunch = 0x854500;
            constexpr std::ptrdiff_t GetViewAngles = 0xB728B0;
            constexpr std::ptrdiff_t SetViewAngles = 0xB82710;
            constexpr std::ptrdiff_t dwCSGOInput = 0x257F980;
            constexpr std::ptrdiff_t dwEntityList = 0x271EB28;
            constexpr std::ptrdiff_t dwGameEntitySystem = 0x271EB28;
            constexpr std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x2120;
            constexpr std::ptrdiff_t dwGameRules = 0x2569378;
            constexpr std::ptrdiff_t dwGlobalVars = 0x2234108;
            constexpr std::ptrdiff_t dwGlowManager = 0x25660A0;
            constexpr std::ptrdiff_t dwLocalPlayerController = 0x25413F8;
            constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x25698C8;
            constexpr std::ptrdiff_t dwPlantedC4 = 0x24CFB60;
            constexpr std::ptrdiff_t dwPrediction = 0x25697D0;
            constexpr std::ptrdiff_t dwSensitivity = 0x2566BC0;
            constexpr std::ptrdiff_t dwSensitivity_sensitivity = 0x58;
            constexpr std::ptrdiff_t dwViewAngles = 0x2580008;
            constexpr std::ptrdiff_t dwViewMatrix = 0x256F1D0;
            constexpr std::ptrdiff_t dwViewRender = 0x256FB98;
            constexpr std::ptrdiff_t dwWeaponC4 = 0x24CE108;
            constexpr std::ptrdiff_t pCSGOInputInstance = 0x257F980;
            constexpr std::ptrdiff_t pCSInventoryManager = 0x24DCCB0;
            constexpr std::ptrdiff_t pClientMode = 0x257D6E0;
            constexpr std::ptrdiff_t pDynamicLightManager = 0x255DD28;
            constexpr std::ptrdiff_t pEconItemSystem = 0x25F83E0;
            constexpr std::ptrdiff_t pEntitySystem = 0x23BFA00;
            constexpr std::ptrdiff_t pGameEntitySystem = 0x271EB28;
            constexpr std::ptrdiff_t pGameEventManager = 0x254FE00;
            constexpr std::ptrdiff_t pGameRules = 0x2569378;
            constexpr std::ptrdiff_t pGameTraceManager = 0x222AFA8;
            constexpr std::ptrdiff_t pGlobalVariables = 0x2234108;
            constexpr std::ptrdiff_t pGlowManager = 0x25660A0;
            constexpr std::ptrdiff_t pHudPanel = 0x25DA648;
            constexpr std::ptrdiff_t pInterpolationPhase = 0x21C30A0;
            constexpr std::ptrdiff_t pInventoryCategoryTree = 0x25F7130;
            constexpr std::ptrdiff_t pLocalPlayerController = 0x25413F8;
            constexpr std::ptrdiff_t pMainMenuPanel = 0x25CD8C8;
            constexpr std::ptrdiff_t pMatchFramework = 0x27A1090;
            constexpr std::ptrdiff_t pMatchmakingReservation = 0x2580E88;
            constexpr std::ptrdiff_t pMaterialManager = 0x27A11D0;
            constexpr std::ptrdiff_t pParticleManager = 0x222B248;
            constexpr std::ptrdiff_t pPlantedC4s = 0x24CFB60;
            constexpr std::ptrdiff_t pPrediction = 0x25697D0;
            constexpr std::ptrdiff_t pPredictionPlayer = 0x255AA50;
            constexpr std::ptrdiff_t pPredictionRandomSeed = 0x21C6E90;
            constexpr std::ptrdiff_t pRenderGameSystem = 0x23C1240;
            constexpr std::ptrdiff_t pSensitivity = 0x2566BC0;
            constexpr std::ptrdiff_t pUIInventory = 0x252F7A0;
            constexpr std::ptrdiff_t pUiEngine = 0x2738160;
            constexpr std::ptrdiff_t pViewRender = 0x256FB98;
            constexpr std::ptrdiff_t pViewToProjectionMatrix = 0x256F190;
            constexpr std::ptrdiff_t pWeaponC4 = 0x24CE108;
            constexpr std::ptrdiff_t pWorldToProjectionMatrix = 0x256F1D0;
        }
        // Module: engine2.dll
        namespace engine2_dll {
            constexpr std::ptrdiff_t dwBuildNumber = 0x6BA028;
            constexpr std::ptrdiff_t dwNetworkGameClient = 0x9B88C0;
            constexpr std::ptrdiff_t dwNetworkGameClient_clientTickCount = 0x398;
            constexpr std::ptrdiff_t dwNetworkGameClient_deltaTick = 0x24C;
            constexpr std::ptrdiff_t dwNetworkGameClient_isBackgroundMap = 0x2C143F;
            constexpr std::ptrdiff_t dwNetworkGameClient_localPlayer = 0xF8;
            constexpr std::ptrdiff_t dwNetworkGameClient_maxClients = 0x240;
            constexpr std::ptrdiff_t dwNetworkGameClient_serverTickCount = 0x24C;
            constexpr std::ptrdiff_t dwNetworkGameClient_signOnState = 0x230;
            constexpr std::ptrdiff_t dwWindowHeight = 0x9BCFEC;
            constexpr std::ptrdiff_t dwWindowWidth = 0x9BCFE8;
            constexpr std::ptrdiff_t pBuildNumber = 0x6BA028;
            constexpr std::ptrdiff_t pFrameInputRing = 0x9BACB0;
            constexpr std::ptrdiff_t pFrameInputRingIndex = 0x9BA99C;
            constexpr std::ptrdiff_t pNetworkGameClient = 0x9B88C0;
            constexpr std::ptrdiff_t pPVSManager = 0x6C0400;
            constexpr std::ptrdiff_t pWindowHeight = 0x9BCFEC;
            constexpr std::ptrdiff_t pWindowWidth = 0x9BCFE8;
        }
        // Module: inputsystem.dll
        namespace inputsystem_dll {
            constexpr std::ptrdiff_t dwInputSystem = 0x46BC0;
            constexpr std::ptrdiff_t pInputSystemSvc = 0x46BC0;
        }
        // Module: matchmaking.dll
        namespace matchmaking_dll {
            constexpr std::ptrdiff_t dwGameTypes = 0x1B0FD0;
        }
        // Module: networksystem.dll
        namespace networksystem_dll {
            constexpr std::ptrdiff_t pNetworkSystem = 0x2911A0;
        }
        // Module: scenesystem.dll
        namespace scenesystem_dll {
            constexpr std::ptrdiff_t pSceneLightDataQueue = 0x921A28;
            constexpr std::ptrdiff_t pSceneSystem = 0x91FB20;
        }
        // Module: schemasystem.dll
        namespace schemasystem_dll {
            constexpr std::ptrdiff_t pSchemaSystem = 0x76710;
        }
        // Module: soundsystem.dll
        namespace soundsystem_dll {
            constexpr std::ptrdiff_t dwSoundSystem = 0x532420;
            constexpr std::ptrdiff_t pSoundSystem = 0x532420;
        }
        // Module: tier0.dll
        namespace tier0_dll {
            constexpr std::ptrdiff_t pCVar = 0x3AC670;
            constexpr std::ptrdiff_t pCvarRegistry = 0x3A9568;
        }
    }
}
