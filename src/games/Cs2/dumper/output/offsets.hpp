// Generated using https://github.com/a2x/cs2-dumper
// 2026-11-07 22:05:22.994283500 UTC

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
            constexpr std::ptrdiff_t C_CSPlayerPawn_GetAimPunch = 0x854680;
            constexpr std::ptrdiff_t GetViewAngles = 0xB72A30;
            constexpr std::ptrdiff_t SetViewAngles = 0xB82890;
            constexpr std::ptrdiff_t dwCSGOInput = 0x2580A10;
            constexpr std::ptrdiff_t dwEntityList = 0x271FBB8;
            constexpr std::ptrdiff_t dwGameEntitySystem = 0x271FBB8;
            constexpr std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0x2120;
            constexpr std::ptrdiff_t dwGameRules = 0x256A3F8;
            constexpr std::ptrdiff_t dwGlobalVars = 0x2235108;
            constexpr std::ptrdiff_t dwGlowManager = 0x2567120;
            constexpr std::ptrdiff_t dwLocalPlayerController = 0x2542548;
            constexpr std::ptrdiff_t dwLocalPlayerPawn = 0x256A948;
            constexpr std::ptrdiff_t dwPlantedC4 = 0x24D0B70;
            constexpr std::ptrdiff_t dwPrediction = 0x256A850;
            constexpr std::ptrdiff_t dwSensitivity = 0x2567C40;
            constexpr std::ptrdiff_t dwSensitivity_sensitivity = 0x58;
            constexpr std::ptrdiff_t dwViewAngles = 0x2581098;
            constexpr std::ptrdiff_t dwViewMatrix = 0x2570250;
            constexpr std::ptrdiff_t dwViewRender = 0x2570C18;
            constexpr std::ptrdiff_t dwWeaponC4 = 0x24CF118;
            constexpr std::ptrdiff_t pCSGOInputInstance = 0x2580A10;
            constexpr std::ptrdiff_t pCSInventoryManager = 0x24DDCD0;
            constexpr std::ptrdiff_t pClientMode = 0x257E770;
            constexpr std::ptrdiff_t pDynamicLightManager = 0x255ED38;
            constexpr std::ptrdiff_t pEconItemSystem = 0x25F9470;
            constexpr std::ptrdiff_t pEntitySystem = 0x23C0A10;
            constexpr std::ptrdiff_t pGameEntitySystem = 0x271FBB8;
            constexpr std::ptrdiff_t pGameEventManager = 0x2550E00;
            constexpr std::ptrdiff_t pGameRules = 0x256A3F8;
            constexpr std::ptrdiff_t pGameTraceManager = 0x222BFA8;
            constexpr std::ptrdiff_t pGlobalVariables = 0x2235108;
            constexpr std::ptrdiff_t pGlowManager = 0x2567120;
            constexpr std::ptrdiff_t pHudPanel = 0x25DB6C8;
            constexpr std::ptrdiff_t pInterpolationPhase = 0x21C40A0;
            constexpr std::ptrdiff_t pInventoryCategoryTree = 0x25F81C0;
            constexpr std::ptrdiff_t pLocalPlayerController = 0x2542548;
            constexpr std::ptrdiff_t pMainMenuPanel = 0x25CE948;
            constexpr std::ptrdiff_t pMatchFramework = 0x27A2120;
            constexpr std::ptrdiff_t pMatchmakingReservation = 0x257DAB0;
            constexpr std::ptrdiff_t pMaterialManager = 0x27A2260;
            constexpr std::ptrdiff_t pParticleManager = 0x222C248;
            constexpr std::ptrdiff_t pPlantedC4s = 0x24D0B70;
            constexpr std::ptrdiff_t pPrediction = 0x256A850;
            constexpr std::ptrdiff_t pPredictionPlayer = 0x255BA58;
            constexpr std::ptrdiff_t pPredictionRandomSeed = 0x21C7E90;
            constexpr std::ptrdiff_t pRenderGameSystem = 0x23C2250;
            constexpr std::ptrdiff_t pSensitivity = 0x2567C40;
            constexpr std::ptrdiff_t pUIInventory = 0x25307C0;
            constexpr std::ptrdiff_t pUiEngine = 0x27391E0;
            constexpr std::ptrdiff_t pViewRender = 0x2570C18;
            constexpr std::ptrdiff_t pViewToProjectionMatrix = 0x2570210;
            constexpr std::ptrdiff_t pWeaponC4 = 0x24CF118;
            constexpr std::ptrdiff_t pWorldToProjectionMatrix = 0x2570250;
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
