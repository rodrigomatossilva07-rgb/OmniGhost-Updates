#pragma once

#include "widgets.h"
#include <string>
#include <vector>

namespace CyberWidgets {

// =============================================================================
// Layout/Container Widgets
// =============================================================================

// Section headers
void SectionHeader(const char* label);
void SectionHeaderWithIcon(const char* label, const char* icon);

// Separators
void Separator(float thickness = 1.0f);
void SeparatorText(const char* label);

// Spacing
void Spacing(float y = 8.0f);
void Indent(float width = 16.0f);
void Unindent(float width = 16.0f);

// Groups
void BeginGroup(const char* id = nullptr);
void EndGroup();

// Tree/Expandable
bool TreeNode(const char* label, bool defaultOpen = false);
bool TreeNodeEx(const char* label, ImGuiTreeNodeFlags flags, bool defaultOpen = false);
void TreePop();

// Tabs
bool BeginTabBar(const char* id, ImGuiTabBarFlags flags = 0);
void EndTabBar();
bool BeginTabItem(const char* label, bool* p_open = nullptr, ImGuiTabItemFlags flags = 0);
void EndTabItem();

// Tables
bool BeginTable(const char* id, int columns, ImGuiTableFlags flags = 0, const ImVec2& outer_size = ImVec2(0, 0), float inner_width = 0.0f);
void EndTable();
void TableSetupColumn(const char* label, ImGuiTableColumnFlags flags = 0, float init_width_or_weight = 0.0f, ImGuiID user_id = 0);
void TableSetupScrollFreeze(int cols, int rows);
void TableHeadersRow();
bool TableNextRow(ImGuiTableRowFlags flags = 0, float min_row_height = 0.0f);
void TableNextColumn();
void TableSetColumnIndex(int column_n);

// Layout helpers
void SameLine(float offset_from_start_x = 0.0f, float spacing = -1.0f);
void NewLine();
void Dummy(const ImVec2& size);
void InvisibleButton(const char* id, const ImVec2& size, ImGuiButtonFlags flags = 0);

// Alignment
void AlignTextToFramePadding();
float GetTextLineHeight();
float GetTextLineHeightWithSpacing();

// =============================================================================
// Modal/Popup Helpers
// =============================================================================

bool BeginModal(const char* name, bool* p_open = nullptr, ImGuiWindowFlags flags = 0);
void EndModal();
void OpenPopup(const char* str_id, ImGuiPopupFlags flags = 0);
bool BeginPopup(const char* str_id, ImGuiWindowFlags flags = 0);
void EndPopup();
bool BeginPopupContextItem(const char* str_id = nullptr, ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight);
bool BeginPopupContextWindow(const char* str_id = nullptr, ImGuiPopupFlags flags = ImGuiPopupFlags_MouseButtonRight);
void CloseCurrentPopup();

// =============================================================================
// Tooltip/Help Helpers
// =============================================================================

void Tooltip(const char* text);
void BeginTooltip();
void EndTooltip();
void SetTooltip(const char* fmt, ...);
void HelpMarker(const char* desc);

// =============================================================================
// Notification/Toast
// =============================================================================

void Notify(const char* message, float duration = 3.0f);
void NotifySuccess(const char* message, float duration = 3.0f);
void NotifyWarning(const char* message, float duration = 3.0f);
void NotifyError(const char* message, float duration = 3.0f);
void NotifyInfo(const char* message, float duration = 3.0f);

} // namespace CyberWidgets