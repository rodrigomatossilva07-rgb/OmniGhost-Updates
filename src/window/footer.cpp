#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "localization.h"
#include "globals.h"
#include "../../Cs2/cs2_game.h"
#include "../../Warzone/warzone_game.h"
#include "../config/app_settings.h"
#include "game/offsets.h"
#include "../platform/session_log.h"
#include "hardware_monitor.h"

#include <cmath>
#include <cfloat>
#include <cstdio>
#include <string>

namespace CyberWidgets {

    namespace {
        float DrawFooterItem(ImDrawList* dl, ImFont* font, float x, float y,
                             const char* label, ImU32 color, float size)
        {
            if (font)
                dl->AddText(font, size, ImVec2(x, y), color, label);
            else
                dl->AddText(ImVec2(x, y), color, label);

            const ImVec2 text_size = font
                ? font->CalcTextSizeA(size, FLT_MAX, 0.0f, label)
                : ImGui::CalcTextSize(label);
            return x + text_size.x;
        }

        float DrawDivider(ImDrawList* dl, float x, float y)
        {
            dl->AddCircleFilled(ImVec2(x + 9.0f, y + 7.0f), 1.5f,
                CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.60f), 8);
            return x + 20.0f;
        }
    }

    void DrawFooter(const ImVec2& wp, const ImVec2& ws, float fps, bool dma_ok,
                    const char* build, int ping_ms, int players)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* body = CyberFonts::GetBodyFont();
        const float h = CyberTheme::Metrics::FooterHeight;
        const ImVec2 a(wp.x, wp.y + ws.y - h);
        const ImVec2 b(wp.x + ws.x, wp.y + ws.y);

        const ImVec4 footer_bg = CyberTheme::Colors.Background;
        dl->AddRectFilled(a, b,
            CyberTheme::U32(footer_bg),
            CyberTheme::Metrics::WindowRounding,
            ImDrawFlags_RoundCornersBottom);
        dl->AddLine(a, ImVec2(b.x, a.y),
            CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.42f), 1.0f);

        constexpr float kFont = 14.0f;
        const float text_y = a.y + (h - kFont) * 0.5f;
        float x = a.x + 16.0f;

        const float pulse = 0.55f + 0.45f * std::sin(static_cast<float>(ImGui::GetTime()) * 2.0f);
        const int alpha = dma_ok ? static_cast<int>(100 + 140 * pulse) : 220;
        dl->AddCircleFilled(ImVec2(x + 4.0f, text_y + 7.5f), 3.8f,
            dma_ok ? CyberTheme::WithAlpha(CyberTheme::Colors.Success, alpha / 255.0f)
                   : CyberTheme::U32(CyberTheme::Colors.Error), 12);
        x += 16.0f;
        x = DrawFooterItem(dl, body, x, text_y, "DMA",
            CyberTheme::U32(CyberTheme::Colors.Gold), kFont);
        x = DrawDivider(dl, x, text_y);
        const char* connection_text = (g_activeGame == ActiveGame::Warzone)
            ? (Warzone::runtime.module_base ? "Processo ligado" : "A ligar...")
            : (dma_ok ? Loc::Tr("footer.connected") : Loc::Tr("footer.offline"));
        x = DrawFooterItem(dl, body, x, text_y,
            connection_text,
            CyberTheme::U32(CyberTheme::Colors.Text), kFont);
        x = DrawDivider(dl, x, text_y);

        char value[192];
        if (g_activeGame == ActiveGame::CS2) {
            snprintf(value, sizeof(value), "%s", CS2::StatusLine());
        } else if (g_activeGame == ActiveGame::Warzone) {
            snprintf(value, sizeof(value), "%s", Warzone::StatusLine());
        } else {
            // FiveM: game build
            snprintf(value, sizeof(value), Loc::Tr("footer.build"), build ? build : "--");
        }
        x = DrawFooterItem(dl, body, x, text_y, value,
            CyberTheme::U32(CyberTheme::Colors.TextDisabled), kFont);
        x = DrawDivider(dl, x, text_y);
        {
            bool verified = false;
#ifdef UI_PREVIEW
            verified = true;
#else
            if (g_activeGame == ActiveGame::CS2) {
                const auto snapshot = CS2::AcquireRuntimeSnapshot();
                verified = (snapshot && snapshot->offsets_self_test_ok) || CS2::ready;
            }
            else if (g_activeGame == ActiveGame::Warzone)
                verified = Warzone::runtime.matrix_ok;
            else
                verified = FiveM::IsBuildSupported();
#endif
            const char* offsets_text =
                (g_activeGame == ActiveGame::Warzone && Warzone::runtime.matrix_ok) ? "Matrix OK" :
                (g_activeGame == ActiveGame::Warzone && Warzone::offsets.loaded) ? "Offsets carregados" :
                (g_activeGame == ActiveGame::Warzone) ? "Offsets ?" :
                (verified ? "Offsets OK" : "Offsets ?");
            x = DrawFooterItem(dl, body, x, text_y,
                offsets_text,
                verified ? CyberTheme::U32(CyberTheme::Colors.Success) : CyberTheme::U32(CyberTheme::Colors.Warning), kFont);
            x = DrawDivider(dl, x, text_y);
        }
        // Support links and exact version strings belong in Definições > Sobre.

        char fps_value[24];
        snprintf(fps_value, sizeof(fps_value), Loc::Tr("footer.fps"), fps);
        
        // Get DMA latency from HardwareMonitor
        const auto& metrics = HardwareMonitor::GetSystemMetrics();
        char dma_latency_value[32];
        if (metrics.dma_read_latency_ms > 0.0f || metrics.dma_write_latency_ms > 0.0f) {
            snprintf(dma_latency_value, sizeof(dma_latency_value), "DMA R:%.1fms W:%.1fms", 
                metrics.dma_read_latency_ms, metrics.dma_write_latency_ms);
        } else {
            snprintf(dma_latency_value, sizeof(dma_latency_value), "-- ms");
        }
        
        char ping_value[24];
        if (ping_ms >= 0)
            snprintf(ping_value, sizeof(ping_value), Loc::Tr("footer.ping"), ping_ms);
        else
            snprintf(ping_value, sizeof(ping_value), "-- ms");
        char players_value[32];
        if (players >= 0)
            snprintf(players_value, sizeof(players_value), Loc::Tr("footer.players"), players);
        else
            players_value[0] = '\0';

        const std::string lastError = OmniGhost::SessionLog::LastErrorId();
        if (!lastError.empty() && ws.x > CyberTheme::Px(900.0f)) {
            const std::string errorLabel = "Last error: " + lastError;
            DrawFooterItem(dl, body, b.x - CyberTheme::Px(360.0f), text_y, errorLabel.c_str(),
                CyberTheme::U32(CyberTheme::Colors.Error), kFont);
        }

        float right = b.x - 16.0f;
        if (players >= 0 && players_value[0]) {
            const ImVec2 size = body
                ? body->CalcTextSizeA(kFont, FLT_MAX, 0.0f, players_value)
                : ImGui::CalcTextSize(players_value);
            right -= size.x;
            DrawFooterItem(dl, body, right, text_y, players_value,
                CyberTheme::U32(CyberTheme::Colors.TextDisabled), kFont);
            right -= 20.0f;
        }

        const ImVec2 dma_latency_size = body
            ? body->CalcTextSizeA(kFont, FLT_MAX, 0.0f, dma_latency_value)
            : ImGui::CalcTextSize(dma_latency_value);
        right -= dma_latency_size.x;
        DrawFooterItem(dl, body, right, text_y, dma_latency_value,
            CyberTheme::U32(CyberTheme::Colors.Gold), kFont);
        right -= 20.0f;

        const ImVec2 ping_size = body
            ? body->CalcTextSizeA(kFont, FLT_MAX, 0.0f, ping_value)
            : ImGui::CalcTextSize(ping_value);
        right -= ping_size.x;
        DrawFooterItem(dl, body, right, text_y, ping_value,
            CyberTheme::U32(CyberTheme::Colors.Text), kFont);
        right -= 20.0f;

        if (app_settings::config.show_fps) {
            const ImVec2 fps_size = body
                ? body->CalcTextSizeA(kFont, FLT_MAX, 0.0f, fps_value)
                : ImGui::CalcTextSize(fps_value);
            right -= fps_size.x;
            DrawFooterItem(dl, body, right, text_y, fps_value,
                CyberTheme::U32(CyberTheme::Colors.Gold), kFont);
        }
    }

} // namespace CyberWidgets
