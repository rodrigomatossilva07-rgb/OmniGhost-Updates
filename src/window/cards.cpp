#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "../ImGui/imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <vector>
#include <cstdio>

namespace CyberWidgets {

    namespace {
        struct CardState {
            ImVec2 start;
            float width;
            float content_width;
            const char* title;
            bool inside_row;
            ImGuiWindow* window;
            float content_region_max_x;
            float work_rect_max_x;
        };

        std::vector<CardState> g_cards;
        int g_card_row_depth = 0;
        int g_card_row_columns = 2;
        float g_card_row_cell = 0.0f;
    }

    void BeginCard(const char* title, float width)
    {
        ImGui::PushID(title);

        const float available = ImGui::GetContentRegionAvail().x;
        const float card_width = std::max(120.0f, width > 0.0f ? width : available);
        const float inset = CyberTheme::Metrics::CardPadding;
        const float content_width = std::max(80.0f, card_width - inset * 2.0f);
        const ImVec2 start = ImGui::GetCursorScreenPos();
        ImGuiWindow* window = ImGui::GetCurrentWindow();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);

        g_cards.push_back({
            start,
            card_width,
            content_width,
            title,
            g_card_row_depth > 0,
            window,
            window->ContentRegionRect.Max.x,
            window->WorkRect.Max.x
        });

        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(card_width, CyberTheme::Metrics::CardHeaderHeight));
        ImGui::Indent(inset);
        ImGui::PushItemWidth(content_width);

        // Constrain native widgets that use -1/GetContentRegionAvail (list
        // boxes, inputs, etc.) to the card just like the custom controls.
        const float content_max_x = start.x + card_width - inset;
        window->ContentRegionRect.Max.x =
            std::min(window->ContentRegionRect.Max.x, content_max_x);
        window->WorkRect.Max.x = std::min(window->WorkRect.Max.x, content_max_x);

        // Keep custom and native controls inside the card without introducing
        // another child window/panel.
        dl->PushClipRect(
            ImVec2(start.x + 1.0f, start.y + 1.0f),
            ImVec2(start.x + card_width - 1.0f, FLT_MAX),
            true);
    }

    void EndCard()
    {
        if (g_cards.empty())
            return;

        const CardState card = g_cards.back();
        g_cards.pop_back();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PopClipRect();

        // Product-wide minimum card height keeps neighbouring cards visually
        // aligned without changing size on hover or while async state changes.
        const float minimumEndY = card.start.y + CyberTheme::Metrics::CardMinHeight - CyberTheme::Px(10.0f);
        const float currentY = ImGui::GetCursorScreenPos().y;
        if (currentY < minimumEndY)
            ImGui::Dummy(ImVec2(0.0f, minimumEndY - currentY));
        ImGui::Dummy(ImVec2(0.0f, CyberTheme::Spacing::Sm));
        ImGui::PopItemWidth();
        ImGui::Unindent(CyberTheme::Metrics::CardPadding);
        ImGui::EndGroup();

        card.window->ContentRegionRect.Max.x = card.content_region_max_x;
        card.window->WorkRect.Max.x = card.work_rect_max_x;

        const ImVec2 end_cursor = ImGui::GetCursorScreenPos();
        const ImVec2 a = card.start;
        const ImVec2 b(card.start.x + card.width, end_cursor.y - 2.0f);
        constexpr float cut = 9.f;
        const ImVec2 shape[] = {
            a, ImVec2(b.x - cut, a.y), ImVec2(b.x, a.y + cut),
            b, ImVec2(a.x + cut, b.y), ImVec2(a.x, b.y - cut)
        };

        dl->ChannelsSetCurrent(0);

        // Soft elevation instead of a second framed window.
        dl->AddRectFilled(
            ImVec2(a.x + 1.0f, a.y + 3.0f),
            ImVec2(b.x + 1.0f, b.y + 4.0f),
            CyberTheme::SafeShadowU32(24), CyberTheme::Metrics::CardRounding + 1.0f);
        dl->AddRectFilled(
            ImVec2(a.x + 2.0f, a.y + 2.0f),
            ImVec2(b.x + 2.0f, b.y + 3.0f),
            CyberTheme::SafeShadowU32(13), CyberTheme::Metrics::CardRounding + 1.0f);

        dl->AddConvexPolyFilled(shape, 6, CyberTheme::U32(CyberTheme::Colors.Panel));
        dl->AddPolyline(shape, 6, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.48f),
            true, 1.0f);
        dl->AddLine(ImVec2(a.x + 12.f, a.y + 1.f), ImVec2(b.x - cut - 5.f, a.y + 1.f),
            CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.09f), 1.f);
        dl->AddLine(ImVec2(a.x + cut + 4.f, b.y - 2.f), ImVec2(b.x - 12.f, b.y - 2.f),
            CyberTheme::SafeShadowU32(120), 1.f);

        // Clean card title — gold accent bar, no template numbering
        {
            dl->AddRectFilledMultiColor(
                ImVec2(a.x + 12.0f, a.y + 12.0f),
                ImVec2(a.x + 15.0f, a.y + 26.0f),
                IM_COL32(116, 86, 18, 255), IM_COL32(255, 226, 138, 255),
                IM_COL32(212, 175, 55, 255), IM_COL32(116, 86, 18, 255));

            ImFont* body = CyberFonts::GetBodyFont();
            const float titleSize = CyberTheme::Px(15.0f);
            if (body)
                dl->AddText(body, titleSize, ImVec2(a.x + 22.0f, a.y + 10.0f),
                    CyberTheme::U32(CyberTheme::Colors.Text), card.title);
            else
                dl->AddText(ImVec2(a.x + 22.0f, a.y + 11.0f),
                    CyberTheme::U32(CyberTheme::Colors.Text), card.title);

            // Soft hover outline only
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y) {
                dl->AddPolyline(shape, 6,
                    CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.20f),
                    true, 1.0f);
            }
        }
        dl->ChannelsMerge();

        ImGui::PopID();

        if (!card.inside_row)
            ImGui::Dummy(ImVec2(0.0f, 7.0f));
    }

    void CardGap(float y)
    {
        ImGui::Dummy(ImVec2(0.0f, y));
    }

    void BeginCardRow(int columns)
    {
        ++g_card_row_depth;
        g_card_row_columns = columns < 1 ? 1 : columns;
        const float gaps = CyberTheme::Metrics::GridGap * static_cast<float>(g_card_row_columns - 1);
        g_card_row_cell = std::max(110.0f,
            (ImGui::GetContentRegionAvail().x - gaps) / static_cast<float>(g_card_row_columns));
        ImGui::BeginGroup();
    }

    void NextCardColumn()
    {
        ImGui::SameLine(0.0f, CyberTheme::Metrics::GridGap);
    }

    void EndCardRow()
    {
        ImGui::EndGroup();
        if (g_card_row_depth > 0)
            --g_card_row_depth;
        g_card_row_columns = 2;
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    float CardRowHalfWidth()
    {
        return g_card_row_cell;
    }

    float CardContentWidth()
    {
        return g_cards.empty()
            ? ImGui::GetContentRegionAvail().x
            : g_cards.back().content_width;
    }

} // namespace CyberWidgets
