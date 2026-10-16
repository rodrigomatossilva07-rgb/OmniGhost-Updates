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
        std::string id;
        std::string label;
        ActionType type;
        std::chrono::system_clock::time_point timestamp;


        std::variant<bool, int, float, std::string, ImU32> prev_value;

        std::variant<bool, int, float, std::string, ImU32> new_value;
    };

    void Initialize();
    void Shutdown();


    void RecordChange(const std::string& id, const std::string& label, ActionType type,
                      const std::variant<bool, int, float, std::string, ImU32>& prev_value,
                      const std::variant<bool, int, float, std::string, ImU32>& new_value);


    bool Undo();


    bool Redo();


    bool CanUndo();
    bool CanRedo();


    const std::vector<HistoryEntry>& GetHistory();
    const std::vector<HistoryEntry>& GetUndoStack();
    const std::vector<HistoryEntry>& GetRedoStack();


    void ClearHistory();


    void SetMaxHistorySize(size_t size);


    void DrawHistoryWidget();

}