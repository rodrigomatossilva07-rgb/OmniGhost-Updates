#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "../../../globals.h"
#include "config/app_settings.h"
#include "Cs2/cs2_config.h"
#include "Rust/rust_config.h"
#include "Warzone/warzone_config.h"
#include "Valorant/valorant_config.h"
#include "esp/esp.h"
#ifdef UI_PREVIEW
#include "preview/preview_runtime.h"
#else
#include "config/config_manager.h"
#endif

#include <algorithm>
#include <cstdio>

namespace {

const char* ActiveGameName() {
    switch (g_activeGame) {
    case ActiveGame::CS2: return "Counter-Strike 2";
    case ActiveGame::Rust: return "Rust";
    case ActiveGame::Warzone: return "Warzone";
    case ActiveGame::Valorant: return "Valorant";
    default: return "FiveM";
    }
}

struct VisualPreviewState {
    bool enabled = false;
    bool box = false;
    bool skeleton = false;
    bool health = false;
    bool name = false;
    bool distance = false;
    bool radar = false;
};

struct AimPreviewState {
    bool enabled = false;
    float fov = 0.0f;
    float smooth = 0.0f;
    int bone = 0;
    bool rcs = false;
    bool prediction = false;
    int aim_key = 0;
    float max_dist = 0.0f;
};

VisualPreviewState CurrentVisualState() {
    VisualPreviewState value{};
    switch (g_activeGame) {
    case ActiveGame::CS2:
        value.enabled = CS2::config.esp_enabled;
        value.box = CS2::config.box || CS2::config.box_corner;
        value.skeleton = CS2::config.skeleton;
        value.health = CS2::config.health_bar;
        value.name = CS2::config.name;
        value.distance = CS2::config.distance;
        value.radar = CS2::config.radar_2d;
        break;
    case ActiveGame::Rust:
        value.enabled = Rust::config.esp_enabled;
        value.box = Rust::config.box || Rust::config.box_corner;
        value.skeleton = Rust::config.skeleton;
        value.health = Rust::config.health_bar;
        value.name = Rust::config.name;
        value.distance = Rust::config.distance;
        value.radar = Rust::config.radar_2d;
        break;
    case ActiveGame::Warzone:
        value.enabled = Warzone::config.esp_enabled;
        value.box = Warzone::config.box || Warzone::config.box_corner;
        value.skeleton = Warzone::config.skeleton;
        value.health = Warzone::config.health_bar;
        value.name = Warzone::config.name;
        value.distance = Warzone::config.distance;
        value.radar = Warzone::config.radar_2d;
        break;
    case ActiveGame::Valorant:
        value.enabled = Valorant::config.esp_enabled;
        value.box = Valorant::config.box || Valorant::config.box_corner;
        value.skeleton = Valorant::config.skeleton;
        value.health = Valorant::config.health_bar;
        value.name = Valorant::config.name;
        value.distance = Valorant::config.distance;
        value.radar = false;
        break;
    default:
        value.enabled = esp::config.enabled;
        value.box = esp::config.box_2d || esp::config.corner_box;
        value.skeleton = esp::config.skeleton;
        value.health = esp::config.health_bar;
        value.name = esp::config.player_name;
        value.distance = esp::config.distance;
        value.radar = esp::config.radar_enabled || esp::config.triangle_radar || esp::config.square_radar;
        break;
    }
    return value;
}

AimPreviewState CurrentAimState() {
    AimPreviewState value{};
    switch (g_activeGame) {
    case ActiveGame::CS2:
        value.enabled = CS2::config.aim_enabled;
        value.fov = CS2::config.aim_fov;
        value.smooth = CS2::config.aim_smooth;
        value.bone = CS2::config.aim_bone;
        value.rcs = false;
        value.prediction = CS2::config.aim_prediction;
        value.aim_key = CS2::config.aim_bind;
        value.max_dist = CS2::config.aim_max_dist;
        break;
    case ActiveGame::Rust:
        value.enabled = Rust::config.aim_enabled;
        value.fov = Rust::config.aim_fov;
        value.smooth = Rust::config.aim_smooth;
        value.bone = Rust::config.aim_bone;
        value.rcs = Rust::config.no_recoil;
        value.prediction = Rust::config.aim_prediction;
        value.aim_key = Rust::config.aim_bind;
        value.max_dist = Rust::config.aim_max_dist;
        break;
    case ActiveGame::Warzone:
        value.enabled = Warzone::config.aim_enabled;
        value.fov = Warzone::config.aim_fov;
        value.smooth = Warzone::config.aim_smooth;
        value.bone = Warzone::config.aim_bone;
        value.rcs = false;
        value.prediction = Warzone::config.aim_prediction;
        value.aim_key = Warzone::config.aim_bind;
        value.max_dist = Warzone::config.aim_max_dist;
        break;
    case ActiveGame::Valorant:
        value.enabled = Valorant::config.aim_enabled;
        value.fov = Valorant::config.aim_fov;
        value.smooth = Valorant::config.aim_smooth;
        value.bone = Valorant::config.aim_bone;
        value.rcs = false;
        value.prediction = Valorant::config.aim_prediction;
        value.aim_key = Valorant::config.aim_bind;
        value.max_dist = Valorant::config.aim_max_dist;
        break;
    default:
        // FiveM uses aimbot from aimbot.cpp
        value.enabled = false;
        value.fov = 0.0f;
        value.smooth = 0.0f;
        value.bone = 0;
        value.rcs = false;
        value.prediction = false;
        value.aim_key = 0;
        value.max_dist = 0.0f;
        break;
    }
    return value;
}

void DrawVisualPreview() {
    const VisualPreviewState state = CurrentVisualState();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float previewWidth = ImGui::GetContentRegionAvail().x;
    const float previewHeight = 220.f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + previewWidth, origin.y + previewHeight);

    draw->AddRectFilled(origin, max, CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.68f), CyberTheme::Radius::Md);
    draw->AddRect(origin, max, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.55f), CyberTheme::Radius::Md);

    const ImU32 accent = app_settings::config.color_primary;
    const ImU32 muted = CyberTheme::U32(CyberTheme::Colors.TextDisabled);
    const ImVec2 center(origin.x + previewWidth * 0.52f, origin.y + 112.f);
    const float bodyH = 118.f;
    const float bodyW = 58.f;
    const ImVec2 bodyMin(center.x - bodyW * .5f, center.y - bodyH * .5f);
    const ImVec2 bodyMax(center.x + bodyW * .5f, center.y + bodyH * .5f);

    draw->AddCircleFilled(ImVec2(center.x, bodyMin.y + 14.f), 11.f,
        CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.22f), 20);
    draw->AddRectFilled(ImVec2(center.x - 14.f, bodyMin.y + 27.f),
        ImVec2(center.x + 14.f, bodyMax.y - 20.f),
        CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.12f), 6.f);

    if (!state.enabled) {
        const char* disabled = "Visuais desativados";
        const ImVec2 text = ImGui::CalcTextSize(disabled);
        draw->AddText(ImVec2(origin.x + (previewWidth - text.x) * .5f, origin.y + 98.f), muted, disabled);
    } else {
        if (state.box)
            draw->AddRect(ImVec2(bodyMin.x - 12.f, bodyMin.y - 3.f),
                ImVec2(bodyMax.x + 12.f, bodyMax.y + 3.f), accent, 3.f, 0, 1.5f);
        if (state.skeleton) {
            const ImVec2 head(center.x, bodyMin.y + 14.f);
            const ImVec2 neck(center.x, bodyMin.y + 31.f);
            const ImVec2 hip(center.x, bodyMax.y - 34.f);
            draw->AddLine(head, neck, accent, 1.5f);
            draw->AddLine(neck, hip, accent, 1.5f);
            draw->AddLine(neck, ImVec2(center.x - 28.f, center.y - 3.f), accent, 1.5f);
            draw->AddLine(neck, ImVec2(center.x + 28.f, center.y - 3.f), accent, 1.5f);
            draw->AddLine(hip, ImVec2(center.x - 22.f, bodyMax.y + 1.f), accent, 1.5f);
            draw->AddLine(hip, ImVec2(center.x + 22.f, bodyMax.y + 1.f), accent, 1.5f);
        }
        if (state.health) {
            draw->AddRectFilled(ImVec2(bodyMin.x - 19.f, bodyMin.y), ImVec2(bodyMin.x - 14.f, bodyMax.y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Error, 0.45f), 2.f);
            draw->AddRectFilled(ImVec2(bodyMin.x - 19.f, bodyMin.y + bodyH * .28f), ImVec2(bodyMin.x - 14.f, bodyMax.y),
                CyberTheme::U32(CyberTheme::Colors.Success), 2.f);
        }
        if (state.name)
            draw->AddText(ImVec2(center.x - 34.f, bodyMin.y - 22.f), CyberTheme::U32(CyberTheme::Colors.Text), "Jogador");
        if (state.distance)
            draw->AddText(ImVec2(center.x - 16.f, bodyMax.y + 10.f), muted, "42 m");
        if (state.radar) {
            const ImVec2 radarCenter(origin.x + 54.f, origin.y + 55.f);
            draw->AddCircle(radarCenter, 31.f, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.85f), 32, 1.2f);
            draw->AddLine(ImVec2(radarCenter.x - 31.f, radarCenter.y), ImVec2(radarCenter.x + 31.f, radarCenter.y), muted, 1.f);
            draw->AddLine(ImVec2(radarCenter.x, radarCenter.y - 31.f), ImVec2(radarCenter.x, radarCenter.y + 31.f), muted, 1.f);
            draw->AddCircleFilled(ImVec2(radarCenter.x + 10.f, radarCenter.y - 8.f), 3.5f, accent, 12);
        }
    }

    ImGui::InvisibleButton("##visual_profile_preview", ImVec2(previewWidth, previewHeight));
}

void DrawAimPreview() {
    const AimPreviewState state = CurrentAimState();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float previewWidth = ImGui::GetContentRegionAvail().x;
    const float previewHeight = 220.f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 max(origin.x + previewWidth, origin.y + previewHeight);

    draw->AddRectFilled(origin, max, CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.68f), CyberTheme::Radius::Md);
    draw->AddRect(origin, max, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.55f), CyberTheme::Radius::Md);

    const ImU32 accent = app_settings::config.color_primary;
    const ImU32 muted = CyberTheme::U32(CyberTheme::Colors.TextDisabled);
    const ImVec2 center(origin.x + previewWidth * 0.5f, origin.y + previewHeight * 0.5f);
    
    if (!state.enabled) {
        const char* disabled = "Mira desativada";
        const ImVec2 text = ImGui::CalcTextSize(disabled);
        draw->AddText(ImVec2(origin.x + (previewWidth - text.x) * .5f, origin.y + 98.f), muted, disabled);
    } else {
        // FOV circle
        float fov_radius = std::clamp(state.fov * 2.0f, 20.0f, 150.0f);
        ImVec4 accentColor = ImGui::ColorConvertU32ToFloat4(accent);
        accentColor.w = 0.3f;
        draw->AddCircle(center, fov_radius, ImGui::ColorConvertFloat4ToU32(accentColor), 64, 1.5f);
        accentColor.w = 0.6f;
        draw->AddCircle(center, fov_radius, ImGui::ColorConvertFloat4ToU32(accentColor), 64, 1.0f);
        
        // Smooth indicator
        char smooth_str[32];
        std::snprintf(smooth_str, sizeof(smooth_str), "Smooth: %.1f", state.smooth);
        draw->AddText(ImVec2(center.x - 40.f, center.y + fov_radius + 20.f), accent, smooth_str);
        
        // Target dot
        draw->AddCircleFilled(center, 4.0f, accent, 12);
        
        // Crosshair
        float cross_size = 12.0f;
        draw->AddLine(ImVec2(center.x - cross_size, center.y), ImVec2(center.x + cross_size, center.y), accent, 1.5f);
        draw->AddLine(ImVec2(center.x, center.y - cross_size), ImVec2(center.x, center.y + cross_size), accent, 1.5f);
        
        // Info panel
        ImVec2 info_pos(origin.x + 20.f, origin.y + 20.f);
        char fov_str[32];
        std::snprintf(fov_str, sizeof(fov_str), "FOV: %.0f°", state.fov);
        draw->AddText(info_pos, CyberTheme::U32(CyberTheme::Colors.Text), fov_str);
        
        char bone_str[32];
        std::snprintf(bone_str, sizeof(bone_str), "Bone: %d", state.bone);
        draw->AddText(ImVec2(info_pos.x, info_pos.y + 20.f), CyberTheme::U32(CyberTheme::Colors.Text), bone_str);
        
        char dist_str[32];
        std::snprintf(dist_str, sizeof(dist_str), "Max Dist: %.0fm", state.max_dist);
        draw->AddText(ImVec2(info_pos.x, info_pos.y + 40.f), CyberTheme::U32(CyberTheme::Colors.Text), dist_str);
        
        if (state.rcs) {
            draw->AddText(ImVec2(info_pos.x, info_pos.y + 60.f), CyberTheme::U32(CyberTheme::Colors.Success), "RCS: ON");
        }
        if (state.prediction) {
            draw->AddText(ImVec2(info_pos.x, info_pos.y + 80.f), CyberTheme::U32(CyberTheme::Colors.Info), "Prediction: ON");
        }
        
        // Key binding indicator
        if (state.aim_key > 0) {
            char key_str[32];
            std::snprintf(key_str, sizeof(key_str), "Tecla: 0x%02X", state.aim_key);
            draw->AddText(ImVec2(origin.x + previewWidth - 120.f, origin.y + 20.f), muted, key_str);
        }
    }

    ImGui::InvisibleButton("##aim_profile_preview", ImVec2(previewWidth, previewHeight));
}

} // namespace

void DrawSaveConfigs()
{
    CyberWidgets::BeginCard("Perfis por jogo");
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "%s · perfil atual: %s",
        ActiveGameName(), config_manager::CurrentGameProfileName());
    CyberWidgets::TextLine("Os perfis são apenas conjuntos de configuração; não indicam segurança nem deteção.",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::CardGap(8.f);

    if (CyberWidgets::GoldButton("Predefinido", ImVec2(112, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Default);
        CyberWidgets::Notify("Perfil predefinido aplicado", CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Mínimo", ImVec2(112, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Minimal);
        CyberWidgets::Notify("Perfil mínimo aplicado", CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Visual", ImVec2(112, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Visual);
        CyberWidgets::Notify("Perfil Visual aplicado", CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Personalizado", ImVec2(132, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Custom);
        CyberWidgets::Notify("Perfil personalizado carregado", CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::GoldButton("Guardar personalizado", ImVec2(180, 34))) {
        const bool ok = config_manager::SaveCustomGameProfile();
        CyberWidgets::Notify(ok ? "Perfil personalizado atualizado" : "Falha ao guardar o perfil personalizado",
            ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Pré-visualização · Visuais");
    CyberWidgets::TextLine("Pré-visualização imediata dos principais elementos visuais do perfil ativo.",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::CardGap(8.f);
    DrawVisualPreview();
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("Pré-visualização · Mira");
    CyberWidgets::TextLine("Pré-visualização imediata das configurações de mira do perfil ativo.",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::CardGap(8.f);
    DrawAimPreview();
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("save.title"));
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "Config ativa: %s",
        config_manager::ActiveConfigName());
    CyberWidgets::TextLine("Nome (escreve e guarda)", CyberWidgets::TextTone::Secondary);
    CyberWidgets::InputField("##cfg_name_full", config_manager::new_config_name,
        sizeof(config_manager::new_config_name), "ex: Visual cidade X", 0, -1.0f);
    CyberWidgets::CardGap(6.0f);
    if (CyberWidgets::GoldButton(Loc::TrID("save.create"), ImVec2(140, 32))) {
        char* n = config_manager::new_config_name;
        while (*n == ' ') ++n;
        if (*n) config_manager::SaveToFile(n);
        else config_manager::SaveToFile("config");
        CyberWidgets::Notify(Loc::Tr("status.config_saved"), CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("save.open_folder"), ImVec2(120, 32)))
        config_manager::OpenConfigFolder();
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("common.refresh"), ImVec2(100, 32)))
        config_manager::RefreshList();
    CyberWidgets::Separator();
    if (CyberWidgets::GoldButton(Loc::TrID("save.copy"), ImVec2(160, 32))) {
        config_manager::CopyToClipboard();
        CyberWidgets::Notify(Loc::Tr("status.copied"), CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("save.paste"), ImVec2(160, 32))) {
        config_manager::PasteFromClipboard();
        CyberWidgets::Notify(Loc::Tr("status.pasted"), CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Repor jogo", ImVec2(130, 32))) {
        config_manager::ResetActiveGameToDefaults();
        CyberWidgets::Notify("Configuração do jogo reposta", CyberWidgets::ToastType::Warning);
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("save.list"));
    CyberWidgets::TextLine("Arrasta para reordenar — a do topo carrega ao iniciar",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::BeginSurfaceList("##config_list", 240.0f);
    int moveFrom = -1, moveTo = -1;
    for (int i = 0; i < (int)config_manager::saved_list.size(); ++i) {
        const auto& cfg = config_manager::saved_list[i];
        ImGui::PushID(i);
        const ImVec2 row = ImGui::GetCursorScreenPos();
        const float row_width = ImGui::GetContentRegionAvail().x;
        const float rowH = 36.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::InvisibleButton("##drag", ImVec2(row_width - 180.0f, rowH));
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            dl->AddRectFilled(row, ImVec2(row.x + row_width, row.y + rowH), IM_COL32(212, 175, 55, 20), 4.f);

        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload("CFG_ORDER", &i, sizeof(int));
            ImGui::Text("%s", cfg.name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CFG_ORDER")) {
                moveFrom = *(const int*)payload->Data;
                moveTo = i;
            }
            ImGui::EndDragDropTarget();
        }

        if (i == 0) {
            dl->AddText(ImVec2(row.x + 2.0f, row.y + 2.0f),
                CyberTheme::U32(CyberTheme::Colors.Success), "STARTUP");
            dl->AddText(ImVec2(row.x + 2.0f, row.y + 16.0f),
                CyberTheme::U32(CyberTheme::Colors.Gold), cfg.name.c_str());
        } else {
            dl->AddText(ImVec2(row.x + 2.0f, row.y + 10.0f),
                CyberTheme::U32(CyberTheme::Colors.Gold), cfg.name.c_str());
        }
        dl->AddText(ImVec2(row.x + row_width * 0.32f, row.y + 10.0f),
            CyberTheme::U32(CyberTheme::Colors.TextDisabled), cfg.date_str.c_str());

        constexpr float delete_width = 76.0f;
        constexpr float load_width = 88.0f;
        constexpr float action_gap = 6.0f;
        ImGui::SetCursorScreenPos(ImVec2(
            row.x + row_width - delete_width - load_width - action_gap, row.y + 2.0f));
        if (CyberWidgets::CyberButton(Loc::TrID("common.delete"), ImVec2(delete_width, 32.0f))) {
            config_manager::DeleteConfigFile(cfg.name);
            ImGui::PopID();
            break;
        }
        ImGui::SameLine(0.0f, action_gap);
        if (CyberWidgets::GoldButton(Loc::TrID("common.load"), ImVec2(load_width, 32.0f))) {
            config_manager::LoadFromFile(cfg.name);
            CyberWidgets::Notify(Loc::Tr("status.config_loaded"), CyberWidgets::ToastType::Success);
        }
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + rowH + 2.0f));
        ImGui::PopID();
    }
    if (moveFrom >= 0 && moveTo >= 0)
        config_manager::MoveConfig(moveFrom, moveTo);
    CyberWidgets::EndSurfaceList();
    CyberWidgets::EndCard();
}
