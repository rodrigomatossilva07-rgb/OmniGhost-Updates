#include "../../widgets.h"
#include "../../theme.h"
#include "../common/EspPreview.h"
#include "../../../games/Rust/rust_game.h"
#include "third_party/DMALibrary/Memory/Memory.h"
#include "imgui.h"

#include <algorithm>
#include <cstdio>

extern Memory mem;

namespace {

class RustPreviewAdapter final : public EspPreview::IConfigAdapter {
public:
    const EspPreview::PreviewConfig& GetPreviewConfig() const override {
        preview_ = {};
        preview_.esp_enabled = Rust::config.esp_enabled;
        preview_.box = Rust::config.box_2d;
        preview_.box_fill = Rust::config.box_fill;
        preview_.box_thickness = Rust::config.box_thickness;
        preview_.box_rounding = Rust::config.box_rounding;
        std::copy_n(Rust::config.box_color, 4, preview_.col_box);
        std::copy_n(Rust::config.box_fill_color, 4, preview_.col_box_fill);
        return preview_;
    }

    ImU32 GetBoxColor(bool, bool, bool) const override {
        return EspPreview::ToImU32(Rust::config.box_color);
    }

private:
    mutable EspPreview::PreviewConfig preview_{};
};

RustPreviewAdapter g_rustPreview;

void DrawUnavailable(const char* title) {
    CyberWidgets::BeginCard(title);
    CyberWidgets::TextLine(
        "A estrutura do menu está preparada, mas esta funcionalidade ainda não faz leituras nem escreve no jogo.",
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::TextLine("Nesta fase apenas a Caixa 2D está ligada ao renderer Rust.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
}

CyberWidgets::HealthStatus State(bool ok, bool waiting = false) {
    return ok ? CyberWidgets::HealthStatus::Ok
              : (waiting ? CyberWidgets::HealthStatus::Warning
                         : CyberWidgets::HealthStatus::Error);
}

}

void DrawRustVisuals() {
    using namespace CyberWidgets;
    const float available = ImGui::GetContentRegionAvail().x;
    const float height = (std::max)(CyberTheme::Px(410.f), ImGui::GetContentRegionAvail().y);
    const float gap = CyberTheme::Metrics::GridGap;
    const float previewWidth = std::clamp(available * 0.31f, CyberTheme::Px(250.f), CyberTheme::Px(310.f));
    const float settingsWidth = (std::max)(CyberTheme::Px(220.f), available - previewWidth - gap * 2.f);
    const float featuresWidth = settingsWidth * 0.5f;
    const float colorsWidth = settingsWidth - featuresWidth;

    ImGui::BeginChild("##rust_esp_features", ImVec2(featuresWidth, height), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    BeginCard("ESP DE JOGADORES");
    ToggleSwitch("Ativar ESP", &Rust::config.esp_enabled);
    Separator();
    ImGui::BeginDisabled(!Rust::config.esp_enabled);
    ToggleSwitch("Caixa 2D", &Rust::config.box_2d);
    SectionTitle("DESENHO");
    ToggleSwitch("Preenchimento da caixa", &Rust::config.box_fill);
    SliderFloat("Espessura", &Rust::config.box_thickness, 0.5f, 4.0f, "%.1f");
    SliderFloat("Arredondamento", &Rust::config.box_rounding, 0.0f, 18.0f, "%.0f");
    SliderFloat("Distância máxima", &Rust::config.max_distance, 20.0f, 500.0f, "%.0f m");
    ImGui::EndDisabled();
    EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginChild("##rust_esp_colors", ImVec2(colorsWidth, height), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    BeginCard("PERSONALIZAÇÃO");
    ImGui::BeginDisabled(!Rust::config.esp_enabled || !Rust::config.box_2d);
    ImGui::ColorEdit4("Caixa 2D", Rust::config.box_color,
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::ColorEdit4("Cor do preenchimento", Rust::config.box_fill_color,
                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
    ImGui::EndDisabled();
    Separator();
    TextLine("Só são mostradas opções suportadas pelo backend Rust atual.",
             TextTone::Secondary);
    EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0.f, gap);
    ImGui::BeginGroup();
    BeginCard("PRÉ-VISUALIZAÇÃO ESP", previewWidth);
    TextLine("A pré-visualização usa exatamente a configuração da Caixa 2D.", TextTone::Secondary);
    const float panelHeight = (std::max)(
        CyberTheme::Px(260.f),
        height - CyberTheme::Metrics::CardHeaderHeight -
            CyberTheme::Metrics::CardPadding * 2.f - CyberTheme::Px(72.f));
    const float drawWidth = (std::max)(CyberTheme::Px(180.f),
        previewWidth - CyberTheme::Metrics::CardPadding * 2.f);
    EspPreview::DrawPreview(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(),
                            drawWidth, panelHeight, g_rustPreview, "");
    EndCard();
    ImGui::EndGroup();
}

void DrawRustAim() { DrawUnavailable("ASSISTÊNCIA DE MIRA"); }
void DrawRustMisc() { DrawUnavailable("DIVERSOS"); }
void DrawRustRadar() { DrawUnavailable("RADAR"); }

void DrawRustStatus() {
    using namespace CyberWidgets;
    const auto diagnostics = mem.GetDiagnosticsSnapshot();
    const Rust::Runtime& rt = Rust::runtime;
    char value[96]{};

    BeginCardRow();
    const float half = CardRowHalfWidth();
    BeginCard("SISTEMA", half);
    HealthRow("DMA", diagnostics.deviceOpen ? "Ligado" : "Desligado", State(diagnostics.deviceOpen));
    HealthRow("RustClient.exe", rt.process_ok ? "Detetado" : "Não detetado", State(rt.process_ok));
    HealthRow("Ligação ao processo", diagnostics.processInitialized ? "Ligada" : "Desligada",
              State(diagnostics.processInitialized));
    HealthRow("GameAssembly.dll", rt.game_assembly ? "Resolvido" : "Em falta", State(rt.game_assembly != 0));
    HealthRow("UnityPlayer.dll", rt.unity_player ? "Resolvido" : "Em falta", State(rt.unity_player != 0));
    EndCard();

    NextCardColumn();
    BeginCard("AQUISIÇÃO REAL", half);
    HealthRow("Catálogo 2026-10-22", rt.offset_catalog_ok ? "Carregado" : "Inválido", State(rt.offset_catalog_ok));
    HealthRow("BaseNetworkable", rt.entity_source_ok ? "Resolvido" : "Offset TypeInfo em falta",
              State(rt.entity_source_ok, true));
    HealthRow("Entity list", rt.entity_list ? "Disponível" : "A aguardar", State(rt.entity_list != 0, true));
    HealthRow("Câmara / viewMatrix", rt.camera_ok ? "Disponível" : "Offset em falta",
              State(rt.camera_ok, true));
    std::snprintf(value, sizeof(value), "%zu jogadores", rt.players.size());
    HealthRow("Jogadores válidos", value, State(rt.acquisition_ok, true));
    HealthRow("Caixa 2D", rt.acquisition_ok ? "Operacional" : "Bloqueada pelos dados acima",
              State(rt.acquisition_ok, true));
    EndCard();
    EndCardRow();

    CardGap();
    BeginCard("DETALHES");
    std::snprintf(value, sizeof(value), "PID: %u", rt.pid);
    TextLine(value, TextTone::Secondary);
    std::snprintf(value, sizeof(value), "GameAssembly: 0x%llX",
                  static_cast<unsigned long long>(rt.game_assembly));
    TextLine(value, TextTone::Secondary);
    std::snprintf(value, sizeof(value), "UnityPlayer: 0x%llX",
                  static_cast<unsigned long long>(rt.unity_player));
    TextLine(value, TextTone::Secondary);
    TextLine(rt.status.c_str(), rt.acquisition_ok ? TextTone::Success : TextTone::Warning);
    EndCard();
}
