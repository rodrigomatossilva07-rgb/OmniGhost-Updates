#pragma once
#include <string>
#include <vector>
#include <functional>

namespace GlobalSearch {

    struct SearchResult {
        std::string label;
        std::string page;
        std::string section;
        std::function<void()> action; // Callback to navigate to the setting
        float relevance = 0.0f;
    };

    void Initialize();
    void Update();
    void Shutdown();
    
    // Register a searchable item
    void RegisterItem(const std::string& label, const std::string& page, const std::string& section, std::function<void()> action);
    
    // Perform search
    std::vector<SearchResult> Search(const std::string& query, int max_results = 10);
    
    // Draw search UI (call from header or global overlay)
    void DrawSearchOverlay(bool* open);
    void DrawSearchBar(const char* hint = "Search settings...", float width = 300.0f);
    
    // Check if search is active
    bool IsSearchActive();
    const std::string& GetSearchQuery();
    
    // Clear search
    void ClearSearch();

} // namespace GlobalSearch