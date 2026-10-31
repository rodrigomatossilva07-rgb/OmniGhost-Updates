use std::collections::BTreeMap;

use anyhow::Result;

use log::{debug, error, warn};

use memflow::prelude::v1::*;

use pelite::pattern;
use pelite::pattern::{Atom, save_len};
use pelite::pe64::{Pe, PeView, Rva};

use phf::{Map, phf_map};

pub type OffsetMap = BTreeMap<String, BTreeMap<String, Rva>>;

macro_rules! pattern_map {
    ($($module:ident => {
        $($name:expr => $pattern:expr $(=> $callback:expr)?),+ $(,)?
    }),+ $(,)?) => {
        $(
            mod $module {
                use super::*;

                pub(super) const PATTERNS: Map<
                    &'static str,
                    (
                        &'static [Atom],
                        Option<fn(&PeView, &mut BTreeMap<String, Rva>, Rva)>,
                    ),
                > = phf_map! {
                    $($name => ($pattern, $($callback)?)),+
                };

                pub fn offsets(view: PeView<'_>) -> BTreeMap<String, Rva> {
                    let mut map = BTreeMap::new();

                    for (&name, (pat, callback)) in &PATTERNS {
                        let mut save = vec![0; save_len(pat)];

                        if !view.scanner().finds_code(pat, &mut save) {
                            error!("outdated pattern: {}", name);

                            continue;
                        }

                        let rva = save[1];

                        map.insert(name.to_string(), rva);

                        if let Some(callback) = callback {
                            callback(&view, &mut map, rva);
                        }
                    }

                    for (name, value) in &map {
                        debug!(
                            "found \"{}\" at {:#X} ({}.dll + {:#X})",
                            name,
                            *value as u64 + view.optional_header().ImageBase,
                            stringify!($module),
                            value
                        );
                    }

                    map
                }
            }
        )+
    };
}

// Extended IDA signatures adapted from the cs2-sdk pattern export
// (https://cs2-sdk.com). Only entries that resolve a data/global RVA are part
// of OffsetMap; function-entry signatures are deliberately not mislabeled as
// offsets.
pattern_map! {
    animationsystem => {
        "pAnimationSystemUtils" => pattern!("488d05${'} c3 cccccccccccccccc 4883ec28 488bca 488d15") => None,
    },
    client => {
        // Function RVAs requested by OmniGhost. The leading save atom captures
        // the start of the matched function; embedded wildcards are not
        // interpreted as global pointers.
        "GetLocalPawn" => pattern!("' 4883ec? 83f9ff 75? 488b0d???? 488d5424? 488b01 ff90???????? 8b08 4863c1") => None,
        "C_CSPlayerPawn_GetAimPunch" => pattern!("' 48895c2408 55 56 57 488d6c24b9 4881ec") => None,
        "GetViewAngles" => pattern!("' 4c8bc1 85d2 7408 488d05???? c3 8b") => None,
        "SetViewAngles" => pattern!("' 85d2 753d 486381500b0000 f2410f1000") => None,
        "dwCSGOInput" => pattern!("488905${'} 0f57c0 0f1105") => Some(|view, map, rva| {
            let mut save = [0; 2];

            if view.scanner().finds_code(pattern!("f2420f108428u4"), &mut save) {
                map.insert("dwViewAngles".to_string(), rva + save[1]);
            }
        }),
        "dwEntityList" => pattern!("48890d${'} e9${} cc") => None,
        "dwGameEntitySystem" => pattern!("488b1d${'} 48891d[4] 4c63b3") => None,
        "dwGameEntitySystem_highestEntityIndex" => pattern!("ff81u4 4885d2") => None,
        "dwGameRules" => pattern!("f6c1010f85${} 4c8b05${'} 4d85") => None,
        "dwGlobalVars" => pattern!("488915${'} 488942") => None,
        "dwGlowManager" => pattern!("488b05${'} c3 cccccccccccccccc 8b41") => None,
        "dwLocalPlayerController" => pattern!("488b05${'} 4189be") => None,
        "dwPlantedC4" => pattern!("488b1d${'} 4532f6") => None,
        "dwPrediction" => pattern!("488d05${'} c3 cccccccccccccccc 405356 4154") => Some(|view, map, rva| {
            let mut save = [0; 2];

            if view.scanner().finds_code(pattern!("4c39b6u4 74? 4488be"), &mut save) {
                map.insert("dwLocalPlayerPawn".to_string(), rva + save[1]);
            }
        }),
        "dwSensitivity" => pattern!("488d0d${'} 0f57c9 0f28f0") => Some(|_view, map, _rva| {
            map.insert("dwSensitivity_sensitivity".to_string(), 0x58);
        }),
        "dwViewMatrix" => pattern!("488d0d${'} 48c1e006") => None,
        "dwViewRender" => pattern!("488905${'} 488bc8 4885c0") => None,
        "dwWeaponC4" => pattern!("488b15${'} 488b5c24? ffc0 8905${} 488bc6 488934ea 80be") => None,
        // Extended cs2-sdk globals. These retain their upstream p... names so
        // consumers can distinguish them from the stable dw... compatibility
        // surface above.
        "pCSGOInputInstance" => pattern!("488905${'} 0f57c0 0f1105") => None,
        "pCSInventoryManager" => pattern!("488b05${'} 4c8b80") => None,
        "pClientMode" => pattern!("488d0d${'} 4869c0???? 4803c1 c3 cc cc") => None,
        "pDynamicLightManager" => pattern!("488b0d${'} f7da e8") => None,
        "pEconItemSystem" => pattern!("488b1d${'} 4d8bf5") => None,
        "pEntitySystem" => pattern!("488b0d${'} 48897c2460 8bfb") => None,
        "pGameEntitySystem" => pattern!("488b1d${'} 48891d${}") => None,
        "pGameEventManager" => pattern!("483905${'} 48890d") => None,
        "pGameRules" => pattern!("488b1d${'} 488d5424? 0f28d0 488d4c24?") => None,
        "pGameTraceManager" => pattern!("488b0d${'} 488bd0 c74424?04000000 48c74424?01301c00") => None,
        "pGlobalVariables" => pattern!("488915${'} 488942") => None,
        "pGlowManager" => pattern!("488b05${'} c3 cccccccccccccccc 8b41") => None,
        "pHudPanel" => pattern!("488935${'} e8${} 4885") => None,
        "pInterpolationPhase" => pattern!("488b05${'} 413bc7") => None,
        "pInventoryCategoryTree" => pattern!("488b15${'} 488bcd") => None,
        "pLocalPlayerController" => pattern!("488b05${'} 4189be") => None,
        "pMainMenuPanel" => pattern!("4c8b05${'} 83e902") => None,
        "pMatchFramework" => pattern!("488b0d${'} 48897db0") => None,
        "pMatchmakingReservation" => pattern!("488b0d${'} 4c893d") => None,
        "pMaterialManager" => pattern!("488b0d${'} 806507ef") => None,
        "pParticleManager" => pattern!("488b0d${'} 41b8???? f30f117424? 48c74424?????") => None,
        "pPlantedC4s" => pattern!("48392d${'} 75? 4989ae") => None,
        "pPrediction" => pattern!("488d05${'} c3 cccccccccccccccc 405356 4154") => None,
        "pPredictionPlayer" => pattern!("4c8935${'} 83fd03") => None,
        "pPredictionRandomSeed" => pattern!("8b3d${'} 488b03 488bcb") => None,
        "pRenderGameSystem" => pattern!("488b0d${'} 0fb69f") => None,
        "pSensitivity" => pattern!("488d0d${'} 0f57c9 0f28f0") => None,
        "pUIInventory" => pattern!("48891d${'} 4885ff") => None,
        "pUiEngine" => pattern!("488978? 48890d${'}") => None,
        "pViewRender" => pattern!("488905${'} 488bc8 4885c0") => None,
        "pViewToProjectionMatrix" => pattern!("48894c24? 4c8d0d${'} 488b0d") => None,
        "pWeaponC4" => pattern!("488b15${'} 488b5c24? ffc0 8905${} 488bc6 488934ea 80be") => None,
        "pWeaponRecoilTable" => pattern!("4c8d05${'} 66893b") => None,
        "pWorldToProjectionMatrix" => pattern!("488d0d${'} 48c1e006") => None,
    },
    engine2 => {
        "dwBuildNumber" => pattern!("8905${'} 488d0d${} ff15${} 488b0d") => None,
        "dwNetworkGameClient" => pattern!("48893d${'} ff87") => None,
        "dwNetworkGameClient_clientTickCount" => pattern!("8b81u4 c3 cccccccccccccccccc 8b81${} c3 cccccccccccccccccc 83b9") => None,
        "dwNetworkGameClient_deltaTick" => pattern!("4c8db7u4 4c897c24") => None,
        "dwNetworkGameClient_isBackgroundMap" => pattern!("0fb681u4 c3 cccccccccccccccc 0fb681${} c3 cccccccccccccccc 4883ec") => None,
        "dwNetworkGameClient_localPlayer" => pattern!("428b94d3u4 5b 49ffe3 32c0 5b c3 cccccccccccccccc 4053") => None,
        "dwNetworkGameClient_maxClients" => pattern!("8b81u4 c3????????? 8b81[4] c3????????? 8b81") => None,
        "dwNetworkGameClient_serverTickCount" => pattern!("8b81u4 c3 cccccccccccccccccc 83b9") => None,
        "dwNetworkGameClient_signOnState" => pattern!("448b81u4 488d0d") => None,
        "dwWindowHeight" => pattern!("8b05${'} 8903") => None,
        "dwWindowWidth" => pattern!("8b05${'} 8907") => None,
        "pBuildNumber" => pattern!("8905${'} 488d0d${} ff15${} 488b0d") => None,
        "pFrameInputRing" => pattern!("488d05${'} 0f1004c8") => None,
        "pFrameInputRingIndex" => pattern!("486315${'} 83fa0a") => None,
        "pNetworkGameClient" => pattern!("48893d${'} ff87") => None,
        "pPVSManager" => pattern!("488d0d${'} 33d2 ff50") => None,
        "pWindowHeight" => pattern!("8b05${'} 8903") => None,
        "pWindowWidth" => pattern!("8b05${'} 8907") => None,
    },
    input_system => {
        "dwInputSystem" => pattern!("488905${'} 33c0") => None,
        "pInputSystemSvc" => pattern!("488d05${'} c3 cccccccccccccccc 4053 4883ec20 33db") => None,
    },
    matchmaking => {
        "dwGameTypes" => pattern!("488d0d${'} ff90") => None,
    },
    networksystem => {
        "pNetworkSystem" => pattern!("488d0d${'} 488bd7 e8${} eb") => None,
    },
    scenesystem => {
        "pSceneLightDataQueue" => pattern!("488b05${'} 4c63d3") => None,
        "pSceneSystem" => pattern!("4c8d05${'} 4863c6") => None,
    },
    schemasystem => {
        "pSchemaSystem" => pattern!("488d05${'} c3 cccccccccccccccc 48895c2408 488974") => None,
    },
    soundsystem => {
        "dwSoundSystem" => pattern!("488d0d${'} e8${} 488b0d${} [3] 4c8b82") => Some(|_view, map, rva| {
            map.insert("pSoundSystem".to_string(), rva);
        }),
        "dwSoundSystem_engineViewData" => pattern!("0f1147u1 0f104e? 0f118f") => None,
        "pSoundChannels" => pattern!("488b15${'} 85c9 78") => None,
    },
    tier0 => {
        "pCVar" => pattern!("488d05${'} c3 cccccccccccccccc e9") => None,
        "pCvarRegistry" => pattern!("488b0d${'} 4885c9 74? 488b01 ff9048010000") => None,
    },
}

pub fn offsets<P: Process + MemoryView>(process: &mut P) -> Result<OffsetMap> {
    let mut map = BTreeMap::new();

    let modules: [(&str, fn(PeView) -> BTreeMap<String, u32>); 10] = [
        ("animationsystem.dll", animationsystem::offsets),
        ("client.dll", client::offsets),
        ("engine2.dll", engine2::offsets),
        ("inputsystem.dll", input_system::offsets),
        ("matchmaking.dll", matchmaking::offsets),
        ("networksystem.dll", networksystem::offsets),
        ("scenesystem.dll", scenesystem::offsets),
        ("schemasystem.dll", schemasystem::offsets),
        ("soundsystem.dll", soundsystem::offsets),
        ("tier0.dll", tier0::offsets),
    ];

    for (module_name, offsets) in &modules {
        let module = match process.module_by_name(module_name) {
            Ok(module) => module,
            Err(err) => {
                warn!("skipping unavailable module {}: {}", module_name, err);
                continue;
            }
        };

        let buf = match process.read_raw(module.base, module.size as _).data_part() {
            Ok(buf) => buf,
            Err(err) => {
                warn!("failed to read module {}: {}", module_name, err);
                continue;
            }
        };

        let view = match PeView::from_bytes(&buf) {
            Ok(view) => view,
            Err(err) => {
                warn!("failed to parse module {} as PE: {}", module_name, err);
                continue;
            }
        };

        map.insert(module_name.to_string(), offsets(view));
    }

    Ok(map)
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::sync::Once;

    use serde_json::Value;

    use simplelog::*;

    use super::*;

    #[test]
    fn extended_pattern_catalog_is_well_formed() {
        let catalogs = [
            ("animationsystem.dll", &animationsystem::PATTERNS),
            ("client.dll", &client::PATTERNS),
            ("engine2.dll", &engine2::PATTERNS),
            ("inputsystem.dll", &input_system::PATTERNS),
            ("matchmaking.dll", &matchmaking::PATTERNS),
            ("networksystem.dll", &networksystem::PATTERNS),
            ("scenesystem.dll", &scenesystem::PATTERNS),
            ("schemasystem.dll", &schemasystem::PATTERNS),
            ("soundsystem.dll", &soundsystem::PATTERNS),
            ("tier0.dll", &tier0::PATTERNS),
        ];

        for (module, catalog) in catalogs {
            assert!(!catalog.is_empty(), "{} has no patterns", module);

            for (name, (pattern, _)) in catalog {
                assert!(
                    save_len(pattern) >= 2,
                    "{}.{} does not capture a resolved RVA",
                    module,
                    name
                );
            }
        }

        assert!(client::PATTERNS.contains_key("pGameTraceManager"));
        assert!(client::PATTERNS.contains_key("pWorldToProjectionMatrix"));
        assert!(client::PATTERNS.contains_key("GetLocalPawn"));
        assert!(client::PATTERNS.contains_key("C_CSPlayerPawn_GetAimPunch"));
        assert!(client::PATTERNS.contains_key("GetViewAngles"));
        assert!(client::PATTERNS.contains_key("SetViewAngles"));
        assert!(engine2::PATTERNS.contains_key("pFrameInputRing"));
        assert!(scenesystem::PATTERNS.contains_key("pSceneSystem"));
        assert!(tier0::PATTERNS.contains_key("pCvarRegistry"));
    }

    #[test]
    fn build_number() -> Result<()> {
        let mut process = setup()?;

        let engine_base = process.module_by_name("engine2.dll")?.base;

        let offset = read_offset("engine2.dll", "dwBuildNumber").unwrap();

        let build_number: u32 = process.read(engine_base + offset).data_part()?;

        debug!("build number: {}", build_number);

        Ok(())
    }

    #[test]
    fn global_vars() -> Result<()> {
        let mut process = setup()?;

        let client_base = process.module_by_name("client.dll")?.base;

        let offset = read_offset("client.dll", "dwGlobalVars").unwrap();

        let global_vars: u64 = process.read(client_base + offset).data_part()?;

        let map_name_addr = process
            .read_addr64((global_vars + 0x180).into())
            .data_part()?;

        let map_name = process.read_utf8(map_name_addr, 128).data_part()?;

        debug!("[global vars] map name: \"{}\"", map_name);

        Ok(())
    }

    #[test]
    fn local_controller() -> Result<()> {
        let mut process = setup()?;

        let client_base = process.module_by_name("client.dll")?.base;

        let local_controller_offset = read_offset("client.dll", "dwLocalPlayerController").unwrap();

        let player_name_offset =
            read_class_field("client.dll", "CBasePlayerController", "m_iszPlayerName").unwrap();

        let local_controller: u64 = process
            .read(client_base + local_controller_offset)
            .data_part()?;

        let player_name = process
            .read_utf8((local_controller + player_name_offset).into(), 128)
            .data_part()?;

        debug!("[local controller] name: \"{}\"", player_name);

        Ok(())
    }

    #[test]
    fn local_pawn() -> Result<()> {
        #[derive(Pod)]
        #[repr(C)]
        struct Vector3D {
            x: f32,
            y: f32,
            z: f32,
        }

        let mut process = setup()?;

        let client_base = process.module_by_name("client.dll")?.base;

        let local_player_pawn_offset = read_offset("client.dll", "dwLocalPlayerPawn").unwrap();

        let game_scene_node_offset =
            read_class_field("client.dll", "C_BaseEntity", "m_pGameSceneNode").unwrap();

        let origin_offset =
            read_class_field("client.dll", "CGameSceneNode", "m_vecAbsOrigin").unwrap();

        let local_player_pawn: u64 = process
            .read(client_base + local_player_pawn_offset)
            .data_part()?;

        let game_scene_node: u64 = process
            .read((local_player_pawn + game_scene_node_offset).into())
            .data_part()?;

        let origin: Vector3D = process
            .read((game_scene_node + origin_offset).into())
            .data_part()?;

        debug!(
            "[local pawn] origin: {:.2}, y: {:.2}, z: {:.2}",
            origin.x, origin.y, origin.z
        );

        Ok(())
    }

    #[test]
    fn window_size() -> Result<()> {
        let mut process = setup()?;

        let engine_base = process.module_by_name("engine2.dll")?.base;

        let window_width_offset = read_offset("engine2.dll", "dwWindowWidth").unwrap();
        let window_height_offset = read_offset("engine2.dll", "dwWindowHeight").unwrap();

        let window_width: u32 = process
            .read(engine_base + window_width_offset)
            .data_part()?;

        let window_height: u32 = process
            .read(engine_base + window_height_offset)
            .data_part()?;

        debug!("window size: {}x{}", window_width, window_height);

        Ok(())
    }

    fn setup() -> Result<IntoProcessInstanceArcBox<'static>> {
        static LOGGER: Once = Once::new();

        LOGGER.call_once(|| {
            SimpleLogger::init(LevelFilter::Trace, Config::default()).ok();
        });

        let os = memflow_native::create_os(&OsArgs::default(), LibArc::default())?;

        let process = os.into_process_by_name("cs2.exe")?;

        Ok(process)
    }

    fn read_class_field(module_name: &str, class_name: &str, field_name: &str) -> Option<u64> {
        let content =
            fs::read_to_string(format!("output/{}.json", module_name.replace(".", "_"))).ok()?;

        let value: Value = serde_json::from_str(&content).ok()?;

        value
            .get(module_name)?
            .get("classes")?
            .get(class_name)?
            .get("fields")?
            .get(field_name)?
            .as_u64()
    }

    fn read_offset(module_name: &str, offset_name: &str) -> Option<u64> {
        let content = fs::read_to_string("output/offsets.json").ok()?;
        let value: Value = serde_json::from_str(&content).ok()?;

        let offset = value.get(module_name)?.get(offset_name)?;

        offset.as_u64()
    }
}
