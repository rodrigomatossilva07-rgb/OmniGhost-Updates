#include "../../widgets.h"
#include "../../theme.h"
#include "cs2_config.h"
#include "cs2_radar.h"
#include "fonts.h"
#include "../../localization.h"
#include "../../../launcher/launcher_assets.h"
#include "imgui.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace {
void DrawCs2Preview(float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImVec2 e(o.x + width, o.y + height);
    dl->AddRectFilled(o, e, IM_COL32(8, 8, 12, 240), 10.f);
    dl->AddRect(o, e, IM_COL32(212, 175, 55, 40), 10.f);
    dl->AddRectFilled(o, ImVec2(e.x, o.y + 28.f), IM_COL32(14, 14, 18, 255), 10.f, ImDrawFlags_RoundCornersTop);
    // Keep the title centred even when the preview width changes.
    const char* previewTitle = "ESP Preview";
    const ImVec2 previewTitleSize = ImGui::CalcTextSize(previewTitle);
    dl->AddText(ImVec2(o.x + (width - previewTitleSize.x) * .5f, o.y + 7.f),
        IM_COL32(230, 230, 235, 255), previewTitle);

    // Reuse the embedded operator portrait used by the FiveM preview. It is a
    // UI-only asset; every ESP layer below still follows CS2's own settings.
    const LauncherAssets::Texture portrait = LauncherAssets::FiveMEspPreview();
    if (portrait.id) {
        const float portraitH = height - 50.f;
        const float portraitW = portraitH * 0.52f;
        dl->AddImage(portrait.id, ImVec2(o.x + width * .5f - portraitW * .5f, o.y + 34.f),
            ImVec2(o.x + width * .5f + portraitW * .5f, o.y + 34.f + portraitH),
            ImVec2(0.f, 0.f), ImVec2(1.f, 1.f), IM_COL32(255,255,255,224));
    }

    const float cx = o.x + width * .5f;
    const float top = o.y + 34.f;
    const float figureH = height - 50.f;
    const float sc = figureH / 370.f;
    // Normalized to the real full-body operator portrait, not to the old
    // mannequin. This keeps the skeleton aligned when the card is resized.
    const ImVec2 head(cx, top + figureH * .090f), neck(cx, top + figureH * .166f);
    const ImVec2 chest(cx, top + figureH * .300f), pelvis(cx, top + figureH * .530f);
    const ImVec2 lsh(cx - figureH * .104f, top + figureH * .218f), rsh(cx + figureH * .104f, top + figureH * .218f);
    const ImVec2 lel(cx - figureH * .140f, top + figureH * .390f), rel(cx + figureH * .140f, top + figureH * .390f);
    const ImVec2 lha(cx - figureH * .158f, top + figureH * .510f), rha(cx + figureH * .158f, top + figureH * .510f);
    const ImVec2 lkn(cx - figureH * .064f, top + figureH * .770f), rkn(cx + figureH * .064f, top + figureH * .770f);
    const ImVec2 lft(cx - figureH * .078f, top + figureH * .940f), rft(cx + figureH * .078f, top + figureH * .940f);

    ImU32 sk = IM_COL32((int)(CS2::config.col_skeleton[0]*255), (int)(CS2::config.col_skeleton[1]*255),
        (int)(CS2::config.col_skeleton[2]*255), 220);
    auto color = [](const float value[4], float alpha = 1.f) {
        return IM_COL32((int)(value[0] * 255.f), (int)(value[1] * 255.f),
            (int)(value[2] * 255.f), (int)(value[3] * alpha * 255.f));
    };
    auto bone = [&](ImVec2 a, ImVec2 b) { if (CS2::config.skeleton) dl->AddLine(a, b, sk, 2.f); };
    auto joint = [&](ImVec2 p) {
        if (CS2::config.skeleton_joints) {
            dl->AddCircleFilled(p, 2.8f, IM_COL32(80, 220, 90, 255), 10);
            dl->AddCircle(p, 2.8f, CyberTheme::SafeShadowU32(180), 10, 1.f);
        }
    };
    bone(head,neck); bone(neck,chest); bone(chest,pelvis);
    bone(neck,lsh); bone(neck,rsh); bone(lsh,lel); bone(lel,lha); bone(rsh,rel); bone(rel,rha);
    bone(pelvis,lkn); bone(lkn,lft); bone(pelvis,rkn); bone(rkn,rft);
    for (ImVec2 p : {head,neck,chest,pelvis,lsh,rsh,lel,rel,lha,rha,lkn,rkn,lft,rft}) joint(p);
    if (CS2::config.head_dot) {
        dl->AddCircle(head, 12.f * sc, IM_COL32(244, 64, 75, 230), 24, 1.6f);
        dl->AddCircleFilled(head, 2.4f, IM_COL32(40,244,91,255), 12);
    }
    if (CS2::config.head_halo) {
        ImVec2 halo[25]{};
        for (int i = 0; i <= 24; ++i) {
            const float angle = i * 6.28318530718f / 24.f;
            halo[i] = ImVec2(head.x + std::cos(angle) * 16.f,
                             head.y - 16.f + std::sin(angle) * 5.f);
        }
        dl->AddPolyline(halo, 25, color(CS2::config.col_halo), false, 1.7f);
    }
    if (CS2::config.look_direction) {
        const ImVec2 end(head.x + 48.f, head.y - 4.f);
        dl->AddLine(head, end, color(CS2::config.col_look), 1.7f);
        dl->AddCircleFilled(end, 2.2f, color(CS2::config.col_look), 8);
    }
    if (CS2::config.health_bar) {
        float top = head.y - 8.f, bot = lft.y, bx = (std::min)(lha.x, lft.x) - 18.f, h = bot - top;
        dl->AddRectFilled(ImVec2(bx-4, top), ImVec2(bx, bot), CyberTheme::SafeShadowU32(180));
        dl->AddRectFilledMultiColor(ImVec2(bx-4, bot - h*.72f), ImVec2(bx, bot), IM_COL32(227,190,72,255), IM_COL32(227,190,72,255), IM_COL32(235,50,35,255), IM_COL32(235,50,35,255));
    }
    if (CS2::config.armor_bar) {
        float top = head.y - 8.f, bot = rft.y, bx = (std::max)(rha.x, rft.x) + 14.f, h = bot - top;
        dl->AddRectFilled(ImVec2(bx, top), ImVec2(bx+4, bot), CyberTheme::SafeShadowU32(180));
        dl->AddRectFilled(ImVec2(bx, bot - h*0.55f), ImVec2(bx+4, bot), IM_COL32(70, 150, 255, 255));
    }
    if (CS2::config.name) {
        const char* name = "Player";
        const ImVec2 nameSize = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(cx - nameSize.x * .5f, head.y - 25.f),
            IM_COL32(216,216,210,235), name);
    }
    if (CS2::config.weapon_icons || CS2::config.distance) {
        const ImVec2 footer(cx - 40.f, lft.y + 8.f);
        if (CS2::config.weapon_icons)
            dl->AddText(footer, IM_COL32(227,198,90,255), "Pistol");
        if (CS2::config.distance)
            dl->AddText(ImVec2(footer.x + (CS2::config.weapon_icons ? 38.f : 0.f), footer.y),
                IM_COL32(226,88,183,255), CS2::config.weapon_icons ? "| 85m" : "85m");
    }

    if (CS2::config.radar_2d) {
        const ImVec2 radar(o.x + 58.f, o.y + 75.f);
        const float radius = 34.f;
        dl->AddCircleFilled(radar, radius, CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.82f), 36);
        dl->AddCircle(radar, radius, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.82f), 36, 1.2f);
        dl->AddLine(ImVec2(radar.x - radius, radar.y), ImVec2(radar.x + radius, radar.y), IM_COL32(140,140,150,90), 1.f);
        dl->AddLine(ImVec2(radar.x, radar.y - radius), ImVec2(radar.x, radar.y + radius), IM_COL32(140,140,150,90), 1.f);
        dl->AddCircleFilled(radar, 3.f, IM_COL32(220,220,225,255), 12);
        dl->AddCircleFilled(ImVec2(radar.x + 13.f, radar.y - 10.f), 3.8f, color(CS2::config.col_enemy), 12);
        dl->AddText(ImVec2(radar.x - 25.f, radar.y + radius + 5.f), IM_COL32(160,160,170,210), "Radar");
    }
    ImGui::Dummy(ImVec2(width, height));
}
}

void DrawCs2Visuals_FORCE(); void DrawCs2Visuals() {
    CS2::config.trails = false;
    CS2::config.look_direction = false;

    CyberWidgets::SearchBar(Loc::Tr("vis.search"));
    float full = CyberWidgets::CardContentWidth();
    float left_w = full * 0.52f;
    float right_w = full - left_w - 12.f;

    ImGui::BeginGroup();
    CyberWidgets::BeginCard(Loc::Tr("vis.esp_configs"), left_w);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &CS2::config.visibility_colors);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.enable"), &CS2::config.esp_enabled);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.self_esp"), &CS2::config.self_esp);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.team_check"), &CS2::config.team_check);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.skeleton"), &CS2::config.skeleton);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.joints"), &CS2::config.skeleton_joints);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.head_circle"), &CS2::config.head_dot);
    ImGui::BeginDisabled();
    CyberWidgets::ToggleSwitch("Trails (rasto de movimento)", &CS2::config.trails);
    ImGui::EndDisabled();
    CyberWidgets::ToggleSwitch("Auréola na cabeça", &CS2::config.head_halo);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.health_bar"), &CS2::config.health_bar);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.armor_bar"), &CS2::config.armor_bar);
    CyberWidgets::ToggleSwitch("Nome da arma", &CS2::config.weapon_icons);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.box_2d"), &CS2::config.box);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.corner"), &CS2::config.box_corner);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.snaplines"), &CS2::config.snaplines);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.name"), &CS2::config.name);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.distance"), &CS2::config.distance);
    CyberWidgets::SliderFloat(Loc::TrID("vis.max_dist"), &CS2::config.max_distance, 20.f, 500.f, "%.0f m");
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("vis.colors"), left_w);
    ImGui::ColorEdit4(Loc::Tr("vis.col_enemy"), CS2::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4(Loc::Tr("vis.col_team"), CS2::config.col_team, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4(Loc::Tr("vis.col_skeleton"), CS2::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4(Loc::Tr("vis.col_name"), CS2::config.col_name, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Pontos do esqueleto", CS2::config.col_joints, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Barra de vida", CS2::config.col_health, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Barra de armadura", CS2::config.col_armor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Linhas guia", CS2::config.col_snaplines, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Auréola", CS2::config.col_halo, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Radar", left_w);
    // Legacy QR setting never had a renderer. Keep the serialized field for
    // backwards compatibility, but never advertise or enable a fake feature.
    CS2::config.webradar_qr = false;
    CyberWidgets::ToggleSwitch("Radar 2D integrado", &CS2::config.radar_2d);
    CyberWidgets::SliderFloat("Tamanho do radar", &CS2::config.radar_2d_size, 110.f, 320.f, "%.0f px");
    const bool radarToggled = CyberWidgets::ToggleSwitch(
        Loc::TrID("vis.webradar"), &CS2::config.webradar_enabled);
    if (radarToggled && !CS2::config.webradar_enabled) {
        CS2::config.webradar_cloudflare = false;
        CS2_Radar::Shutdown();
    }
    if (CS2::config.webradar_enabled) {
        CyberWidgets::ToggleSwitch("Acesso LAN", &CS2::config.webradar_lan);
        CyberWidgets::InputInt("Porta", &CS2::config.webradar_port);
        if (CS2::config.webradar_port < 1024 || CS2::config.webradar_port > 65535)
            CS2::config.webradar_port = 8080;
        CyberWidgets::TextLine(CS2_Radar::Status(), CyberWidgets::TextTone::Secondary);
        if (CS2::config.webradar_lan) {
            CyberWidgets::TextLine("A LAN está protegida por um token temporário. Partilha apenas o endereço copiado abaixo.", CyberWidgets::TextTone::Warning);
            const char* accessUrl = CS2_Radar::LanUrl();
            if (accessUrl && accessUrl[0] &&
                CyberWidgets::Button("Copiar endereço LAN seguro", CyberWidgets::ButtonStyle::Secondary, ImVec2(230.f, 32.f))) {
                CyberWidgets::CopyToClipboard(accessUrl, "Endereço seguro copiado");
            }
        } else {
            CyberWidgets::TextLine("Apenas este computador pode aceder ao radar.", CyberWidgets::TextTone::Secondary);
        }

        ImGui::Dummy(ImVec2(0.f, 6.f));
        CyberWidgets::TextLine(
            "Link público temporário protegido por token. Qualquer pessoa com o endereço poderá ver o radar até o terminares.",
            CyberWidgets::TextTone::Warning);
        if (!CS2_Radar::CloudflareRunning()) {
            if (CyberWidgets::Button("Criar link público seguro",
                    CyberWidgets::ButtonStyle::Primary, ImVec2(230.f, 32.f))) {
                CS2_Radar::EnsureRunning(CS2::config.webradar_port, CS2::config.webradar_lan);
                CS2_Radar::StartCloudflareTunnel(CS2::config.webradar_port);
                CS2::config.webradar_cloudflare = true;
            }
        } else {
            const char* publicUrl = CS2_Radar::PublicUrl();
            if (publicUrl && publicUrl[0]) {
                if (CyberWidgets::Button("Copiar link público seguro",
                        CyberWidgets::ButtonStyle::Primary, ImVec2(230.f, 32.f))) {
                    CyberWidgets::CopyToClipboard(publicUrl, "Link público seguro copiado");
                }
            } else {
                CyberWidgets::TextLine("A Cloudflare está a preparar o endereço...",
                    CyberWidgets::TextTone::Secondary);
            }
            ImGui::SameLine(0.f, 8.f);
            if (CyberWidgets::Button("Terminar link",
                    CyberWidgets::ButtonStyle::Destructive, ImVec2(145.f, 32.f))) {
                CS2_Radar::StopCloudflareTunnel();
                CS2::config.webradar_cloudflare = false;
            }
        }
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("vis.extras"), left_w);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.weapon_icons"), &CS2::config.weapon_icons);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.show_bots"), &CS2::config.show_bots);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.bomb"), &CS2::config.bomb_timer);
    CyberWidgets::ToggleSwitch(Loc::TrID("vis.spectators"), &CS2::config.spectator_list);
    CyberWidgets::EndCard();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, 12.f);
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("##cs2_preview", right_w);
    DrawCs2Preview((std::max)(260.f, right_w - 24.f), 520.f);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
