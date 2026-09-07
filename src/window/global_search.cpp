#include "global_search.h"
#include "widgets.h"
#include "theme.h"
#include "localization.h"
#include "../config/app_settings.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <cstdio>

namespace GlobalSearch {

    namespace {
        struct SearchItem {
            std::string label;
            std::string page;
            std::string section;
            std::function<void()> action;
            std::string search_text; // Lowercase combined text for searching
        };

        std::vector<SearchItem> g_items;
        std::string g_query;
        std::vector<SearchResult> g_results;
        bool g_search_active = false;
        bool g_show_overlay = false;
        int g_selected_index = 0;
        
        std::string ToLower(const std::string& str) {
            std::string result = str;
            std::transform(result.begin(), result.end(), result.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return result;
        }
        
        float CalculateRelevance(const std::string& item_text, const std::string& query) {
            if (query.empty()) return 0.0f;
            
            std::string item_lower = ToLower(item_text);
            std::string query_lower = ToLower(query);
            
            // Exact match
            if (item_lower == query_lower) return 1.0f;
            
            // Starts with query
            if (item_lower.rfind(query_lower, 0) == 0) return 0.9f;
            
            // Contains query
            size_t pos = item_lower.find(query_lower);
            if (pos != std::string::npos) {
                // Earlier position = higher relevance
                return 0.8f - (pos / static_cast<float>(item_lower.length())) * 0.3f;
            }
            
            // Fuzzy match - check if all query chars appear in order
            size_t item_pos = 0;
            bool all_found = true;
            for (char c : query_lower) {
                item_pos = item_lower.find(c, item_pos);
                if (item_pos == std::string::npos) {
                    all_found = false;
                    break;
                }
                item_pos++;
            }
            if (all_found) return 0.4f;
            
            return 0.0f;
        }
    }

    void Initialize() {
        g_items.clear();
        g_query.clear();
        g_results.clear();
        g_search_active = false;
        g_show_overlay = false;
        g_selected_index = 0;
    }

    void Update() {
        // Update search results when query changes
        if (!g_query.empty()) {
            g_results.clear();
            for (const auto& item : g_items) {
                float relevance = CalculateRelevance(item.search_text, g_query);
                if (relevance > 0.0f) {
                    SearchResult result;
                    result.label = item.label;
                    result.page = item.page;
                    result.section = item.section;
                    result.action = item.action;
                    result.relevance = relevance;
                    g_results.push_back(result);
                }
            }
            
            // Sort by relevance
            std::sort(g_results.begin(), g_results.end(),
                [](const SearchResult& a, const SearchResult& b) {
                    return a.relevance > b.relevance;
                });
            
            g_selected_index = 0;
        } else {
            g_results.clear();
        }
    }

    void Shutdown() {
        g_items.clear();
        g_results.clear();
        g_query.clear();
    }

    void RegisterItem(const std::string& label, const std::string& page, const std::string& section, std::function<void()> action) {
        SearchItem item;
        item.label = label;
        item.page = page;
        item.section = section;
        item.action = action;
        item.search_text = ToLower(label + " " + page + " " + section);
        g_items.push_back(item);
    }

    std::vector<SearchResult> Search(const std::string& query, int max_results) {
        g_query = query;
        Update();
        
        std::vector<SearchResult> limited_results;
        int count = std::min(max_results, static_cast<int>(g_results.size()));
        for (int i = 0; i < count; ++i) {
            limited_results.push_back(g_results[i]);
        }
        return limited_results;
    }

    void DrawSearchBar(const char* hint, float width) {
        ImGui::SetNextItemWidth(width);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s", g_query.c_str());
        if (ImGui::InputTextWithHint("##global_search", hint, buf, sizeof(buf))) {
            g_query = buf;
            g_search_active = !g_query.empty();
            Update();
        }
        
        // Show results dropdown if search is active
        if (g_search_active && !g_results.empty()) {
            ImGui::SameLine();
            if (ImGui::BeginPopup("##search_results")) {
                for (size_t i = 0; i < g_results.size(); ++i) {
                    const auto& result = g_results[i];
                    bool selected = static_cast<int>(i) == g_selected_index;
                    
                    if (ImGui::Selectable((result.label + "  [" + result.page + "]").c_str(), selected)) {
                        if (result.action) result.action();
                        g_query.clear();
                        g_search_active = false;
                        ImGui::CloseCurrentPopup();
                    }
                    
                    if (selected) g_selected_index = static_cast<int>(i);
                }
                ImGui::EndPopup();
            }
        }
        
        // Keyboard navigation
        if (g_search_active && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
                g_selected_index = std::min(g_selected_index + 1, static_cast<int>(g_results.size()) - 1);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
                g_selected_index = std::max(g_selected_index - 1, 0);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) && g_selected_index >= 0 && g_selected_index < static_cast<int>(g_results.size())) {
                if (g_results[g_selected_index].action) g_results[g_selected_index].action();
                g_query.clear();
                g_search_active = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                g_query.clear();
                g_search_active = false;
            }
        }
    }

    void DrawSearchOverlay(bool* open) {
        if (!open || !*open) return;
        
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Global Search", open, ImGuiWindowFlags_NoCollapse)) {
            ImGui::Text("Search Settings");
            ImGui::Separator();
            
            DrawSearchBar("Type to search settings...", -1.0f);
            
            ImGui::Spacing();
            
            if (g_results.empty() && !g_query.empty()) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "No results found for \"%s\"", g_query.c_str());
            } else if (g_results.empty() && g_query.empty()) {
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Type to search across all settings pages");
            } else {
                ImGui::BeginChild("results", ImVec2(0, -1), true);
                for (size_t i = 0; i < g_results.size(); ++i) {
                    const auto& result = g_results[i];
                    bool selected = static_cast<int>(i) == g_selected_index;
                    
                    if (ImGui::Selectable((result.label + "##" + std::to_string(i)).c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
                        if (result.action) result.action();
                        g_query.clear();
                        g_search_active = false;
                        *open = false;
                    }
                    
                    if (selected) g_selected_index = static_cast<int>(i);
                    
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s > %s]", result.page.c_str(), result.section.c_str());
                }
                ImGui::EndChild();
            }
        }
        ImGui::End();
    }

    bool IsSearchActive() {
        return g_search_active;
    }

    const std::string& GetSearchQuery() {
        return g_query;
    }

    void ClearSearch() {
        g_query.clear();
        g_search_active = false;
        g_results.clear();
        g_selected_index = 0;
    }

} // namespace GlobalSearch