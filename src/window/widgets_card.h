#pragma once

#include "widgets.h"
#include "theme.h"

namespace CyberWidgets {

// =============================================================================
// Card/Surface Widgets
// =============================================================================

void BeginCard(const char* title, bool collapsible = false);
void EndCard();

void BeginCardRow(int columns);
void NextCardColumn();
void EndCardRow();
void CardGap(float gap);

// Surface list for scrollable content
void BeginSurfaceList(const char* id, float height = 300.0f);
void EndSurfaceList();

// =============================================================================
// Layout Helpers
// =============================================================================

void DrawCheckers(ImDrawList* dl, const ImVec2& a, const ImVec2& b, float rounding);
void ApplyCursorForItem(bool enabled = true, ImGuiMouseCursor cursor = ImGuiMouseCursor_Hand);
void DrawFocusRing(float rounding = -1.0f);
void DrawSpinner(ImDrawList* dl, const ImVec2& center, float radius, float thickness, TextTone tone = TextTone::Accent);

} // namespace CyberWidgets
