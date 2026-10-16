#pragma once
#include <string>
#include <vector>
#include <functional>

namespace GlobalSearch {

    struct SearchResult {
        std::string label;
        std::string page;
        std::string section;
        std::function<void()> action;
        float relevance = 0.0f;
    };

    void Initialize();
    void Update();
    void Shutdown();


    void RegisterItem(const std::string& label, const std::string& page, const std::string& section, std::function<void()> action);


    std::vector<SearchResult> Search(const std::string& query, int max_results = 10);


    void DrawSearchOverlay(bool* open);
    void DrawSearchBar(const char* hint = "Search settings...", float width = 300.0f);


    bool IsSearchActive();
    const std::string& GetSearchQuery();


    void ClearSearch();

}