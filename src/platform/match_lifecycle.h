#pragma once
// Shared lobby → match lifecycle for all OmniGhost games.
// Shared match/session lifecycle pattern.
//  - Always keep probing while attached; never "give up" in lobby.
//  - Detect enter/leave match transitions.
//  - Invalidate caches when world/session generation changes.
//  - Human-readable phase for UI status lines.
//
// Usage per-frame after resolving world / local / players:
//   auto phase = MatchLifecycle::Update(state, has_world, has_local, player_count, world_ptr);
//   if (phase == MatchLifecycle::Phase::EnteredMatch) { /* clear entity caches */ }

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace MatchLifecycle {

enum class Phase : int {
    Offline = 0,   // no world / not attached context
    Lobby,         // world ok, no local pawn / no players yet
    WaitingEntities, // local present but player list still empty
    EnteredMatch,  // transition edge: lobby → match this frame
    InMatch,       // stable in match
    LeftMatch      // transition edge: match → lobby this frame
};

struct State {
    bool was_in_match = false;
    uintptr_t last_world = 0;
    uint64_t world_generation = 0;
    int zero_player_streak = 0;
    int enter_count = 0;
    int leave_count = 0;
};

// has_world: process world/module context valid
// has_local: local pawn/player resolved
// player_count: verified entities for ESP/aim
// world_ptr: pointer identity for generation tracking (UWorld, GA base, etc.)
inline Phase Update(State& s, bool has_world, bool has_local, int player_count, uintptr_t world_ptr) {
    if (world_ptr && world_ptr != s.last_world) {
        s.last_world = world_ptr;
        ++s.world_generation;
        // New world: treat as not-yet-in-match until signals confirm.
        s.was_in_match = false;
        s.zero_player_streak = 0;
    }

    const bool in_match_now = has_world && (has_local || player_count > 0);

    if (!has_world) {
        s.was_in_match = false;
        s.zero_player_streak = 0;
        return Phase::Offline;
    }

    if (in_match_now && !s.was_in_match) {
        s.was_in_match = true;
        s.zero_player_streak = 0;
        ++s.enter_count;
        return Phase::EnteredMatch;
    }

    if (!in_match_now && s.was_in_match) {
        s.was_in_match = false;
        ++s.leave_count;
        return Phase::LeftMatch;
    }

    s.was_in_match = in_match_now;

    if (in_match_now) {
        if (player_count <= 0) {
            ++s.zero_player_streak;
            return Phase::WaitingEntities;
        }
        s.zero_player_streak = 0;
        return Phase::InMatch;
    }

    return Phase::Lobby;
}

inline const char* PhaseName(Phase p) {
    switch (p) {
    case Phase::Offline: return "Offline";
    case Phase::Lobby: return "Lobby";
    case Phase::WaitingEntities: return "A resolver entidades";
    case Phase::EnteredMatch: return "Entrada em partida";
    case Phase::InMatch: return "Em jogo";
    case Phase::LeftMatch: return "Saida de partida";
    default: return "?";
    }
}

// Fill a short status line suitable for game debug panels.
inline void FormatStatus(char* out, size_t out_len, Phase phase, int players, const char* extra = nullptr) {
    if (!out || out_len == 0) return;
    if (extra && extra[0]) {
        std::snprintf(out, out_len, "%s | players=%d | %s", PhaseName(phase), players, extra);
    } else {
        switch (phase) {
        case Phase::Offline:
            std::snprintf(out, out_len, "Offline / a aguardar mundo");
            break;
        case Phase::Lobby:
            std::snprintf(out, out_len, "Lobby / a aguardar partida (a procurar...)");
            break;
        case Phase::WaitingEntities:
            std::snprintf(out, out_len, "Em partida — a resolver lista (players=%d)", players);
            break;
        case Phase::EnteredMatch:
            std::snprintf(out, out_len, "Entrada em partida | players=%d", players);
            break;
        case Phase::InMatch:
            std::snprintf(out, out_len, "Em jogo | players=%d", players);
            break;
        case Phase::LeftMatch:
            std::snprintf(out, out_len, "Lobby / a aguardar partida");
            break;
        default:
            std::snprintf(out, out_len, "Estado desconhecido");
            break;
        }
    }
}

} // namespace MatchLifecycle
