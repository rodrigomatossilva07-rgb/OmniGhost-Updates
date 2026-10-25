#include "../../widgets.h"
#include "../../theme.h"
#include "config/app_settings.h"
#include "games/Fivem/objects/object_debugger.h"
#include "games/Fivem/objects/object_explorer.h"

#include "third_party/ImGui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {

CyberWidgets::TextTone ToneFor(FiveM::ObjectDebugger::AcquisitionState state) {
    using State = FiveM::ObjectDebugger::AcquisitionState;
    if (state == State::CandidateData || state == State::NoObjects ||
        state == State::LayoutUnavailable)
        return CyberWidgets::TextTone::Warning;
    if (state == State::Idle || state == State::WaitingForGame)
        return CyberWidgets::TextTone::Secondary;
    return CyberWidgets::TextTone::Error;
}

}

void DrawObjectEsp()
{
    using namespace FiveM::ObjectDebugger;
    RequestRefresh();
    const auto snapshotLease = AcquireSnapshot();
    static const Snapshot emptySnapshot{};
    const Snapshot& snapshot = snapshotLease ? *snapshotLease : emptySnapshot;

    static char search[96]{};
    static int stateFilter = 0;
    static bool onlyKnownModels = false;

    CyberWidgets::BeginCard("OBJECT DEBUGGER");
    CyberWidgets::Badge(app_settings::T("DIAGNOSTICO — ESP BLOQUEADO", "DIAGNOSTIC — ESP LOCKED"),
        CyberWidgets::TextTone::Warning);
    CyberWidgets::TextLine(
        app_settings::T(
            "Esta ferramenta valida a aquisicao de CObject. Nenhum objeto e desenhado no jogo.",
            "This tool validates CObject acquisition. No objects are drawn in game."),
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::CardGap();
    CyberWidgets::BeginCard(app_settings::T("ESTADO DA AQUISICAO", "ACQUISITION STATUS"));
    CyberWidgets::Badge(StateText(snapshot.state), ToneFor(snapshot.state));
    char value[96]{};
    std::snprintf(value, sizeof(value), "b%d", snapshot.build);
    CyberWidgets::KeyValueRow("Build", value);
    std::snprintf(value, sizeof(value), "%u", snapshot.reportedCount);
    CyberWidgets::KeyValueRow(app_settings::T("Objetos reportados", "Reported objects"), value);
    std::snprintf(value, sizeof(value), "%u / %u", snapshot.inspectedCount, snapshot.reportedCount);
    CyberWidgets::KeyValueRow(app_settings::T("Entradas inspecionadas", "Inspected entries"), value);
    std::snprintf(value, sizeof(value), "%u", snapshot.validPositions);
    CyberWidgets::KeyValueRow(app_settings::T("Posicoes validas", "Valid positions"), value);
    std::snprintf(value, sizeof(value), "%u", snapshot.rejectedPointers);
    CyberWidgets::KeyValueRow(app_settings::T("Ponteiros rejeitados", "Rejected pointers"), value);
    std::snprintf(value, sizeof(value), "%.2f ms", snapshot.acquisitionMs);
    CyberWidgets::KeyValueRow(app_settings::T("Tempo de aquisicao", "Acquisition time"), value);
    CyberWidgets::InlineMessage(
        app_settings::T(
            "A leitura permanece bloqueada enquanto nao existir um layout CObject verificado para a build ativa. Offsets antigos nao sao utilizados.",
            "Reading remains locked until a verified CObject layout exists for the active build. Old offsets are not used."),
        CyberWidgets::TextTone::Warning);
    CyberWidgets::EndCard();

    CyberWidgets::CardGap();
    CyberWidgets::BeginCard(app_settings::T("OBJETOS RECEBIDOS", "RECEIVED OBJECTS"));
    CyberWidgets::InputField("##object_debug_search", search, sizeof(search),
        app_settings::T("Pesquisar nome ou hash...", "Search name or hash..."));
    const char* statesPt[] = { "Todos os estados", "Validos", "Com problemas" };
    const char* statesEn[] = { "All states", "Valid", "With issues" };
    CyberWidgets::Combo(app_settings::T("Estado", "State"), &stateFilter,
        app_settings::config.language == app_settings::Language::PT ? statesPt : statesEn, 3);
    CyberWidgets::ToggleSwitch(
        app_settings::T("Apenas modelos conhecidos", "Known models only"), &onlyKnownModels);

    static std::vector<const ObjectRecord*> filtered;
    filtered.clear();
    if (filtered.capacity() < snapshot.objects.size()) filtered.reserve(snapshot.objects.size());
    for (const auto& object : snapshot.objects) {
        const bool valid = object.state == ObjectState::Valid;
        if (stateFilter == 1 && !valid) continue;
        if (stateFilter == 2 && valid) continue;
        if (onlyKnownModels && object.modelName.rfind("Unknown [", 0) == 0) continue;
        if (!FiveM::ObjectExplorer::MatchesSearch(
                object.modelName, object.modelHash, search)) continue;
        filtered.push_back(&object);
    }
    std::stable_sort(filtered.begin(), filtered.end(), [](const auto* left, const auto* right) {
        const bool leftValid = left->state == ObjectState::Valid;
        const bool rightValid = right->state == ObjectState::Valid;
        if (leftValid != rightValid) return leftValid > rightValid;
        return left->distance < right->distance;
    });

    std::snprintf(value, sizeof(value), "%zu / %zu", filtered.size(), snapshot.objects.size());
    CyberWidgets::KeyValueRow(app_settings::T("Resultados", "Results"), value);

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##object_debugger_table", 7, flags,
            ImVec2(0.f, CyberTheme::Px(330.f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, CyberTheme::Px(48.f));
        ImGui::TableSetupColumn(app_settings::T("Modelo", "Model"), ImGuiTableColumnFlags_WidthStretch, 1.8f);
        ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, CyberTheme::Px(92.f));
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch, .7f);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch, .7f);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthStretch, .7f);
        ImGui::TableSetupColumn(app_settings::T("Distancia / estado", "Distance / state"),
            ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const ObjectRecord& object = *filtered[static_cast<std::size_t>(row)];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%u", object.slot);
                ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(object.modelName.c_str());
                ImGui::TableSetColumnIndex(2); ImGui::Text("0x%08X", object.modelHash);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%.2f", object.position.x);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%.2f", object.position.y);
                ImGui::TableSetColumnIndex(5); ImGui::Text("%.2f", object.position.z);
                ImGui::TableSetColumnIndex(6);
                if (object.state == ObjectState::Valid)
                    ImGui::Text("%.1f m - %s", object.distance, ObjectStateText(object.state));
                else
                    ImGui::TextUnformatted(ObjectStateText(object.state));
            }
        }
        ImGui::EndTable();
    }

    if (filtered.empty())
        CyberWidgets::TextLine(
            app_settings::T("Ainda nao existem objetos que correspondam aos filtros.",
                            "No objects currently match the filters."),
            CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
}
