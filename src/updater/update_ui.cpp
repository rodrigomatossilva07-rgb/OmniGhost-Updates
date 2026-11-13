#include "update_ui.h"
#include "update_service.h"
#include "../app_version.h"
#include "../platform/app_paths.h"
#include "../window/theme.h"
#include "../window/ui_format.h"
#include "../window/widgets.h"
#include "../window/ui_feedback.h"
#include "../window/transition_overlay.h"
#include "src/project/third_party/ImGui/imgui.h"

#include <Windows.h>
#include <Shellapi.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>
#include "platform/strprotect/strprotect.h"

namespace OmniGhost::UpdateUI {
namespace {

const char* StageLabel(const std::string& stage) {
    if (stage == OGS_LIT("release-resolution")) return OGS_LIT("Resolução da versão");
    if (stage == OGS_LIT("check")) return OGS_LIT("Rede / transferência do manifesto");
    if (stage == OGS_LIT("manifest")) return OGS_LIT("Validação do manifesto");
    if (stage == OGS_LIT("integrity")) return OGS_LIT("Integridade da instalação");
    if (stage == OGS_LIT("repair")) return OGS_LIT("Preparação da reparação");
    if (stage == OGS_LIT("version")) return OGS_LIT("Comparação de versões");
    if (stage == OGS_LIT("download")) return OGS_LIT("Transferência do pacote");
    if (stage == OGS_LIT("sha256")) return OGS_LIT("Validação SHA-256");
    if (stage == OGS_LIT("prepare")) return OGS_LIT("Preparação do pacote");
    if (stage == OGS_LIT("launch-updater")) return OGS_LIT("Arranque do atualizador");
    if (stage == OGS_LIT("installation")) return OGS_LIT("Instalação");
    if (stage == OGS_LIT("restart")) return OGS_LIT("Reinício / confirmação de arranque");
    return OGS_LIT("Etapa desconhecida");
}

std::string ClipboardDiagnostic(const Update::Snapshot& state) {
    std::ostringstream output;
    output << OGS_LIT("Erro do atualizador OmniGhost\r\n")
           << OGS_LIT("Versão instalada: ") << UiFormat::Version(OmniGhost::Version) << "\r\n"
           << OGS_LIT("Versão disponível: ")
           << (state.availableVersion.empty() ? "—" : UiFormat::Version(state.availableVersion)) << "\r\n"
           << OGS_LIT("Etapa: ") << (state.errorStage.empty() ? OGS_LIT("desconhecida") : state.errorStage) << "\r\n"
           << OGS_LIT("Mensagem: ") << state.userMessage << "\r\n"
           << OGS_LIT("Detalhe técnico: ") << state.technicalDetail << "\r\n"
           << OGS_LIT("Pasta dos registos: ") << OmniGhost::Paths::Logs().string();
    return output.str();
}

const char* TitleFor(const Update::Snapshot& state) {
    using Update::Status;
    switch (state.status) {
    case Status::Checking: return OGS_LIT("A verificar atualizações");
    case Status::UpToDate: return OGS_LIT("O launcher está atualizado");
    case Status::Available: return state.mandatory ? OGS_LIT("Atualização necessária") : OGS_LIT("Atualização disponível");
    case Status::Downloading: return OGS_LIT("A descarregar atualização");
    case Status::Ready: return OGS_LIT("Atualização pronta");
    case Status::Installing: return state.repairMode ? OGS_LIT("A reparar instalação") : OGS_LIT("A instalar atualização");
    case Status::Completed: return OGS_LIT("Atualização concluída");
    case Status::RolledBack: return OGS_LIT("Versão anterior restaurada");
    case Status::Error: return state.errorTitle.empty() ? OGS_LIT("Não foi possível concluir a atualização") : state.errorTitle.c_str();
    default: return OGS_LIT("Atualizações");
    }
}

CyberWidgets::TextTone MessageTone(const Update::Snapshot& state) {
    using Update::Status;
    switch (state.status) {
    case Status::UpToDate:
    case Status::Completed: return CyberWidgets::TextTone::Success;
    case Status::RolledBack:
    case Status::Available: return state.mandatory ? CyberWidgets::TextTone::Warning : CyberWidgets::TextTone::Info;
    case Status::Error: return CyberWidgets::TextTone::Error;
    default: return CyberWidgets::TextTone::Info;
    }
}

void OpenLogs() {
    const std::wstring logs = OmniGhost::Paths::Logs().wstring();
    ShellExecuteW(nullptr, OGS_WIDE("open"), logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void DrawVersionLine(const Update::Snapshot& state) {
    const std::string installed = UiFormat::Version(OmniGhost::Version);
    if (state.availableVersion.empty()) {
        CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
            OGS_LIT("Versão instalada: %s"), installed.c_str());
        return;
    }
    const std::string available = UiFormat::Version(state.availableVersion);
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
        "%s  →  %s", installed.c_str(), available.c_str());
}

}

void Draw() {
    using namespace Update;

    UpdateService& service = UpdateService::Instance();
    const Snapshot state = service.GetSnapshot();

    static Status previousStatus = Status::Idle;
    static std::chrono::steady_clock::time_point upToDateSince{};
    static bool technicalDetailsExpanded = false;
    static std::string previousTechnicalDetail;

    if (state.status != previousStatus || state.technicalDetail != previousTechnicalDetail) {
        if (state.status != previousStatus) {
            if (state.status == Status::Available || state.status == Status::Completed)
                OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Update);
            else if (state.status == Status::Error)
                OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Error);
        }
        if (state.status == Status::UpToDate)
            upToDateSince = std::chrono::steady_clock::now();
        technicalDetailsExpanded = false;
        previousTechnicalDetail = state.technicalDetail;
        previousStatus = state.status;
    }

    if (state.status == Status::Idle || state.status == Status::Deferred)
        return;
    if (state.status == Status::UpToDate &&
        std::chrono::steady_clock::now() - upToDateSince > std::chrono::seconds(4))
        return;

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const bool errorExpanded = state.status == Status::Error && technicalDetailsExpanded;
    const float preferredWidth = errorExpanded ? CyberTheme::Px(530.0f) : CyberTheme::Px(420.0f);
    const float width = std::max(CyberTheme::Px(300.0f),
        std::min(preferredWidth, display.x - CyberTheme::Px(28.0f)));
    float height = CyberTheme::Px(190.0f);
    if (state.status == Status::Checking || state.status == Status::UpToDate) height = CyberTheme::Px(128.0f);
    if (state.status == Status::Downloading) height = CyberTheme::Px(194.0f);
    if (state.status == Status::Completed || state.status == Status::RolledBack) height = CyberTheme::Px(184.0f);
    if (errorExpanded) height = std::min(display.y - CyberTheme::Px(32.0f), CyberTheme::Px(390.0f));

    const ImVec2 position = display.x < CyberTheme::Px(620.0f)
        ? ImVec2((display.x - width) * 0.5f, display.y - height - CyberTheme::Px(24.0f))
        : ImVec2(display.x - width - CyberTheme::Px(24.0f), CyberTheme::Px(70.0f));

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.98f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CyberTheme::Colors.Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.72f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Md);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CyberTheme::Spacing::Lg, CyberTheme::Spacing::Md));

    ImGui::Begin(OGS_LIT("##omnighost_update"), nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);

    CyberWidgets::TextLine(TitleFor(state), CyberWidgets::TextTone::Accent);

    if (state.status == Status::Checking) {
        CyberWidgets::Spinner(OGS_LIT("update_check_spinner"), CyberTheme::Px(7.0f), CyberTheme::Px(1.8f));
        ImGui::SameLine(0.0f, CyberTheme::Spacing::Sm);
        ImGui::TextColored(CyberTheme::Colors.TextDisabled, OGS_LIT("A procurar uma versão mais recente…"));
    } else {
        DrawVersionLine(state);
        if (!state.userMessage.empty())
            CyberWidgets::InlineMessage(state.userMessage.c_str(), MessageTone(state));

        if (state.status == Status::Error && technicalDetailsExpanded) {
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Accent,
                OGS_LIT("Etapa: %s"), StageLabel(state.errorStage));
            ImGui::BeginChild(OGS_LIT("##omnighost_update_error_detail"),
                ImVec2(-1.0f, CyberTheme::Px(104.0f)), true,
                ImGuiWindowFlags_AlwaysVerticalScrollbar);
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
            ImGui::TextColored(CyberTheme::Colors.TextDisabled, "%s",
                state.technicalDetail.empty() ? OGS_LIT("Nenhum detalhe técnico disponível.") : state.technicalDetail.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
            ImGui::Dummy(ImVec2(0.0f, CyberTheme::Spacing::Xs));
        }
    }

    if (state.status == Status::Downloading) {
        float fraction = 0.0f;
        if (state.bytesTotal > 0) {
            fraction = static_cast<float>(state.bytesReceived) / static_cast<float>(state.bytesTotal);
            fraction = std::clamp(fraction, 0.0f, 1.0f);
        }
        char overlay[96]{};
        std::snprintf(overlay, sizeof(overlay), "%.1f / %.1f MB · %.1f MB/s",
            static_cast<double>(state.bytesReceived) / 1048576.0,
            static_cast<double>(state.bytesTotal) / 1048576.0,
            state.bytesPerSecond / 1048576.0);
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, CyberTheme::Px(16.0f)), overlay);
        if (CyberWidgets::Button(OGS_LIT("Cancelar"), CyberWidgets::ButtonStyle::Secondary,
                ImVec2(CyberTheme::Px(120.0f), CyberTheme::Px(34.0f))))
            service.Cancel();
    } else if (state.status == Status::Available) {
        if (state.bytesTotal > 0)
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, OGS_LIT("Pacote: %.1f MB"),
                static_cast<double>(state.bytesTotal) / 1048576.0);
        if (CyberWidgets::Button(state.mandatory ? OGS_LIT("Instalar atualização") : OGS_LIT("Instalar agora"),
                CyberWidgets::ButtonStyle::Primary, ImVec2(CyberTheme::Px(180.0f), CyberTheme::Px(34.0f))))
            service.InstallUpdateAsync();
        if (!state.mandatory) {
            ImGui::SameLine();
            if (CyberWidgets::Button(OGS_LIT("Instalar ao sair"), CyberWidgets::ButtonStyle::Secondary,
                    ImVec2(CyberTheme::Px(142.0f), CyberTheme::Px(34.0f))))
                service.DownloadAndScheduleInstallOnExitAsync();
            if (CyberWidgets::Button(OGS_LIT("Mais tarde"), CyberWidgets::ButtonStyle::Ghost,
                    ImVec2(CyberTheme::Px(110.0f), CyberTheme::Px(32.0f))))
                service.DeferForSession();
            if (!state.releaseNotesUrl.empty()) {
                ImGui::SameLine();
                if (CyberWidgets::Button(OGS_LIT("Notas"), CyberWidgets::ButtonStyle::Ghost,
                        ImVec2(CyberTheme::Px(90.0f), CyberTheme::Px(32.0f))))
                    ShellExecuteA(nullptr, OGS_LIT("open"), state.releaseNotesUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
    } else if (state.status == Status::Ready) {
        if (state.bytesTotal > 0)
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Success, OGS_LIT("Pacote validado: %.1f MB"),
                static_cast<double>(state.bytesTotal) / 1048576.0);
        if (CyberWidgets::Button(state.repairMode ? OGS_LIT("Reparar e reiniciar") : OGS_LIT("Instalar agora"),
                CyberWidgets::ButtonStyle::Primary, ImVec2(CyberTheme::Px(170.0f), CyberTheme::Px(34.0f))))
            service.InstallPreparedUpdateAsync();
        if (!state.mandatory) {
            ImGui::SameLine();
            if (CyberWidgets::Button(state.installOnExit ? OGS_LIT("Instalar ao sair ✓") : OGS_LIT("Instalar ao sair"),
                    CyberWidgets::ButtonStyle::Secondary, ImVec2(CyberTheme::Px(150.0f), CyberTheme::Px(34.0f))))
                service.ScheduleInstallOnExit();
            if (CyberWidgets::Button(OGS_LIT("Mais tarde"), CyberWidgets::ButtonStyle::Ghost,
                    ImVec2(CyberTheme::Px(110.0f), CyberTheme::Px(32.0f))))
                service.DeferForSession();
        }
    } else if (state.status == Status::Installing) {
        const float elapsed = static_cast<float>(ImGui::GetTime());
        OmniGhost::UI::DrawTransitionOverlay({
            state.repairMode ? OGS_LIT("A reparar o OmniGhost") : OGS_LIT("A instalar atualização"),
            OGS_LIT("A aplicação reiniciará automaticamente quando estiver pronta"),
            elapsed,
            1.0f,
            false
        });
    } else if (state.status == Status::Completed) {
        if (!state.updateFromVersion.empty()) {
            const std::string from = UiFormat::Version(state.updateFromVersion);
            const std::string to = UiFormat::Version(OmniGhost::Version);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Success, "%s → %s", from.c_str(), to.c_str());
        }
        if (!state.releaseNotesForCompletedUpdate.empty()) {
            if (CyberWidgets::Button(OGS_LIT("O que há de novo"), CyberWidgets::ButtonStyle::Secondary,
                    ImVec2(CyberTheme::Px(150.0f), CyberTheme::Px(34.0f))))
                ShellExecuteA(nullptr, OGS_LIT("open"), state.releaseNotesForCompletedUpdate.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::SameLine();
        }
        if (CyberWidgets::Button(OGS_LIT("Concluir"), CyberWidgets::ButtonStyle::Primary,
                ImVec2(CyberTheme::Px(110.0f), CyberTheme::Px(34.0f))))
            service.DismissStartupNotice();
    } else if (state.status == Status::RolledBack) {
        if (!state.availableVersion.empty()) {
            const std::string failed = UiFormat::Version(state.availableVersion);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Warning, OGS_LIT("Falhou: %s"), failed.c_str());
        }
        if (!state.errorStage.empty())
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, OGS_LIT("Etapa: %s"), StageLabel(state.errorStage));
        if (CyberWidgets::Button(OGS_LIT("Tentar novamente"), CyberWidgets::ButtonStyle::Primary,
                ImVec2(CyberTheme::Px(150.0f), CyberTheme::Px(34.0f)))) {
            service.DismissStartupNotice();
            service.CheckAsync(true);
        }
        ImGui::SameLine();
        if (CyberWidgets::Button(OGS_LIT("Abrir logs"), CyberWidgets::ButtonStyle::Secondary,
                ImVec2(CyberTheme::Px(110.0f), CyberTheme::Px(34.0f))))
            OpenLogs();
        ImGui::SameLine();
        if (CyberWidgets::Button(OGS_LIT("Ignorar"), CyberWidgets::ButtonStyle::Ghost,
                ImVec2(CyberTheme::Px(90.0f), CyberTheme::Px(34.0f))))
            service.DismissStartupNotice();
    } else if (state.status == Status::Error) {
        if (CyberWidgets::Button(OGS_LIT("Tentar novamente"), CyberWidgets::ButtonStyle::Primary,
                ImVec2(CyberTheme::Px(150.0f), CyberTheme::Px(34.0f))))
            service.RetryLastFailure();
        if (!state.mandatory) {
            ImGui::SameLine();
            if (CyberWidgets::Button(OGS_LIT("Fechar"), CyberWidgets::ButtonStyle::Ghost,
                    ImVec2(CyberTheme::Px(90.0f), CyberTheme::Px(34.0f))))
                service.DeferForSession();
        }
        ImGui::SameLine();
        if (CyberWidgets::Button(technicalDetailsExpanded ? OGS_LIT("Ocultar detalhes") : OGS_LIT("Detalhes"),
                CyberWidgets::ButtonStyle::Secondary, ImVec2(CyberTheme::Px(120.0f), CyberTheme::Px(34.0f))))
            technicalDetailsExpanded = !technicalDetailsExpanded;

        if (technicalDetailsExpanded) {
            if (CyberWidgets::Button(OGS_LIT("Copiar erro"), CyberWidgets::ButtonStyle::Ghost,
                    ImVec2(CyberTheme::Px(115.0f), CyberTheme::Px(32.0f)))) {
                const std::string diagnostic = ClipboardDiagnostic(state);
                CyberWidgets::CopyToClipboard(diagnostic.c_str(), OGS_LIT("Erro copiado"));
            }
            ImGui::SameLine();
            if (CyberWidgets::Button(OGS_LIT("Abrir logs"), CyberWidgets::ButtonStyle::Ghost,
                    ImVec2(CyberTheme::Px(105.0f), CyberTheme::Px(32.0f))))
                OpenLogs();
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

}
