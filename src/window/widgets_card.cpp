#include "widgets_card.h"
#include "theme.h"
#include "animations.h"
#include "localization.h"
#include "src/project/third_party/ImGui/imgui_internal.h"

#include <vector>

namespace CyberWidgets {

namespace {

struct SurfaceListState {
    ImVec2 position;
    float width;
    float height;
};

std::vector<SurfaceListState> g_surface_lists;

struct CardRowState {
    int columns = 1;
    int currentColumn = 0;
    float startX = 0.0f;
    float columnWidth = 0.0f;
    float maxHeight = 0.0f;
    std::vector<float> columnHeights;
};

std::vector<CardRowState> g_card_rows;

} // anonymous namespace

// =============================================================================
// Card Widgets Implementation
// =============================================================================

void BeginCard(const char* title, bool collapsible) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 cardMin(pos.x, pos.y);
    const ImVec2 cardMax(pos.x + width, pos.y + CyberTheme::Metrics::CardHeaderHeight);

    // Card background
    dl->AddRectFilled(cardMin, ImVec2(cardMax.x, cardMin.y + 200.0f),
        CyberTheme::U32(CyberTheme::Colors.Card), CyberTheme::Radius::Lg);
    dl->AddRect(cardMin, ImVec2(cardMax.x, cardMin.y + 200.0f),
        CyberTheme::U32(CyberTheme::Colors.Border), CyberTheme::Radius::Lg);

    // Header
    if (title && title[0]) {
        dl->AddText(ImVec2(cardMin.x + CyberTheme::Spacing::Md, cardMin.y + CyberTheme::Spacing::Sm),
            CyberTheme::U32(CyberTheme::Colors.Text), title);
    }

    ImGui::Dummy(ImVec2(0, CyberTheme::Metrics::CardHeaderHeight));
}

void EndCard() {
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Sm));
}

void BeginCardRow(int columns) {
    CardRowState state;
    state.columns = columns;
    state.currentColumn = 0;
    state.startX = ImGui::GetCursorScreenPos().x;
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    state.columnWidth = (availableWidth - CyberTheme::Spacing::Lg * (columns - 1)) / columns;
    state.columnHeights.assign(columns, 0.0f);

    g_card_rows.push_back(state);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(CyberTheme::Spacing::Lg, CyberTheme::Spacing::Md));
}

void NextCardColumn() {
    if (g_card_rows.empty()) return;
    CardRowState& state = g_card_rows.back();

    ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Xs));
    state.columnHeights[state.currentColumn] = ImGui::GetCursorScreenPos().y - ImGui::GetCursorScreenPos().y + ImGui::GetCursorPosY();

    state.currentColumn = (state.currentColumn + 1) % state.columns;
    if (state.currentColumn == 0) {
        // New row
        ImGui::Dummy(ImVec2(0, CyberTheme::Spacing::Lg));
    } else {
        ImGui::SameLine();
    }
}

void EndCardRow() {
    if (g_card_rows.empty()) return;
    CardRowState& state = g_card_rows.back();

    float maxHeight = 0.0f;
    for (float h : state.columnHeights) {
        maxHeight = std::max(maxHeight, h);
    }

    ImGui::Dummy(ImVec2(0, maxHeight + CyberTheme::Spacing::Md));
    ImGui::PopStyleVar();
    g_card_rows.pop_back();
}

void CardGap(float gap) {
    ImGui::Dummy(ImVec2(0, gap));
}

void BeginSurfaceList(const char* id, float height) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) return;

    const ImGuiID childId = window->GetID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;

    SurfaceListState state;
    state.position = pos;
    state.width = width;
    state.height = height;

    g_surface_lists.push_back(state);

    ImGui::BeginChild(childId, ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar);
}

void EndSurfaceList() {
    ImGui::EndChild();
    if (!g_surface_lists.empty()) g_surface_lists.pop_back();
}

} // namespace CyberWidgets
