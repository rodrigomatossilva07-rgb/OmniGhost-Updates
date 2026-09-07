#pragma once
#include "imgui.h"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <variant>

namespace ConfigHistory {

    enum class ActionType {
        SetBool,
        SetInt,
        SetFloat,
        SetString,
        SetColor,
        SetEnum
    };

    struct HistoryEntry {
        std::string id;           // Unique identifier for the setting
        std::string label;        // Human-readable label
        ActionType type;
        std::chrono::system_clock::time_point timestamp;
        
        // Previous value (for undo)
        std::variant<bool, int, float, std::string, ImU32> prev_value;
        // New value (for redo)
        std::variant<bool, int, float, std::string, ImU32> new_value;
    };

    void Initialize();
    void Shutdown();
    
    // Record a change
    void RecordChange(const std::string& id, const std::string& label, ActionType type,
                      const std::variant<bool, int, float, std::string, ImU32>& prev_value,
                      const std::variant<bool, int, float, std::string, ImU32>& new_value);
    
    // Undo last change
    bool Undo();
    
    // Redo last undone change
    bool Redo();
    
    // Check if undo/redo available
    bool CanUndo();
    bool CanRedo();
    
    // Get history for display
    const std::vector<HistoryEntry>& GetHistory();
    const std::vector<HistoryEntry>& GetUndoStack();
    const std::vector<HistoryEntry>& GetRedoStack();
    
    // Clear history
    void ClearHistory();
    
    // Set max history size
    void SetMaxHistorySize(size_t size);
    
    // Draw history UI
    void DrawHistoryWidget();

} // namespace ConfigHistory