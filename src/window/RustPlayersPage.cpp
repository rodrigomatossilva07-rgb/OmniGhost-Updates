#include "widgets.h"
#include "theme.h"
#include "../../Rust/rust_game.h"
#include "../../Rust/rust_aim.h"
#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
bool ContainsInsensitive(const char* text, const char* needle) {
    if (!needle || !*needle) return true;
    if (!text) return false;
    std::string a(text), b(needle);
    std::transform(a.begin(), a.end(), a.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return a.find(b) != std::string::npos;
}
}

void DrawRustPlayers() {
    static char filter[64]{};

    CyberWidgets::BeginCard("Rust · Jogadores");
    CyberWidgets::StatusBadge("Em jogo", Rust::runtime.in_game);
    CyberWidgets::StatusBadge("Matriz", Rust::runtime.matrix_ok);
    CyberWidgets::StatusBadge("Lista de entidades", Rust::runtime.list_ok);
    char countText[48]{};
    std::snprintf(countText, sizeof(countText), "%d detetados", Rust::runtime.player_count);
    CyberWidgets::KeyValueRow("Jogadores", countText);
    CyberWidgets::TextInput("Pesquisar###rust_players_filter", filter, sizeof(filter), "Nome do jogador...");

    if (Rust::runtime.players.empty()) {
        CyberWidgets::EmptyState(
            "Ainda não existem jogadores no snapshot",
            "A tabela aparece automaticamente quando o runtime receber entidades válidas.");
        CyberWidgets::EndCard();
        return;
    }

    const int aimIdx = Rust_Aim::ActiveTargetIndex();
    int visible = 0;
    for (const auto& p : Rust::runtime.players)
        if (p.valid && ContainsInsensitive(p.name[0] ? p.name : "???", filter)) ++visible;
    char summary[96]{};
    std::snprintf(summary, sizeof(summary), "%d visíveis · alvo ativo %s", visible, aimIdx >= 0 ? "sim" : "não");
    CyberWidgets::TextLine(summary, CyberWidgets::TextTone::Secondary);

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable |
        ImGuiTableFlags_Hideable;
    if (ImGui::BeginTable("##rust_players", 6, flags, ImVec2(0, 390.f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch, 2.4f);
        ImGui::TableSetupColumn("Equipa", ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableSetupColumn("Vida", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("Distância", ImGuiTableColumnFlags_WidthFixed, 85.f);
        ImGui::TableSetupColumn("Estado", ImGuiTableColumnFlags_WidthStretch, 1.6f);
        ImGui::TableSetupColumn("Alvo", ImGuiTableColumnFlags_WidthFixed, 70.f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < static_cast<int>(Rust::runtime.players.size()); ++i) {
            const auto& p = Rust::runtime.players[i];
            if (!p.valid || !ContainsInsensitive(p.name[0] ? p.name : "???", filter)) continue;
            ImGui::TableNextRow();
            if (i == aimIdx)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.10f));

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(p.name[0] ? p.name : "???");
            if (p.npc) { ImGui::SameLine(); CyberWidgets::Badge("NPC", CyberWidgets::TextTone::Secondary); }

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", p.team_id);

            ImGui::TableSetColumnIndex(2);
            const float maxHealth = (std::max)(1.f, p.max_health);
            const float fraction = std::clamp(p.health / maxHealth, 0.f, 1.f);
            char hp[32]{};
            std::snprintf(hp, sizeof(hp), "%.0f / %.0f", p.health, p.max_health);
            ImGui::ProgressBar(fraction, ImVec2(-1.f, 18.f), hp);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.0f m", p.distance);

            ImGui::TableSetColumnIndex(4);
            if (p.sleeping) CyberWidgets::Badge("A dormir", CyberWidgets::TextTone::Secondary);
            else if (p.wounded) CyberWidgets::Badge("Ferido", CyberWidgets::TextTone::Warning);
            else if (p.aiming) CyberWidgets::Badge("A apontar", CyberWidgets::TextTone::Warning);
            else CyberWidgets::Badge("Ativo", CyberWidgets::TextTone::Success);

            ImGui::TableSetColumnIndex(5);
            if (i == aimIdx) CyberWidgets::Badge("ALVO ATIVO", CyberWidgets::TextTone::Accent);
            else ImGui::TextDisabled("—");
        }
        ImGui::EndTable();
    }
    CyberWidgets::EndCard();
}
