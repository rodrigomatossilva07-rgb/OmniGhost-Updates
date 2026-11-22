#include "../../widgets.h"
#include "../../theme.h"
#include "../../platform/strprotect/strprotect.h"
#include "../../localization.h"
#include "../../../globals.h"
#include "../../../licensing/license_service.h"
#include "../../../app_version.h"
#include "config/app_settings.h"
#include "src/games/Cs2/config/cs2_config.h"
#include "aimbot/aimbot.h"
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
    case ActiveGame::Warzone: return "Warzone";
    case ActiveGame::Fortnite: return "Fortnite";
    case ActiveGame::Apex: return "Apex";
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
        value.radar = CS2::config.radar_2d;
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
    default:
        value.enabled = aimbot::config.aimbot_enabled;
        value.fov = aimbot::config.fov_size;
        value.smooth = aimbot::config.smooth_x;
        value.bone = static_cast<int>(aimbot::config.hitbox);
        value.rcs = false;
        value.prediction = aimbot::config.velocity_prediction;
        value.aim_key = aimbot::config.aimbot_bind;
        value.max_dist = aimbot::config.max_distance;
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

        float fov_radius = std::clamp(state.fov * 2.0f, 20.0f, 150.0f);
        ImVec4 accentColor = ImGui::ColorConvertU32ToFloat4(accent);
        accentColor.w = 0.3f;
        draw->AddCircle(center, fov_radius, ImGui::ColorConvertFloat4ToU32(accentColor), 64, 1.5f);
        accentColor.w = 0.6f;
        draw->AddCircle(center, fov_radius, ImGui::ColorConvertFloat4ToU32(accentColor), 64, 1.0f);


        char smooth_str[32];
        std::snprintf(smooth_str, sizeof(smooth_str), "Smooth: %.1f", state.smooth);
        draw->AddText(ImVec2(center.x - 40.f, center.y + fov_radius + 20.f), accent, smooth_str);


        draw->AddCircleFilled(center, 4.0f, accent, 12);


        float cross_size = 12.0f;
        draw->AddLine(ImVec2(center.x - cross_size, center.y), ImVec2(center.x + cross_size, center.y), accent, 1.5f);
        draw->AddLine(ImVec2(center.x, center.y - cross_size), ImVec2(center.x, center.y + cross_size), accent, 1.5f);


        ImVec2 info_pos(origin.x + 20.f, origin.y + 20.f);
        char fov_str[32];
        std::snprintf(fov_str, sizeof(fov_str), "FOV: %.0fÂ°", state.fov);
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


        if (state.aim_key > 0) {
            char key_str[32];
            std::snprintf(key_str, sizeof(key_str), "Tecla: 0x%02X", state.aim_key);
            draw->AddText(ImVec2(origin.x + previewWidth - 120.f, origin.y + 20.f), muted, key_str);
        }
    }

    ImGui::InvisibleButton("##aim_profile_preview", ImVec2(previewWidth, previewHeight));
}

// DEV Config Hub (admin only, Development builds) - Enhanced with versioning & notifications
void DrawDevConfigHub() {
    static char devSearch[81]{};
    static char devNewName[81]{};
    static char devTitle[81]{};
    static char devDescription[256]{};
    static char devChangelog[512]{};
    static char devTags[128]{};
    static bool devLoaded = false;
    static bool showUploadForm = false;
    static std::vector<config_manager::DevConfigInfo> devConfigs;
    static int pendingUpdatesCount = 0;
    
    // Check for updates on first load
    static bool checkedUpdates = false;
    if (!checkedUpdates && OmniGhost::Licensing::IsAdmin() && config_manager::IsDevConfigAvailable()) {
        config_manager::CheckDevConfigUpdates();
        auto updates = config_manager::GetPendingDevConfigUpdates();
        pendingUpdatesCount = static_cast<int>(updates.size());
        checkedUpdates = true;
    }
    
    CyberWidgets::BeginCard("DEV Configs (Admin)", 0.f);
    
    // Header with update indicator
    if (pendingUpdatesCount > 0) {
        CyberWidgets::TextLineF(CyberWidgets::TextTone::Accent, 
            "⚠ %d config(s) DEV com atualização disponível! Clique em 'Verificar Updates' para detalhes.", pendingUpdatesCount);
    } else {
        CyberWidgets::TextLine("Configs de desenvolvimento para teste antes do publish. Visíveis apenas para admins.", CyberWidgets::TextTone::Secondary);
    }
    
    // Action buttons row
    if (CyberWidgets::GoldButton("Atualizar Lista", ImVec2(130.f, 32.f))) {
        devConfigs = config_manager::ListDevConfigsDetailed();
        devLoaded = true;
        CyberWidgets::Notify(config_manager::last_status.c_str(), devLoaded ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton("Verificar Updates", ImVec2(140.f, 32.f))) {
        config_manager::CheckDevConfigUpdates();
        auto updates = config_manager::GetPendingDevConfigUpdates();
        pendingUpdatesCount = static_cast<int>(updates.size());
        if (pendingUpdatesCount > 0) {
            CyberWidgets::Notify(("Encontradas " + std::to_string(pendingUpdatesCount) + " atualizações DEV").c_str(), CyberWidgets::ToastType::Info);
        } else {
            CyberWidgets::Notify("Nenhuma atualização DEV disponível", CyberWidgets::ToastType::Success);
        }
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(showUploadForm ? "Cancelar Envio" : "Nova Config DEV", ImVec2(140.f, 32.f))) {
        showUploadForm = !showUploadForm;
        if (!showUploadForm) {
            devNewName[0] = 0; devTitle[0] = 0; devDescription[0] = 0; devChangelog[0] = 0; devTags[0] = 0;
        }
    }
    
    CyberWidgets::InputField("##dev_search", devSearch, sizeof(devSearch), "Filtrar por nome, autor ou tags", 0, -1.f);
    CyberWidgets::CardGap(8.f);
    
    // Upload form
    if (showUploadForm) {
        CyberWidgets::BeginCard("Enviar Nova Config DEV", 0.f);
        CyberWidgets::TextLine("Preencha os dados da config DEV:", CyberWidgets::TextTone::Secondary);
        
        CyberWidgets::InputField("Nome (id)", devNewName, sizeof(devNewName), "Ex: dev-aim-v1, dev-esp-test (letras, números, -_)");
        CyberWidgets::InputField("Título", devTitle, sizeof(devTitle), "Título amigável para exibição");
        CyberWidgets::InputField("Descrição", devDescription, sizeof(devDescription), "Descrição breve do que a config faz");
        CyberWidgets::InputField("Changelog", devChangelog, sizeof(devChangelog), "O que mudou nesta versão");
        CyberWidgets::InputField("Tags (separadas por vírgula)", devTags, sizeof(devTags), "Ex: aim,legit,rage,esp,visual");
        
        if (CyberWidgets::GoldButton("Enviar Config Atual como DEV", ImVec2(220.f, 32.f))) {
            if (devNewName[0] == 0) {
                CyberWidgets::Notify("Nome é obrigatório", CyberWidgets::ToastType::Error);
            } else {
                // Parse tags
                std::vector<std::string> tags;
                std::string tagStr(devTags);
                std::stringstream ss(tagStr);
                std::string tag;
                while (std::getline(ss, tag, ',')) {
                    // Trim whitespace
                    tag.erase(0, tag.find_first_not_of(" \t"));
                    tag.erase(tag.find_last_not_of(" \t") + 1);
                    if (!tag.empty()) tags.push_back(tag);
                }
                
                const bool ok = config_manager::UploadDevConfigVersioned(
                    devNewName, devTitle, devDescription, devChangelog, tags
                );
                if (ok) {
                    devNewName[0] = 0; devTitle[0] = 0; devDescription[0] = 0; devChangelog[0] = 0; devTags[0] = 0;
                    showUploadForm = false;
                    devConfigs = config_manager::ListDevConfigsDetailed();
                }
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
            }
        }
        ImGui::SameLine();
        if (CyberWidgets::CyberButton("Cancelar", ImVec2(100.f, 32.f))) {
            showUploadForm = false;
            devNewName[0] = 0; devTitle[0] = 0; devDescription[0] = 0; devChangelog[0] = 0; devTags[0] = 0;
        }
        CyberWidgets::EndCard();
        CyberWidgets::CardGap(8.f);
    }
    
    // Config list
    if (devLoaded) {
        CyberWidgets::BeginSurfaceList("##dev_config_list", 350.f);
        for (std::size_t index = 0; index < devConfigs.size(); ++index) {
            const auto& info = devConfigs[index];
            if (devSearch[0] != 0) {
                std::string searchLower = devSearch;
                std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
                std::string nameLower = info.name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                std::string authorLower = info.author;
                std::transform(authorLower.begin(), authorLower.end(), authorLower.begin(), ::tolower);
                
                bool matches = nameLower.find(searchLower) != std::string::npos ||
                               authorLower.find(searchLower) != std::string::npos ||
                               info.title.find(searchLower) != std::string::npos;
                
                for (const auto& tag : info.tags) {
                    std::string tagLower = tag;
                    std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(), ::tolower);
                    if (tagLower.find(searchLower) != std::string::npos) {
                        matches = true; break;
                    }
                }
                if (!matches) continue;
            }
            
            ImGui::PushID(static_cast<int>(index));
            CyberWidgets::BeginCard(info.name.c_str());
            
            // Badges
            if (info.official) CyberWidgets::Badge("OFICIAL", CyberWidgets::TextTone::Accent);
            else CyberWidgets::Badge("DEV", CyberWidgets::TextTone::Accent);
            
            if (info.has_update) {
                ImGui::SameLine();
                CyberWidgets::Badge("UPDATE!", CyberWidgets::TextTone::Warning);
            }
            
            // Version info
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, 
                "v%s (int v%d) · EXE: %s · %s · %d downloads",
                info.version.c_str(), info.version_int, info.exe_version.c_str(),
                info.author.empty() ? "autor desconhecido" : info.author.c_str(), info.downloads);
            
            if (!info.title.empty()) CyberWidgets::TextLine(info.title.c_str());
            if (!info.description.empty()) CyberWidgets::TextLine(info.description.c_str(), CyberWidgets::TextTone::Secondary);
            if (!info.changelog.empty()) CyberWidgets::TextLine(("Changelog: " + info.changelog).c_str(), CyberWidgets::TextTone::Accent);
            if (!info.tags.empty()) {
                std::string tagStr;
                for (const auto& tag : info.tags) { if (!tagStr.empty()) tagStr += ", "; tagStr += tag; }
                CyberWidgets::TextLine(("Tags: " + tagStr).c_str(), CyberWidgets::TextTone::Secondary);
            }
            if (!info.updated_at.empty()) {
                CyberWidgets::TextLine(("Atualizado: " + info.updated_at).c_str(), CyberWidgets::TextTone::Secondary);
            }
            
            // Compatibility check
            bool compatible = true;
            std::string compatMsg;
            if (info.exe_version != OmniGhost::Version) {
                compatible = false;
                compatMsg = "⚠ EXE compatível: " + info.exe_version + " (atual: " + OmniGhost::Version + ")";
            }
            if (compatible) {
                CyberWidgets::TextLine("✓ Compatível com versão atual", CyberWidgets::TextTone::Success);
            } else {
                CyberWidgets::TextLine(compatMsg.c_str(), CyberWidgets::TextTone::Warning);
            }
            
            // Action buttons
            if (info.has_update) {
                if (CyberWidgets::GoldButton("Atualizar", ImVec2(100.f, 30.f))) {
                    const bool ok = config_manager::DownloadDevConfigVersioned(info.name, info.version_int);
                    if (ok) {
                        config_manager::MarkDevConfigUpdateNotified(info.name);
                        devConfigs = config_manager::ListDevConfigsDetailed();
                        pendingUpdatesCount = std::max(0, pendingUpdatesCount - 1);
                    }
                    CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
                }
                ImGui::SameLine();
            } else {
                if (CyberWidgets::GoldButton("Baixar", ImVec2(100.f, 30.f))) {
                    const bool ok = config_manager::DownloadDevConfigVersioned(info.name);
                    CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
                }
                ImGui::SameLine();
            }
            
            if (CyberWidgets::CyberButton("Deletar", ImVec2(100.f, 30.f))) {
                const bool ok = config_manager::DeleteDevConfig(info.name);
                if (ok) {
                    devConfigs.erase(devConfigs.begin() + index);
                    --index;
                }
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Warning : CyberWidgets::ToastType::Error);
            }
            
            CyberWidgets::EndCard();
            ImGui::PopID();
        }
        if (devConfigs.empty()) {
            CyberWidgets::TextLine("Nenhuma config DEV encontrada.", CyberWidgets::TextTone::Secondary);
        }
        CyberWidgets::EndSurfaceList();
    }
    
    CyberWidgets::EndCard();
    CyberWidgets::CardGap();
}

void DrawCommunityConfigHub() {
    static int scope = 0;
    static int sort = 0;
    static int visibility = 2;
    static char search[81]{};
    static char styleFilter[25]{};
    static bool compatibleOnly = true;
    static char title[81]{};
    static char description[601]{};
    static char tags[160]{};
    static char changelog[501]{};
    static char reportReason[301] = "Conteúdo incompatível ou enganador";
    static std::string editConfigId;
    static bool loaded = false;
    const auto scopeValue = static_cast<config_manager::CommunityScope>(scope);
    const char* sortValue = sort == 1 ? "recent" : sort == 2 ? "rating" : "popular";

    CyberWidgets::BeginCard(Loc::Tr("save.community.hub"));
    CyberWidgets::TextLine(Loc::Tr("save.community.desc"),
        CyberWidgets::TextTone::Secondary);
    const char* scopes[] = { "Comunidade", "Marketplace", "As minhas", "Favoritos", "Histórico" };
    const char* sorts[] = { "Populares", "Recentes", "Melhor avaliadas" };
    CyberWidgets::Combo("Origem", &scope, scopes, 5);
    ImGui::SameLine();
    CyberWidgets::Combo("Ordenar", &sort, sorts, 3);
    CyberWidgets::InputField("##community_search", search, sizeof(search), "Pesquisar nome, autor ou código de partilha", 0, -1.f);
    CyberWidgets::InputField("##community_style", styleFilter, sizeof(styleFilter), "Filtrar etiqueta/estilo", 0, -1.f);
    CyberWidgets::ToggleSwitch("Apenas compatíveis com esta versão", &compatibleOnly);
    if (CyberWidgets::GoldButton(Loc::Tr("save.refresh"), ImVec2(130.f, 32.f))) {
        loaded = config_manager::RefreshCommunityConfigs(scopeValue, search, sortValue);
        CyberWidgets::Notify(config_manager::last_status.c_str(), loaded ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.revert"), ImVec2(205.f, 32.f))) {
        const bool ok = config_manager::RevertCommunityConfig();
        CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }

    if (loaded) {
        CyberWidgets::CardGap(8.f);
        CyberWidgets::BeginSurfaceList("##community_config_list", 310.f);
        const auto& configs = config_manager::CommunityConfigs();
        for (std::size_t index = 0; index < configs.size(); ++index) {
            const auto& item = configs[index];
            bool styleMatches = styleFilter[0] == 0;
            for (const auto& tag : item.tags) if (tag.find(styleFilter) != std::string::npos) styleMatches = true;
            if (!styleMatches || (compatibleOnly && !item.compatible)) continue;
            ImGui::PushID(static_cast<int>(index));
            CyberWidgets::BeginCard(item.title.c_str());
            if (item.official) CyberWidgets::Badge("OFICIAL", CyberWidgets::TextTone::Accent);
            else if (item.verified) CyberWidgets::Badge("VERIFICADA", CyberWidgets::TextTone::Success);
            else CyberWidgets::Badge(item.status.c_str(), CyberWidgets::TextTone::Warning);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "%s Â· v%d Â· %d downloads Â· %.1f/5",
                item.author.c_str(), item.version, item.downloads, item.rating);
            CyberWidgets::TextLineF(item.compatible ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning,
                "Schema %d Â· Launcher mínimo %s Â· %s", item.schema_version, item.min_launcher_version.c_str(),
                item.compatible ? "Compatível" : "Requer launcher mais recente");
            if (!item.description.empty()) CyberWidgets::TextLine(item.description.c_str(), CyberWidgets::TextTone::Secondary);
            if (!item.tags.empty()) {
                std::string label;
                for (const auto& tag : item.tags) { if (!label.empty()) label += " Â· "; label += tag; }
                CyberWidgets::TextLine(label.c_str(), CyberWidgets::TextTone::Accent);
            }
            if (CyberWidgets::CyberButton(Loc::Tr("save.preview"), ImVec2(130.f, 30.f))) {
                const bool ok = config_manager::PreviewCommunityConfig(item.id);
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Info : CyberWidgets::ToastType::Error);
            }
            ImGui::SameLine();
            if (CyberWidgets::GoldButton(Loc::Tr("save.try"), ImVec2(120.f, 30.f))) {
                const bool ok = config_manager::ApplyCommunityConfig(item.id, false);
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
            }
            ImGui::SameLine();
            if (CyberWidgets::CyberButton(Loc::Tr("save.save"), ImVec2(100.f, 30.f))) {
                bool ok = config_manager::ApplyCommunityConfig(item.id, true);
                if (ok) ok = config_manager::UploadCloudConfig(config_manager::ActiveConfigName());
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
            }
            if (CyberWidgets::CyberButton(item.favorite ? "Remover favorito" : "Favorito", ImVec2(145.f, 30.f))) {
                const bool ok = config_manager::ToggleCommunityFavorite(item.id, !item.favorite);
                if (ok) loaded = config_manager::RefreshCommunityConfigs(scopeValue, search, sortValue);
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
            }
            ImGui::SameLine();
            for (int star = 1; star <= 5; ++star) {
                if (star > 1) ImGui::SameLine(0.f, 3.f);
                const std::string label = (star <= item.my_rating ? "â˜…##rate" : "â˜†##rate") + std::to_string(star);
                if (CyberWidgets::Button(label.c_str(), CyberWidgets::ButtonStyle::Ghost, ImVec2(30.f, 30.f))) {
                    const bool ok = config_manager::RateCommunityConfig(item.id, star);
                    if (ok) loaded = config_manager::RefreshCommunityConfigs(scopeValue, search, sortValue);
                }
            }
            if (item.owned) {
                if (CyberWidgets::CyberButton(Loc::Tr("save.edit_version"), ImVec2(175.f, 30.f))) {
                    editConfigId = item.id;
                    strncpy_s(title, item.title.c_str(), _TRUNCATE);
                    strncpy_s(description, item.description.c_str(), _TRUNCATE);
                    std::string tagText;
                    for (const auto& tag : item.tags) { if (!tagText.empty()) tagText += ", "; tagText += tag; }
                    strncpy_s(tags, tagText.c_str(), _TRUNCATE);
                    visibility = item.visibility == "private" ? 0 : item.visibility == "unlisted" ? 1 : 2;
                    changelog[0] = 0;
                }
                ImGui::SameLine();
                if (CyberWidgets::CyberButton(Loc::Tr("save.copy_code"), ImVec2(130.f, 30.f)))
                    CyberWidgets::CopyToClipboard(item.id.c_str(), "Código de partilha copiado.");
                if (CyberWidgets::DangerButton(Loc::Tr("save.withdraw"), ImVec2(170.f, 30.f))) {
                    const bool ok = config_manager::WithdrawCommunityConfig(item.id);
                    if (ok) loaded = config_manager::RefreshCommunityConfigs(scopeValue, search, sortValue);
                    CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Warning : CyberWidgets::ToastType::Error);
                }
            } else {
                CyberWidgets::InputField("##report_reason", reportReason, sizeof(reportReason), Loc::Tr("save.report_reason"), 0, -1.f);
                if (CyberWidgets::DangerButton(Loc::Tr("save.report"), ImVec2(120.f, 30.f))) {
                    const bool ok = config_manager::ReportCommunityConfig(item.id, reportReason);
                    CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Info : CyberWidgets::ToastType::Error);
                }
            }
            CyberWidgets::EndCard();
            ImGui::PopID();
        }
        if (configs.empty()) CyberWidgets::EmptyState(Loc::Tr("save.empty"), Loc::Tr("save.empty_hint"));
        CyberWidgets::EndSurfaceList();
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("save.publish"));
    CyberWidgets::TextLine(Loc::Tr("save.publish_desc"), CyberWidgets::TextTone::Secondary);
    CyberWidgets::InputField("##community_title", title, sizeof(title), Loc::Tr("save.title_field"), 0, -1.f);
    ImGui::InputTextMultiline("##community_description", description, sizeof(description), ImVec2(-1.f, 72.f));
    CyberWidgets::InputField("##community_tags", tags, sizeof(tags), Loc::Tr("save.tags"), 0, -1.f);
    CyberWidgets::InputField("##community_changelog", changelog, sizeof(changelog), Loc::Tr("save.changelog"), 0, -1.f);
    const char* visibilityItems[] = { Loc::Tr("save.private"), Loc::Tr("save.unlisted"), Loc::Tr("save.public") };
    CyberWidgets::Combo("Visibilidade", &visibility, visibilityItems, 3);
    if (!editConfigId.empty()) {
        CyberWidgets::Badge(Loc::Tr("save.editing"), CyberWidgets::TextTone::Warning);
        ImGui::SameLine();
        if (CyberWidgets::CyberButton(Loc::Tr("save.cancel_edit"), ImVec2(135.f, 30.f))) editConfigId.clear();
    }
    if (CyberWidgets::GoldButton(Loc::Tr(editConfigId.empty() ? "save.publish_snapshot" : "save.publish_version"), ImVec2(190.f, 34.f))) {
        const char* value = visibility == 0 ? "private" : visibility == 1 ? "unlisted" : "public";
        const bool ok = config_manager::PublishCommunityConfig(title, description, value, tags, changelog, editConfigId);
        CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
        if (ok) { editConfigId.clear(); loaded = config_manager::RefreshCommunityConfigs(config_manager::CommunityScope::Mine, {}, "recent"); }
    }
    CyberWidgets::EndCard();
}

}

void DrawSaveConfigs()
{
    const auto rowButtonWidth = [](int count) {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        return (std::max)(96.0f,
            (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) /
                static_cast<float>(count));
    };

    CyberWidgets::BeginCard(app_settings::T("Perfis por jogo", "Game profiles", "Spielprofile", "Perfiles de juego", "Profils de jeu", "Profili di gioco", "Профили игр"));
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, std::string{app_settings::T("%s Â· perfil atual: %s", "%s Â· current profile: %s", "%s Â· aktuelles Profil: %s", "%s Â· perfil actual: %s", "%s Â· profil actuel: %s", "%s Â· profilo attuale: %s", "%s Â· текущий профиль: %s")}.c_str(),
        ActiveGameName(), config_manager::CurrentGameProfileName());
    CyberWidgets::TextLine(app_settings::T("Os perfis são apenas conjuntos de configuração; não indicam segurança nem deteção.",
                                          "Profiles are configuration sets; they do not indicate safety or detection.",
                                          "Profile sind Konfigurationssätze; sie geben keine Sicherheit oder Erkennung an.",
                                          "Los perfiles son conjuntos de configuración; no indican seguridad ni detección.",
                                          "Les profils sont des ensembles de configuration ; ils n'indiquent ni sécurité ni détection.",
                                          "I profili sono set di configurazione; non indicano sicurezza né rilevamento.",
                                          "Профили — это наборы конфигурации; они не указывают на безопасность или обнаружение."),
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::CardGap(8.f);

    const float profileButtonWidth = rowButtonWidth(5);
    if (CyberWidgets::GoldButton(Loc::Tr("save.default"), ImVec2(profileButtonWidth, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Default);
        CyberWidgets::Notify(Loc::Tr("save.default_applied"), CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.minimal"), ImVec2(profileButtonWidth, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Minimal);
        CyberWidgets::Notify(Loc::Tr("save.minimal_applied"), CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.visual"), ImVec2(profileButtonWidth, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Visual);
        CyberWidgets::Notify(Loc::Tr("save.visual_applied"), CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.custom"), ImVec2(profileButtonWidth, 34))) {
        config_manager::ApplyGameProfile(config_manager::GameProfile::Custom);
        CyberWidgets::Notify("Perfil personalizado carregado", CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::GoldButton(Loc::Tr("save.save_custom"), ImVec2(profileButtonWidth, 34))) {
        const bool ok = config_manager::SaveCustomGameProfile();
        CyberWidgets::Notify(ok ? Loc::Tr("save.custom_updated") : Loc::Tr("save.custom_save_failed"),
            ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard(Loc::Tr("save.title"));
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, std::string{Loc::Tr("save.active_config")}.c_str(),
        config_manager::ActiveConfigName());
    CyberWidgets::TextLine(Loc::Tr("save.name_field"), CyberWidgets::TextTone::Secondary);
    CyberWidgets::InputField("##cfg_name_full", config_manager::new_config_name,
        sizeof(config_manager::new_config_name), Loc::Tr("save.example"), 0, -1.0f);
    CyberWidgets::CardGap(6.0f);
    const float fileButtonWidth = rowButtonWidth(3);
    if (CyberWidgets::GoldButton(Loc::TrID("save.create"), ImVec2(fileButtonWidth, 32))) {
        char* n = config_manager::new_config_name;
        while (*n == ' ') ++n;
        const bool saved = config_manager::SaveToFile(*n ? n : "config");
        CyberWidgets::Notify(saved ? Loc::Tr("status.config_saved") : config_manager::last_status.c_str(),
            saved ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("save.open_folder"), ImVec2(fileButtonWidth, 32)))
        config_manager::OpenConfigFolder();
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("common.refresh"), ImVec2(fileButtonWidth, 32)))
        config_manager::RefreshList();
    if (!config_manager::active_config_name.empty()) {
        CyberWidgets::CardGap(6.f);
        const float activeButtonWidth = rowButtonWidth(2);
        if (CyberWidgets::CyberButton(Loc::Tr("save.duplicate"), ImVec2(activeButtonWidth, 32))) {
            const bool ok = config_manager::DuplicateConfigFile(
                config_manager::active_config_name, config_manager::new_config_name);
            CyberWidgets::Notify(config_manager::last_status.c_str(),
                ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
        }
        ImGui::SameLine();
        if (CyberWidgets::CyberButton(Loc::Tr("save.rename"), ImVec2(activeButtonWidth, 32))) {
            const bool ok = config_manager::RenameConfigFile(
                config_manager::active_config_name, config_manager::new_config_name);
            CyberWidgets::Notify(config_manager::last_status.c_str(),
                ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::Separator();
    const float clipboardButtonWidth = rowButtonWidth(2);
    if (CyberWidgets::GoldButton(Loc::TrID("save.copy"), ImVec2(clipboardButtonWidth, 32))) {
        config_manager::CopyToClipboard();
        CyberWidgets::Notify(Loc::Tr("status.copied"), CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::TrID("save.paste"), ImVec2(clipboardButtonWidth, 32))) {
        config_manager::PasteFromClipboard();
        CyberWidgets::Notify(Loc::Tr("status.pasted"), CyberWidgets::ToastType::Info);
    }
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle("JSON");
    CyberWidgets::TextLine(app_settings::T(
        "Exporta para OmniGhost-export.json. Para importar, coloca OmniGhost-import.json na pasta Configs.",
        "Exports to OmniGhost-export.json. To import, place OmniGhost-import.json in the Configs folder.",
        "Exportiert nach OmniGhost-export.json. Zum Importieren OmniGhost-import.json in den Configs-Ordner legen.",
        "Exporta a OmniGhost-export.json. Para importar, coloca OmniGhost-import.json en la carpeta Configs.",
        "Exporte vers OmniGhost-export.json. Pour importer, placez OmniGhost-import.json dans le dossier Configs.",
        "Esporta in OmniGhost-export.json. Per importare, metti OmniGhost-import.json nella cartella Configs.",
        "Экспорт в OmniGhost-export.json. Для импорта поместите OmniGhost-import.json в папку Configs."),
        CyberWidgets::TextTone::Secondary);
    const float jsonButtonWidth = rowButtonWidth(3);
    if (CyberWidgets::GoldButton(Loc::Tr("save.export_json"), ImVec2(jsonButtonWidth, 32))) {
        const bool ok = config_manager::ExportJson();
        CyberWidgets::Notify(config_manager::last_status.c_str(),
            ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.import_json"), ImVec2(jsonButtonWidth, 32))) {
        const bool ok = config_manager::ImportJson();
        CyberWidgets::Notify(config_manager::last_status.c_str(),
            ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.reset_game"), ImVec2(jsonButtonWidth, 32))) {
        config_manager::ResetActiveGameToDefaults();
        CyberWidgets::Notify(Loc::Tr("save.game_reset"), CyberWidgets::ToastType::Warning);
    }
    CyberWidgets::Separator();
    CyberWidgets::SectionTitle(Loc::Tr("save.cloud_configs"));
    CyberWidgets::TextLine(Loc::Tr("save.cloud_desc"),
        CyberWidgets::TextTone::Secondary);
    const float cloudButtonWidth = rowButtonWidth(3);
    if (CyberWidgets::GoldButton(Loc::Tr("save.cloud_save"), ImVec2(cloudButtonWidth, 32))) {
        const bool ok = config_manager::UploadCloudConfig(config_manager::new_config_name);
        CyberWidgets::Notify(config_manager::last_status.c_str(),
            ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.cloud_load"), ImVec2(cloudButtonWidth, 32))) {
        const bool ok = config_manager::DownloadCloudConfig(config_manager::new_config_name);
        CyberWidgets::Notify(config_manager::last_status.c_str(),
            ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("save.cloud_delete"), ImVec2(cloudButtonWidth, 32))) {
        const bool ok = config_manager::DeleteCloudConfig(config_manager::new_config_name);
        CyberWidgets::Notify(config_manager::last_status.c_str(),
            ok ? CyberWidgets::ToastType::Warning : CyberWidgets::ToastType::Error);
    }
    CyberWidgets::EndCard();

    // DEV Configs section (Development build + Admin only)
#if defined(OMNIGHOST_DEV_BUILD)
    if (OmniGhost::Licensing::IsAdmin()) {
        DrawDevConfigHub();
    }
#endif

    DrawCommunityConfigHub();

    CyberWidgets::BeginCard(Loc::Tr("save.list"));
    CyberWidgets::TextLine(Loc::Tr("save.favorite_hint"),
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::BeginSurfaceList("##config_list", 240.0f);
    int moveFrom = -1, moveTo = -1;
    for (int i = 0; i < static_cast<int>(config_manager::saved_list.size()); ++i) {
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

        const bool toggle_favorite = hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);

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

        float name_x = row.x + 2.0f;
        if (cfg.favorite) {


            ImVec2 points[10];
            constexpr float pi = 3.14159265f;
            const ImVec2 center(name_x + 7.0f, row.y + 18.0f);
            for (int point = 0; point < 10; ++point) {
                const float radius = (point & 1) ? 3.1f : 7.0f;
                const float angle = -pi * 0.5f + pi * static_cast<float>(point) / 5.0f;
                points[point] = ImVec2(center.x + std::cos(angle) * radius,
                                       center.y + std::sin(angle) * radius);
            }
            dl->AddConvexPolyFilled(points, 10, CyberTheme::U32(CyberTheme::Colors.Gold));
            name_x += 18.0f;
        }
        dl->AddText(ImVec2(name_x, row.y + 10.0f),
            CyberTheme::U32(CyberTheme::Colors.Gold), cfg.name.c_str());
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
            const bool loaded = config_manager::LoadFromFile(cfg.name);
            CyberWidgets::Notify(config_manager::last_status.c_str(),
                loaded ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
        }
        if (toggle_favorite) {
            if (config_manager::ToggleFavoriteConfig(cfg.name)) {
                const bool favorite = config_manager::IsFavoriteConfig(cfg.name);
                CyberWidgets::Notify(favorite
                    ? app_settings::T("Config favorita definida", "Favorite config selected")
                    : app_settings::T("Favorito removido", "Favorite removed"),
                    favorite ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Info);
            } else {
                CyberWidgets::Notify(config_manager::last_status.c_str(), CyberWidgets::ToastType::Error);
            }
            ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + rowH + 2.0f));
            ImGui::PopID();
            break;
        }
        ImGui::SetCursorScreenPos(ImVec2(row.x, row.y + rowH + 2.0f));
        ImGui::PopID();
    }
    if (moveFrom >= 0 && moveTo >= 0)
        config_manager::MoveConfig(moveFrom, moveTo);
    CyberWidgets::EndSurfaceList();
    CyberWidgets::EndCard();
}

