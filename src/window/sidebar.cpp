#include "widgets.h"
#include "theme.h"
#include "icons.h"
#include "fonts.h"
#include "menu_tab.h"
#include "localization.h"
#include "globals.h"
#include "../config/app_settings.h"
#include "../launcher/launcher_assets.h"
#include "brand_assets.h"
#include "../auth/local_auth_service.h"
#include "../licensing/license_service.h"
#include <algorithm>
#include <string>

namespace {

std::string ProfileName()
{
    const auto license = OmniGhost::Licensing::GetSnapshot();
    std::string name = license.remoteUsername;
    if (name.empty())
        name = OmniGhost::Auth::LocalAuthService::Instance().CurrentEmail();
    if (const std::size_t at = name.find('@'); at != std::string::npos)
        name.resize(at);
    if (name.empty()) name = "OmniGhost User";
    if (name.size() > 22) name.resize(22);
    return name;
}

void DrawProfileImage(ImDrawList* dl, ImVec2 min, ImVec2 max)
{
    LauncherAssets::Texture texture = LauncherAssets::ProfileAvatar();
    ImTextureID image = texture.id ? texture.id : BrandAssets::GetLogoTexture();
    ImVec2 uv0(0.f, 0.f), uv1(1.f, 1.f);
    if (texture.id && texture.width > 0 && texture.height > 0) {
        if (texture.width > texture.height) {
            const float inset = (1.f - static_cast<float>(texture.height) / texture.width) * .5f;
            uv0.x = inset; uv1.x = 1.f - inset;
        } else if (texture.height > texture.width) {
            const float inset = (1.f - static_cast<float>(texture.width) / texture.height) * .5f;
            uv0.y = inset; uv1.y = 1.f - inset;
        }
    }
    if (image)
        dl->AddImageRounded(image, min, max, uv0, uv1, IM_COL32_WHITE,
            (max.x - min.x) * .5f);
    else
        dl->AddCircleFilled(ImVec2((min.x + max.x) * .5f, (min.y + max.y) * .5f),
            (max.x - min.x) * .5f, CyberTheme::U32(CyberTheme::Colors.Gold), 32);
}

} // namespace

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
            { "nav.radar",   MenuTab::TAB_RADAR,      CyberIcons::DrawRadarIcon },
            { "nav.object_esp", MenuTab::TAB_FIVEM_OBJECT_ESP, CyberIcons::DrawESPIcon },
            { "nav.friends", MenuTab::TAB_FRIENDS,    CyberIcons::DrawUserIcon },
            { "nav.status",  MenuTab::TAB_STATUS,     CyberIcons::DrawStatusIcon },
            { "nav.save",    MenuTab::TAB_SAVECONFIG, CyberIcons::DrawSaveIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS,    CyberIcons::DrawSettingsIcon },
        };
        static const TabItem cs2Tabs[] = {
            { "nav.visuals", MenuTab::TAB_CS2_VISUALS, CyberIcons::DrawESPIcon },
            { "nav.aim",     MenuTab::TAB_CS2_AIM,     CyberIcons::DrawAimIcon },
            { "nav.misc",    MenuTab::TAB_CS2_MISC,    CyberIcons::DrawWrenchIcon },
            { "nav.radar",   MenuTab::TAB_CS2_RADAR,   CyberIcons::DrawRadarIcon },
            { "nav.configs", MenuTab::TAB_CONFIGS,     CyberIcons::DrawSettingsIcon },
            { "nav.save",    MenuTab::TAB_SAVECONFIG,  CyberIcons::DrawSaveIcon },
        };
        static const TabItem warzoneTabs[] = {
            { "nav.aim", MenuTab::TAB_WARZONE_AIM,      CyberIcons::DrawAimIcon },
            { "nav.player_esp", MenuTab::TAB_WARZONE_VISUALS,  CyberIcons::DrawESPIcon },
            { "nav.world_esp", MenuTab::TAB_WARZONE_WORLD,    CyberIcons::DrawWorldIcon },
            { "nav.radar", MenuTab::TAB_WARZONE_RADAR,    CyberIcons::DrawRadarIcon },
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
        static const TabItem rustTabs[] = {
            { "nav.visuals", MenuTab::TAB_RUST_VISUALS, CyberIcons::DrawESPIcon },
            { "nav.aim",     MenuTab::TAB_RUST_AIM,     CyberIcons::DrawAimIcon },
            { "nav.status",  MenuTab::TAB_RUST_SYSTEM,  CyberIcons::DrawStatusIcon },
        };
        if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::CS2) {
            tabs = cs2Tabs;
            tabCount = (int)(sizeof(cs2Tabs) / sizeof(cs2Tabs[0]));
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Warzone) {
            tabs = warzoneTabs;
            tabCount = (int)(sizeof(warzoneTabs) / sizeof(warzoneTabs[0]));
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Valorant) {
            tabs = valorantTabs;
            tabCount = (int)(sizeof(valorantTabs) / sizeof(valorantTabs[0]));
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Rust) {
            tabs = rustTabs;
            tabCount = (int)(sizeof(rustTabs) / sizeof(rustTabs[0]));
        } else if (OmniGhost::GameContext::Instance().GetActiveGame() == OmniGhost::ActiveGame::Fortnite) {
            tabs = fortniteTabs;
            tabCount = (int)(sizeof(fortniteTabs) / sizeof(fortniteTabs[0]));
        } else {
            tabs = fivemTabs;
            tabCount = (int)(sizeof(fivemTabs) / sizeof(fivemTabs[0]));
        }
        
        // Fit every game's complete navigation in the same shared sidebar.
        const float topPad = CyberTheme::Px(34.0f);
        const float profileHeight = CyberTheme::Px(72.0f);
        const float usableHeight = (std::max)(CyberTheme::Px(300.0f),
            size.y - topPad - profileHeight - CyberTheme::Spacing::Lg);
        float gapY = CyberTheme::Px(6.0f);
        float itemH = CyberTheme::Metrics::SidebarItemHeight;
        float blockHeight = tabCount * itemH +
            (tabCount > 0 ? (tabCount - 1) * gapY : 0.0f);
        if (blockHeight > usableHeight && tabCount > 0) {
            gapY = CyberTheme::Px(3.0f);
            itemH = (std::max)(CyberTheme::Px(34.0f),
                (usableHeight - (tabCount - 1) * gapY) / tabCount);
            blockHeight = tabCount * itemH + (tabCount - 1) * gapY;
        }
        // Vertically centre the navigation stack inside the sidebar.
        const float startY = pos.y + topPad +
            (std::max)(0.0f, (usableHeight - blockHeight) * 0.5f);

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

        const float profileY = pos.y + size.y - profileHeight;
        const ImVec2 profileMin(pos.x + CyberTheme::Px(8.f), profileY);
        const ImVec2 profileMax(pos.x + size.x - CyberTheme::Px(8.f), pos.y + size.y - CyberTheme::Px(7.f));
        dl->AddRectFilled(profileMin, profileMax,
            CyberTheme::WithAlpha(CyberTheme::Colors.Panel, .72f), CyberTheme::Px(9.f));
        dl->AddRect(profileMin, profileMax,
            CyberTheme::WithAlpha(CyberTheme::Colors.Border, .48f), CyberTheme::Px(9.f));
        const ImVec2 avatarMin(profileMin.x + CyberTheme::Px(8.f), profileMin.y + CyberTheme::Px(8.f));
        const ImVec2 avatarMax(avatarMin.x + CyberTheme::Px(46.f), avatarMin.y + CyberTheme::Px(46.f));
        DrawProfileImage(dl, avatarMin, avatarMax);
        dl->AddCircle(ImVec2((avatarMin.x + avatarMax.x) * .5f, (avatarMin.y + avatarMax.y) * .5f),
            CyberTheme::Px(23.f), CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .55f), 32, 1.f);
        const std::string profileName = ProfileName();
        const float textX = avatarMax.x + CyberTheme::Px(9.f);
        dl->AddText(ImVec2(textX, profileMin.y + CyberTheme::Px(13.f)),
            CyberTheme::U32(CyberTheme::Colors.Text), profileName.c_str());
        dl->AddText(ImVec2(textX, profileMin.y + CyberTheme::Px(34.f)),
            CyberTheme::U32(CyberTheme::Colors.TextDisabled), Loc::Tr("profile.label"));
    }

} // namespace CyberWidgets
