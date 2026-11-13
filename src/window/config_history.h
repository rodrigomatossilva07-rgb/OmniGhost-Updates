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

    using ConfigValue = std::variant<bool, int, float, std::string, ImU32>;
    using ConfigTarget = std::variant<bool*, int*, float*, std::string*, ImU32*>;

    struct HistoryEntry {
        std::string id;
        std::string label;
        ActionType type;
        std::chrono::system_clock::time_point timestamp;

        ConfigTarget target{};

        ConfigValue prev_value;

        ConfigValue new_value;
    };

    void Initialize();
    void Shutdown();


    void RecordChange(const std::string& id, const std::string& label, ActionType type,
                      const ConfigTarget& target,
                      const ConfigValue& prev_value,
                      const ConfigValue& new_value);


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