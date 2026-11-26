#include "../widgets.h"
#include "../theme.h"
#include "../../auth/user_profile.h"
#include "../../auth/discord_ipc.h"
#include "imgui.h"
#include "../fa_icons.h"

namespace {

void DrawRoleBadge(OmniGhost::Auth::UserRole role) {
    ImU32 color = OmniGhost::Auth::RoleToColor(role);
    const char* label = OmniGhost::Auth::RoleToString(role);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0,0,0,0));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::Button(label);
    ImGui::PopStyleColor(4);
}

void DrawLicenseInfo(const OmniGhost::Auth::LicenseInfo& license) {
    CyberWidgets::BeginCard("LICENÇA");

    if (license.license_key.empty()) {
        CyberWidgets::InlineMessage("Nenhuma licença aplicada", CyberWidgets::TextTone::Warning);
    } else {
        CyberWidgets::KeyValueRow("Chave", license.license_key.c_str());

        if (license.expires_unix == 0) {
            CyberWidgets::KeyValueRow("Expiração", "Vitalícia");
        } else {
            auto exp = std::chrono::system_clock::from_time_t(license.expires_unix);
            auto now = std::chrono::system_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::days>(exp - now);

            char buf[64];
            if (diff.count() > 0) {
                snprintf(buf, sizeof(buf), "%lld dias restantes", diff.count());
            } else {
                snprintf(buf, sizeof(buf), "EXPIRADA (%lld dias atrás)", -diff.count());
            }
            CyberWidgets::KeyValueRow("Expiração", buf);
        }

        if (license.hwid_locked) {
            CyberWidgets::KeyValueRow("HWID", license.hwid.c_str());
            CyberWidgets::InlineMessage("Licença travada a este hardware", CyberWidgets::TextTone::Info);
        }

        if (!license.features.empty()) {
            CyberWidgets::SectionTitle("Features");
            for (const auto& f : license.features) {
                CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "  %s", f.c_str());
            }
        }
    }

    ImGui::Spacing();
    static char license_input[128] = "";
    ImGui::InputText("##license_key", license_input, sizeof(license_input), ImGuiInputTextFlags_Password);
    ImGui::SameLine();
    if (ImGui::Button("Aplicar Licença")) {
        if (OmniGhost::Auth::ProfileManager::Instance().ApplyLicense(license_input)) {
            OmniGhost::UI::Toast::Success("Licença", "Aplicada com sucesso!");
            license_input[0] = 0;
        } else {
            OmniGhost::UI::Toast::Error("Licença", "Chave inválida");
        }
    }

    CyberWidgets::EndCard();
}

void DrawDiscordSection(OmniGhost::Auth::UserProfile& profile) {
    CyberWidgets::BeginCard("DISCORD");

    if (profile.discord_connected) {
        // Avatar
        ImGui::BeginGroup();
        if (!profile.discord_profile.avatar_url.empty()) {
            // Nota: ImGui não carrega imagens de URL nativamente
            // Em produção, usaríamos um texture loader assíncrono
            ImGui::Text("%s", ICON_FA_USER_CIRCLE);
            ImGui::SameLine();
        }
        ImGui::Text("%s", profile.discord_profile.display_name.c_str());
        ImGui::TextDisabled("@%s", profile.discord_profile.username.c_str());
        ImGui::Text("ID: %s", profile.discord_profile.id.c_str());
        ImGui::EndGroup();

        ImGui::SameLine(200);

        ImGui::BeginGroup();
        if (ImGui::Button(ICON_FA_SYNC " Atualizar")) {
            OmniGhost::Auth::ProfileManager::Instance().UpdateDiscordProfile();
            OmniGhost::UI::Toast::Info("Discord", "Perfil atualizado");
        }
        if (ImGui::Button(ICON_FA_EXTERNAL_LINK " Abrir Discord")) {
            ShellExecuteA(nullptr, "open", "https://discord.com", nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::EndGroup();
    } else {
        CyberWidgets::InlineMessage("Discord não detectado. Abra o Discord e clique em Atualizar.", CyberWidgets::TextTone::Warning);
        if (ImGui::Button(ICON_FA_SYNC " Tentar Conectar")) {
            OmniGhost::Auth::ProfileManager::Instance().UpdateDiscordProfile();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_EXTERNAL_LINK " Abrir Discord")) {
            ShellExecuteA(nullptr, "open", "https://discord.com", nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    CyberWidgets::EndCard();
}

void DrawLocalAccount(OmniGhost::Auth::UserProfile& profile) {
    CyberWidgets::BeginCard("CONTA LOCAL");

    static char username_buf[64] = "";
    static char email_buf[128] = "";

    if (username_buf[0] == 0) strncpy(username_buf, profile.username.c_str(), sizeof(username_buf)-1);
    if (email_buf[0] == 0) strncpy(email_buf, profile.email.c_str(), sizeof(email_buf)-1);

    ImGui::InputText("Username", username_buf, sizeof(username_buf));
    ImGui::InputText("Email", email_buf, sizeof(email_buf));

    if (ImGui::Button("Salvar")) {
        profile.username = username_buf;
        profile.email = email_buf;
        OmniGhost::Auth::ProfileManager::Instance().Save();
        OmniGhost::UI::Toast::Success("Conta", "Dados salvos");
    }

    CyberWidgets::EndCard();
}

void DrawCloudSync(OmniGhost::Auth::UserProfile& profile) {
    CyberWidgets::BeginCard("SINCRONIZAÇÃO NA NUVEM");

    bool sync = profile.cloud_sync_enabled;
    if (ImGui::Checkbox("Sincronizar configs automaticamente", &sync)) {
        profile.cloud_sync_enabled = sync;
        OmniGhost::Auth::ProfileManager::Instance().Save();
    }

    if (profile.cloud_sync_enabled) {
        CyberWidgets::InlineMessage("Configs serão sincronizadas ao fechar/abrir o menu", CyberWidgets::TextTone::Info);
        if (ImGui::Button(ICON_FA_CLOUD_UPLOAD " Sincronizar Agora")) {
            // TODO: Implementar sync real
            OmniGhost::UI::Toast::Info("Sync", "Sincronização manual não implementada ainda");
        }
    }

    CyberWidgets::EndCard();
}

} // namespace

void DrawProfile() {
    auto& profile = OmniGhost::Auth::ProfileManager::Instance().GetMutableProfile();

    // Header com role
    ImGui::BeginGroup();
    ImGui::Text("%s", profile.GetDisplayName().c_str());
    DrawRoleBadge(profile.role);
    ImGui::EndGroup();

    ImGui::Separator();

    DrawDiscordSection(profile);
    ImGui::Spacing();

    DrawLicenseInfo(profile.license);
    ImGui::Spacing();

    DrawLocalAccount(profile);
    ImGui::Spacing();

    DrawCloudSync(profile);
    ImGui::Spacing();

    // Actions
    CyberWidgets::BeginCard("AÇÕES");
    if (ImGui::Button(ICON_FA_SIGN_OUT " Logout")) {
        OmniGhost::Auth::ProfileManager::Instance().Logout();
        OmniGhost::UI::Toast::Info("Logout", "Desconectado");
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH " Reset Perfil")) {
        // TODO: Confirm dialog
        OmniGhost::Auth::ProfileManager::Instance().Logout();
        OmniGhost::UI::Toast::Warning("Perfil", "Resetado");
    }
    CyberWidgets::EndCard();
}