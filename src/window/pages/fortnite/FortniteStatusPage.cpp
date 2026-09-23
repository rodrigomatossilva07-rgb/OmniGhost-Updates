#include "../../widgets.h"
#include "src/games/Fortnite/fortnite_game.h"

void DrawFortniteStatus() {
    CyberWidgets::SectionTitle("Attach status");
    CyberWidgets::StatusBadge("Process / DMA", Fortnite::runtime.attached);
    CyberWidgets::StatusBadge("Chain", Fortnite::runtime.chain_ok);
    CyberWidgets::StatusBadge("Local Pawn", Fortnite::runtime.local_pawn_ok);
    CyberWidgets::StatusBadge("Camera", Fortnite::runtime.camera_ok);
    CyberWidgets::StatusBadge("World to screen", Fortnite::runtime.w2s_ok);

    CyberWidgets::BeginCard("Connection details");
    CyberWidgets::KeyValueRow("Process", Fortnite::runtime.process_name.c_str());
    CyberWidgets::KeyValueRow("Status", Fortnite::StatusText());
    if (!Fortnite::runtime.last_fail.empty())
        CyberWidgets::KeyValueRow("Last failure", Fortnite::runtime.last_fail.c_str());
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "PID: %u", Fortnite::runtime.pid);
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "Frames: %llu",
        static_cast<unsigned long long>(Fortnite::runtime.frames));
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Lista de jogadores (PlayerArray)");
    CyberWidgets::StatusBadge("GameState / PlayerArray", Fortnite::runtime.player_array_ok);
    CyberWidgets::StatusBadge("Entradas legíveis", Fortnite::runtime.player_data_ok);
    CyberWidgets::KeyValueRow("Estado", Fortnite::runtime.player_array_status.c_str());
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
        "Entradas: %d / capacidade %d | após filtros: %d",
        Fortnite::runtime.player_array_count,
        Fortnite::runtime.player_array_capacity,
        static_cast<int>(Fortnite::runtime.players.size()));
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
        "Local: %d | sem Pawn: %d | sem vida: %d",
        Fortnite::runtime.players_local,
        Fortnite::runtime.players_without_pawn,
        Fortnite::runtime.players_dying);
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
        "Distância: %d | mesma equipa: %d | inválidos: %d",
        Fortnite::runtime.players_out_of_range,
        Fortnite::runtime.players_same_team,
        Fortnite::runtime.players_invalid);
    CyberWidgets::InlineMessage(
        "Confirma em partida: PlayerArray e Entradas legíveis a verde; "
        "o total deve acompanhar os jogadores e mudar com os filtros.",
        CyberWidgets::TextTone::Info);
    CyberWidgets::EndCard();

    if (!Fortnite::runtime.attached)
        CyberWidgets::InlineMessage("Usa o launcher para ligar ao Fortnite.", CyberWidgets::TextTone::Info);
    else if (!Fortnite::runtime.chain_ok)
        CyberWidgets::InlineMessage("A aguardar cadeia local valida (World/GI/PC).", CyberWidgets::TextTone::Warning);
    else if (!Fortnite::runtime.camera_ok)
        CyberWidgets::InlineMessage("A aguardar dados estaveis da camera.", CyberWidgets::TextTone::Warning);
    else if (!Fortnite::runtime.local_pawn_ok)
        CyberWidgets::InlineMessage(
            "Sem Local Pawn (normal no lobby). Entra numa partida para spawnar o pawn. "
            "Camera/W2S usam a origem da camera no lobby.",
            CyberWidgets::TextTone::Info);
    else if (!Fortnite::runtime.w2s_ok)
        CyberWidgets::InlineMessage("Pawn OK — a aguardar projecao World-to-Screen.", CyberWidgets::TextTone::Warning);
    else
        CyberWidgets::InlineMessage("Cadeia OK: Local Pawn + Camera + W2S.", CyberWidgets::TextTone::Success);
}
