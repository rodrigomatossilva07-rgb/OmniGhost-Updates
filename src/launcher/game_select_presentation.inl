// This implementation fragment is included by game_select.cpp inside Launcher's private namespace.
// It is separated by responsibility to keep the launcher coordinator reviewable.

void DrawCornerBracket(
    ImDrawList* draw,
    const ImVec2& corner,
    float horizontal_direction,
    float vertical_direction,
    ImU32 color) {
    constexpr float length = 18.0f;
    draw->AddLine(
        corner,
        ImVec2(corner.x + horizontal_direction * length, corner.y),
        color,
        1.2f);
    draw->AddLine(
        corner,
        ImVec2(corner.x, corner.y + vertical_direction * length),
        color,
        1.2f);
}

void DrawCircuitTrace(
    ImDrawList* draw,
    const ImVec2& start,
    const ImVec2& end,
    float bend_x,
    ImU32 line_color,
    ImU32 node_color,
    bool animate) {
    const ImVec2 points[] = {
        start,
        ImVec2(bend_x, start.y),
        ImVec2(bend_x, end.y),
        end
    };

    draw->AddPolyline(points, 4, line_color, false, 1.0f);
    draw->AddCircleFilled(start, 2.0f, node_color, 10);
    draw->AddCircleFilled(end, 2.0f, node_color, 10);

    if (animate) {
        const float phase = std::fmod(g_time * 0.18f + start.x * 0.0013f, 1.0f);
        const float segment = phase * 3.0f;
        ImVec2 pulse{};

        if (segment < 1.0f) {
            pulse = ImVec2(
                start.x + (bend_x - start.x) * segment,
                start.y);
        } else if (segment < 2.0f) {
            pulse = ImVec2(
                bend_x,
                start.y + (end.y - start.y) * (segment - 1.0f));
        } else {
            pulse = ImVec2(
                bend_x + (end.x - bend_x) * (segment - 2.0f),
                end.y);
        }

        draw->AddCircleFilled(pulse, 2.7f, node_color, 12);
        draw->AddCircle(pulse, 5.5f, WithAlpha(node_color, 45), 14, 1.0f);
    }
}

void DrawBackground(
    ImDrawList* draw,
    ImVec2 display,
    bool performance_mode) {
    if (!draw || display.x <= 0.0f || display.y <= 0.0f)
        return;

    const float header = S(58.f);
    const float footer = S(36.f);
    draw->AddRectFilled(ImVec2(0.0f, 0.0f), display, C_BG());
    draw->AddRectFilledMultiColor(ImVec2(0.f, header), ImVec2(display.x, display.y - footer),
        C_PANEL(252), C_CARD(238), C_BG(252), C_PANEL(244));

    // A static, almost imperceptible texture provides depth without another glow layer.
    CyberTheme::DrawSubtleNoise(draw, ImVec2(0.f, header),
        ImVec2(display.x, display.y - footer), performance_mode ? 0.010f : 0.018f,
        0x4F474C41u, performance_mode ? 150 : 330);

    const float rainDensity = app_settings::DigitalRainDensity();
    // Rain is a peripheral brand texture, never the foreground.  Keeping it
    // inside this narrow range also makes the visual stable across monitors.
    const float rainOpacity = std::clamp(app_settings::DigitalRainOpacity(), 0.02f, 0.04f);
    DigitalRain::Draw(
        draw,
        ImVec2(0.0f, header),
        ImVec2(display.x, display.y - header - footer),
        rainDensity > 0.0f,
        performance_mode,
        0.0f,
        rainOpacity,
        rainDensity,
        app_settings::AnimationScale(),
        false);

    // Quiet technical grid: hierarchy comes from spacing and surfaces, not glow.
    const float grid = S(performance_mode ? 118.0f : 94.0f);
    const ImU32 minor_grid = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, performance_mode ? 0.004f : 0.008f);
    const ImU32 major_grid = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, performance_mode ? 0.008f : 0.014f);
    int column = 0;
    for (float x = 0.0f; x < display.x; x += grid, ++column) {
        draw->AddLine(ImVec2(x, header), ImVec2(x, display.y - footer),
            column % 4 == 0 ? major_grid : minor_grid, 1.0f);
    }
    int row = 0;
    for (float y = header; y < display.y - footer; y += grid, ++row) {
        draw->AddLine(ImVec2(0.0f, y), ImVec2(display.x, y),
            row % 4 == 0 ? major_grid : minor_grid, 1.0f);
    }

    if (!performance_mode && app_settings::MotionEnabled() &&
        app_settings::config.animation_intensity == app_settings::EffectLevel::Full) {
        const float scan_offset = std::fmod(g_time * S(14.0f), S(12.0f));
        for (float y = header + scan_offset; y < display.y - footer; y += S(12.0f))
            draw->AddLine(ImVec2(0.0f, y), ImVec2(display.x, y), IM_COL32(255, 255, 255, 1), 1.0f);
    }

    const ImU32 trace = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, performance_mode ? 0.045f : 0.075f);
    const ImU32 node = CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, performance_mode ? 0.14f : 0.23f);
    DrawCircuitTrace(draw, ImVec2(S(34.0f), header + S(78.0f)),
        ImVec2((std::min)(display.x * 0.32f, S(380.0f)), header + S(40.0f)),
        S(98.0f), trace, node, !performance_mode && app_settings::MotionEnabled());
    DrawCircuitTrace(draw, ImVec2(display.x - S(34.0f), display.y - footer - S(78.0f)),
        ImVec2((std::max)(display.x * 0.68f, display.x - S(380.0f)), display.y - footer - S(40.0f)),
        display.x - S(98.0f), trace, node, !performance_mode && app_settings::MotionEnabled());

    const float vignette = (std::min)(S(150.0f), display.x * 0.18f);
    draw->AddRectFilledMultiColor(ImVec2(0.0f, header), ImVec2(vignette, display.y - footer),
        C_BG(170), C_BG(0), C_BG(0), C_BG(170));
    draw->AddRectFilledMultiColor(ImVec2(display.x - vignette, header), ImVec2(display.x, display.y - footer),
        C_BG(0), C_BG(170), C_BG(170), C_BG(0));
}

void ChangeNavigation(int page) {
    if (page < static_cast<int>(NavPage::Home) || page > static_cast<int>(NavPage::Account))
        page = static_cast<int>(NavPage::Home);
    if (page == static_cast<int>(NavPage::Updates))
        page = static_cast<int>(NavPage::Library);
    if (page == g_nav) return;
    g_nav = page;
    g_page_transition = app_settings::MotionEnabled() ? 0.f : 1.f;
    g_keyboard_active = false;
    g_profile_menu_open = false;
}

void DrawTrackedText(ImDrawList* draw, ImFont* font, ImVec2 pos,
                     const char* text, ImU32 color, float tracking) {
    if (!draw || !text || !*text) return;
    if (!font) {
        draw->AddText(pos, color, text);
        return;
    }

    float x = pos.x;
    for (const char* it = text; *it; ++it) {
        char glyph[2] = { *it, 0 };
        draw->AddText(font, font->FontSize, ImVec2(x, pos.y), color, glyph);
        const ImVec2 size = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, glyph);
        x += size.x + tracking;
    }
}

void DrawStatusChip(ImDrawList* draw, float& x, float y, const char* label,
                    const char* value, ImU32 color, float maxRight) {
    if (!label || !value || x >= maxRight) return;
    char text[112]{};
    std::snprintf(text, sizeof(text), "%s  %s", label, value);
    const ImVec2 size = ImGui::CalcTextSize(text);
    const float width = size.x + S(25.f);
    if (x + width > maxRight)
        return;
    const ImVec2 min(x, y);
    const ImVec2 max(x + width, y + S(27.f));
    draw->AddRectFilled(min, max, CyberTheme::WithAlpha(CyberTheme::Colors.Card, 0.76f), CyberTheme::Radius::Sm);
    draw->AddRect(min, max, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.34f), CyberTheme::Radius::Sm);
    draw->AddCircleFilled(ImVec2(min.x + S(10.f), min.y + S(13.5f)), S(3.f), color, 10);
    draw->AddText(ImVec2(min.x + S(18.f), min.y + S(5.f)), C_MUTED(), text);
    x = max.x + S(7.f);
}

void DrawHeader(ImDrawList* draw, ImVec2 display) {
    const float height = S(58.f);
    draw->AddRectFilled(ImVec2(0, 0), ImVec2(display.x, height), C_PANEL());
    draw->AddLine(ImVec2(0, height), ImVec2(display.x, height),
                  CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.55f));

    ImFont* body = CyberFonts::GetBodyFont();
    const float motion = app_settings::AnimationScale();
    const float intro = motion <= 0.f ? 1.f : std::clamp(g_time / (0.52f / (std::max)(0.25f, motion)), 0.f, 1.f);
    const float eased = intro * intro * (3.f - 2.f * intro);
    const ImVec2 brandCenter(S(39.f), S(29.f));
    CyberTheme::DrawRadialAccent(draw, brandCenter, S(62.f), 0.065f * eased);
    const ImVec2 logoMin(S(13.f), S(3.f));
    const ImVec2 logoMax(S(65.f), S(55.f));
    if (ImTextureID logo = BrandAssets::GetLogoTexture()) {
        draw->AddCircleFilled(brandCenter, S(27.f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.08f * eased), 28);
        draw->AddImage(logo, logoMin, logoMax, ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, static_cast<int>(255 * eased)));
    }

    if (body) ImGui::PushFont(body);

    struct NavItem { NavPage page; const char* key; };
    static constexpr NavItem kTabs[] = {
        {NavPage::Home, "launcher.home"},
        {NavPage::Library, "launcher.library"},
        {NavPage::Marketplace, "launcher.marketplace"},
        {NavPage::Diagnostics, "launcher.diagnostics"},
    };
    float x = display.x < S(900.f) ? S(92.f) : S(112.f);
    for (const NavItem& item : kTabs) {
        const char* text = Loc::Tr(item.key);
        const ImVec2 size = ImGui::CalcTextSize(text);
        const ImVec2 min(x - S(8.f), S(7.f));
        const ImVec2 max(x + size.x + S(8.f), height - S(2.f));
        const bool active = static_cast<int>(item.page) == g_nav;
        const bool hovered = !g_profile_menu_open && ImGui::IsMouseHoveringRect(min, max);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddText(ImVec2(x, S(20.f)), active ? C_GOLD_LT() : (hovered ? C_TEXT() : C_MUTED()), text);
        if (active)
            draw->AddRectFilled(ImVec2(x, height - S(3.f)), ImVec2(x + size.x, height), C_GOLD(), S(1.f));
        if (hovered && ImGui::IsMouseClicked(0))
            ChangeNavigation(static_cast<int>(item.page));
        x += size.x + (display.x < S(900.f) ? S(18.f) : S(28.f));
    }

    // Real, local status only. No "services online" claim is made without a server.
    if (display.x >= S(1180.f)) {
        float statusX = (std::max)(x + S(18.f), display.x - S(620.f));
        const float statusRight = display.x - S(254.f);
        const auto dma = mem.GetDiagnosticsSnapshot();
        const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
        DrawStatusChip(draw, statusX, S(15.f), Loc::Tr("launcher.dma"),
            dma.deviceOpen ? Loc::Tr("launcher.connected")
                           : (dma.deviceDetected ? Loc::Tr("launcher.device_detected") : Loc::Tr("launcher.device_not_detected")),
            (dma.deviceOpen || dma.deviceDetected) ? C_GREEN() : C_MUTED(), statusRight);
        DrawStatusChip(draw, statusX, S(15.f), Loc::Tr("launcher.input"), InputDeviceName(),
            InputDeviceConnected() ? C_GREEN() : C_MUTED(), statusRight);
        DrawStatusChip(draw, statusX, S(15.f), Loc::Tr("launcher.updates_status"), UpdateStatusText(update),
            UpdateStatusColor(update), statusRight);
    }

    const auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const auto license = OmniGhost::Licensing::GetSnapshot();
    const bool remoteSession = license.remoteServiceConfigured && license.remoteAuthenticated;
    const std::string identity = remoteSession ? license.remoteUsername : MaskEmail(auth.CurrentEmail());
    const float profileWidth = display.x >= S(950.f) ? S(155.f) : S(48.f);
    const float profileX = display.x - S(92.f) - profileWidth;
    const ImVec2 profileMin(profileX, S(10.f));
    const ImVec2 profileMax(profileX + profileWidth, S(48.f));
    const bool profileHover = ImGui::IsMouseHoveringRect(profileMin, profileMax);
    if (profileHover) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    draw->AddRectFilled(profileMin, profileMax,
        profileHover || g_profile_menu_open ? C_CARD_TOP() : C_CARD(), CyberTheme::Radius::Sm);
    draw->AddRect(profileMin, profileMax,
        g_profile_menu_open ? CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.42f)
                            : CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.35f),
        CyberTheme::Radius::Sm);
    const ImVec2 avatar(profileMin.x + S(19.f), (profileMin.y + profileMax.y) * .5f);
    draw->AddCircleFilled(avatar, S(11.f), CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.14f), 20);
    char initial[2] = {'O', 0};
    if (!identity.empty())
        initial[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(identity[0])));
    const ImVec2 initSize = ImGui::CalcTextSize(initial);
    draw->AddText(ImVec2(avatar.x - initSize.x * .5f, avatar.y - initSize.y * .5f), C_GOLD_LT(), initial);
    if (profileWidth > S(90.f)) {
        std::string shortMasked = identity;
        if (shortMasked.size() > 19) shortMasked.resize(19), shortMasked += "…";
        draw->AddText(ImVec2(profileMin.x + S(38.f), profileMin.y + S(11.f)), C_TEXT(), shortMasked.c_str());
    }
    if (profileHover && ImGui::IsMouseClicked(0)) {
        g_profile_menu_open = !g_profile_menu_open;
        if (g_profile_menu_open) g_card_menu_game = GameId::None;
    }

    if (g_profile_menu_open) {
        const ImVec2 menuMin(profileMax.x - S(182.f), height + S(6.f));
        const ImVec2 menuMax(profileMax.x, menuMin.y + S(126.f));
        draw->AddRectFilled(ImVec2(menuMin.x, menuMin.y + S(4.f)), ImVec2(menuMax.x, menuMax.y + S(5.f)),
            CyberTheme::SafeShadowU32(80), CyberTheme::Radius::Md);
        draw->AddRectFilled(menuMin, menuMax, C_PANEL(252), CyberTheme::Radius::Md);
        draw->AddRect(menuMin, menuMax, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.58f), CyberTheme::Radius::Md);
        struct AccountItem { const char* key; NavPage page; bool logout; };
        const AccountItem items[] = {
            {"launcher.profile", NavPage::Account, false},
            {"launcher.settings", NavPage::Settings, false},
            {"launcher.logout", NavPage::Home, true},
        };
        float iy = menuMin.y + S(8.f);
        for (const AccountItem& item : items) {
            const ImVec2 itemMin(menuMin.x + S(7.f), iy);
            const ImVec2 itemMax(menuMax.x - S(7.f), iy + S(34.f));
            const bool hovered = ImGui::IsMouseHoveringRect(itemMin, itemMax, false);
            if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (hovered)
                draw->AddRectFilled(itemMin, itemMax, item.logout ? CyberTheme::WithAlpha(CyberTheme::Colors.Error, 0.09f) : C_CARD_TOP(), CyberTheme::Radius::Sm);
            draw->AddText(ImVec2(itemMin.x + S(10.f), itemMin.y + S(8.f)),
                item.logout ? (hovered ? C_RED() : C_MUTED()) : (hovered ? C_GOLD_LT() : C_TEXT()), Loc::Tr(item.key));
            if (hovered && ImGui::IsMouseClicked(0)) {
                g_profile_menu_open = false;
                if (item.logout) {
                    OmniGhost::Licensing::LogoutRemote();
                    OmniGhost::Auth::LocalAuthService::Instance().Logout(true);
                    g_logout_requested = true;
                } else {
                    ChangeNavigation(static_cast<int>(item.page));
                }
            }
            iy += S(38.f);
        }
        if (ImGui::IsMouseClicked(0) &&
            !ImGui::IsMouseHoveringRect(menuMin, menuMax, false) &&
            !ImGui::IsMouseHoveringRect(profileMin, profileMax, false)) {
            g_profile_menu_open = false;
        }
    }

    auto window_button = [&](float button_x, bool close) {
        const float hit = S(36.f);
        const ImVec2 min(button_x, S(11.f));
        const ImVec2 max(button_x + hit, S(11.f) + hit);
        const bool hovered = ImGui::IsMouseHoveringRect(min, max);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (hovered)
            draw->AddRectFilled(min, max, close ? CyberTheme::WithAlpha(CyberTheme::Colors.Error, 0.10f)
                                               : CyberTheme::WithAlpha(CyberTheme::Colors.CardHover, 0.82f),
                                CyberTheme::Radius::Sm);
        const ImVec2 center((min.x + max.x) * .5f, (min.y + max.y) * .5f);
        const ImU32 icon = hovered ? (close ? C_RED() : C_GOLD_LT()) : C_MUTED();
        if (close) {
            const float r = S(5.2f);
            draw->AddLine(ImVec2(center.x - r, center.y - r), ImVec2(center.x + r, center.y + r), icon, S(1.5f));
            draw->AddLine(ImVec2(center.x + r, center.y - r), ImVec2(center.x - r, center.y + r), icon, S(1.5f));
        } else {
            draw->AddLine(ImVec2(center.x - S(6.f), center.y + S(4.f)),
                          ImVec2(center.x + S(6.f), center.y + S(4.f)), icon, S(1.5f));
        }
        if (hovered && ImGui::IsMouseClicked(0)) {
            HWND hwnd = (g_overlay_instance ? g_overlay_instance->overlay : nullptr);
            if (!hwnd) return;
            if (close) {
                if (app_settings::config.close_behavior == app_settings::CloseBehavior::Minimize)
                    ShowWindow(hwnd, SW_MINIMIZE);
                else
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
            } else if (app_settings::config.minimize_behavior == app_settings::MinimizeBehavior::Minimize) {
                ShowWindow(hwnd, SW_MINIMIZE);
            } else {
                PushToast(Loc::Tr("launcher.minimize_disabled"), C_GOLD(), ToastAction::None, nullptr);
            }
        }
    };
    window_button(display.x - S(82.f), false);
    window_button(display.x - S(42.f), true);
    if (body) ImGui::PopFont();
}

void DrawFooter(ImDrawList* draw, ImVec2 display) {
    const float height = S(36.f);
    draw->AddRectFilled(ImVec2(0, display.y - height), display, C_PANEL());
    draw->AddLine(ImVec2(0, display.y - height), ImVec2(display.x, display.y - height),
                  CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.42f));
    ImFont* body = CyberFonts::GetBodyFont();
    if (body) ImGui::PushFont(body);
    const std::string version = "OmniGhost " + UiFormat::Version(OmniGhost::Version) + "  ·  " +
        LocalizedChannelLabel(OmniGhost::BuildChannel);
    draw->AddText(ImVec2(S(24.f), display.y - S(25.f)), C_MUTED(), version.c_str());
    if (display.x >= S(720.f)) {
        const char* disc = Loc::Tr("launcher.support");
        const ImVec2 dpos(display.x * .45f, display.y - S(25.f));
        const ImVec2 dsz = ImGui::CalcTextSize(disc);
        const bool dhover = ImGui::IsMouseHoveringRect(dpos, ImVec2(dpos.x + dsz.x, dpos.y + dsz.y));
        draw->AddText(dpos, dhover ? C_GOLD() : C_MUTED2(), disc);
        if (dhover) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(0))
                ShellExecuteA(nullptr, "open", "https://discord.gg/WJuVGckQ", nullptr, nullptr, SW_SHOWNORMAL);
        }
    }

    const PerformanceMode::State performance = PerformanceMode::Current();
    if (performance.effective && display.x >= S(980.f)) {
        const char* mode = performance.auto_engaged ? Loc::Tr("launcher.performance_auto") : Loc::Tr("launcher.performance_manual");
        draw->AddCircleFilled(ImVec2(display.x - S(315.f), display.y - S(18.f)), S(3.f), C_GOLD(), 10);
        draw->AddText(ImVec2(display.x - S(304.f), display.y - S(25.f)), C_GOLD_LT(), mode);
    }

    const std::string lastError = OmniGhost::SessionLog::LastErrorId();
    if (!lastError.empty() && display.x >= S(860.f)) {
        const std::string label = std::string(Loc::Tr("launcher.last_error")) + ": " + lastError;
        draw->AddText(ImVec2(display.x - S(430.f), display.y - S(25.f)), C_RED(), label.c_str());
    }
    draw->AddText(ImVec2(display.x - S(172.f), display.y - S(25.f)), C_MUTED2(),
                  Loc::Tr("launcher.esc_back"));
    if (body) ImGui::PopFont();
}

std::wstring NormalizeUtf8ForSearch(const char* value) {
    if (!value || !*value) return {};
    const int wideCount = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (wideCount <= 1) return {};
    std::wstring wide(static_cast<std::size_t>(wideCount), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, wide.data(), wideCount) <= 0)
        return {};
    if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
    const int mappedCount = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr, 0);
    if (mappedCount <= 0)
        return wide;
    std::wstring mapped(static_cast<std::size_t>(mappedCount), L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
        wide.c_str(), static_cast<int>(wide.size()), mapped.data(), mappedCount,
        nullptr, nullptr, 0) <= 0)
        return wide;
    return mapped;
}

bool IsVisible(const GameRuntime& runtime) {
    if (!runtime.definition) return false;
    if (g_filter == 1 && !runtime.installed) return false;
    if (g_filter == 2 && runtime.state != CardState::Running) return false;
    if (g_filter == 3 && !IsReadyState(runtime.state)) return false;
    if (g_search[0]) {
        const std::wstring query = NormalizeUtf8ForSearch(g_search);
        const std::wstring name = NormalizeUtf8ForSearch(runtime.definition->name);
        const std::wstring description = NormalizeUtf8ForSearch(Loc::Tr(runtime.definition->description));
        const std::wstring tagline = NormalizeUtf8ForSearch(Loc::Tr(runtime.definition->tagline));
        if (!query.empty() && name.find(query) == std::wstring::npos &&
            description.find(query) == std::wstring::npos &&
            tagline.find(query) == std::wstring::npos)
            return false;
    }
    return true;
}

void DrawFallbackIdentity(ImDrawList* draw, const GameDefinition& game,
                          ImVec2 min, ImVec2 max, float scale, int alpha) {
    const ImU32 accent = GameColor(game.accent, alpha);
    const ImVec2 center((min.x + max.x) * .5f, (min.y + max.y) * .5f);
    const float radius = 28.f * scale;
    draw->AddCircle(center, radius, WithAlpha(accent, 180), 28, 1.8f);
    draw->AddLine(ImVec2(center.x - radius * .55f, center.y + radius * .45f),
                  ImVec2(center.x + radius * .55f, center.y - radius * .45f),
                  WithAlpha(accent, 160), 2.f);
    ImFont* title = CyberFonts::GetTitleFont();
    if (title) ImGui::PushFont(title);
    const ImVec2 mark = ImGui::CalcTextSize(game.fallback_mark);
    draw->AddText(ImVec2(center.x - mark.x * .5f, center.y - mark.y * .5f),
                  WithAlpha(C_TEXT(), alpha), game.fallback_mark);
    if (title) ImGui::PopFont();
}

void DrawThemeArtwork(ImDrawList* draw, const GameDefinition& game,
                      ImVec2 min, ImVec2 max, float hover, int alpha) {
    const ImU32 top = IM_COL32(
        game.banner_top.r, game.banner_top.g, game.banner_top.b, alpha);
    const ImU32 bottom = IM_COL32(
        game.banner_bottom.r, game.banner_bottom.g, game.banner_bottom.b, alpha);
    const ImU32 accent = GameColor(game.accent, alpha);
    draw->AddRectFilledMultiColor(min, max, top, top, bottom, bottom);

    if (game.launch_id == GameId::FiveM) {
        for (int index = 0; index < 11; ++index) {
            const float width = 18.f + static_cast<float>((index * 7) % 19);
            const float height = 24.f + static_cast<float>((index * 17) % 55);
            const float x = min.x + index * ((max.x - min.x) / 10.f) - 4.f;
            draw->AddRectFilled(ImVec2(x, max.y - height), ImVec2(x + width, max.y),
                IM_COL32(4, 7, 13, static_cast<int>(135 * alpha / 255.f)));
            if (index % 2 == 0)
                draw->AddRectFilled(ImVec2(x + 5.f, max.y - height + 9.f),
                    ImVec2(x + 7.f, max.y - height + 12.f), WithAlpha(accent, 85));
        }
    } else {
        for (int index = -2; index < 8; ++index) {
            const float x = min.x + index * 52.f + hover * 4.f;
            draw->AddLine(ImVec2(x, max.y), ImVec2(x + 72.f, min.y),
                          WithAlpha(accent, static_cast<int>(18 + hover * 22.f)));
        }
    }
}

void DrawTextureCover(ImDrawList* draw, const LauncherAssets::Texture& texture,
                      ImVec2 min, ImVec2 max, ImU32 tint) {
    if (!texture || texture.width <= 0 || texture.height <= 0) return;
    const float boxWidth = max.x - min.x;
    const float boxHeight = max.y - min.y;
    const float textureRatio = static_cast<float>(texture.width) / texture.height;
    const float boxRatio = boxWidth / (std::max)(1.0f, boxHeight);
    ImVec2 uv0(0.0f, 0.0f);
    ImVec2 uv1(1.0f, 1.0f);
    if (textureRatio > boxRatio) {
        const float visible = boxRatio / textureRatio;
        uv0.x = (1.0f - visible) * 0.5f;
        uv1.x = 1.0f - uv0.x;
    } else {
        const float visible = textureRatio / boxRatio;
        uv0.y = (1.0f - visible) * 0.5f;
        uv1.y = 1.0f - uv0.y;
    }
    draw->AddImage(texture.id, min, max, uv0, uv1, tint);
}

void DrawTextureContained(ImDrawList* draw, const LauncherAssets::Texture& texture,
                          ImVec2 min, ImVec2 max, float scale, ImU32 tint) {
    if (!texture || texture.width <= 0 || texture.height <= 0) return;
    const float box_width = max.x - min.x;
    const float box_height = max.y - min.y;
    const float ratio = static_cast<float>(texture.width) / texture.height;
    float width = box_width;
    float height = width / ratio;
    if (height > box_height) { height = box_height; width = height * ratio; }
    width *= scale;
    height *= scale;
    const ImVec2 image_min((min.x + max.x - width) * .5f,
                           (min.y + max.y - height) * .5f);
    draw->AddImage(texture.id, image_min,
        ImVec2(image_min.x + width, image_min.y + height), ImVec2(0, 0), ImVec2(1, 1), tint);
}

float OpticalLogoScale(GameId id) {
    switch (id) {
    case GameId::FiveM: return 0.72f;
    case GameId::CS2: return 0.88f;
    case GameId::Warzone: return 0.72f;
    case GameId::Apex: return 0.66f;
    case GameId::Fortnite: return 0.84f;
    default: return 0.72f;
    }
}

GameRuntime* FindRuntime(GameId id) {
    for (GameRuntime& runtime : g_games) {
        if (runtime.definition && runtime.definition->launch_id == id)
            return &runtime;
    }
    return nullptr;
}

void ActivateGame(GameRuntime& runtime) {
    const GameDefinition& game = *runtime.definition;
    const bool attach_to_running = runtime.state == CardState::Running && !AdapterAttached(game.launch_id);
    if (IsReadyState(runtime.state) || attach_to_running) {
        if (!OmniGhost::Licensing::HasGameAccess(game.id)) {
            runtime.state = CardState::LicenseRequired;
            g_help_game = game.launch_id;
            return;
        }
        if (OmniGhost::Update::UpdateService::Instance().BlocksGameLaunch()) {
            runtime.state = CardState::UpdateRequired;
            g_help_game = game.launch_id;
            return;
        }
        if (!CanStartGame(game.launch_id))
            return;
        // Dependency / FPGA checks run during the real game launch, not here.
        MarkGameUsed(game.launch_id);
        RecordGameSession(game.launch_id, SessionResult::None, {});
        runtime.state = CardState::Launching;
        runtime.launch_timer = 0.f;
        g_selected = game.launch_id;
        char message[96]{};
        std::snprintf(message, sizeof(message), Loc::Tr("launcher.toast.starting"), game.name);
        PushToast(message);
        return;
    }
    if (runtime.state == CardState::Running) {
        PushToast(Loc::Tr("launcher.toast.already_running"), C_MUTED());
        return;
    }
    // Unavailable/degraded states explain the cause instead of silently doing nothing.
    g_help_game = game.launch_id;
}

void DrawCardContextMenu(const GameDefinition& game, ImVec2 anchor) {
    if (g_card_menu_game != game.launch_id)
        return;

    // Let ImGui own the popup lifetime and input handling. The previous
    // implementation mixed a raw foreground draw list with manual hit
    // testing while the card itself lived inside a scrolling child window.
    // That made the context menu fragile during focus/resize transitions.
    char popupId[96]{};
    std::snprintf(popupId, sizeof(popupId), "##game_context_%s",
                  game.id ? game.id : "unknown");

    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1.f, 0.f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(S(184.f), 0.f), ImVec2(S(184.f), S(220.f)));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(6.f), S(6.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Md);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, C_PANEL(252));
    ImGui::PushStyleColor(ImGuiCol_Border,
        CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.58f));

    if (!ImGui::BeginPopup(popupId)) {
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return;
    }

    struct MenuAction { const char* label; int action; };
    const GameHistory history = GetGameHistory(game.launch_id);
    const MenuAction actions[] = {
        {"Histórico de sessões", 5},
        {Loc::Tr("launcher.action.diagnostics"), 0},
        {Loc::Tr("launcher.action.refresh_offsets"), 1},
        {Loc::Tr("launcher.action.reset_settings"), 2},
        {history.favorite ? Loc::Tr("launcher.action.unpin")
                          : Loc::Tr("launcher.action.pin"), 3},
        {Loc::Tr("launcher.action.open_logs"), 4},
    };

    for (const MenuAction& item : actions) {
        if (!item.label || !*item.label)
            continue;

        if (ImGui::Selectable(item.label, false)) {
            g_card_menu_game = GameId::None;
            ImGui::CloseCurrentPopup();

            switch (item.action) {
            case 5:
                g_history_game = game.launch_id;
                break;
            case 0:
                g_detail_game = game.launch_id;
                ChangeNavigation(static_cast<int>(NavPage::Diagnostics));
                break;
            case 1:
                RequestOffsetRefresh(game.launch_id);
                break;
            case 2:
                g_reset_settings_game = game.launch_id;
                break;
            case 3:
                SetFavorite(game.launch_id, !history.favorite);
                break;
            case 4: {
                const std::filesystem::path logs =
                    OmniGhost::Paths::LocalData() / L"logs";
                const HINSTANCE result = ShellExecuteW(
                    nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (reinterpret_cast<INT_PTR>(result) <= 32)
                    PushToast("Não foi possível abrir a pasta de logs.", C_RED());
                break;
            }
            }
            break;
        }
    }

    ImGui::EndPopup();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}
bool DrawGameCard(ImDrawList* draw, ImVec2 min, ImVec2 max,
                  GameRuntime& runtime, float delta, bool keyboard_selected) {
    const GameDefinition& game = *runtime.definition;
    const float motion = app_settings::AnimationScale();
    if (motion <= 0.f) runtime.entry = 1.f;
    else runtime.entry = (std::min)(1.f, runtime.entry + delta * (2.8f + motion));
    const float entry = runtime.entry * runtime.entry * (3.f - 2.f * runtime.entry);
    const bool hovered = !g_profile_menu_open && ImGui::IsMouseHoveringRect(min, max);
    const float hover_rate = motion <= 0.f ? 1.f : (1.f - std::exp(-delta * 12.f * (0.65f + motion)));
    runtime.hover += ((hovered ? 1.f : 0.f) - runtime.hover) * hover_rate;
    if (motion <= 0.f) runtime.hover = hovered ? 1.f : 0.f;
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    const bool launching = runtime.state == CardState::Launching;
    const bool running = runtime.state == CardState::Running;
    const float lift = motion > 0.f ? runtime.hover * S(3.f) : 0.f;
    const float entry_offset = motion > 0.f ? (1.f - entry) * S(14.f) : 0.f;
    ImVec2 card_min(min.x, min.y - lift + entry_offset);
    ImVec2 card_max(max.x, max.y - lift + entry_offset);
    const int alpha = static_cast<int>(255 * entry);
    const ImU32 accent = GameColor(game.accent, alpha);

    draw->AddRectFilled(ImVec2(card_min.x, card_min.y + CyberTheme::Shadow::SoftOffsetY),
        ImVec2(card_max.x, card_max.y + CyberTheme::Shadow::SoftOffsetY),
        CyberTheme::SafeShadowU32(static_cast<int>(CyberTheme::Shadow::SoftAlpha * entry)),
        CyberTheme::Radius::Md);
    draw->AddRectFilled(card_min, card_max,
        keyboard_selected || runtime.hover > .02f ? C_CARD_TOP(alpha) : C_CARD(alpha),
        CyberTheme::Radius::Md);

    const float artHeight = std::clamp((card_max.y - card_min.y) * 0.45f, S(103.0f), S(127.0f));
    const ImVec2 art_max(card_max.x, card_min.y + artHeight);
    draw->PushClipRect(card_min, art_max, true);
    DrawThemeArtwork(draw, game, card_min, art_max, runtime.hover, alpha);
    const LauncherAssets::Texture banner = LauncherAssets::Banner(game.launch_id);
    if (banner) {
        DrawTextureCover(draw, banner, card_min, art_max,
                         IM_COL32(255, 255, 255, static_cast<int>(72 * entry)));
        draw->AddRectFilled(card_min, art_max, C_BG(static_cast<int>(122 * entry)));
    }
    CyberTheme::DrawRadialAccent(draw,
        ImVec2(card_max.x - S(42.f), card_min.y + artHeight * 0.42f),
        S(86.f), 0.028f + 0.018f * runtime.hover);

    const float logo_scale = OpticalLogoScale(game.launch_id) * (1.f + runtime.hover * .025f);
    const ImVec2 logo_min(card_min.x + S(24.f), card_min.y + S(15.f));
    const ImVec2 logo_max(card_max.x - S(24.f), art_max.y - S(18.f));
    const LauncherAssets::Texture logo = LauncherAssets::Logo(game.launch_id);
    if (logo) DrawTextureContained(draw, logo, logo_min, logo_max, logo_scale, IM_COL32(255, 255, 255, alpha));
    else DrawFallbackIdentity(draw, game, logo_min, logo_max, logo_scale, alpha);

    if (game.tagline && game.tagline[0])
        draw->AddText(ImVec2(card_min.x + S(15.f), art_max.y - S(22.f)),
                      WithAlpha(C_MUTED(), static_cast<int>(185 * entry)), Loc::Tr(game.tagline));
    draw->PopClipRect();
    draw->AddRectFilledMultiColor(ImVec2(card_min.x, art_max.y - S(30.f)), art_max,
        C_CARD(0), C_CARD(0), C_CARD(alpha), C_CARD(alpha));

    ImU32 border = CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.50f * entry);
    if (keyboard_selected) border = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.72f * entry);
    else if (running) border = CyberTheme::WithAlpha(CyberTheme::Colors.Success, 0.64f * entry);
    draw->AddRect(card_min, card_max, border, CyberTheme::Radius::Md, 0,
                  keyboard_selected ? S(1.6f) : S(1.0f));
    draw->AddRectFilled(ImVec2(card_min.x + S(14.f), card_min.y + S(1.f)),
        ImVec2(card_min.x + S(64.f), card_min.y + S(3.f)),
        WithAlpha(accent, static_cast<int>((110 + 75 * runtime.hover) * entry)), S(1.f));
    if (keyboard_selected) {
        draw->AddRectFilled(ImVec2(card_min.x + S(1.f), card_min.y + S(16.f)),
            ImVec2(card_min.x + S(3.f), card_max.y - S(16.f)),
            WithAlpha(C_GOLD(), static_cast<int>(220 * entry)), S(1.f));
    }

    ImFont* title = CyberFonts::GetTitleFont();
    ImFont* body = CyberFonts::GetBodyFont();
    if (title) ImGui::PushFont(title);
    draw->AddText(ImVec2(card_min.x + S(20.f), art_max.y + S(7.f)), WithAlpha(C_TEXT(), alpha), game.name);
    if (title) ImGui::PopFont();
    if (body) ImGui::PushFont(body);
    draw->AddText(ImVec2(card_min.x + S(20.f), art_max.y + S(38.f)), WithAlpha(C_MUTED(), alpha), Loc::Tr(game.description));

    const GameHistory history = GetGameHistory(game.launch_id);
    char historyLine[180]{};
    if (history.sessionCount) {
        std::snprintf(historyLine, sizeof(historyLine), "%u %s  ·  %s de menu ativo",
            history.sessionCount, history.sessionCount == 1 ? "sessão" : "sessões",
            FormatActiveDuration(history.totalActiveSeconds).c_str());
    } else if (history.lastUsedUnix) {
        std::snprintf(historyLine, sizeof(historyLine), "Última tentativa: %s",
            SessionResultDisplay(history.lastResult));
    } else {
        std::snprintf(historyLine, sizeof(historyLine), "0 sessões  ·  0s de menu ativo");
    }
    draw->PushClipRect(ImVec2(card_min.x + S(16.f), card_max.y - S(70.f)),
        ImVec2(card_max.x - S(16.f), card_max.y - S(49.f)), true);
    draw->AddText(ImVec2(card_min.x + S(20.f), card_max.y - S(66.f)), WithAlpha(C_MUTED2(), alpha), historyLine);
    draw->PopClipRect();

    const SteamGameUpdateCheck::Status steamStatus = SteamGameUpdateCheck::Get(game.launch_id);
    const EpicGameUpdateCheck::Status epicStatus = EpicGameUpdateCheck::Get(game.launch_id);
    const bool gameUpdateDetected = steamStatus.state == SteamGameUpdateCheck::State::OffsetsOutdated || epicStatus.updatePending;
    if (gameUpdateDetected) {
        draw->AddText(ImVec2(card_min.x + S(20.f), card_max.y - S(49.f)),
            WithAlpha(C_GOLD(), alpha), "JOGO ATUALIZADO · ATUALIZA OS OFFSETS");
    } else if (steamStatus.state == SteamGameUpdateCheck::State::Checking) {
        draw->AddText(ImVec2(card_min.x + S(20.f), card_max.y - S(49.f)),
            WithAlpha(C_MUTED2(), alpha), "A verificar atualização Steam…");
    }

    const char* status = CardStateLabel(runtime.state);
    const ImU32 status_color = CardStateColor(runtime.state);
    const ImVec2 status_size = ImGui::CalcTextSize(status);
    const ImVec2 badge_min(card_max.x - status_size.x - S(57.f), card_min.y + S(14.f));
    const ImVec2 badge_max(card_max.x - S(38.f), card_min.y + S(36.f));
    draw->AddRectFilled(badge_min, badge_max, C_BG(218), CyberTheme::Radius::Sm);
    draw->AddCircleFilled(ImVec2(badge_min.x + S(10.f), badge_min.y + S(11.f)), S(3.2f),
                          WithAlpha(status_color, alpha), 10);
    draw->AddText(ImVec2(badge_min.x + S(19.f), badge_min.y + S(3.f)), WithAlpha(C_MUTED(), alpha), status);

    // Persistent pin and maturity badges. A favourite beta game still remains visibly BETA.
    float leftBadgeX = card_min.x + S(12.f);
    if (history.favorite) {
        const char* pin = "PIN";
        const ImVec2 pinSize = ImGui::CalcTextSize(pin);
        const ImVec2 pinMin(leftBadgeX, card_min.y + S(13.f));
        const ImVec2 pinMax(pinMin.x + pinSize.x + S(16.f), pinMin.y + S(22.f));
        draw->AddRectFilled(pinMin, pinMax, C_BG(218), CyberTheme::Radius::Sm);
        draw->AddText(ImVec2(pinMin.x + S(8.f), pinMin.y + S(3.f)), WithAlpha(C_GOLD(), alpha), pin);
        leftBadgeX = pinMax.x + S(6.f);
    }
    if (game.beta) {
        const char* beta = "BETA";
        const ImVec2 beta_size = ImGui::CalcTextSize(beta);
        const ImVec2 beta_min(leftBadgeX, card_min.y + S(13.f));
        const ImVec2 beta_max(beta_min.x + beta_size.x + S(16.f), beta_min.y + S(22.f));
        draw->AddRectFilled(beta_min, beta_max, C_BG(218), CyberTheme::Radius::Sm);
        draw->AddRect(beta_min, beta_max, WithAlpha(accent, static_cast<int>(135 * entry)), CyberTheme::Radius::Sm);
        draw->AddText(ImVec2(beta_min.x + S(8.f), beta_min.y + S(3.f)), WithAlpha(accent, alpha), beta);
    }

    // Three-dot context menu has a dedicated hit target and never launches the card.
    const ImVec2 moreMin(card_max.x - S(35.f), card_min.y + S(10.f));
    const ImVec2 moreMax(card_max.x - S(8.f), card_min.y + S(39.f));
    const bool moreHovered = ImGui::IsMouseHoveringRect(moreMin, moreMax);
    if (moreHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (moreHovered || g_card_menu_game == game.launch_id)
        draw->AddRectFilled(moreMin, moreMax, C_BG(170), CyberTheme::Radius::Sm);
    const float cy = (moreMin.y + moreMax.y) * .5f;
    for (int dot = 0; dot < 3; ++dot)
        draw->AddCircleFilled(ImVec2(moreMin.x + S(8.f + dot * 6.f), cy), S(1.4f), moreHovered ? C_GOLD_LT() : C_MUTED(), 8);
    if (moreHovered && ImGui::IsMouseClicked(0)) {
        const bool closing = g_card_menu_game == game.launch_id;
        g_card_menu_game = closing ? GameId::None : game.launch_id;
        if (!closing) {
            char popupId[96]{};
            std::snprintf(popupId, sizeof(popupId), "##game_context_%s",
                          game.id ? game.id : "unknown");
            ImGui::OpenPopup(popupId);
        }
    }

    const char* action = Loc::Tr("launcher.click_to_start");
    if (runtime.state == CardState::Launching) action = Loc::Tr("launcher.starting");
    else if (runtime.state == CardState::Running) action = Loc::Tr("launcher.running_detected");
    else if (runtime.state == CardState::ComingSoon) action = Loc::Tr("launcher.action.how_fix");
    else if (runtime.state == CardState::NeedsOffsets) action = Loc::Tr("launcher.action.how_fix");
    else if (runtime.state == CardState::NotInstalled) action = Loc::Tr("launcher.action.launch");
    else if (runtime.state == CardState::DeviceMissing || runtime.state == CardState::GameNotFound)
        action = Loc::Tr("launcher.action.how_fix");
    else if (runtime.state == CardState::LicenseRequired)
        action = Loc::Tr("launcher.action.add_license");
    else if (runtime.state == CardState::UpdateRequired) action = Loc::Tr("launcher.action.how_fix");
    else if (runtime.state == CardState::LaunchFailed) action = Loc::Tr("launcher.action.how_fix");

    char action_text[112]{};
    if ((hovered || keyboard_selected) && IsReadyState(runtime.state))
        std::snprintf(action_text, sizeof(action_text), Loc::Tr("launcher.start_game"), game.name);
    else
        std::snprintf(action_text, sizeof(action_text), "%s", action);
    draw->AddText(ImVec2(card_min.x + S(20.f), card_max.y - S(32.f)),
        WithAlpha((hovered || keyboard_selected || launching) ? C_GOLD() : C_MUTED(), alpha), action_text);
    if (launching) CyberWidgets::DrawSpinner(draw, ImVec2(card_max.x - S(30.f), card_max.y - S(25.f)), S(8.f), S(1.8f));
    if (body) ImGui::PopFont();

    if (hovered && !moreHovered && g_card_menu_game == GameId::None) {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(game.name);
        ImGui::Separator();
        ImGui::TextColored(ImColor(status_color), "%s", status);
        if (gameUpdateDetected) {
            ImGui::Spacing();
            ImGui::TextColored(ImColor(C_GOLD()), "Jogo atualizado: atualiza os offsets antes de usar.");
        }
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
        ImGui::TextWrapped("%s", CardStateDescription(runtime.state));
        if (!history.detail.empty()) {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", history.detail.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    if (hovered && !moreHovered && g_card_menu_game == GameId::None && ImGui::IsMouseClicked(0))
        ActivateGame(runtime);

    DrawCardContextMenu(game, ImVec2(card_max.x - S(6.f), card_min.y + S(42.f)));

    if (runtime.state == CardState::Launching) {
        runtime.launch_timer += delta;
        return runtime.launch_timer >= .45f;
    }
    return false;
}

void DrawLibrary(ImDrawList* draw, ImVec2 display, float delta) {
    SteamGameUpdateCheck::StartForLibrary();
    EpicGameUpdateCheck::StartForLibrary();
    const float top = S(78.f);
    const float side_margin = S(24.f);
    const float content_max_w = (std::min)(display.x - side_margin * 2.f, S(1240.f));
    const float content_x = (display.x - content_max_w) * 0.5f;
    const float content_right = content_x + content_max_w;

    ImFont* title = CyberFonts::GetTitleFont();
    ImFont* body = CyberFonts::GetBodyFont();
    if (body) ImGui::PushFont(body);
    if (title) ImGui::PushFont(title);
    draw->AddText(ImVec2(content_x, top - S(4.f)), C_TEXT(), Loc::Tr("launcher.library"));
    if (title) ImGui::PopFont();

    int visible_count = 0;
    for (const GameRuntime& game : g_games) if (IsVisible(game)) ++visible_count;
    char count[72]{};
    std::snprintf(count, sizeof(count), Loc::Tr("launcher.games_available"), visible_count);

    // Search field + always-visible "Procurar atualizações" on the same row.
    const float check_btn_w = S(178.f);
    const float check_btn_h = S(34.f);
    const float search_width = display.x < S(720.f) ? S(150.f) : S(220.f);
    const float row_y = top - S(3.f);
    const float check_x = content_right - check_btn_w;
    const float search_x = check_x - S(10.f) - search_width;

    ImGui::SetCursorScreenPos(ImVec2(search_x, row_y));
    CyberWidgets::InputField("##library_search", g_search, sizeof(g_search),
        Loc::Tr("launcher.search_hint"), 0, search_width);

    {
        using US = OmniGhost::Update::Status;
        const auto updSnap = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
        const bool checking = updSnap.status == US::Checking ||
                              updSnap.status == US::Downloading ||
                              updSnap.status == US::Installing;
        const char* checkLabel = checking
            ? app_settings::T("A procurar…", "Checking…")
            : app_settings::T("Procurar atualizações", "Check for updates");

        ImGui::SetCursorScreenPos(ImVec2(check_x, row_y));
        ImGui::PushStyleColor(ImGuiCol_Button, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.50f));
        ImGui::PushStyleColor(ImGuiCol_Text, C_GOLD_LT());
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, CyberTheme::Radius::Sm);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.75f));
        const bool clicked = ImGui::Button(checkLabel, ImVec2(check_btn_w, check_btn_h));
        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(2);
        if (clicked && !checking) {
            OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
            PushToast(app_settings::T("A procurar atualizações…", "Checking for updates…"),
                      C_GOLD(), ToastAction::None, nullptr);
        }
    }

    draw->AddText(ImVec2(content_x, top + S(36.f)), C_MUTED(), count);

    const char* filters[] = {
        Loc::Tr("launcher.filter.all"),
        Loc::Tr("launcher.filter.installed"),
        Loc::Tr("launcher.filter.running"),
        Loc::Tr("launcher.filter.ready")
    };
    float filter_x = content_x + ImGui::CalcTextSize(count).x + S(28.f);
    const float filter_y = top + S(31.f);
    for (int index = 0; index < 4; ++index) {
        const ImVec2 text_size = ImGui::CalcTextSize(filters[index]);
        const ImVec2 min(filter_x, filter_y);
        const ImVec2 max(filter_x + text_size.x + S(20.f), filter_y + S(28.f));
        const bool active = g_filter == index;
        const bool hovered = !g_profile_menu_open && ImGui::IsMouseHoveringRect(min, max);
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        draw->AddRectFilled(min, max,
            active ? CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.12f)
                   : (hovered ? C_CARD_TOP() : C_CARD()), CyberTheme::Radius::Sm);
        draw->AddRect(min, max,
            active ? CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.42f)
                   : CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.36f),
            CyberTheme::Radius::Sm);
        draw->AddText(ImVec2(min.x + S(10.f), min.y + S(5.f)), active ? C_GOLD() : C_MUTED(), filters[index]);
        if (hovered && ImGui::IsMouseClicked(0)) {
            g_filter = index;
            g_keyboard_index = 0;
        }
        filter_x += text_size.x + S(28.f);
    }
    if (body) ImGui::PopFont();

    // Update install UI lives in the top-right UpdateUI only (no mid-page banner).
    const float gap = display.x < S(760.f) ? S(14.f) : S(20.f);
    const float start_y = top + S(79.f);
    const float footer = S(36.f);
    const float grid_height = (std::max)(S(130.0f), display.y - start_y - footer - S(10.f));
    const float scrollbar_reserve = S(14.0f);
    const float available_width = (std::max)(S(260.0f), content_max_w - scrollbar_reserve);
    int columns = available_width < S(620.f) ? 1 : (available_width < S(960.f) ? 2 : 3);
    float card_width = (available_width - gap * (columns - 1)) / columns;
    card_width = (std::min)(card_width, S(350.f));
    const float card_height = std::clamp(grid_height * 0.60f, S(228.0f), S(274.0f));

    std::vector<GameRuntime*> visible;
    visible.reserve(g_games.size());
    for (GameRuntime& game : g_games) if (IsVisible(game)) visible.push_back(&game);
    std::stable_sort(visible.begin(), visible.end(), [](const GameRuntime* a, const GameRuntime* b) {
        if (!a || !b || !a->definition || !b->definition) return false;
        return IsFavorite(a->definition->launch_id) && !IsFavorite(b->definition->launch_id);
    });
    if (visible.empty()) g_keyboard_index = 0;
    else g_keyboard_index = std::clamp(g_keyboard_index, 0, static_cast<int>(visible.size()) - 1);

    bool keyboard_moved = false;
    if (!ImGui::GetIO().WantTextInput && !visible.empty()) {
        int next = g_keyboard_index;
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { --next; keyboard_moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { ++next; keyboard_moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { next -= columns; keyboard_moved = true; }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { next += columns; keyboard_moved = true; }
        if (keyboard_moved) {
            g_keyboard_index = std::clamp(next, 0, static_cast<int>(visible.size()) - 1);
            g_keyboard_active = true;
        }
        if (g_keyboard_active && ImGui::IsKeyPressed(ImGuiKey_Enter))
            ActivateGame(*visible[g_keyboard_index]);
    }
    if (std::fabs(ImGui::GetIO().MouseDelta.x) + std::fabs(ImGui::GetIO().MouseDelta.y) > 0.75f)
        g_keyboard_active = false;

    ImGui::SetCursorScreenPos(ImVec2(content_x, start_y));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, CyberTheme::WithAlpha(CyberTheme::Colors.Background, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.74f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.44f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.68f));
    ImGui::BeginChild("##library_grid_scroll", ImVec2(content_max_w, grid_height), false,
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    ImDrawList* grid_draw = ImGui::GetWindowDrawList();
    const ImVec2 grid_origin = ImGui::GetCursorScreenPos();
    int visible_index = 0;
    for (GameRuntime* game_ptr : visible) {
        GameRuntime& game = *game_ptr;
        if (game.entry <= 0.f && (app_settings::AnimationScale() <= 0.f || g_time > visible_index * .06f))
            game.entry = app_settings::AnimationScale() <= 0.f ? 1.f : .001f;
        const int column = visible_index % columns;
        const int row = visible_index / columns;
        const ImVec2 min(grid_origin.x + column * (card_width + gap),
                         grid_origin.y + row * (card_height + gap));
        const ImVec2 max(min.x + card_width, min.y + card_height);
        const bool selected = g_keyboard_active && visible_index == g_keyboard_index;
        DrawGameCard(grid_draw, min, max, game, delta, selected);
        if (selected && keyboard_moved) {
            const float target = row * (card_height + gap);
            const float current = ImGui::GetScrollY();
            const float viewport = ImGui::GetWindowHeight();
            if (target < current || target + card_height > current + viewport)
                ImGui::SetScrollY((std::max)(0.f, target - S(8.f)));
        }
        ++visible_index;
    }

    if (visible_index == 0) {
        ImGui::SetCursorScreenPos(ImVec2(grid_origin.x, grid_origin.y + S(18.f)));
        CyberWidgets::EmptyState(Loc::Tr("launcher.empty_title"),
            Loc::Tr("launcher.empty_desc"));
    } else {
        const int rows = (visible_index + columns - 1) / columns;
        const float total_height = rows * card_height + (std::max)(0, rows - 1) * gap + S(10.0f);
        ImGui::Dummy(ImVec2(available_width, total_height));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(5);
}
