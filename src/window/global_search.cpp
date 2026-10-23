#include "global_search.h"
#include "theme.h"
#include "third_party/ImGui/imgui.h"
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace GlobalSearch {
namespace {
struct SearchItem {
    std::string label, page, section, related, search_text;
    std::function<void()> navigate;
    bool* toggle = nullptr;
};
std::vector<SearchItem> g_items;
std::string g_query, g_page, g_section;
std::function<void()> g_navigate;
bool g_open = false, g_focus_search = false;
int g_selected = 0;

std::string DisplayLabel(const char* label) {
    if (!label) return {};
    std::string value(label);
    if (const std::size_t marker = value.find("##"); marker != std::string::npos) value.resize(marker);
    return value;
}
std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
std::string InferRelated(const std::string& label) {
    const std::string lower = Lower(label);
    if (lower.find("visib") != std::string::npos || lower.find("ocult") != std::string::npos)
        return "Relacionado: verificação e cores de visibilidade";
    if (lower.find("esqueleto") != std::string::npos || lower.find("skeleton") != std::string::npos ||
        lower.find("osso") != std::string::npos)
        return "Requer: ESP ativo e dados de ossos disponíveis";
    if (lower.find("cabeça") != std::string::npos || lower.find("head") != std::string::npos ||
        lower.find("halo") != std::string::npos)
        return "Relacionado: elementos da cabeça e escala por distância";
    if (lower.find("radar") != std::string::npos)
        return "Relacionado: posição, alcance e dados do jogador";
    return {};
}
float Relevance(const std::string& text, const std::string& query) {
    if (query.empty()) return 0.25f;
    const std::string haystack = Lower(text), needle = Lower(query);
    if (haystack == needle) return 1.0f;
    if (haystack.starts_with(needle)) return 0.96f;
    if (const std::size_t pos = haystack.find(needle); pos != std::string::npos)
        return 0.88f - static_cast<float>(pos) /
            static_cast<float>((std::max)(std::size_t{1}, haystack.size())) * 0.18f;
    std::size_t cursor = 0;
    for (const char character : needle) {
        cursor = haystack.find(character, cursor);
        if (cursor == std::string::npos) return 0.0f;
        ++cursor;
    }
    return 0.42f;
}
void Upsert(SearchItem item) {
    if (item.label.empty() || item.page.empty()) return;
    const auto same = [&](const SearchItem& current) {
        return current.label == item.label && current.page == item.page && current.section == item.section;
    };
    if (auto it = std::find_if(g_items.begin(), g_items.end(), same); it != g_items.end()) {
        it->navigate = std::move(item.navigate);
        if (item.toggle) it->toggle = item.toggle;
        if (!item.related.empty()) it->related = std::move(item.related);
        it->search_text = it->label + " " + it->page + " " + it->section + " " + it->related;
        return;
    }
    item.search_text = item.label + " " + item.page + " " + item.section + " " + item.related;
    g_items.push_back(std::move(item));
}
}

void Initialize() { g_items.clear(); g_query.clear(); g_open = false; g_focus_search = false; g_selected = 0; }
void Shutdown() { Initialize(); g_page.clear(); g_section.clear(); g_navigate = {}; }
void BeginPage(const std::string& page, const std::string& section, std::function<void()> navigate) {
    g_page = page; g_section = section; g_navigate = std::move(navigate);
    RegisterPage(page, page, section, g_navigate);
}
void RegisterPage(const std::string& label, const std::string& page,
                  const std::string& section, std::function<void()> navigate) {
    Upsert({label, page, section, "Abrir página", {}, std::move(navigate), nullptr});
}
void ObserveToggle(const char* label, bool* value, const char* related) {
    if (!value || g_page.empty()) return;
    const std::string visible = DisplayLabel(label);
    Upsert({visible, g_page, g_section, related ? related : InferRelated(visible), {}, g_navigate, value});
}
void ObserveItem(const char* label, const char* related) {
    if (g_page.empty()) return;
    const std::string visible = DisplayLabel(label);
    Upsert({visible, g_page, g_section, related ? related : InferRelated(visible), {}, g_navigate, nullptr});
}
std::vector<SearchResult> Search(const std::string& query, int max_results) {
    std::vector<SearchResult> results;
    for (const SearchItem& item : g_items) {
        const float score = Relevance(item.search_text, query);
        if (score > 0.0f)
            results.push_back({item.label, item.page, item.section, item.related,
                               item.navigate, item.toggle, score});
    }
    std::stable_sort(results.begin(), results.end(), [](const SearchResult& lhs, const SearchResult& rhs) {
        if (lhs.relevance != rhs.relevance) return lhs.relevance > rhs.relevance;
        if (lhs.page != rhs.page) return lhs.page < rhs.page;
        return lhs.label < rhs.label;
    });
    if (static_cast<int>(results.size()) > max_results) results.resize(static_cast<std::size_t>(max_results));
    return results;
}
void Open() { g_open = true; g_focus_search = true; g_selected = 0; }
bool IsOpen() { return g_open; }

void DrawCommandPalette(bool* open) {
    if (!open || !*open) return;
    g_open = true;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.32f));
    ImGui::SetNextWindowSize(ImVec2(CyberTheme::Px(620.f), CyberTheme::Px(430.f)), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CyberTheme::Px(16.f), CyberTheme::Px(16.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Px(12.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CyberTheme::Colors.Panel);
    if (ImGui::Begin("##omnighost_command_palette", open,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("PALETA DE COMANDOS");
        ImGui::SameLine(); ImGui::TextDisabled("  CTRL + K"); ImGui::Spacing();
        char query[192]{};
        std::snprintf(query, sizeof(query), "%s", g_query.c_str());
        ImGui::SetNextItemWidth(-1.f);
        if (g_focus_search) { ImGui::SetKeyboardFocusHere(); g_focus_search = false; }
        if (ImGui::InputTextWithHint("##command_query", "Procurar opção ou página...", query, sizeof(query))) {
            g_query = query; g_selected = 0;
        }
        std::vector<SearchResult> results = Search(g_query);
        if (!results.empty()) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) g_selected = (std::min)(g_selected + 1, static_cast<int>(results.size()) - 1);
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) g_selected = (std::max)(g_selected - 1, 0);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) *open = false;
        ImGui::Spacing(); ImGui::Separator();
        ImGui::BeginChild("##command_results", ImVec2(0.f, -CyberTheme::Px(24.f)), false);
        if (results.empty()) { ImGui::Spacing(); ImGui::TextDisabled("Nenhum resultado encontrado."); }
        for (int index = 0; index < static_cast<int>(results.size()); ++index) {
            SearchResult& result = results[static_cast<std::size_t>(index)];
            ImGui::PushID(index);
            const bool selected = index == g_selected;
            if (ImGui::Selectable(result.label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick,
                    ImVec2(result.toggle ? -CyberTheme::Px(94.f) : -1.f, CyberTheme::Px(42.f)))) {
                g_selected = index;
                if (result.navigate) result.navigate();
                *open = false;
            }
            if (ImGui::IsItemHovered()) g_selected = index;
            const ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + CyberTheme::Px(10.f), min.y + CyberTheme::Px(24.f)),
                CyberTheme::U32(CyberTheme::Colors.TextDisabled),
                (result.page + "  >  " + result.section +
                 (result.related.empty() ? "" : "  ·  " + result.related)).c_str());
            if (result.toggle) {
                ImGui::SameLine();
                if (ImGui::SmallButton(*result.toggle ? "DESATIVAR" : "ATIVAR")) *result.toggle = !*result.toggle;
            }
            if (selected && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
                if (result.toggle) *result.toggle = !*result.toggle;
                if (result.navigate) result.navigate();
                *open = false;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::TextDisabled("Enter: abrir/alternar  ·  setas: navegar  ·  Esc: fechar");
    }
    ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    g_open = *open;
    if (!g_open) ClearSearch();
}
void ClearSearch() { g_query.clear(); g_selected = 0; }
}
