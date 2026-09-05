#include "license_page.h"
#include "../licensing/license_service.h"
#include "../platform/app_paths.h"
#include "window/fonts.h"
#include "../window/theme.h"
#include "../window/widgets.h"
#include "imgui.h"

#include <Windows.h>
#include <Shellapi.h>

namespace LauncherLicensing {
namespace {
ImU32 C_CARD() { return IM_COL32(0, 0, 0, 255); }
constexpr ImU32 C_BORDER = IM_COL32(53, 53, 58, 255);
constexpr ImU32 C_GOLD = IM_COL32(212, 175, 55, 255);
constexpr ImU32 C_TEXT = IM_COL32(242, 242, 244, 255);
constexpr ImU32 C_MUTED = IM_COL32(143, 143, 152, 255);

void LabelValue(const char* label, const char* value) {
    ImGui::TextColored(ImColor(C_MUTED), "%s", label);
    ImGui::SameLine(180.0f);
    ImGui::TextColored(ImColor(C_TEXT), "%s", value);
}
}

void Draw(const ImVec2& displaySize, float contentTop, float contentBottom) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, contentTop));
    ImGui::SetNextWindowSize(ImVec2(displaySize.x, displaySize.y - contentTop - contentBottom));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##launcher_licensing", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground);

    if (ImFont* title = CyberFonts::GetTitleFont()) ImGui::PushFont(title);
    ImGui::TextColored(ImColor(C_TEXT), "Licenciamento");
    if (CyberFonts::GetTitleFont()) ImGui::PopFont();
    ImGui::TextColored(ImColor(C_MUTED),
        "Estado local agora; integração VPS/API preparada para uma fase futura.");
    ImGui::Dummy(ImVec2(0.0f, 14.0f));

    const auto snapshot = OmniGhost::Licensing::GetSnapshot();
    const float width = (displaySize.x > 820.0f) ? 620.0f : (displaySize.x - 56.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImColor(C_CARD()).Value);
    ImGui::PushStyleColor(ImGuiCol_Border, ImColor(C_BORDER).Value);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::BeginChild("##license_status_card", ImVec2(width, 220.0f), true);

    const ImVec4 stateColor = snapshot.localLicenseValid ? CyberTheme::Colors.Success : CyberTheme::Colors.Warning;
    ImGui::TextColored(ImColor(C_GOLD), "LICENÇA LOCAL");
    ImGui::SameLine();
    ImGui::TextColored(stateColor, "%s",
        OmniGhost::Licensing::StateLabel(snapshot.localState));
    ImGui::Separator();
    LabelValue("Modo", "Local temporário");
    LabelValue("Cache", snapshot.protectedStorage ? "Protegido com Windows DPAPI" : "Ainda não protegido");
    LabelValue("VPS / API", snapshot.remoteServiceConfigured ? "Configurado" : "Não configurado");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextWrapped("O modo local mantém o launcher funcional sem servidor. Para licenciamento real, "
                       "a próxima etapa é ligar esta camada a uma VPS HTTPS e usar entitlements assinados; "
                       "nenhuma chave privada deve ficar dentro do executável.");
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    if (CyberWidgets::Button("Atualizar estado", CyberWidgets::ButtonStyle::Secondary, ImVec2(145.0f, 30.0f)))
        OmniGhost::Licensing::Refresh();
    ImGui::SameLine();
    if (CyberWidgets::Button("Abrir pasta local", CyberWidgets::ButtonStyle::Ghost, ImVec2(145.0f, 30.0f))) {
        const auto folder = OmniGhost::Paths::LocalData();
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace LauncherLicensing
