#include "widgets_input.h"
#include "theme.h"
#include "animations.h"
#include "localization.h"
#include "src/project/third_party/ImGui/imgui_internal.h"
#include <misc/cpp/imgui_stdlib.h>

namespace CyberWidgets {

namespace {

float WidgetWidth() {
    return std::max(40.0f,
        ImGui::GetContentRegionAvail().x);
}

} // namespace

// =============================================================================
// Input Text Widgets Implementation
// =============================================================================

bool InputText(const char* label, std::string* value, ImGuiInputTextFlags flags) {
    if (!value) return false;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    ImVec2 size = ImVec2(WidgetWidth(), CyberTheme::Metrics::ControlHeight);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);

    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    const bool focused = ImGui::IsItemFocused();

    // Draw background
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 bgColor = focused ? CyberTheme::Colors.CardHover : (hovered ? CyberTheme::Colors.PanelHover : CyberTheme::Colors.Panel);
    dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(end.x, end.y),
        CyberTheme::U32(bgColor), CyberTheme::Metrics::ControlRounding);
    dl->AddRect(ImVec2(pos.x, pos.y), ImVec2(end.x, end.y),
        CyberTheme::U32(hovered ? CyberTheme::Colors.Gold : CyberTheme::Colors.Border),
        CyberTheme::Metrics::ControlRounding, 0, 1.0f);

    // Label
    if (label && label[0]) {
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(pos.x + CyberTheme::Spacing::Sm, pos.y - CyberTheme::Spacing::Sm - CyberTheme::Typography::Caption),
            CyberTheme::U32(CyberTheme::Colors.TextDisabled), label);
    }

    // Input field
    ImGui::PushID(label);
    ImGui::SetCursorScreenPos(ImVec2(pos.x + CyberTheme::Spacing::Sm, pos.y + CyberTheme::Spacing::Xs));
    ImGui::SetNextItemWidth(size.x - CyberTheme::Spacing::Md);
    bool changed = ImGui::InputText("##input", value, flags | ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopID();

    return changed;
}

bool InputTextMultiline(const char* label, std::string* value, const ImVec2& size, ImGuiInputTextFlags flags) {
    if (!value) return false;

    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;

    const ImVec2 size_final = size.x > 0 ? ImVec2(size.x, size.y) : ImVec2(WidgetWidth(), 100.0f);
    
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::InputTextMultiline("##multiline", value, size_final, flags);
    ImGui::PopID();

    return changed;
}

bool InputFloat(const char* label, float* value, float step, float step_fast, const char* format) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::InputFloat(label, value, step, step_fast, format);
    ImGui::PopID();

    return changed;
}

bool InputDouble(const char* label, double* value, double step, double step_fast, const char* format) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::InputDouble(label, value, step, step_fast, format);
    ImGui::PopID();

    return changed;
}

bool InputInt(const char* label, int* value, int step, int step_fast) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::InputInt(label, value, step, step_fast);
    ImGui::PopID();

    return changed;
}

bool InputInt64(const char* label, long long* value, long long step, long long step_fast) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::InputScalar(label, ImGuiDataType_S64, value, step > 0 ? &step : nullptr, step_fast > 0 ? &step_fast : nullptr);
    ImGui::PopID();

    return changed;
}

bool InputUInt64(const char* label, unsigned long long* value, unsigned long long step, unsigned long long step_fast) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::InputScalar(label, ImGuiDataType_U64, value, step > 0 ? &step : nullptr, step_fast > 0 ? &step_fast : nullptr);
    ImGui::PopID();

    return changed;
}

bool DragFloat(const char* label, float* value, float speed, float min, float max, const char* format) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragFloat(label, value, speed, min, max, format)
        : ImGui::DragFloat(label, value, speed, 0.0f, 0.0f, format);
    ImGui::PopID();

    return changed;
}

bool DragFloat2(const char* label, float v[2], float speed, float min, float max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragFloat2(label, v, speed, min, max, format)
        : ImGui::DragFloat2(label, v, speed, 0.0f, 0.0f, format);
    ImGui::PopID();

    return changed;
}

bool DragFloat3(const char* label, float v[3], float speed, float min, float max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragFloat3(label, v, speed, min, max, format)
        : ImGui::DragFloat3(label, v, speed, 0.0f, 0.0f, format);
    ImGui::PopID();

    return changed;
}

bool DragFloat4(const char* label, float v[4], float speed, float min, float max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragFloat4(label, v, speed, min, max, format)
        : ImGui::DragFloat4(label, v, speed, 0.0f, 0.0f, format);
    ImGui::PopID();

    return changed;
}

bool DragInt(const char* label, int* value, float speed, int min, int max, const char* format) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragInt(label, value, speed, min, max, format)
        : ImGui::DragInt(label, value, speed, 0, 0, format);
    ImGui::PopID();

    return changed;
}

bool DragInt2(const char* label, int v[2], float speed, int min, int max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragInt2(label, v, speed, min, max, format)
        : ImGui::DragInt2(label, v, speed, 0, 0, format);
    ImGui::PopID();

    return changed;
}

bool DragInt3(const char* label, int v[3], float speed, int min, int max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragInt3(label, v, speed, min, max, format)
        : ImGui::DragInt3(label, v, speed, 0, 0, format);
    ImGui::PopID();

    return changed;
}

bool DragInt4(const char* label, int v[4], float speed, int min, int max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = min > 0 && max > 0
        ? ImGui::DragInt4(label, v, speed, min, max, format)
        : ImGui::DragInt4(label, v, speed, 0, 0, format);
    ImGui::PopID();

    return changed;
}

bool SliderFloat(const char* label, float* value, float min, float max, const char* format) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderFloat(label, value, min, max, format);
    ImGui::PopID();

    return changed;
}

bool SliderFloat2(const char* label, float v[2], float min, float max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderFloat2(label, v, min, max, format);
    ImGui::PopID();

    return changed;
}

bool SliderFloat3(const char* label, float v[3], float min, float max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderFloat3(label, v, min, max, format);
    ImGui::PopID();

    return changed;
}

bool SliderFloat4(const char* label, float v[4], float min, float max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderFloat4(label, v, min, max, format);
    ImGui::PopID();

    return changed;
}

bool SliderInt(const char* label, int* value, int min, int max, const char* format) {
    if (!value) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderInt(label, value, min, max, format);
    ImGui::PopID();

    return changed;
}

bool SliderInt2(const char* label, int v[2], int min, int max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderInt2(label, v, min, max, format);
    ImGui::PopID();

    return changed;
}

bool SliderInt3(const char* label, int v[3], int min, int max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderInt3(label, v, min, max, format);
    ImGui::PopID();

    return changed;
}

bool SliderInt4(const char* label, int v[4], int min, int max, const char* format) {
    if (!v) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::SliderInt4(label, v, min, max, format);
    ImGui::PopID();

    return changed;
}

bool ColorEdit3(const char* label, float col[3], ImGuiColorEditFlags flags) {
    if (!col) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::ColorEdit3(label, col, flags);
    ImGui::PopID();

    return changed;
}

bool ColorEdit4(const char* label, float col[4], ImGuiColorEditFlags flags) {
    if (!col) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::ColorEdit4(label, col, flags);
    ImGui::PopID();

    return changed;
}

bool ColorPicker3(const char* label, float col[3], ImGuiColorEditFlags flags) {
    if (!col) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::ColorPicker3(label, col, flags);
    ImGui::PopID();

    return changed;
}

bool ColorPicker4(const char* label, float col[4], ImGuiColorEditFlags flags) {
    if (!col) return false;

    ImGui::PushID(label);
    ImGui::SetNextItemWidth(WidgetWidth() - CyberTheme::Spacing::Md);
    bool changed = ImGui::ColorPicker4(label, col, flags);
    ImGui::PopID();

    return changed;
}

} // namespace CyberWidgets
