#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "animations.h"
#include "../config/app_settings.h"
#include "third_party/ImGui/imgui_internal.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>
#include <unordered_map>
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
            float entry_progress;
        };

        struct CardEntryState {
            int last_frame = -100;
            float progress = 0.0f;
        };

        std::vector<CardState> g_cards;
        int g_card_row_depth = 0;
        int g_card_row_columns = 2;
        float g_card_row_cell = 0.0f;
        std::unordered_map<ImGuiID, CardEntryState> g_card_entries;
    }

    void BeginCard(const char* title, float width)
    {
        ImGui::PushID(title);

        const float available = ImGui::GetContentRegionAvail().x;
        const float card_width = std::max(120.0f, width > 0.0f ? width : available);
        const float inset = CyberTheme::Metrics::CardPadding;
        const float content_width = std::max(80.0f, card_width - inset * 2.0f);
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID entryId = window->GetID("##card_entry");
        CardEntryState& entry = g_card_entries[entryId];
        const int frame = ImGui::GetFrameCount();
        if (frame - entry.last_frame > 2)
            entry.progress = 0.0f;
        entry.last_frame = frame;
        if (!app_settings::MotionEnabled())
            entry.progress = 1.0f;
        else
            entry.progress = std::min(1.0f, entry.progress +
                std::min(ImGui::GetIO().DeltaTime, 0.05f) / 0.19f);
        const float inverse = 1.0f - entry.progress;
        const float easedEntry = 1.0f - inverse * inverse * inverse;
        ImVec2 start = ImGui::GetCursorScreenPos();
        start.y += (1.0f - easedEntry) * CyberTheme::Px(7.0f);
        ImGui::SetCursorScreenPos(start);

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
            window->WorkRect.Max.x,
            easedEntry
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

        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const bool hovered = mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y;
        const ImGuiID hoverId = card.window->GetID("##card_surface_hover");
        CyberAnimations::SetHover(hoverId, hovered, 10.0f);
        const float hover = CyberAnimations::GetHover(hoverId);
        const bool fullMotion = app_settings::MotionEnabled() &&
            app_settings::config.animation_intensity == app_settings::EffectLevel::Full;

        dl->ChannelsSetCurrent(0);

        // Soft elevation instead of a second framed window.
        dl->AddRectFilled(
            ImVec2(a.x + 1.0f, a.y + 3.0f - hover),
            ImVec2(b.x + 1.0f, b.y + 4.0f + hover),
            CyberTheme::SafeShadowU32(static_cast<int>(24 + 14 * hover)), CyberTheme::Metrics::CardRounding + 1.0f);
        dl->AddRectFilled(
            ImVec2(a.x + 2.0f, a.y + 2.0f),
            ImVec2(b.x + 2.0f, b.y + 3.0f),
            CyberTheme::SafeShadowU32(13), CyberTheme::Metrics::CardRounding + 1.0f);

        dl->AddRectFilled(a, b, CyberTheme::U32(CyberTheme::Colors.Panel),
            CyberTheme::Metrics::CardRounding);
        dl->AddRectFilled(a,
            ImVec2(b.x, a.y + CyberTheme::Metrics::CardHeaderHeight),
            CyberTheme::U32(CyberTheme::Mix(CyberTheme::Colors.Panel,
                                            CyberTheme::Colors.CardHover, 0.46f)),
            CyberTheme::Metrics::CardRounding, ImDrawFlags_RoundCornersTop);
        dl->AddRect(a, b, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.62f),
            CyberTheme::Metrics::CardRounding, 0, 1.0f);
        if (hover > 0.01f) {
            dl->AddRect(a, b, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.28f * hover),
                CyberTheme::Metrics::CardRounding, 0, 1.0f + 0.35f * hover);
            dl->AddRectFilledMultiColor(
                ImVec2(a.x + CyberTheme::Px(18.f), a.y + 1.f),
                ImVec2(a.x + CyberTheme::Px(18.f) + card.width * 0.34f * hover, a.y + 2.f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.0f),
                CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.42f * hover),
                CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.42f * hover),
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.0f));
            if (fullMotion) {
                const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.48f, 1.0f);
                const float sheenX = a.x - CyberTheme::Px(52.f) +
                    (card.width + CyberTheme::Px(104.f)) * phase;
                dl->PushClipRect(a, b, true);
                dl->AddRectFilledMultiColor(
                    ImVec2(sheenX - CyberTheme::Px(34.f), a.y),
                    ImVec2(sheenX + CyberTheme::Px(34.f), b.y),
                    IM_COL32(255, 255, 255, 0),
                    IM_COL32(224, 188, 70, static_cast<int>(9.f * hover)),
                    IM_COL32(224, 188, 70, static_cast<int>(9.f * hover)),
                    IM_COL32(255, 255, 255, 0));
                dl->PopClipRect();
            }
        }
        dl->AddLine(
            ImVec2(a.x + CyberTheme::Metrics::CardPadding,
                   a.y + CyberTheme::Metrics::CardHeaderHeight),
            ImVec2(b.x - CyberTheme::Metrics::CardPadding,
                   a.y + CyberTheme::Metrics::CardHeaderHeight),
            CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.44f), 1.0f);

        // Clean card title — gold accent bar, no template numbering
        {
            dl->AddRectFilledMultiColor(
                ImVec2(a.x + 13.0f, a.y + 13.0f),
                ImVec2(a.x + 16.0f, a.y + 29.0f),
                IM_COL32(116, 86, 18, 255), IM_COL32(255, 226, 138, 255),
                IM_COL32(212, 175, 55, 255), IM_COL32(116, 86, 18, 255));

            ImFont* body = CyberFonts::GetBodyFont();
            const float titleSize = CyberTheme::Px(15.0f);
            const char* titleEnd = ImGui::FindRenderedTextEnd(card.title);
            const ImVec2 titleExtent = body
                ? body->CalcTextSizeA(titleSize, FLT_MAX, 0.0f, card.title, titleEnd)
                : ImGui::CalcTextSize(card.title, titleEnd);
            const float titleX = a.x + (card.width - titleExtent.x) * 0.5f;
            if (body)
                dl->AddText(body, titleSize, ImVec2(titleX, a.y + 11.0f),
                    CyberTheme::U32(CyberTheme::Colors.Text), card.title, titleEnd);
            else
                dl->AddText(ImVec2(titleX, a.y + 12.0f),
                    CyberTheme::U32(CyberTheme::Colors.Text), card.title, titleEnd);

        }
        if (card.entry_progress < 0.999f) {
            dl->ChannelsSetCurrent(1);
            dl->AddRectFilled(a, b,
                CyberTheme::WithAlpha(CyberTheme::Colors.Panel,
                          std::clamp(1.0f - card.entry_progress, 0.0f, 1.0f)),
                CyberTheme::Metrics::CardRounding);
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
