#pragma once

#include <array>
#include <cstddef>

enum class ResolveKind { RipRelative, MatchAddress };

struct PatternCandidate {
    const char* symbol;
    const char* id;
    const char* pattern;
    ResolveKind resolve;
    std::size_t displacement_offset;
    std::size_t instruction_length;
    int dereference_count;
    std::ptrdiff_t addend;
    int minimum_build;
    int maximum_build;
    const char* source;
};

// Community candidates are deliberately kept as candidates until runtime validation succeeds.
// Sources:
// - UnknownCheats thread 759172 (FiveM Signature Pattern List V2)
// - syslink47/fivem-offset-dumper- (MIT), signatures.h
// - vgszf/Fivem-External-SilentAim, Fivem.hpp (cross-reference for core patterns)
// Legacy and Enhanced must never share a candidate inventory implicitly.  The
// Enhanced list is intentionally empty until patterns are captured and
// validated against an Enhanced executable.
inline constexpr PatternCandidate kLegacyPatternCandidates[] = {
    // b3258 patterns generated from unique RIP references in the user's running module.
    {"world", "world_b3258", "48 8B 0D ? ? ? ? 8B C3 48 85 C9 74 0E 48 39 59 08 0F 95", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"viewport", "viewport_b3258", "48 8B 15 ? ? ? ? 48 8D 2D F8 4D FF 01 48 8B CD E8 6C 8C", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"camera", "camera_b3258", "48 8B 1D ? ? ? ? 0F 29 70 E8 48 8B F2 48 8B F9 48 85 DB", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"camera_gameplay_director", "camera_gameplay_director_b3258", "48 8B 05 ? ? ? ? 38 98 F6 08 00 00 8A C3 74 02 B0 01 80", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"replay_interface", "replay_interface_b3258", "48 8D 0D ? ? ? ? 48 89 FA E8 BD 96 10 00 48 8D 0D FA 0E", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"ped_pool", "ped_pool_b3258", "48 8B 05 ? ? ? ? 48 85 C0 74 0B 8B 50 20 C1 E2 02 C1 FA", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"vehicle_pool", "vehicle_pool_b3258", "48 8B 1D ? ? ? ? 48 83 25 4C 9C C4 01 00 48 85 DB 74 10", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"network_player_manager", "network_player_manager_b3258", "48 8B 0D ? ? ? ? 48 85 C9 74 0B 48 8B 01 FF 90 D0 01 00", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"network_object_manager", "network_object_manager_b3258", "48 8B 0D ? ? ? ? 45 33 C0 E8 3B 78 41 01 48 8B C8 48 85", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"blip_list", "blip_list_b3258", "4C 8D 05 ? ? ? ? 0F B7 C1 4D 8B 04 C0 4D 85 C0 74 0A C1", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"swapchain", "swapchain_b3258", "48 8B 0D ? ? ? ? 48 85 C9 0F 84 02 02 00 00 44 38 35 50", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"aim_cped", "aim_cped_b3258", "48 8B 0D ? ? ? ? 33 F6 0F 57 F6 48 89 75 F7 89 75 FF E8", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"player_names", "player_names_b3258", "48 8B 0D ? ? ? ? 44 8B E8 E8 72 0E 86 00 48 85 C0 74 1A", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"sky_settings", "sky_settings_b3258", "48 8D 15 ? ? ? ? 48 8D 0D DD 99 A7 01 E8 50 2D 05 00 48", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"game_build_string", "game_build_string_b3258", "48 8D 0D ? ? ? ? B2 01 E8 50 C5 19 01 48 8D 0D F5 46 0A", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"raycast_xword", "raycast_xword_b3258", "48 8D 15 ? ? ? ? 48 8B 8F D0 00 00 00 48 8B 01 FF 90 80", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},
    {"streamable_texture_dictionary", "streamable_texture_dictionary_b3258", "48 8B 0D ? ? ? ? E8 89 6A 63 01 48 85 C0 74 07 80 A0 00", ResolveKind::RipRelative, 3, 7, 0, 0, 3258, 3258, "runtime-derived b3258 unique xref"},

    {"world", "world_core", "48 8B 05 ? ? ? ? 33 D2 48 8B 40 08 8A CA 48 85 C0 74 16 48 8B", ResolveKind::RipRelative, 3, 7, 1, 0, 0, 0, "UC-v2; Fivem-External-SilentAim"},
    {"viewport", "viewport_core", "48 8B 15 ? ? ? ? 48 8D 2D ? ? ? ? 48 8B CD", ResolveKind::RipRelative, 3, 7, 1, 0, 0, 0, "UC-v2; Fivem-External-SilentAim"},
    {"camera_gameplay_director", "camera_director_core", "4C 8B 35 ? ? ? ? 33 FF 32 DB", ResolveKind::RipRelative, 3, 7, 1, 0, 0, 0, "UC-v2; Fivem-External-SilentAim"},
    {"camera", "camera_b2372_plus", "48 8B 05 ? ? ? ? 38 98 ? ? ? ? 8A C3", ResolveKind::RipRelative, 3, 7, 1, 0, 2372, 0, "Fivem-External-SilentAim (b2372 tested)"},
    {"camera", "camera_virtual", "48 8B 0D ? ? ? ? 48 85 C9 74 0A 48 8B 01 FF 50 20 84 C0", ResolveKind::RipRelative, 3, 7, 1, 0, 0, 0, "UC-v2"},
    {"replay_interface", "replay_mov", "48 8B 05 ? ? ? ? 66 89 0D ? ? ? ? 4C 89 2C D0", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2 (b3095-b3407+)"},
    {"replay_interface", "replay_lea", "48 8D 0D ? ? ? ? 48 ? ? E8 ? ? ? ? 48 8D 0D ? ? ? ? 8A D8 E8 ? ? ? ? 84 DB 75 13 48 8D 0D ? ? ? ? 48 8B D7 E8 ? ? ? ? 84 C0 74 BC 8B 8F", ResolveKind::RipRelative, 3, 7, 0, 0, 0, 0, "Fivem-External-SilentAim"},
    {"blip_list", "blip_list_core", "4C 8D 05 ? ? ? ? 0F B7 C1", ResolveKind::RipRelative, 3, 7, 0, 0, 0, 0, "UC-v2"},
    {"ped_pool", "ped_pool_mov_r8", "4C 8B 05 ? ? ? ? 0F 29 78 D8", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"ped_pool", "ped_pool_mov_rcx", "48 8B 0D ? ? ? ? 48 85 C9 74 ? 8B 41 10", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"vehicle_pool", "vehicle_pool_rcx", "48 8B 0D ? ? ? ? 48 85 C9 74 ? 8B 41 08", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"vehicle_pool", "vehicle_pool_r8", "4C 8B 05 ? ? ? ? 48 8B D9 48 85 C0", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"network_player_manager", "network_player_mgr_core", "48 8B 0D ? ? ? ? 48 85 C9 0F 84 ? ? ? ? 48 8B 01 FF 50 08", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"network_object_manager", "network_object_mgr_core", "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 11", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"swapchain", "swapchain_core", "48 8B 0D ? ? ? ? 48 8B 01 44 8D 43 01 33 D2 FF 50 40 8B C8", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"aim_cped", "aim_cped_core", "48 8B 05 ? ? ? ? 48 8B 48 08 48 85 C9 74 07 48 8B 01 FF 50 10", ResolveKind::RipRelative, 3, 7, 1, 8, 3095, 0, "UC-v2"},
    {"sky_settings", "sky_settings_core", "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 84 C0 74 05", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"game_build_string", "game_build_string_core", "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B C8 E8 ? ? ? ? 48 8D 15", ResolveKind::RipRelative, 3, 7, 0, 0, 3095, 0, "UC-v2"},
    {"raycast_xword", "raycast_xword_core", "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 84 C0 74 10 48 8B 4D", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"streamable_texture_dictionary", "streamable_texture_dictionary_core", "48 8B 05 ? ? ? ? 48 8B 40 10 48 8B 48 20 48 8B 01", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
    {"player_names", "player_names_core", "48 8B 0D ? ? ? ? 48 8B 01 FF 90 ? ? ? ? 48 8B D8 48 85 C0", ResolveKind::RipRelative, 3, 7, 1, 0, 3095, 0, "UC-v2"},
};

inline constexpr std::array<PatternCandidate, 0> kEnhancedPatternCandidates{};
