#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "friends/friends.h"
#endif
#include <cstdio>
#include <mutex>

void DrawFriends()
{
    friends::UpdatePlayerList();

    static char searchBuf[64] = {};

    CyberWidgets::BeginCardRow();
    float half = CyberWidgets::CardRowHalfWidth();

    CyberWidgets::BeginCard("FRN // 05   AMIGOS", half);
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
        "%d jogadores próximos", (int)friends::player_list.size());
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("REGRAS");
    CyberWidgets::ToggleSwitch("Ignorar amigos", &friends::config.ignore_aim);
    if (friends::config.ignore_aim)
        CyberWidgets::ToggleSwitch("Também no disparo automático", &friends::config.ignore_silent);
    CyberWidgets::ToggleSwitch("ESP de amigos", &friends::config.show_friend_esp);
    if (friends::config.show_friend_esp)
        CyberWidgets::ColorEditU32("Cor", &friends::config.friend_color);
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("PROXIMIDADE");
    CyberWidgets::ToggleSwitch("Usar alcance próximo", &friends::config.add_by_prox);
    if (friends::config.add_by_prox)
        CyberWidgets::SliderFloat("Alcance", &friends::config.prox_max_distance, 5.0f, 200.0f, "%.0f m");
    if (CyberWidgets::GoldButton("ADICIONAR MAIS PRÓXIMO", ImVec2(190, 34))) {
        friends::AddClosestAsFriend();
        CyberWidgets::Notify(Loc::Tr("status.friend_added"), CyberWidgets::ToastType::Success);
    }
    if (ImGui::CollapsingHeader("DADOS E MANUTENÇÃO", ImGuiTreeNodeFlags_None)) {
        if (CyberWidgets::CyberButton("Remover mais próximo", ImVec2(180, 34)))
            friends::KickClosestFromFriends();
        if (CyberWidgets::CyberButton("Exportar", ImVec2(110, 34))) {
            if (friends::ExportFriendsClipboard())
                CyberWidgets::Notify(Loc::Tr("status.copied"), CyberWidgets::ToastType::Success);
        }
        ImGui::SameLine();
        if (CyberWidgets::CyberButton("Importar", ImVec2(110, 34))) {
            friends::ImportFriendsClipboard();
            CyberWidgets::Notify(Loc::Tr("status.pasted"), CyberWidgets::ToastType::Success);
        }
    }
    CyberWidgets::Separator();
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
        Loc::Tr("fr.count"), (int)friends::friend_list.size());
    CyberWidgets::BeginSurfaceList("##friend_list", 140.0f);
    uint32_t to_remove = 0;
    {
        std::lock_guard<std::mutex> lock(friends::list_mutex);
        if (friends::friend_list.empty()) {
            ImDrawList* edl = ImGui::GetWindowDrawList();
            const ImVec2 ep = ImGui::GetCursorScreenPos();
            const float ew = ImGui::GetContentRegionAvail().x;
            edl->AddText(ImVec2(ep.x + 10.0f, ep.y + 22.0f),
                CyberTheme::U32(CyberTheme::Colors.TextDisabled),
                Loc::Tr("fr.empty"));
            edl->AddText(ImVec2(ep.x + 10.0f, ep.y + 42.0f),
                IM_COL32(100, 104, 118, 255),
                Loc::Tr("fr.empty_hint"));
            ImGui::Dummy(ImVec2(ew, 72.0f));
        }
        for (size_t i = 0; i < friends::friend_list.size(); ++i) {
            auto& f = friends::friend_list[i];
            ImGui::PushID(static_cast<int>(f.id));
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float row_width = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(row.x + 2.0f, row.y + 7.0f),
                CyberTheme::U32(CyberTheme::Colors.Text), f.name.c_str());

            char friend_id[24];
            snprintf(friend_id, sizeof(friend_id), "[%u]", f.id);
            dl->AddText(ImVec2(row.x + row_width * 0.58f, row.y + 7.0f),
                CyberTheme::U32(CyberTheme::Colors.TextDisabled), friend_id);

            ImGui::SetCursorScreenPos(ImVec2(row.x + row_width - 28.0f, row.y));
            if (CyberWidgets::CyberButton("X##remove", ImVec2(28.0f, 32.0f)))
                to_remove = f.id;
            ImGui::SetCursorScreenPos(ImVec2(
                row.x, row.y + CyberTheme::Metrics::ControlHeight + 2.0f));
            ImGui::PopID();
        }
    }
    CyberWidgets::EndSurfaceList();
    if (to_remove)
        friends::RemoveFriend(to_remove);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();

    CyberWidgets::BeginCard("JOGADORES", half);
    static char search[64] = "";
    CyberWidgets::TextInput(Loc::TrID("fr.search"), search, sizeof(search), Loc::Tr("fr.search_hint"));
    CyberWidgets::Separator();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float width = CyberWidgets::CardContentWidth();
        dl->AddText(pos, ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.TextDisabled), Loc::Tr("common.name"));
        dl->AddText(ImVec2(pos.x + width * 0.68f, pos.y),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.TextDisabled), Loc::Tr("common.id"));
        const char* distance_label = Loc::Tr("common.dist_short");
        const ImVec2 distance_size = ImGui::CalcTextSize(distance_label);
        dl->AddText(ImVec2(pos.x + width - distance_size.x, pos.y),
            ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.TextDisabled),
            distance_label);
        ImGui::Dummy(ImVec2(0.f, 20.f));
    }
    CyberWidgets::Separator();

    // Filter players based on search
    std::vector<const friends::PlayerEntry*> filteredPlayers;
    filteredPlayers.reserve(friends::player_list.size());
    for (const auto& p : friends::player_list) {
        if (!search[0] || p.name.find(search) != std::string::npos) {
            filteredPlayers.push_back(&p);
        }
    }

    // Use virtualized list for performance with large player lists
    static int selectedPlayer = -1;
    const float rowH = 22.f;
    [[maybe_unused]] const float listHeight = 180.f;
    
    CyberWidgets::VirtualizedList<const friends::PlayerEntry*>(
        "##nearby_players",
        filteredPlayers,
        rowH,
        180.f,
        [&]([[maybe_unused]] const friends::PlayerEntry* p, [[maybe_unused]] int idx, [[maybe_unused]] bool isSelected) {
            const bool isF = friends::IsFriend(p->id);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 pos = ImGui::GetCursorScreenPos();
            const float width = CyberWidgets::CardContentWidth();
            const ImU32 color = isF
                ? friends::config.friend_color
                : ImGui::ColorConvertFloat4ToU32(CyberTheme::Colors.Text);

            // Invisible button covers the whole row for hover + double-click
            ImGui::PushID(static_cast<int>(p->id));
            ImGui::InvisibleButton("##row", ImVec2(width, rowH));
            const bool hovered = ImGui::IsItemHovered();
            const bool dbl = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            if (hovered) {
                dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + rowH),
                    IM_COL32(212, 175, 55, 28), 4.0f);
            }
            if (dbl) {
                if (isF) {
                    friends::RemoveFriend(p->id);
                    CyberWidgets::Notify(Loc::Tr("status.friend_removed"), CyberWidgets::ToastType::Info);
                } else {
                    friends::AddFriend(p->name, p->id, false);
                    CyberWidgets::Notify(Loc::Tr("status.friend_added"), CyberWidgets::ToastType::Success);
                }
            }

            dl->PushClipRect(pos, ImVec2(pos.x + width * 0.63f, pos.y + rowH), true);
            dl->AddText(ImVec2(pos.x + 4.f, pos.y + 2.f), color, p->name.c_str());
            dl->PopClipRect();

            char id[16];
            snprintf(id, sizeof(id), "%u", p->id);
            dl->AddText(ImVec2(pos.x + width * 0.68f, pos.y + 2.f), color, id);

            char distance[40];
            snprintf(distance, sizeof(distance), "%.0fHP  %.1fm", p->health_pct, p->distance);
            const ImVec2 distance_size = ImGui::CalcTextSize(distance);
            dl->AddText(ImVec2(pos.x + width - distance_size.x, pos.y + 2.f), color, distance);
            ImGui::PopID();
        },
        rowH,
        180.f,
        [&]([[maybe_unused]] const friends::PlayerEntry* item, [[maybe_unused]] int idx, [[maybe_unused]] bool isSelected) {
            // renderItem is handled inline above
        },
        true,
        &selectedPlayer);

    CyberWidgets::EndCard();

    CyberWidgets::EndCardRow();
}
