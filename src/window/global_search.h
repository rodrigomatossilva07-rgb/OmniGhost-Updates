#pragma once
#include <functional>
#include <string>
#include <vector>

namespace GlobalSearch {
struct SearchResult {
    std::string label, page, section, related;
    std::function<void()> navigate;
    bool* toggle = nullptr;
    float relevance = 0.0f;
};
void Initialize();
void Shutdown();
void BeginPage(const std::string& page, const std::string& section,
               std::function<void()> navigate);
void RegisterPage(const std::string& label, const std::string& page,
                  const std::string& section, std::function<void()> navigate);
void ObserveToggle(const char* label, bool* value, const char* related = nullptr);
void ObserveItem(const char* label, const char* related = nullptr);
std::vector<SearchResult> Search(const std::string& query, int max_results = 14);
void Open();
bool IsOpen();
void DrawCommandPalette(bool* open);
void ClearSearch();
}
