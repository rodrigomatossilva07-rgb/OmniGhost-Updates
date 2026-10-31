// Generated using https://github.com/a2x/cs2-dumper
// 2026-10-31 22:03:44.491654800 UTC

pub const cs2_dumper = struct {
    pub const offsets = struct {
        // Module: animationsystem.dll
        pub const animationsystem_dll = struct {
            pub const pAnimationSystemUtils: usize = 0x83F6B8;
        };
        // Module: client.dll
        pub const client_dll = struct {
            pub const C_CSPlayerPawn_GetAimPunch: usize = 0x854500;
            pub const GetViewAngles: usize = 0xB728B0;
            pub const SetViewAngles: usize = 0xB82710;
            pub const dwCSGOInput: usize = 0x257F980;
            pub const dwEntityList: usize = 0x271EB28;
            pub const dwGameEntitySystem: usize = 0x271EB28;
            pub const dwGameEntitySystem_highestEntityIndex: usize = 0x2120;
            pub const dwGameRules: usize = 0x2569378;
            pub const dwGlobalVars: usize = 0x2234108;
            pub const dwGlowManager: usize = 0x25660A0;
            pub const dwLocalPlayerController: usize = 0x25413F8;
            pub const dwLocalPlayerPawn: usize = 0x25698C8;
            pub const dwPlantedC4: usize = 0x24CFB60;
            pub const dwPrediction: usize = 0x25697D0;
            pub const dwSensitivity: usize = 0x2566BC0;
            pub const dwSensitivity_sensitivity: usize = 0x58;
            pub const dwViewAngles: usize = 0x2580008;
            pub const dwViewMatrix: usize = 0x256F1D0;
            pub const dwViewRender: usize = 0x256FB98;
            pub const dwWeaponC4: usize = 0x24CE108;
            pub const pCSGOInputInstance: usize = 0x257F980;
            pub const pCSInventoryManager: usize = 0x24DCCB0;
            pub const pClientMode: usize = 0x257D6E0;
            pub const pDynamicLightManager: usize = 0x255DD28;
            pub const pEconItemSystem: usize = 0x25F83E0;
            pub const pEntitySystem: usize = 0x23BFA00;
            pub const pGameEntitySystem: usize = 0x271EB28;
            pub const pGameEventManager: usize = 0x254FE00;
            pub const pGameRules: usize = 0x2569378;
            pub const pGameTraceManager: usize = 0x222AFA8;
            pub const pGlobalVariables: usize = 0x2234108;
            pub const pGlowManager: usize = 0x25660A0;
            pub const pHudPanel: usize = 0x25DA648;
            pub const pInterpolationPhase: usize = 0x21C30A0;
            pub const pInventoryCategoryTree: usize = 0x25F7130;
            pub const pLocalPlayerController: usize = 0x25413F8;
            pub const pMainMenuPanel: usize = 0x25CD8C8;
            pub const pMatchFramework: usize = 0x27A1090;
            pub const pMatchmakingReservation: usize = 0x2580E88;
            pub const pMaterialManager: usize = 0x27A11D0;
            pub const pParticleManager: usize = 0x222B248;
            pub const pPlantedC4s: usize = 0x24CFB60;
            pub const pPrediction: usize = 0x25697D0;
            pub const pPredictionPlayer: usize = 0x255AA50;
            pub const pPredictionRandomSeed: usize = 0x21C6E90;
            pub const pRenderGameSystem: usize = 0x23C1240;
            pub const pSensitivity: usize = 0x2566BC0;
            pub const pUIInventory: usize = 0x252F7A0;
            pub const pUiEngine: usize = 0x2738160;
            pub const pViewRender: usize = 0x256FB98;
            pub const pViewToProjectionMatrix: usize = 0x256F190;
            pub const pWeaponC4: usize = 0x24CE108;
            pub const pWorldToProjectionMatrix: usize = 0x256F1D0;
        };
        // Module: engine2.dll
        pub const engine2_dll = struct {
            pub const dwBuildNumber: usize = 0x6BA028;
            pub const dwNetworkGameClient: usize = 0x9B88C0;
            pub const dwNetworkGameClient_clientTickCount: usize = 0x398;
            pub const dwNetworkGameClient_deltaTick: usize = 0x24C;
            pub const dwNetworkGameClient_isBackgroundMap: usize = 0x2C143F;
            pub const dwNetworkGameClient_localPlayer: usize = 0xF8;
            pub const dwNetworkGameClient_maxClients: usize = 0x240;
            pub const dwNetworkGameClient_serverTickCount: usize = 0x24C;
            pub const dwNetworkGameClient_signOnState: usize = 0x230;
            pub const dwWindowHeight: usize = 0x9BCFEC;
            pub const dwWindowWidth: usize = 0x9BCFE8;
            pub const pBuildNumber: usize = 0x6BA028;
            pub const pFrameInputRing: usize = 0x9BACB0;
            pub const pFrameInputRingIndex: usize = 0x9BA99C;
            pub const pNetworkGameClient: usize = 0x9B88C0;
            pub const pPVSManager: usize = 0x6C0400;
            pub const pWindowHeight: usize = 0x9BCFEC;
            pub const pWindowWidth: usize = 0x9BCFE8;
        };
        // Module: inputsystem.dll
        pub const inputsystem_dll = struct {
            pub const dwInputSystem: usize = 0x46BC0;
            pub const pInputSystemSvc: usize = 0x46BC0;
        };
        // Module: matchmaking.dll
        pub const matchmaking_dll = struct {
            pub const dwGameTypes: usize = 0x1B0FD0;
        };
        // Module: networksystem.dll
        pub const networksystem_dll = struct {
            pub const pNetworkSystem: usize = 0x2911A0;
        };
        // Module: scenesystem.dll
        pub const scenesystem_dll = struct {
            pub const pSceneLightDataQueue: usize = 0x921A28;
            pub const pSceneSystem: usize = 0x91FB20;
        };
        // Module: schemasystem.dll
        pub const schemasystem_dll = struct {
            pub const pSchemaSystem: usize = 0x76710;
        };
        // Module: soundsystem.dll
        pub const soundsystem_dll = struct {
            pub const dwSoundSystem: usize = 0x532420;
            pub const pSoundSystem: usize = 0x532420;
        };
        // Module: tier0.dll
        pub const tier0_dll = struct {
            pub const pCVar: usize = 0x3AC670;
            pub const pCvarRegistry: usize = 0x3A9568;
        };
    };
};
