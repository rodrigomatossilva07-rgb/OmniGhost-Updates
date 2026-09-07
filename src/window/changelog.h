#pragma once
#include <string>
#include <vector>
#include <chrono>

namespace Changelog {

    enum class ChangeType {
        Added,
        Changed,
        Fixed,
        Removed,
        Security,
        Performance,
        Experimental
    };

    struct ChangelogEntry {
        std::string version;
        std::chrono::system_clock::time_point date;
        ChangeType type;
        std::string title;
        std::string description;
        std::vector<std::string> details;
        bool highlight = false; // For major features
        
        bool operator==(const ChangelogEntry& other) const {
            return version == other.version &&
                   date == other.date &&
                   type == other.type &&
                   title == other.title &&
                   description == other.description &&
                   details == other.details &&
                   highlight == other.highlight;
        }
    };

    void Initialize();
    void Shutdown();
    
    // Load changelog from embedded resource or file
    bool LoadFromFile(const char* path);
    bool LoadFromString(const std::string& content);
    
    // Get all entries
    const std::vector<ChangelogEntry>& GetEntries();
    
    // Get entries for specific version
    std::vector<ChangelogEntry> GetEntriesForVersion(const std::string& version);
    
    // Get latest entry
    const ChangelogEntry* GetLatestEntry();
    
    // Get highlighted entries
    std::vector<ChangelogEntry> GetHighlightedEntries();
    
    // Check if there are new entries since last viewed
    bool HasNewEntries(const std::string& last_viewed_version);
    
    // Mark as viewed
    void MarkVersionViewed(const std::string& version);
    std::string GetLastViewedVersion();
    
    // Draw changelog UI
    void DrawChangelog();
    void DrawChangelogCompact(); // For sidebar/widget
    
    // Draw version badge
    void DrawVersionBadge(const ChangelogEntry& entry);
    
    // Export to string
    std::string ExportToMarkdown();
    std::string ExportToHtml();

} // namespace Changelog