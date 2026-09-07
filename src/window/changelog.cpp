#include "changelog.h"
#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "localization.h"
#include "../config/app_settings.h"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Changelog {

    namespace {
        std::vector<ChangelogEntry> g_entries;
        std::string g_last_viewed_version;
        bool g_initialized = false;
        
        ImU32 GetTypeColor(ChangeType type) {
            switch (type) {
                case ChangeType::Added: return IM_COL32(0, 200, 100, 255);
                case ChangeType::Changed: return IM_COL32(100, 150, 255, 255);
                case ChangeType::Fixed: return IM_COL32(255, 150, 0, 255);
                case ChangeType::Removed: return IM_COL32(255, 80, 80, 255);
                case ChangeType::Security: return IM_COL32(200, 0, 200, 255);
                case ChangeType::Performance: return IM_COL32(0, 255, 200, 255);
                case ChangeType::Experimental: return IM_COL32(255, 200, 0, 255);
            }
            return IM_COL32(200, 200, 200, 255);
        }
        
        const char* GetTypeLabel(ChangeType type) {
            switch (type) {
                case ChangeType::Added: return "Added";
                case ChangeType::Changed: return "Changed";
                case ChangeType::Fixed: return "Fixed";
                case ChangeType::Removed: return "Removed";
                case ChangeType::Security: return "Security";
                case ChangeType::Performance: return "Performance";
                case ChangeType::Experimental: return "Experimental";
            }
            return "Unknown";
        }
        
        void AddDefaultEntries() {
            g_entries.clear();
            
            auto addEntry = [&](const std::string& ver, ChangeType type, const std::string& title, 
                               const std::string& desc, const std::vector<std::string>& details, bool highlight = false) {
                ChangelogEntry entry;
                entry.version = ver;
                entry.date = std::chrono::system_clock::now();
                entry.type = type;
                entry.title = title;
                entry.description = desc;
                entry.details = details;
                entry.highlight = highlight;
                g_entries.push_back(entry);
            };
            
            // Version 2.5.0 - Current
            addEntry("2.5.0", ChangeType::Added, "Theme System Overhaul", 
                "Complete rewrite of the theme system with import/export support",
                {"Light/Dark theme modes", "9 accent color presets", "Custom accent color picker", "Theme import/export (JSON)", "High contrast mode improvements"});
            addEntry("2.5.0", ChangeType::Added, "Real-time Hardware Monitoring",
                "Live status tracking for DMA, Makcu, KMBox-Net, and Ferrum devices",
                {"Per-device latency graphs", "Connection health indicators", "Error tracking and diagnostics", "Historical metrics (60s window)", "Export metrics to JSON"});
            addEntry("2.5.0", ChangeType::Added, "Global Settings Search",
                "Instant search across all configuration pages",
                {"Fuzzy matching", "Keyboard navigation", "Category filtering", "Quick actions from results", "Ctrl+K to open"});
            addEntry("2.5.0", ChangeType::Added, "Config Change History with Undo/Redo",
                "Track and revert configuration changes",
                {"Full change history", "Undo/Redo support (Ctrl+Z/Y)", "Per-setting change tracking", "Visual diff display", "Export history"});
            addEntry("2.5.0", ChangeType::Added, "Fully Configurable Hotkeys",
                "Custom keyboard shortcuts for all actions",
                {"15+ assignable actions", "Modifier key support", "Conflict detection", "Import/Export hotkey profiles", "Visual binding editor"});
            addEntry("2.5.0", ChangeType::Added, "First-Run Onboarding Wizard",
                "Guided setup for new users",
                {"7-step wizard", "Hardware auto-detection", "Theme/Hotkey presets", "Game selection", "DMA setup guidance"});
            addEntry("2.5.0", ChangeType::Performance, "Animation System Optimization",
                "Reduced jank and improved frame pacing",
                {"60% less allocation per frame", "Smoother hover transitions", "Reduced motion respects system setting", "Virtualized lists for large datasets"});
            
            // Version 2.4.0
            addEntry("2.4.0", ChangeType::Added, "Multi-Monitor Overlay Positioning",
                "Advanced overlay placement across multiple displays",
                {"Per-monitor DPI scaling", "Custom position offsets", "Fullscreen/windowed modes", "Monitor hot-plug detection"});
            addEntry("2.4.0", ChangeType::Added, "Performance Dashboard",
                "Real-time performance metrics with graphs",
                {"FPS history graph", "Frame time distribution", "Entity count tracking", "Memory operation stats", "DMA latency percentile view"});
            addEntry("2.4.0", ChangeType::Fixed, "DMA Reconnection Stability",
                "Improved reliability for DMA device reconnection",
                {"Async reinit with progress", "Cancellation support", "Better error reporting", "State preservation"});
            
            // Version 2.3.0
            addEntry("2.3.0", ChangeType::Added, "ESP Preview System",
                "Real-time visualization of ESP settings",
                {"Live preview for all games", "Box/Skeleton/Health/Name toggles", "Radar preview", "Color-coded elements"});
            addEntry("2.3.0", ChangeType::Changed, "Config System Modernization",
                "Improved configuration management",
                {"Schema versioning", "Auto-migration", "Import/Export profiles", "Drag-drop reorder", "Clipboard sync"});
            addEntry("2.3.0", ChangeType::Fixed, "High DPI Scaling Fixes",
                "Perfect scaling at 125%, 150%, 200%, 250%",
                {"Crisp text rendering", "Proper hit-testing", "No blur on fractional scales"});
            
            g_last_viewed_version = "2.2.0";
        }
    }

    void Initialize() {
        if (g_initialized) return;
        AddDefaultEntries();
        g_last_viewed_version = app_settings::config.changelog_last_viewed;
        g_initialized = true;
    }

    void Shutdown() {
        g_entries.clear();
        g_initialized = false;
    }

    bool LoadFromFile(const char* path) {
        try {
            std::ifstream file(path);
            if (!file) return false;
            std::stringstream buffer;
            buffer << file.rdbuf();
            return LoadFromString(buffer.str());
        } catch (...) {
            return false;
        }
    }

    bool LoadFromString(const std::string& /*content*/) {
        // Simple parser for markdown-style changelog
        // This would be expanded for production
        return true;
    }

    const std::vector<ChangelogEntry>& GetEntries() {
        return g_entries;
    }

    std::vector<ChangelogEntry> GetEntriesForVersion(const std::string& version) {
        std::vector<ChangelogEntry> result;
        for (const auto& entry : g_entries) {
            if (entry.version == version) {
                result.push_back(entry);
            }
        }
        return result;
    }

    const ChangelogEntry* GetLatestEntry() {
        if (g_entries.empty()) return nullptr;
        return &g_entries[0];
    }

    std::vector<ChangelogEntry> GetHighlightedEntries() {
        std::vector<ChangelogEntry> result;
        for (const auto& entry : g_entries) {
            if (entry.highlight) result.push_back(entry);
        }
        return result;
    }

    bool HasNewEntries(const std::string& last_viewed_version) {
        if (last_viewed_version.empty()) return !g_entries.empty();
        
        for (const auto& entry : g_entries) {
            if (entry.version == last_viewed_version) break;
            return true;
        }
        return false;
    }

    void MarkVersionViewed(const std::string& version) {
        g_last_viewed_version = version;
        app_settings::config.changelog_last_viewed = version;
        app_settings::SaveGlobal(nullptr);
    }

    std::string GetLastViewedVersion() {
        return g_last_viewed_version;
    }

    void DrawVersionBadge(const ChangelogEntry& entry) {
        ImU32 color = GetTypeColor(entry.type);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float height = 20.0f;
        float padding = 8.0f;
        ImVec2 text_size = ImGui::CalcTextSize(entry.version.c_str());
        
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(pos, ImVec2(pos.x + text_size.x + padding * 2, pos.y + height), color, 4.0f);
        dl->AddText(ImVec2(pos.x + padding, pos.y + 2.0f), IM_COL32(0, 0, 0, 255), entry.version.c_str());
        
        ImGui::Dummy(ImVec2(text_size.x + padding * 2, height));
        ImGui::SameLine(0, 8);
    }

    void DrawChangelogCompact() {
        if (!g_initialized) Initialize();
        
        CyberWidgets::BeginCard("Changelog Highlights");
        
        auto highlights = GetHighlightedEntries();
        for (size_t i = 0; i < std::min(highlights.size(), size_t(3)); ++i) {
            const auto& entry = highlights[i];
            
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetCursorScreenPos();
            
            // Type badge
            DrawVersionBadge(entry);
            
            // Title
            dl->AddText(ImGui::GetCursorScreenPos(), CyberTheme::U32(CyberTheme::Colors.Text), entry.title.c_str());
            ImGui::Dummy(ImVec2(0, 18));
            
            // Description
            CyberWidgets::TextLine(entry.description.c_str(), CyberWidgets::TextTone::Secondary);
            
            if (i < std::min(highlights.size(), size_t(3)) - 1) {
                CyberWidgets::Separator();
            }
        }
        
        if (HasNewEntries(g_last_viewed_version)) {
            ImGui::Spacing();
            if (CyberWidgets::Button("View Full Changelog", CyberWidgets::ButtonStyle::Secondary)) {
                // Would open full changelog
            }
        }
        
        CyberWidgets::EndCard();
    }

    void DrawChangelog() {
        if (!g_initialized) Initialize();
        
        CyberWidgets::BeginCard("Changelog");
        
        // Toolbar
        CyberWidgets::BeginCardRow(3);
        CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
        static char filter_ver[16] = "";
        CyberWidgets::InputField("##version_filter", filter_ver, sizeof(filter_ver), "Filter by version...");
        CyberWidgets::EndCard();
        CyberWidgets::NextCardColumn();
        CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
        if (CyberWidgets::Button("Mark All Read", CyberWidgets::ButtonStyle::Ghost)) {
            if (!g_entries.empty()) MarkVersionViewed(g_entries[0].version);
        }
        CyberWidgets::EndCard();
        CyberWidgets::NextCardColumn();
        CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
        if (CyberWidgets::Button("Export", CyberWidgets::ButtonStyle::Secondary)) {
            // Export functionality
        }
        CyberWidgets::EndCard();
        CyberWidgets::EndCardRow();
        CyberWidgets::CardGap(12.0f);
        
        if (g_entries.empty()) {
            CyberWidgets::TextLine("No changelog entries available", CyberWidgets::TextTone::Secondary);
            CyberWidgets::EndCard();
            return;
        }
        
        CyberWidgets::BeginSurfaceList("##changelog_list", 500.0f);
        
        std::string current_version;
        for (size_t i = 0; i < g_entries.size(); ++i) {
            const auto& entry = g_entries[i];
            
            // Version header
            if (entry.version != current_version) {
                current_version = entry.version;
                bool is_new = HasNewEntries(g_last_viewed_version) && 
                    std::find_if(g_entries.begin(), g_entries.end(),
                        [&](const ChangelogEntry& e) { return e.version == g_last_viewed_version; }) > 
                    std::find(g_entries.begin(), g_entries.end(), entry);
                
                ImGui::PushID(("ver_" + entry.version).c_str());
                
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                
                // Version badge with "NEW" indicator
                ImU32 ver_color = is_new ? IM_COL32(255, 200, 0, 255) : CyberTheme::U32(CyberTheme::Colors.Gold);
                float badge_w = 120.0f;
                dl->AddRectFilled(pos, ImVec2(pos.x + badge_w, pos.y + 28.0f), ver_color, 6.0f);
                dl->AddText(ImVec2(pos.x + 10.0f, pos.y + 5.0f), IM_COL32(0, 0, 0, 255), entry.version.c_str());
                if (is_new) {
                    dl->AddText(ImVec2(pos.x + badge_w + 10.0f, pos.y + 5.0f), IM_COL32(255, 200, 0, 255), "NEW");
                }
                
                ImGui::Dummy(ImVec2(badge_w, 36.0f));
                ImGui::PopID();
            }
            
            // Entry
            ImGui::PushID(static_cast<int>(i));
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float row_w = ImGui::GetContentRegionAvail().x;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            
            // Type badge
            ImU32 type_color = GetTypeColor(entry.type);
            const char* type_label = GetTypeLabel(entry.type);
            ImVec2 type_size = ImGui::CalcTextSize(type_label);
            dl->AddRectFilled(pos, ImVec2(pos.x + type_size.x + 16.0f, pos.y + 22.0f), type_color, 4.0f);
            dl->AddText(ImVec2(pos.x + 8.0f, pos.y + 3.0f), IM_COL32(0, 0, 0, 255), type_label);
            
            // Highlight star
            if (entry.highlight) {
                dl->AddText(ImVec2(pos.x + type_size.x + 24.0f, pos.y + 1.0f), IM_COL32(255, 215, 0, 255), "★");
            }
            
            // Title
            ImVec2 title_pos(pos.x + type_size.x + 32.0f, pos.y + 2.0f);
            dl->AddText(title_pos, CyberTheme::U32(CyberTheme::Colors.Text), entry.title.c_str());
            
            // Description
            ImVec2 desc_pos(pos.x + type_size.x + 32.0f, pos.y + 22.0f);
            dl->AddText(desc_pos, CyberTheme::U32(CyberTheme::Colors.TextDisabled), entry.description.c_str());
            
            // Details
            for (size_t d = 0; d < entry.details.size(); ++d) {
                ImVec2 det_pos(pos.x + type_size.x + 48.0f, pos.y + 42.0f + d * 18.0f);
                dl->AddCircleFilled(ImVec2(det_pos.x - 6.0f, det_pos.y + 7.0f), 3.0f, CyberTheme::U32(CyberTheme::Colors.Gold));
                dl->AddText(det_pos, CyberTheme::U32(CyberTheme::Colors.TextDisabled), entry.details[d].c_str());
            }
            
            float entry_height = 42.0f + entry.details.size() * 18.0f;
            ImGui::Dummy(ImVec2(row_w, entry_height + 8.0f));
            ImGui::PopID();
        }
        
        CyberWidgets::EndSurfaceList();
        CyberWidgets::EndCard();
    }

    std::string ExportToMarkdown() {
        std::ostringstream out;
        out << "# OmniGhost Changelog\n\n";
        
        std::string current_version;
        for (const auto& entry : g_entries) {
            if (entry.version != current_version) {
                current_version = entry.version;
                out << "## Version " << entry.version << "\n\n";
            }
            
            out << "- **[" << GetTypeLabel(entry.type) << "]** " << entry.title;
            if (entry.highlight) out << " ⭐";
            out << "\n";
            out << "  " << entry.description << "\n";
            for (const auto& detail : entry.details) {
                out << "  - " << detail << "\n";
            }
            out << "\n";
        }
        return out.str();
    }

    std::string ExportToHtml() {
        std::ostringstream out;
        out << "<!DOCTYPE html><html><head><title>OmniGhost Changelog</title>";
        out << "<style>body{font-family:system-ui;max-width:800px;margin:2rem auto;padding:0 1rem;} ";
        out << ".version{background:#d4af37;color:#000;padding:0.5rem 1rem;border-radius:4px;display:inline-block;margin:1rem 0;} ";
        out << ".entry{margin:1rem 0;padding:1rem;border-left:4px solid #d4af37;background:#1a1a1a;} ";
        out << ".type{display:inline-block;padding:0.2rem 0.5rem;border-radius:3px;font-size:0.8rem;font-weight:bold;} ";
        out << ".added{background:#00c864;} .changed{background:#6496ff;} .fixed{background:#ff9600;} ";
        out << ".removed{background:#ff5050;} .security{background:#c800c8;} .performance{background:#00ffc8;} ";
        out << ".experimental{background:#ffc800;} .highlight{border-color:#ffd700;} ";
        out << "</style></head><body>";
        out << "<h1>OmniGhost Changelog</h1>";
        
        std::string current_version;
        for (const auto& entry : g_entries) {
            if (entry.version != current_version) {
                current_version = entry.version;
                out << "<div class='version'>Version " << entry.version << "</div>";
            }
            
            out << "<div class='entry " << (entry.highlight ? "highlight" : "") << "'>";
            out << "<span class='type " << GetTypeLabel(entry.type) << "'>" << GetTypeLabel(entry.type) << "</span> ";
            out << "<strong>" << entry.title << "</strong>";
            if (entry.highlight) out << " ⭐";
            out << "<br>" << entry.description << "<ul>";
            for (const auto& detail : entry.details) {
                out << "<li>" << detail << "</li>";
            }
            out << "</ul></div>";
        }
        out << "</body></html>";
        return out.str();
    }

} // namespace Changelog