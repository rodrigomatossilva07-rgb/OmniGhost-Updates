#include "widgets.h"
#include "theme.h"
#include "icons.h"
#include "fonts.h"
#include "menu_tab.h"
#include "localization.h"
#include "globals.h"
#include "../config/app_settings.h"
#include <algorithm>

namespace CyberWidgets {

    void DrawSidebar(const ImVec2& wp, const ImVec2& ws, MenuTab* current_tab)
    {
        if (!current_tab)
            return;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 pos(wp.x, wp.y + CyberTheme::Metrics::HeaderHeight);
        const ImVec2 size(
            CyberTheme::Metrics::SidebarWidth,
            ws.y - CyberTheme::Metrics::HeaderHeight - CyberTheme::Metrics::FooterHeight);

        dl->AddRectFilledMultiColor(
            pos, ImVec2(pos.x + size.x, pos.y + size.y),
            CyberTheme::WithAlpha(CyberTheme::Colors.Panel, 0.22f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Background, 0.0f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Background, 0.0f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Panel, 0.12f));

        ImFont* body = CyberFonts::GetBodyFont();
        if (body) {
            dl->AddText(body, CyberTheme::Typography::Caption,
                ImVec2(pos.x + CyberTheme::Spacing::Lg, pos.y + CyberTheme::Spacing::Md),
                CyberTheme::U32(CyberTheme::Colors.TextDisabled), Loc::Tr("nav.title"));
        }

        struct TabItem {
            const char* label;
            MenuTab tab;
            void(*icon)(ImDrawList*, ImVec2, float, ImU32);
        };

        const TabItem* tabs = nullptr;
        int tabCount = 0;
        static const TabItem fivemTabs[] = {
            { "nav.visuals", MenuTab::TAB_VISUALS,    CyberIcons::DrawESPIcon },
            { "nav.aim",     MenuTab::TAB_AIM,        CyberIcons::DrawAimIcon },
            { "nav.vehicles", MenuTab::TAB_VEHICLES,   CyberIcons::DrawVehicleIcon },
            { "nav.friends", MenuTab::TAB_FRIENDS,    CyberIcons::DrawUserIcon },
            { "nav.status",  MenuTab::TAB_STATUS,     CyberIcons::DrawStatusIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS,    CyberIcons::DrawSettingsIcon },
            { "nav.save",    MenuTab::TAB_SAVECONFIG, CyberIcons::DrawSaveIcon },
            { "nav.webradar", MenuTab::TAB_FIVEM_WEB_RADAR, CyberIcons::DrawRadarIcon },
            { "nav.object_esp", MenuTab::TAB_FIVEM_OBJECT_ESP, CyberIcons::DrawESPIcon },
        };
        static const TabItem unifiedSystemTabs[] = {
            { "nav.unified_aim", MenuTab::TAB_UNIFIED_AIM,     CyberIcons::DrawAimIcon },
            { "nav.webradar", MenuTab::TAB_WEB_RADAR,   CyberIcons::DrawRadarIcon },
            { "nav.sound_esp", MenuTab::TAB_SOUND_ESP,   CyberIcons::DrawVehicleIcon },
            { "nav.spectator_list", MenuTab::TAB_SPECTATOR_LIST, CyberIcons::DrawUserIcon },
            { "nav.triggerbot", MenuTab::TAB_TRIGGERBOT, CyberIcons::DrawAimIcon },
            { "nav.recoil_control", MenuTab::TAB_RECOIL_CONTROL, CyberIcons::DrawWrenchIcon },
            { "nav.prediction", MenuTab::TAB_PREDICTION, CyberIcons::DrawStatusIcon },
            { "nav.visibility", MenuTab::TAB_VISIBILITY, CyberIcons::DrawESPIcon },
            { "nav.bone_system", MenuTab::TAB_BONE_SYSTEM, CyberIcons::DrawESPIcon },
            { "nav.smooth_curves", MenuTab::TAB_SMOOTH_CURVES, CyberIcons::DrawSettingsIcon },
            { "nav.recoil_patterns", MenuTab::TAB_RECOIL_PATTERNS, CyberIcons::DrawWrenchIcon },
            { "nav.entity_cache", MenuTab::TAB_ENTITY_CACHE, CyberIcons::DrawStatusIcon },
            { "nav.profile_manager", MenuTab::TAB_PROFILE_MANAGER, CyberIcons::DrawSettingsIcon },
            { "nav.offset_manager", MenuTab::TAB_OFFSET_MANAGER, CyberIcons::DrawSettingsIcon },
            { "nav.resolution", MenuTab::TAB_RESOLUTION, CyberIcons::DrawRadarIcon },
            { "nav.game_adapter", MenuTab::TAB_GAME_ADAPTER, CyberIcons::DrawWrenchIcon },
        };
        static const TabItem cs2Tabs[] = {
            { "nav.visuals", MenuTab::TAB_CS2_VISUALS, CyberIcons::DrawESPIcon },
            { "nav.aim", MenuTab::TAB_CS2_AIM,     CyberIcons::DrawAimIcon },
            { "nav.status", MenuTab::TAB_CS2_MISC,    CyberIcons::DrawStatusIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS,     CyberIcons::DrawSettingsIcon },
            { "nav.save", MenuTab::TAB_SAVECONFIG,  CyberIcons::DrawSaveIcon },
        };
        static const TabItem rustTabs[] = {
            { "nav.aim", MenuTab::TAB_RUST_AIM,     CyberIcons::DrawAimIcon },
            { "nav.visuals", MenuTab::TAB_RUST_VISUALS, CyberIcons::DrawESPIcon },
            { "nav.world", MenuTab::TAB_RUST_WORLD,   CyberIcons::DrawWorldIcon },
            { "nav.players", MenuTab::TAB_RUST_PLAYERS, CyberIcons::DrawUserIcon },
            { "nav.radar", MenuTab::TAB_RUST_RADAR,   CyberIcons::DrawRadarIcon },
            { "nav.misc", MenuTab::TAB_RUST_MISC,    CyberIcons::DrawWrenchIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS,      CyberIcons::DrawSettingsIcon },
            { "nav.save", MenuTab::TAB_SAVECONFIG,   CyberIcons::DrawSaveIcon },
        };
        static const TabItem rustAdvancedTabs[] = {
            { "nav.aim", MenuTab::TAB_RUST_AIM,     CyberIcons::DrawAimIcon },
            { "nav.visuals", MenuTab::TAB_RUST_VISUALS, CyberIcons::DrawESPIcon },
            { "nav.world", MenuTab::TAB_RUST_WORLD,   CyberIcons::DrawWorldIcon },
            { "nav.players", MenuTab::TAB_RUST_PLAYERS, CyberIcons::DrawUserIcon },
            { "nav.radar", MenuTab::TAB_RUST_RADAR,   CyberIcons::DrawRadarIcon },
            { "nav.misc", MenuTab::TAB_RUST_MISC,    CyberIcons::DrawWrenchIcon },
            { "nav.debug", MenuTab::TAB_RUST_DEBUG,   CyberIcons::DrawStatusIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS,      CyberIcons::DrawSettingsIcon },
            { "nav.save", MenuTab::TAB_SAVECONFIG,   CyberIcons::DrawSaveIcon },
        };
        static const TabItem warzoneTabs[] = {
            { "nav.aim", MenuTab::TAB_WARZONE_AIM,      CyberIcons::DrawAimIcon },
            { "nav.player_esp", MenuTab::TAB_WARZONE_VISUALS,  CyberIcons::DrawESPIcon },
            { "nav.radar", MenuTab::TAB_WARZONE_RADAR,    CyberIcons::DrawRadarIcon },
            { "nav.world_esp", MenuTab::TAB_WARZONE_WORLD,    CyberIcons::DrawWorldIcon },
            { "nav.players", MenuTab::TAB_WARZONE_PLAYERS,  CyberIcons::DrawUserIcon },
            { "nav.misc", MenuTab::TAB_WARZONE_MISC,     CyberIcons::DrawWrenchIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS,          CyberIcons::DrawSettingsIcon },
            { "nav.save", MenuTab::TAB_SAVECONFIG,       CyberIcons::DrawSaveIcon },
        };
        static const TabItem valorantTabs[] = {
            { "nav.visuals", MenuTab::TAB_VALORANT_VISUALS, CyberIcons::DrawESPIcon },
            { "nav.aim", MenuTab::TAB_VALORANT_AIM,     CyberIcons::DrawAimIcon },
            { "nav.status", MenuTab::TAB_VALORANT_STATUS,  CyberIcons::DrawStatusIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS, CyberIcons::DrawSettingsIcon },
            { "nav.save", MenuTab::TAB_SAVECONFIG, CyberIcons::DrawSaveIcon },
        };
        static const TabItem fortniteTabs[] = {
            { "nav.visuals", MenuTab::TAB_FORTNITE_VISUALS, CyberIcons::DrawESPIcon },
            { "nav.aim", MenuTab::TAB_FORTNITE_AIM,     CyberIcons::DrawAimIcon },
            { "nav.status", MenuTab::TAB_FORTNITE_STATUS,  CyberIcons::DrawStatusIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS, CyberIcons::DrawSettingsIcon },
            { "nav.save", MenuTab::TAB_SAVECONFIG, CyberIcons::DrawSaveIcon },
        };
        if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::CS2) {
            tabs = cs2Tabs;
            tabCount = (int)(sizeof(cs2Tabs) / sizeof(cs2Tabs[0]));
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Rust) {
            const bool developer = app_settings::config.show_advanced
#ifdef _DEBUG
                || true
#endif
#ifdef OMNIGHOST_DIAGNOSTICS
                || true
#endif
                ;
            if (developer) {
                tabs = rustAdvancedTabs;
                tabCount = (int)(sizeof(rustAdvancedTabs) / sizeof(rustAdvancedTabs[0]));
            } else {
                tabs = rustTabs;
                tabCount = (int)(sizeof(rustTabs) / sizeof(rustTabs[0]));
                if (*current_tab == MenuTab::TAB_RUST_DEBUG)
                    *current_tab = MenuTab::TAB_RUST_MISC;
            }
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Warzone) {
            tabs = warzoneTabs;
            tabCount = (int)(sizeof(warzoneTabs) / sizeof(warzoneTabs[0]));
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Valorant) {
            tabs = valorantTabs;
            tabCount = (int)(sizeof(valorantTabs) / sizeof(valorantTabs[0]));
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Fortnite) {
            tabs = fortniteTabs;
            tabCount = (int)(sizeof(fortniteTabs) / sizeof(fortniteTabs[0]));
        } else {
            tabs = fivemTabs;
            tabCount = (int)(sizeof(fivemTabs) / sizeof(fivemTabs[0]));
        }
        
        // Append unified system tabs
        int unifiedCount = (int)(sizeof(unifiedSystemTabs) / sizeof(unifiedSystemTabs[0]));
        int totalTabs = tabCount + unifiedCount;
        
        // Create combined tabs array
        static TabItem combinedTabs[64];
        for (int i = 0; i < tabCount; ++i) combinedTabs[i] = tabs[i];
        for (int i = 0; i < unifiedCount; ++i) combinedTabs[tabCount + i] = unifiedSystemTabs[i];
        
        tabs = combinedTabs;
        tabCount = totalTabs;

        // Fit every game's complete navigation in the same shared sidebar.
        const float topPad = CyberTheme::Px(28.0f);
        const float usableHeight = (std::max)(CyberTheme::Px(360.0f),
            size.y - topPad - CyberTheme::Spacing::Lg);
        float gapY = CyberTheme::Px(6.0f);
        float itemH = CyberTheme::Metrics::SidebarItemHeight;
        const float desiredHeight = tabCount * itemH +
            (tabCount > 0 ? (tabCount - 1) * gapY : 0.0f);
        if (desiredHeight > usableHeight && tabCount > 0) {
            gapY = CyberTheme::Px(3.0f);
            itemH = (std::max)(CyberTheme::Px(34.0f),
                (usableHeight - (tabCount - 1) * gapY) / tabCount);
        }
        const float stackH = tabCount * itemH + (tabCount > 0 ? (tabCount - 1) * gapY : 0.0f);
        float startY = pos.y + topPad + std::max(0.0f,
            (size.y - topPad - CyberTheme::Spacing::Lg - stackH) * 0.35f);

        dl->PushClipRect(
            ImVec2(pos.x, pos.y + CyberTheme::Spacing::Sm),
            ImVec2(pos.x + size.x, pos.y + size.y),
            true);
        ImGui::PushID("##sidebar_navigation");
        ImGui::SetCursorScreenPos(ImVec2(pos.x + CyberTheme::Px(6.0f), startY));
        ImGui::BeginGroup();
        for (int i = 0; i < tabCount; ++i) {
            if (SidebarButton(Loc::Tr(tabs[i].label), *current_tab == tabs[i].tab,
                              tabs[i].icon, itemH))
                *current_tab = tabs[i].tab;
            ImGui::Dummy(ImVec2(0.0f, gapY));
        }
        ImGui::EndGroup();
        ImGui::PopID();
        dl->PopClipRect();
    }

} // namespace CyberWidgets
