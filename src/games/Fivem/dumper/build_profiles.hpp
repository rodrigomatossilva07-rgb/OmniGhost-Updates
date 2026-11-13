#pragma once

#include <cstdint>
#include <string_view>

struct BuildOffset {
    int build;
    std::string_view symbol;
    std::uintptr_t rva;
    std::string_view source;
};

// Published build-specific reference RVAs. These are validation anchors, never export fallbacks.
// A value is emitted only after the current executable resolves and validates it.
inline constexpr BuildOffset kBuildOffsets[] = {
    {2060, "world", 0x24C8858, "vgszf/Fivem-External-SilentAim"},
    {2060, "replay_interface", 0x1EC3828, "vgszf/Fivem-External-SilentAim"},
    {2060, "viewport", 0x1F6A7E0, "vgszf/Fivem-External-SilentAim"},
    {2060, "camera", 0x1F6B940, "vgszf/Fivem-External-SilentAim"},
    {2060, "blip_list", 0x1F4F940, "vgszf/Fivem-External-SilentAim"},
    {2189, "world", 0x24E6D90, "vgszf/Fivem-External-SilentAim"},
    {2189, "replay_interface", 0x1EE18A8, "vgszf/Fivem-External-SilentAim"},
    {2189, "viewport", 0x1F888C0, "vgszf/Fivem-External-SilentAim"},
    {2189, "camera", 0x1F89768, "vgszf/Fivem-External-SilentAim"},
    {2189, "blip_list", 0x1F6EF80, "vgszf/Fivem-External-SilentAim"},
    {2372, "world", 0x252DCD8, "vgszf/Fivem-External-SilentAim"},
    {2372, "replay_interface", 0x1F05208, "vgszf/Fivem-External-SilentAim"},
    {2372, "viewport", 0x1F9E9F0, "vgszf/Fivem-External-SilentAim"},
    {2372, "camera", 0x1F9F898, "vgszf/Fivem-External-SilentAim"},
    {2372, "blip_list", 0x1F9FFA0, "vgszf/Fivem-External-SilentAim"},
    {2545, "world", 0x25667E8, "vgszf/Fivem-External-SilentAim"},
    {2545, "replay_interface", 0x1F2E7A8, "vgszf/Fivem-External-SilentAim"},
    {2545, "viewport", 0x1FD6F70, "vgszf/Fivem-External-SilentAim"},
    {2545, "camera", 0x1FD7E18, "vgszf/Fivem-External-SilentAim"},
    {2545, "blip_list", 0x1F4F940, "vgszf/Fivem-External-SilentAim"},
    {2612, "world", 0x2567DB0, "vgszf/Fivem-External-SilentAim"},
    {2612, "replay_interface", 0x1F77EF0, "vgszf/Fivem-External-SilentAim"},
    {2612, "viewport", 0x1FD8570, "vgszf/Fivem-External-SilentAim"},
    {2612, "camera", 0x1FD9418, "vgszf/Fivem-External-SilentAim"},
    {2612, "blip_list", 0x1FDDD20, "vgszf/Fivem-External-SilentAim"},
    {2699, "world", 0x26684D8, "vgszf/Fivem-External-SilentAim"},
    {2699, "replay_interface", 0x20304C8, "vgszf/Fivem-External-SilentAim"},
    {2699, "viewport", 0x20D8C90, "vgszf/Fivem-External-SilentAim"},
    {2699, "camera", 0x20D9B38, "vgszf/Fivem-External-SilentAim"},
    {2699, "blip_list", 0x20E1420, "vgszf/Fivem-External-SilentAim"},
    {2802, "world", 0x254D448, "vgszf/Fivem-External-SilentAim"},
    {2802, "replay_interface", 0x1F5B820, "vgszf/Fivem-External-SilentAim"},
    {2802, "viewport", 0x1FBC100, "vgszf/Fivem-External-SilentAim"},
    {2802, "camera", 0x1FBCFA8, "vgszf/Fivem-External-SilentAim"},
    {2802, "blip_list", 0x1FBD6E0, "vgszf/Fivem-External-SilentAim"},
    {2802, "player_info", 0x10A8, "OmniGhost validated field profile"},
    {2944, "world", 0x257BEA0, "vgszf/Fivem-External-SilentAim"},
    {2944, "replay_interface", 0x1F42068, "vgszf/Fivem-External-SilentAim"},
    {2944, "viewport", 0x1FEAAC0, "vgszf/Fivem-External-SilentAim"},
    {2944, "camera", 0x1FEB968, "vgszf/Fivem-External-SilentAim"},
    {2944, "blip_list", 0x1FF3130, "vgszf/Fivem-External-SilentAim"},
    {2944, "player_info", 0x10A8, "OmniGhost validated field profile"},
    {3095, "world", 0x2593320, "vgszf/Fivem-External-SilentAim"},
    {3095, "replay_interface", 0x1F58B58, "vgszf/Fivem-External-SilentAim"},
    {3095, "viewport", 0x20019E0, "vgszf/Fivem-External-SilentAim"},
    {3095, "camera", 0x2002888, "vgszf/Fivem-External-SilentAim"},
    {3095, "blip_list", 0x2002FA0, "vgszf/Fivem-External-SilentAim"},
    {3095, "player_info", 0x10A8, "OmniGhost validated field profile"},
    {3258, "world", 0x25B14B0, "CheatOffsets b3258 dump"},
    {3258, "viewport", 0x201DBA0, "CheatOffsets b3258 dump"},
    {3258, "camera", 0x201E7D0, "CheatOffsets b3258 dump"},
    {3258, "camera_gameplay_director", 0x201ED50, "CheatOffsets b3258 dump"},
    {3258, "replay_interface", 0x1FBD4F0, "CheatOffsets b3258 dump"},
    {3258, "ped_pool", 0x25B1758, "CheatOffsets b3258 dump"},
    {3258, "vehicle_pool", 0x2F23F78, "CheatOffsets b3258 dump"},
    {3258, "network_player_manager", 0x2244218, "CheatOffsets b3258 dump"},
    {3258, "network_object_manager", 0x2982050, "CheatOffsets b3258 dump"},
    {3258, "blip_list", 0x2023400, "CheatOffsets b3258 dump"},
    {3258, "swapchain", 0x2D2CAA0, "CheatOffsets b3258 dump"},
    {3258, "aim_cped", 0x1F73508, "CheatOffsets b3258 dump"},
    {3258, "player_names", 0x1E63C68, "CheatOffsets b3258 dump"},
    {3258, "sky_settings", 0x2721250, "CheatOffsets b3258 dump"},
    {3258, "game_build_string", 0x22295B0, "CheatOffsets b3258 dump"},
    {3258, "raycast_xword", 0x2CFA4E0, "CheatOffsets b3258 dump"},
    {3258, "streamable_texture_dictionary", 0x26671F0, "CheatOffsets b3258 dump"},
    {3258, "session_started", 0x1F74BD8, "CheatOffsets b3258 dump"},
    {3258, "player_info", 0x10A8, "OmniGhost validated field profile"},
    {3751, "player_info", 0x10A8, "OmniGhost validated field profile"},
    {3788, "player_info", 0x10A8, "OmniGhost validated field profile"},
    {3889, "world", 0x2503A58, "OmniGhost runtime semantic validation"},
    {3889, "replay_interface", 0x1EC2BB8, "OmniGhost runtime semantic validation"},
    {3889, "viewport", 0x1F6B320, "OmniGhost runtime semantic validation"},
    {3889, "camera", 0x1F6C480, "OmniGhost runtime semantic validation"},
    {3889, "player_info", 0x10A8, "OmniGhost runtime semantic validation"},
};

inline const BuildOffset* FindBuildOffset(int build, std::string_view symbol) {
    for (const auto& entry : kBuildOffsets)
        if (entry.build == build && entry.symbol == symbol) return &entry;
    return nullptr;
}
