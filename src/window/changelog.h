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
        bool highlight = false;

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


    bool LoadFromFile(const char* path);
    bool LoadFromString(const std::string& content);


    const std::vector<ChangelogEntry>& GetEntries();


    std::vector<ChangelogEntry> GetEntriesForVersion(const std::string& version);


    const ChangelogEntry* GetLatestEntry();


    std::vector<ChangelogEntry> GetHighlightedEntries();


    bool HasNewEntries(const std::string& last_viewed_version);


    void MarkVersionViewed(const std::string& version);
    std::string GetLastViewedVersion();


    void DrawChangelog();
    void DrawChangelogCompact();


    void DrawVersionBadge(const ChangelogEntry& entry);


    std::string ExportToMarkdown();
    std::string ExportToHtml();

}