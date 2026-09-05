#include "widgets.h"
#include "theme.h"
#include "localization.h"
#include "../launcher/launcher_assets.h"
#include "../../Rust/rust_game.h"
#include "../../Rust/rust_config.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

void DrawRustEspPreview(float width, float height) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetCursorScreenPos();
    ImVec2 e(o.x + width, o.y + height);
    dl->AddRectFilled(o, e, IM_COL32(10, 10, 14, 245), 10.f);
    dl->AddRect(o, e, IM_COL32(212, 175, 55, 45), 10.f);
    dl->AddRectFilled(o, ImVec2(e.x, o.y + 28.f), IM_COL32(16, 16, 20, 255), 10.f, ImDrawFlags_RoundCornersTop);
    dl->AddText(ImVec2(o.x + 12.f, o.y + 7.f), IM_COL32(230, 230, 235, 255), "Pré-visualização");

    const LauncherAssets::Texture preview = LauncherAssets::RustEspPreview();
    const ImVec2 area_min(o.x + 7.f, o.y + 35.f);
    const ImVec2 area_max(e.x - 7.f, e.y - 7.f);
    const float area_w = area_max.x - area_min.x;
    const float area_h = area_max.y - area_min.y;
    const float aspect = preview && preview.height > 0
        ? static_cast<float>(preview.width) / static_cast<float>(preview.height)
        : 433.f / 577.f;
    float image_h = area_h;
    float image_w = image_h * aspect;
    if (image_w > area_w) {
        image_w = area_w;
        image_h = image_w / aspect;
    }
    const ImVec2 image_min(
        area_min.x + (area_w - image_w) * .5f,
        area_min.y + (area_h - image_h) * .5f);
    const ImVec2 image_max(image_min.x + image_w, image_min.y + image_h);
    if (preview)
        dl->AddImage(preview.id, image_min, image_max, ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
                     IM_COL32(255, 255, 255, 246));

    auto point = [&](float x, float y) {
        return ImVec2(image_min.x + image_w * x, image_min.y + image_h * y);
    };
    const float cx = image_min.x + image_w * .5f;
    const ImVec2 head = point(.50f, .105f);
    const ImVec2 neck = point(.50f, .195f);
    const ImVec2 chest = point(.50f, .315f);
    const ImVec2 pelvis = point(.50f, .535f);
    const ImVec2 lsh = point(.36f, .235f);
    const ImVec2 rsh = point(.64f, .235f);
    const ImVec2 lel = point(.285f, .385f);
    const ImVec2 rel = point(.715f, .385f);
    const ImVec2 lha = point(.255f, .535f);
    const ImVec2 rha = point(.745f, .535f);
    const ImVec2 lkn = point(.405f, .735f);
    const ImVec2 rkn = point(.595f, .735f);
    const ImVec2 lft = point(.355f, .965f);
    const ImVec2 rft = point(.645f, .965f);

    auto col4 = [](const float c[4], float a = 1.f) {
        return IM_COL32((int)(c[0] * 255), (int)(c[1] * 255), (int)(c[2] * 255),
                        (int)(c[3] * a * 255));
    };
    const ImU32 enemy = col4(Rust::config.col_enemy);
    const ImU32 sk = col4(Rust::config.col_skeleton);

    // Keep a code-drawn fallback for installations where the PNG is missing.
    if (!preview) {
        const float fallback = (std::max)(2.f, image_w * .018f);
        dl->AddCircleFilled(head, image_w * .055f, IM_COL32(55, 55, 65, 220), 20);
        dl->AddLine(neck, pelvis, IM_COL32(50, 50, 60, 200), fallback * 1.6f);
        dl->AddLine(lsh, rsh, IM_COL32(50, 50, 60, 200), fallback * 1.3f);
        dl->AddLine(lsh, lha, IM_COL32(50, 50, 60, 180), fallback);
        dl->AddLine(rsh, rha, IM_COL32(50, 50, 60, 180), fallback);
        dl->AddLine(pelvis, lft, IM_COL32(50, 50, 60, 180), fallback);
        dl->AddLine(pelvis, rft, IM_COL32(50, 50, 60, 180), fallback);
    }

    if (Rust::config.skeleton) {
        auto bone = [&](ImVec2 a, ImVec2 b) {
            dl->AddLine(a, b, sk, (std::max)(1.2f, Rust::config.skeleton_thickness));
        };
        bone(head, neck); bone(neck, chest); bone(chest, pelvis);
        bone(neck, lsh); bone(neck, rsh); bone(lsh, lel); bone(lel, lha);
        bone(rsh, rel); bone(rel, rha);
        bone(pelvis, lkn); bone(lkn, lft); bone(pelvis, rkn); bone(rkn, rft);
        if (Rust::config.skeleton_joints) {
            for (ImVec2 p : {head, neck, chest, pelvis, lsh, rsh, lel, rel, lha, rha, lkn, rkn, lft, rft})
                dl->AddCircleFilled(p, 2.6f, IM_COL32(80, 220, 90, 255), 8);
        }
    }

    if (Rust::config.box || Rust::config.box_corner) {
        const ImVec2 a = point(.22f, .018f);
        const ImVec2 b = point(.78f, .985f);
        if (Rust::config.box_corner) {
            const float L = (b.x - a.x) * 0.28f;
            const float T = (b.y - a.y) * 0.18f;
            auto corner = [&](ImVec2 p, float dx, float dy) {
                dl->AddLine(p, ImVec2(p.x + dx, p.y), enemy, 2.f);
                dl->AddLine(p, ImVec2(p.x, p.y + dy), enemy, 2.f);
            };
            corner(a, L, T); corner(ImVec2(b.x, a.y), -L, T);
            corner(ImVec2(a.x, b.y), L, -T); corner(b, -L, -T);
        } else {
            dl->AddRect(a, b, enemy, 0.f, 0, (std::max)(1.2f, Rust::config.box_thickness));
        }
    }

    if (Rust::config.head_dot)
        dl->AddCircle(head, image_w * .062f, IM_COL32(255, 80, 80, 230), 28, 2.f);

    if (Rust::config.health_bar) {
        const float top = image_min.y + image_h * .025f, bot = image_max.y - image_h * .025f;
        const float bx = image_min.x + image_w * .20f;
        const float h = bot - top;
        dl->AddRectFilled(ImVec2(bx - 4.f, top), ImVec2(bx, bot), CyberTheme::SafeShadowU32(180));
        dl->AddRectFilled(ImVec2(bx - 4.f, bot - h * 0.78f), ImVec2(bx, bot), IM_COL32(80, 220, 60, 255));
    }

    if (Rust::config.snaplines) {
        ImVec2 from = Rust::config.snaplines_center
            ? ImVec2(cx, image_min.y + image_h * .92f)
            : ImVec2(cx, image_max.y - 3.f);
        dl->AddLine(from, pelvis, IM_COL32(255, 255, 255, 90),
                    (std::max)(1.f, Rust::config.snapline_thickness));
    }

    if (Rust::config.name || Rust::config.distance) {
        char line[64];
        if (Rust::config.name && Rust::config.distance)
            std::snprintf(line, sizeof(line), "Jogador  |  85 m");
        else if (Rust::config.name)
            std::snprintf(line, sizeof(line), "Jogador");
        else
            std::snprintf(line, sizeof(line), "85m");
        ImVec2 ts = ImGui::CalcTextSize(line);
        dl->AddText(ImVec2(cx - ts.x * 0.5f, image_max.y - ts.y - 4.f),
                    IM_COL32(200, 255, 160, 230), line);
    }

    ImGui::Dummy(ImVec2(width, height));
}

} // namespace

void DrawRustVisuals() {
    const float full = CyberWidgets::CardContentWidth();
    const float preview_w = (std::max)(220.f, full * 0.30f);
    const float left_w = full - preview_w - 16.f;

    // Left column: controls
    ImGui::BeginGroup();
    ImGui::PushItemWidth(left_w - 24.f);

    CyberWidgets::BeginCard("ESP de jogador");
    CyberWidgets::ToggleSwitch("Ativar ESP", &Rust::config.esp_enabled);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.self_esp"), &Rust::config.self_esp);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.team_check"), &Rust::config.team_check);
    CyberWidgets::ToggleSwitch("Cores por visibilidade", &Rust::config.vis_color);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("FILTROS");
    CyberWidgets::ToggleSwitch("Mostrar dormindo", &Rust::config.show_sleepers);
    CyberWidgets::ToggleSwitch("Mostrar NPCs", &Rust::config.show_npc);
    CyberWidgets::ToggleSwitch("Mostrar feridos", &Rust::config.show_wounded);
    CyberWidgets::ToggleSwitch("Mostrar knockados", &Rust::config.show_knocked);
    ImGui::SliderInt("Distancia max", &Rust::config.max_distance, 50, 800);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Elementos");
    CyberWidgets::ToggleSwitch("Nome", &Rust::config.name);
    CyberWidgets::ToggleSwitch("Distancia", &Rust::config.distance);
    CyberWidgets::ToggleSwitch("Arma", &Rust::config.weapon_name);
    CyberWidgets::ToggleSwitch("Barra de vida", &Rust::config.health_bar);
    CyberWidgets::ToggleSwitch("Barra de armadura", &Rust::config.armor_bar);
    CyberWidgets::ToggleSwitch("Flags", &Rust::config.show_flags);
    CyberWidgets::ToggleSwitch("Caixa", &Rust::config.box);
    CyberWidgets::ToggleSwitch("Caixa de canto", &Rust::config.box_corner);
    CyberWidgets::ToggleSwitch("Esqueleto", &Rust::config.skeleton);
    CyberWidgets::ToggleSwitch(Loc::Tr("vis.joints"), &Rust::config.skeleton_joints);
    CyberWidgets::ToggleSwitch("Cabeca", &Rust::config.head_dot);
    CyberWidgets::ToggleSwitch("Linhas guia", &Rust::config.snaplines);
    CyberWidgets::ToggleSwitch("Setas off-screen", &Rust::config.offscreen_arrows);
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("CORES");
    ImGui::ColorEdit4("Inimigo", Rust::config.col_enemy, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Esqueleto", Rust::config.col_skeleton, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Target", Rust::config.col_target, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    CyberWidgets::EndCard();

    ImGui::PopItemWidth();
    ImGui::EndGroup();

    ImGui::SameLine(0.f, 16.f);

    // Right column: live ESP preview (man + toggles)
    ImGui::BeginGroup();
    CyberWidgets::BeginCard("##rust_esp_preview", preview_w);
    DrawRustEspPreview((std::max)(200.f, preview_w - 24.f), 460.f);
    CyberWidgets::EndCard();
    ImGui::EndGroup();
}
