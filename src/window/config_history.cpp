#include "config_history.h"
#include "widgets.h"
#include "theme.h"
#include "localization.h"
#include <algorithm>
#include <variant>

namespace ConfigHistory {

    namespace {
        constexpr size_t DEFAULT_MAX_HISTORY = 100;
        
        std::vector<HistoryEntry> g_history;
        std::vector<HistoryEntry> g_undo_stack;
        std::vector<HistoryEntry> g_redo_stack;
        size_t g_max_history = DEFAULT_MAX_HISTORY;
        bool g_initialized = false;
    }

    void Initialize() {
        if (g_initialized) return;
        g_history.clear();
        g_undo_stack.clear();
        g_redo_stack.clear();
        g_initialized = true;
    }

    void Shutdown() {
        g_history.clear();
        g_undo_stack.clear();
        g_redo_stack.clear();
        g_initialized = false;
    }

    void RecordChange(const std::string& id, const std::string& label, ActionType type,
                      const std::variant<bool, int, float, std::string, ImU32>& prev_value,
                      const std::variant<bool, int, float, std::string, ImU32>& new_value) {
        if (!g_initialized) Initialize();
        
        // Don't record if values are the same
        if (prev_value == new_value) return;
        
        HistoryEntry entry;
        entry.id = id;
        entry.label = label;
        entry.type = type;
        entry.timestamp = std::chrono::system_clock::now();
        entry.prev_value = prev_value;
        entry.new_value = new_value;
        
        g_history.push_back(entry);
        g_undo_stack.push_back(entry);
        g_redo_stack.clear(); // New change clears redo stack
        
        // Limit history size
        if (g_history.size() > g_max_history) {
            g_history.erase(g_history.begin());
        }
        if (g_undo_stack.size() > g_max_history) {
            g_undo_stack.erase(g_undo_stack.begin());
        }
    }

    bool Undo() {
        if (g_undo_stack.empty()) return false;
        
        HistoryEntry entry = g_undo_stack.back();
        g_undo_stack.pop_back();
        g_redo_stack.push_back(entry);
        
        // Apply the previous value (this would need to be connected to actual settings)
        // For now, we just track it. The actual application would be done by the caller.
        return true;
    }

    bool Redo() {
        if (g_redo_stack.empty()) return false;
        
        HistoryEntry entry = g_redo_stack.back();
        g_redo_stack.pop_back();
        g_undo_stack.push_back(entry);
        
        // Apply the new value
        return true;
    }

    bool CanUndo() {
        return !g_undo_stack.empty();
    }

    bool CanRedo() {
        return !g_redo_stack.empty();
    }

    const std::vector<HistoryEntry>& GetHistory() {
        return g_history;
    }

    const std::vector<HistoryEntry>& GetUndoStack() {
        return g_undo_stack;
    }

    const std::vector<HistoryEntry>& GetRedoStack() {
        return g_redo_stack;
    }

    void ClearHistory() {
        g_history.clear();
        g_undo_stack.clear();
        g_redo_stack.clear();
    }

    void SetMaxHistorySize(size_t size) {
        g_max_history = size;
    }

    std::string VariantToString(const std::variant<bool, int, float, std::string, ImU32>& v) {
        if (std::holds_alternative<bool>(v)) {
            return std::get<bool>(v) ? "true" : "false";
        } else if (std::holds_alternative<int>(v)) {
            return std::to_string(std::get<int>(v));
        } else if (std::holds_alternative<float>(v)) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.3f", std::get<float>(v));
            return buf;
        } else if (std::holds_alternative<std::string>(v)) {
            return std::get<std::string>(v);
        } else if (std::holds_alternative<ImU32>(v)) {
            ImU32 c = std::get<ImU32>(v);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", 
                (c >> 0) & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
            return buf;
        }
        return "unknown";
    }

    std::string ActionTypeToString(ActionType type) {
        switch (type) {
            case ActionType::SetBool: return "Toggle";
            case ActionType::SetInt: return "Integer";
            case ActionType::SetFloat: return "Float";
            case ActionType::SetString: return "Text";
            case ActionType::SetColor: return "Color";
            case ActionType::SetEnum: return "Enum";
        }
        return "Unknown";
    }

    void DrawHistoryWidget() {
        if (!g_initialized) Initialize();
        
        CyberWidgets::BeginCard("Config Change History");
        
        // Toolbar
        CyberWidgets::BeginCardRow(3);
        
        CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
        if (CyberWidgets::Button(CanUndo() ? "Undo (Ctrl+Z)" : "Undo", 
                CyberWidgets::ButtonStyle::Secondary, ImVec2(-1, 0), CanUndo())) {
            Undo();
        }
        CyberWidgets::EndCard();
        
        CyberWidgets::NextCardColumn();
        CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
        if (CyberWidgets::Button(CanRedo() ? "Redo (Ctrl+Y)" : "Redo", 
                CyberWidgets::ButtonStyle::Secondary, ImVec2(-1, 0), CanRedo())) {
            Redo();
        }
        CyberWidgets::EndCard();
        
        CyberWidgets::NextCardColumn();
        CyberWidgets::BeginCard("", CyberWidgets::CardRowHalfWidth());
        if (CyberWidgets::Button("Clear History", CyberWidgets::ButtonStyle::Ghost, ImVec2(-1, 0))) {
            ClearHistory();
        }
        CyberWidgets::EndCard();
        
        CyberWidgets::EndCardRow();
        CyberWidgets::CardGap(8.0f);
        
        if (g_history.empty()) {
            CyberWidgets::TextLine("No changes recorded yet", CyberWidgets::TextTone::Secondary);
        } else {
            CyberWidgets::BeginSurfaceList("##history_list", 300.0f);
            
            for (auto it = g_history.rbegin(); it != g_history.rend(); ++it) {
                const auto& entry = *it;
                ImGui::PushID(entry.id.c_str());
                
                const ImVec2 row = ImGui::GetCursorScreenPos();
                const float row_width = ImGui::GetContentRegionAvail().x;
                const float rowH = 48.0f;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                
                // Background
                dl->AddRectFilled(row, ImVec2(row.x + row_width, row.y + rowH),
                    CyberTheme::U32(CyberTheme::Colors.Surface), CyberTheme::Radius::Sm);
                
                // Timestamp
                auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
                char time_str[32];
                std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));
                
                dl->AddText(ImVec2(row.x + 10.0f, row.y + 4.0f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), time_str);
                
                // Label
                dl->AddText(ImVec2(row.x + 10.0f, row.y + 22.0f),
                    CyberTheme::U32(CyberTheme::Colors.Text), entry.label.c_str());
                
                // Type badge
                const std::string type_str_owned = ActionTypeToString(entry.type);
                const char* type_str = type_str_owned.c_str();
                ImVec2 type_size = ImGui::CalcTextSize(type_str);
                dl->AddRectFilled(ImVec2(row.x + row_width - type_size.x - 20.0f, row.y + 4.0f),
                    ImVec2(row.x + row_width - 8.0f, row.y + 22.0f),
                    CyberTheme::U32(CyberTheme::Colors.Gold), 4.0f);
                dl->AddText(ImVec2(row.x + row_width - type_size.x - 14.0f, row.y + 6.0f),
                    IM_COL32(24, 22, 18, 255), type_str);
                
                // Value change
                std::string prev_str = VariantToString(entry.prev_value);
                std::string new_str = VariantToString(entry.new_value);
                char change_str[128];
                std::snprintf(change_str, sizeof(change_str), "%s \u2192 %s", prev_str.c_str(), new_str.c_str());
                dl->AddText(ImVec2(row.x + row_width - 200.0f, row.y + 28.0f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), change_str);
                
                ImGui::Dummy(ImVec2(row_width, rowH + 4.0f));
                ImGui::PopID();
            }
            
            CyberWidgets::EndSurfaceList();
        }
        
        CyberWidgets::EndCard();
    }

} // namespace ConfigHistory