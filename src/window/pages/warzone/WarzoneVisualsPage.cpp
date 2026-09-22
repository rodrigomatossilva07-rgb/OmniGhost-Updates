#include "../../widgets.h"
#include "../../theme.h"
#include "imgui.h"
#include "src/games/Warzone/warzone_game.h"

void DrawWarzoneVisuals() {
    const float full = ImGui::GetContentRegionAvail().x;
    const float gap = CyberTheme::Metrics::GridGap;
    const float preview = (std::max)(250.f, full * .30f);
    const float controls = full - preview - gap;

    ImGui::BeginChild("##wz_visual_controls", ImVec2(controls, 0), false,
        ImGuiWindowFlags_AlwaysVerticalScrollbar);
    CyberWidgets::BeginCard("FUNÇÕES ESP");
    CyberWidgets::ToggleSwitch("Ativar ESP", &Warzone::config.esp_enabled);
    CyberWidgets::Separator();
    CyberWidgets::ToggleSwitch("Ocultar equipa", &Warzone::config.team_check);
    CyberWidgets::ToggleSwitch("Ignorar derrubados", &Warzone::config.ignore_downed);
    CyberWidgets::ToggleSwitch("Ignorar IA", &Warzone::config.ignore_ai);
    CyberWidgets::SectionTitle("ELEMENTOS");
    CyberWidgets::ToggleSwitch("Esqueleto", &Warzone::config.skeleton);
    CyberWidgets::ToggleSwitch("Ponto na cabeça", &Warzone::config.head_dot);
    CyberWidgets::ToggleSwitch("Vida", &Warzone::config.health_bar);
    CyberWidgets::ToggleSwitch("Nome", &Warzone::config.name);
    CyberWidgets::ToggleSwitch("Distância", &Warzone::config.distance);
    CyberWidgets::ToggleSwitch("Caixa", &Warzone::config.box);
    CyberWidgets::ToggleSwitch("Caixa de cantos", &Warzone::config.box_corner);
    CyberWidgets::ToggleSwitch("Linhas guia", &Warzone::config.snaplines);
    ImGui::SliderInt("Distância máxima", &Warzone::config.max_distance, 50, 800, "%d m");
    CyberWidgets::EndCard();
    ImGui::EndChild();
    ImGui::SameLine(0.f, gap);

    ImGui::BeginChild("##wz_visual_preview", ImVec2(preview, 0), false);
    CyberWidgets::BeginCard("PRÉ-VISUALIZAÇÃO DO ESP");
    const ImVec2 o = ImGui::GetCursorScreenPos();
    const ImVec2 e(o.x + preview - 24.f, o.y + 430.f);
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(o, e, IM_COL32(8, 8, 12, 240), 10.f);
    dl->AddRect(o, e, IM_COL32(212, 175, 55, 42), 10.f);
    const float cx = (o.x + e.x) * .5f, top = o.y + 55.f, bottom = e.y - 40.f, h = bottom - top, hw = h * .18f;
    const ImU32 enemy = IM_COL32((int)(Warzone::config.col_enemy[0]*255), (int)(Warzone::config.col_enemy[1]*255), (int)(Warzone::config.col_enemy[2]*255), 255);
    dl->AddCircleFilled(ImVec2(cx, top + h*.10f), h*.06f, IM_COL32(62,64,70,235));
    dl->AddRectFilled(ImVec2(cx-h*.11f,top+h*.18f),ImVec2(cx+h*.11f,top+h*.54f),IM_COL32(48,50,56,235),8.f);
    if (Warzone::config.box) dl->AddRect(ImVec2(cx-hw,top),ImVec2(cx+hw,bottom),enemy,0,0,1.5f);
    if (Warzone::config.health_bar) { dl->AddRectFilled(ImVec2(cx-hw-7,top),ImVec2(cx-hw-3,bottom),IM_COL32(25,25,28,220)); dl->AddRectFilled(ImVec2(cx-hw-7,top+h*.25f),ImVec2(cx-hw-3,bottom),IM_COL32(70,220,95,255)); }
    if (Warzone::config.skeleton) { const ImVec2 a(cx,top+h*.1f), b(cx,top+h*.34f), c(cx,top+h*.55f); dl->AddLine(a,b,IM_COL32(242,215,80,255),1.7f); dl->AddLine(b,c,IM_COL32(242,215,80,255),1.7f); dl->AddLine(b,ImVec2(cx-hw,top+h*.48f),IM_COL32(242,215,80,255),1.7f); dl->AddLine(b,ImVec2(cx+hw,top+h*.48f),IM_COL32(242,215,80,255),1.7f); }
    if (Warzone::config.name || Warzone::config.distance) dl->AddText(ImVec2(cx-38,top-20),IM_COL32(235,235,238,240),"Jogador  |  24m");
    ImGui::Dummy(ImVec2(preview - 24.f, 430.f));
    CyberWidgets::EndCard();
    CyberWidgets::CardGap();
    CyberWidgets::BeginCard("CORES / ESTADO");
    ImGui::ColorEdit4("Inimigo", Warzone::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Equipa", Warzone::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Alvo", Warzone::config.col_target, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::Text("Jogadores: %d · matriz: %s · lista: %s",
        Warzone::runtime.player_count,
        Warzone::runtime.matrix_ok ? "OK" : "—",
        Warzone::runtime.list_ok ? "OK" : "—");
    ImGui::TextWrapped("%s", Warzone::StatusLine());
    CyberWidgets::EndCard();
    ImGui::EndChild();
}
