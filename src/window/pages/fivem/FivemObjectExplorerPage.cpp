#include "../../widgets.h"
#include "../../theme.h"
#include "config/app_settings.h"
#include "games/Fivem/objects/object_debugger.h"
#include "games/Fivem/objects/object_explorer.h"

#include "third_party/ImGui/imgui.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <unordered_set>
#include <vector>

void DrawObjectExplorer()
{
    using namespace FiveM;
    ObjectDebugger::RequestRefresh();
    const auto snapshotLease = ObjectDebugger::AcquireSnapshot();
    static const ObjectDebugger::Snapshot emptySnapshot{};
    const ObjectDebugger::Snapshot& snapshot = snapshotLease ? *snapshotLease : emptySnapshot;

    static char search[96]{};
    static int listFilter = 0;
    static std::uint32_t selectedHash = 0;
    const char* filtersPt[] = { "Todos", "Favoritos", "Visiveis", "Ocultos", "Ignorados" };
    const char* filtersEn[] = { "All", "Favorites", "Visible", "Hidden", "Ignored" };

    CyberWidgets::BeginCard(app_settings::T("PESQUISA E FILTROS", "SEARCH AND FILTERS"));
    CyberWidgets::InputField("##object_explorer_search", search, sizeof(search),
        app_settings::T("Pesquisar por nome ou hash...", "Search by name or hash..."));
    CyberWidgets::Combo(app_settings::T("Lista", "List"), &listFilter,
        app_settings::config.language == app_settings::Language::PT ? filtersPt : filtersEn, 5);
    CyberWidgets::TextLine(
        app_settings::T(
            "A pesquisa usa apenas o snapshot imutavel atual e nao executa leituras DMA adicionais.",
            "Search uses only the current immutable snapshot and performs no additional DMA reads."),
        CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    static std::vector<const ObjectDebugger::ObjectRecord*> filtered;
    static std::unordered_set<std::uint32_t> seenModels;
    filtered.clear();
    seenModels.clear();
    if (filtered.capacity() < snapshot.objects.size()) filtered.reserve(snapshot.objects.size());
    if (seenModels.bucket_count() < snapshot.objects.size()) seenModels.reserve(snapshot.objects.size());
    for (const auto& object : snapshot.objects) {
        if (!object.modelHash || !seenModels.insert(object.modelHash).second) continue;
        const bool favorite = ObjectExplorer::IsFavorite(object.modelHash);
        const bool ignored = ObjectExplorer::IsIgnored(object.modelHash);
        const bool visible = ObjectExplorer::IsVisible(object.modelHash);
        if (listFilter == 1 && !favorite) continue;
        if (listFilter == 2 && !visible) continue;
        if (listFilter == 3 && visible) continue;
        if (listFilter == 4 && !ignored) continue;
        if (!ObjectExplorer::MatchesSearch(object.modelName, object.modelHash, search)) continue;
        filtered.push_back(&object);
    }
    std::stable_sort(filtered.begin(), filtered.end(), [](const auto* left, const auto* right) {
        const bool lf = ObjectExplorer::IsFavorite(left->modelHash);
        const bool rf = ObjectExplorer::IsFavorite(right->modelHash);
        if (lf != rf) return lf > rf;
        return left->distance < right->distance;
    });

    CyberWidgets::CardGap();
    CyberWidgets::BeginCard(app_settings::T("MODELOS DISPONIVEIS", "AVAILABLE MODELS"));
    char count[64]{};
    std::snprintf(count, sizeof(count), "%zu / %zu", filtered.size(), seenModels.size());
    CyberWidgets::KeyValueRow(app_settings::T("Modelos", "Models"), count);

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH |
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##object_explorer_table", 6, flags,
            ImVec2(0.f, CyberTheme::Px(360.f)))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(app_settings::T("Modelo", "Model"), ImGuiTableColumnFlags_WidthStretch, 2.f);
        ImGui::TableSetupColumn("Hash", ImGuiTableColumnFlags_WidthFixed, CyberTheme::Px(92.f));
        ImGui::TableSetupColumn(app_settings::T("Distancia", "Distance"), ImGuiTableColumnFlags_WidthFixed, CyberTheme::Px(72.f));
        ImGui::TableSetupColumn(app_settings::T("Favorito", "Favorite"), ImGuiTableColumnFlags_WidthFixed, CyberTheme::Px(66.f));
        ImGui::TableSetupColumn(app_settings::T("Visivel", "Visible"), ImGuiTableColumnFlags_WidthFixed, CyberTheme::Px(60.f));
        ImGui::TableSetupColumn(app_settings::T("Ignorado", "Ignored"), ImGuiTableColumnFlags_WidthFixed, CyberTheme::Px(66.f));
        ImGui::TableHeadersRow();

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(filtered.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const auto& object = *filtered[static_cast<std::size_t>(row)];
                ImGui::PushID(static_cast<int>(object.modelHash));
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(object.modelName.c_str(), selectedHash == object.modelHash,
                        ImGuiSelectableFlags_SpanAllColumns))
                    selectedHash = object.modelHash;
                ImGui::TableSetColumnIndex(1); ImGui::Text("0x%08X", object.modelHash);
                ImGui::TableSetColumnIndex(2); ImGui::Text("%.1f m", object.distance);

                bool favorite = ObjectExplorer::IsFavorite(object.modelHash);
                ImGui::TableSetColumnIndex(3);
                if (ImGui::Checkbox("##favorite", &favorite))
                    ObjectExplorer::SetFavorite(object.modelHash, favorite);

                bool ignored = ObjectExplorer::IsIgnored(object.modelHash);
                bool visible = ObjectExplorer::IsVisible(object.modelHash);
                ImGui::TableSetColumnIndex(4);
                ImGui::BeginDisabled(ignored);
                if (ImGui::Checkbox("##visible", &visible))
                    ObjectExplorer::SetVisible(object.modelHash, visible);
                ImGui::EndDisabled();

                ImGui::TableSetColumnIndex(5);
                if (ImGui::Checkbox("##ignored", &ignored))
                    ObjectExplorer::SetIgnored(object.modelHash, ignored);
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    if (filtered.empty())
        CyberWidgets::TextLine(
            app_settings::T("Nenhum modelo corresponde a pesquisa e aos filtros atuais.",
                            "No model matches the current search and filters."),
            CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    const auto selectedIt = std::find_if(filtered.begin(), filtered.end(),
        [](const auto* object) { return object->modelHash == selectedHash; });
    if (selectedIt != filtered.end()) {
        const auto& selected = **selectedIt;
        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(app_settings::T("INFORMACAO DO MODELO", "MODEL INFORMATION"));
        CyberWidgets::KeyValueRow(app_settings::T("Nome", "Name"), selected.modelName.c_str());
        char value[96]{};
        std::snprintf(value, sizeof(value), "0x%08X", selected.modelHash);
        CyberWidgets::KeyValueRow("Model Hash", value);
        std::snprintf(value, sizeof(value), "%.2f, %.2f, %.2f",
            selected.position.x, selected.position.y, selected.position.z);
        CyberWidgets::KeyValueRow(app_settings::T("Ultima posicao", "Last position"), value);
        CyberWidgets::KeyValueRow(app_settings::T("Estado", "State"),
            ObjectDebugger::ObjectStateText(selected.state));
        CyberWidgets::EndCard();
    }
}
