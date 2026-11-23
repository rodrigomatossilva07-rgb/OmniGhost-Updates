#include "widgets_layout.h"
#include "theme.h"
#include "animations.h"
#include "localization.h"
#include "ui_feedback.h"
#include "config/app_settings.h"
#include "platform/logging.h"
#include "src/project/third_party/ImGui/imgui_internal.h"

namespace CyberWidgets {

// =============================================================================
// Section Headers
// =============================================================================

void SectionHeader(const char* label) {
    if (!label) return;
    
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Sm));
    ImGui::Text("%s", label);
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + CyberTheme::Typography::Body * 0.5f),
        ImVec2(pos.x + width, pos.y + CyberTheme::Spacing::Xs),
        CyberTheme::U32(CyberTheme::Colors.Border));
}

void SectionHeaderWithIcon(const char* label, const char* icon) {
    if (!label) return;
    
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Sm));
    
    ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::Colors.Gold);
    ImGui::Text("%s %s", icon ? icon : "", label);
    ImGui::PopStyleColor();
    
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));
    
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y + CyberTheme::Typography::Body * 0.5f),
        ImVec2(pos.x + width, pos.y + CyberTheme::Spacing::Xs),
        CyberTheme::U32(CyberTheme::Colors.Border));
}

// =============================================================================
// Separators
// =============================================================================

void Separator(float thickness) {
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    dl->AddRectFilled(
        ImVec2(pos.x, pos.y),
        ImVec2(pos.x + width, pos.y + thickness),
        CyberTheme::U32(CyberTheme::Colors.Border));
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));
}

void SeparatorText(const char* label) {
    if (!label) return;
    
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Sm));
    ImGui::SeparatorText(label);
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));
}

// =============================================================================
// Spacing
// =============================================================================

void Spacing(float y) {
    ImGui::Dummy(ImVec2(0, y));
}

void Indent(float width) {
    ImGui::Indent(width);
}

void Unindent(float width) {
    ImGui::Unindent(width);
}

// =============================================================================
// Groups
// =============================================================================

void BeginGroup(const char* id) {
    if (id && id[0]) {
        ImGui::PushID(id);
    }
    ImGui::BeginGroup();
}

void EndGroup() {
    ImGui::EndGroup();
    ImGui::PopID();
}

// =============================================================================
// Tree/Expandable
// =============================================================================

bool TreeNode(const char* label, bool defaultOpen) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    return ImGui::TreeNodeEx(label, flags);
}

bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags, bool defaultOpen) {
    if (defaultOpen) flags |= ImGuiTreeNodeFlags_DefaultOpen;
    return ImGui::TreeNodeEx(label, flags);
}

void TreePop() {
    ImGui::TreePop();
}

// =============================================================================
// Tabs
// =============================================================================

bool BeginTabBar(const char* id, ImGuiTabBarFlags flags) {
    return ImGui::BeginTabBar(id, flags);
}

void EndTabBar() {
    ImGui::EndTabBar();
}

bool BeginTabItem(const char* label, bool* p_open, ImGuiTabItemFlags flags) {
    return ImGui::BeginTabItem(label, p_open, flags);
}

void EndTabItem() {
    ImGui::EndTabItem();
}

// =============================================================================
// Tables
// =============================================================================

bool BeginTable(const char* id, int columns, ImGuiTableFlags flags, const ImVec2& outer_size, float inner_width) {
    return ImGui::BeginTable(id, columns, flags, outer_size, inner_width);
}

void EndTable() {
    ImGui::EndTable();
}

void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags, float init_width_or_weight, ImGuiID user_id) {
    ImGui::TableSetupColumn(label, flags, init_width_or_weight, user_id);
}

void TableSetupScrollFreeze(int cols, int rows) {
    ImGui::TableSetupScrollFreeze(cols, rows);
}

void TableHeadersRow() {
    ImGui::TableHeadersRow();
}

bool TableNextRow(ImGuiTableRowFlags flags, float min_row_height) {
    ImGui::TableNextRow(flags, min_row_height);
    return true;
}

void TableNextColumn() {
    ImGui::TableNextColumn();
}

void TableSetColumnIndex(int column_n) {
    ImGui::TableSetColumnIndex(column_n);
}

// =============================================================================
// Layout Helpers
// =============================================================================

void SameLine(float offset_from_start_x, float spacing) {
    ImGui::SameLine(offset_from_start_x, spacing);
}

void NewLine() {
    ImGui::NewLine();
}

void Dummy(const ImVec2& size) {
    ImGui::Dummy(size);
}

void InvisibleButton(const char* id, const ImVec2& size, ImGuiButtonFlags flags) {
    ImGui::InvisibleButton(id, size, flags);
}

// =============================================================================
// Alignment
// =============================================================================

void AlignTextToFramePadding() {
    ImGui::AlignTextToFramePadding();
}

float GetTextLineHeight() {
    return ImGui::GetTextLineHeight();
}

float GetTextLineHeightWithSpacing() {
    return ImGui::GetTextLineHeightWithSpacing();
}

// =============================================================================
// Modal/Popup Helpers
// =============================================================================

bool BeginModal(const char* name, bool* p_open, ImGuiWindowFlags flags) {
    return ImGui::BeginPopupModal(name, p_open, flags);
}

void EndModal() {
    ImGui::EndPopup();
}

void OpenPopup(const char* str_id, ImGuiPopupFlags flags) {
    ImGui::OpenPopup(str_id, flags);
}

bool BeginPopup(const char* str_id, ImGuiWindowFlags flags) {
    return ImGui::BeginPopup(str_id, flags);
}

void EndPopup() {
    ImGui::EndPopup();
}

bool BeginPopupContextItem(const char* str_id, ImGuiPopupFlags flags) {
    return ImGui::BeginPopupContextItem(str_id, flags);
}

bool BeginPopupContextWindow(const char* str_id, ImGuiPopupFlags flags) {
    return ImGui::BeginPopupContextWindow(str_id, flags);
}

void CloseCurrentPopup() {
    ImGui::CloseCurrentPopup();
}

// =============================================================================
// Tooltip/Help Helpers
// =============================================================================

void Tooltip(const char* text) {
    if (!text) return;
    if (ImGui::BeginTooltip()) {
        ImGui::TextUnformatted(text);
        ImGui::EndTooltip();
    }
}

void BeginTooltip() {
    ImGui::BeginTooltip();
}

void EndTooltip() {
    ImGui::EndTooltip();
}

void SetTooltip(const char* fmt, ...) {
    if (!fmt) return;
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    ImGui::SetTooltip("%s", buffer);
}

void HelpMarker(const char* desc) {
    if (!desc) return;
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// =============================================================================
// Notification/Toast
// =============================================================================

void Notify(const char* message, float duration) {
    if (!message) return;
    OGS_LOG_INFO("[Notify] {}", message);
    // Implementation would use OmniGhost::UiFeedback system
}

void NotifySuccess(const char* message, float duration) {
    if (!message) return;
    OGS_LOG_INFO("[NotifySuccess] {}", message);
    OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Success);
}

void NotifyWarning(const char* message, float duration) {
    if (!message) return;
    OGS_LOG_WARN("[NotifyWarning] {}", message);
    OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Warning);
}

void NotifyError(const char* message, float duration) {
    if (!message) return;
    OGS_LOG_ERROR("[NotifyError] {}", message);
    OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Error);
}

void NotifyInfo(const char* message, float duration) {
    if (!message) return;
    OGS_LOG_INFO("[NotifyInfo] {}", message);
    OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Navigate);
}

} // namespace CyberWidgets
