// This implementation fragment is included by game_select.cpp inside Launcher's private namespace.
// It is separated by responsibility to keep the launcher coordinator reviewable.

void DrawLauncherModals(ImDrawList* draw, ImVec2 display) {
    (void)draw;
    (void)display;

    if (g_reset_settings_game != GameId::None) {
        const GameDefinition* game = FindGame(g_reset_settings_game);
        char detail[240]{};
        std::snprintf(detail, sizeof(detail),
            Loc::Tr("launcher.game_settings_reset_message"),
            game ? game->name : "este jogo");
        CyberWidgets::OpenModal("##launcher_reset_game");
        const auto result = CyberWidgets::ConfirmModal(
            "##launcher_reset_game", "Repor configuração do jogo", detail,
            "Repor configuração", "Cancelar", CyberWidgets::ButtonStyle::Destructive, S(500.f));
        if (result == CyberWidgets::ModalResult::Cancelled) {
            g_reset_settings_game = GameId::None;
        } else if (result == CyberWidgets::ModalResult::Confirmed) {
            config_manager::ResetActiveGameToDefaults();
            config_manager::FlushActiveConfig();
            g_reset_settings_game = GameId::None;
            PushToast(Loc::Tr("launcher.toast.settings_reset"), C_GOLD(), ToastAction::None, nullptr);
        }
        return;
    }

    if (g_history_game != GameId::None) {
        const GameDefinition* game = FindGame(g_history_game);
        if (!game) {
            g_history_game = GameId::None;
            return;
        }
        const GameHistory history = GetGameHistory(g_history_game);
        CyberWidgets::OpenModal("##launcher_session_history");
        if (!CyberWidgets::BeginModal("##launcher_session_history", "Histórico de sessões", S(500.f)))
            return;
        CyberWidgets::TextLine(game->name, CyberWidgets::TextTone::Primary);
        const std::string total = FormatActiveDuration(history.totalActiveSeconds);
        const std::string last = FormatActiveDuration(history.lastSessionSeconds);
        CyberWidgets::KeyValueRow("Sessões com menu ativo", std::to_string(history.sessionCount).c_str());
        CyberWidgets::KeyValueRow("Tempo total", total.c_str());
        if (history.sessionCount)
            CyberWidgets::KeyValueRow("Última sessão", last.c_str());
        ImGui::Separator();
        if (history.recentSessions.empty()) {
            CyberWidgets::TextLine("As próximas sessões concluídas aparecerão aqui.",
                CyberWidgets::TextTone::Secondary);
        } else {
            for (const auto& session : history.recentSessions) {
                const std::string when = FormatLastUsed(session.endedUnix);
                const std::string duration = FormatActiveDuration(session.activeSeconds);
                CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
                    "%s · %s · %s", when.c_str(), duration.c_str(),
                    SessionResultDisplay(session.result));
            }
        }
        if (CyberWidgets::Button("Fechar", CyberWidgets::ButtonStyle::Secondary,
                ImVec2(S(105.f), S(34.f))) || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            g_history_game = GameId::None;
            ImGui::CloseCurrentPopup();
        }
        CyberWidgets::EndModal();
        return;
    }

    if (g_help_game == GameId::None)
        return;

    GameRuntime* runtime = FindRuntime(g_help_game);
    const GameDefinition* game = FindGame(g_help_game);
    if (!runtime || !game) {
        g_help_game = GameId::None;
        return;
    }

    CyberWidgets::OpenModal("##launcher_game_help");
    if (!CyberWidgets::BeginModal("##launcher_game_help", "Como resolver", S(520.f)))
        return;

    CyberWidgets::TextLine(game->name, CyberWidgets::TextTone::Primary);
    CyberWidgets::TextTone stateTone = CyberWidgets::TextTone::Info;
    if (runtime->state == CardState::Ready || runtime->state == CardState::Running)
        stateTone = CyberWidgets::TextTone::Success;
    else if (runtime->state == CardState::UpdateRequired || runtime->state == CardState::NeedsOffsets ||
             runtime->state == CardState::Beta || runtime->state == CardState::LicenseRequired)
        stateTone = CyberWidgets::TextTone::Warning;
    else if (runtime->state == CardState::DeviceMissing || runtime->state == CardState::GameNotFound ||
             runtime->state == CardState::LaunchFailed || runtime->state == CardState::NotInstalled)
        stateTone = CyberWidgets::TextTone::Error;
    CyberWidgets::InlineMessage(CardStateDescription(runtime->state), stateTone);

    const GameHistory history = GetGameHistory(game->launch_id);
    if (!history.detail.empty())
        CyberWidgets::TextLine(history.detail.c_str(), CyberWidgets::TextTone::Secondary);

    const CardState state = runtime->state;
    const GameId id = game->launch_id;
    const char* action = Loc::Tr("launcher.action.diagnostics");
    if (state == CardState::NeedsOffsets) action = Loc::Tr("launcher.action.refresh_offsets");
    else if (state == CardState::UpdateRequired) action = Loc::Tr("launcher.updates");
    else if (state == CardState::LicenseRequired) action = Loc::Tr("launcher.action.add_license");
    else if (state == CardState::DeviceMissing || state == CardState::GameNotFound ||
             state == CardState::LaunchFailed) action = Loc::Tr("launcher.action.retry");
    else if (state == CardState::NotInstalled) action = Loc::Tr("launcher.action.open_logs");

    const bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (CyberWidgets::Button("Fechar", CyberWidgets::ButtonStyle::Ghost, ImVec2(S(105.f), S(34.f))) || escape) {
        g_help_game = GameId::None;
        ImGui::CloseCurrentPopup();
        CyberWidgets::EndModal();
        return;
    }
    ImGui::SameLine();
    if (CyberWidgets::Button(action, CyberWidgets::ButtonStyle::Primary, ImVec2(S(180.f), S(34.f)))) {
        g_help_game = GameId::None;
        ImGui::CloseCurrentPopup();
        if (state == CardState::LicenseRequired) {
            g_settings_page = app_settings::SettingsPage::Licenses;
            ChangeNavigation(static_cast<int>(NavPage::Settings));
        } else if (state == CardState::NeedsOffsets) RequestOffsetRefresh(id);
        else if (state == CardState::UpdateRequired) ToastOpenUpdates();
        else if (state == CardState::DeviceMissing) {
            // Explicit retry is allowed to leave the launcher so main can make
            // one fresh device-open attempt. The game menu still opens only if
            // that DMA attempt succeeds.
            if (!CanStartGame(id)) {
                CyberWidgets::EndModal();
                return;
            }
            MarkGameUsed(id);
            RecordGameSession(id, SessionResult::None, {});
            runtime->state = CardState::Launching;
            runtime->launch_timer = 0.f;
            g_selected = id;
            char message[96]{};
            std::snprintf(message, sizeof(message), Loc::Tr("launcher.toast.starting"), game->name);
            PushToast(message);
        } else if (state == CardState::GameNotFound || state == CardState::LaunchFailed) {
            RecordGameSession(id, SessionResult::None, {});
            runtime->state = game->beta ? CardState::Beta : CardState::Ready;
            ActivateGame(*runtime);
        } else if (state == CardState::NotInstalled) {
            const std::filesystem::path logs = OmniGhost::Paths::LocalData() / L"logs";
            ShellExecuteW(nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            g_detail_game = id;
            ChangeNavigation(static_cast<int>(NavPage::Diagnostics));
        }
    }
    CyberWidgets::EndModal();
}

void DrawNews(ImDrawList* draw, ImVec2 display) {
    ImFont* title = CyberFonts::GetTitleFont();
    ImFont* body = CyberFonts::GetBodyFont();
    const float left = S(32.f);
    const float top = S(82.f);
    if (title) ImGui::PushFont(title);
    draw->AddText(ImVec2(left, top), C_TEXT(), "Notícias");
    if (title) ImGui::PopFont();
    if (body) ImGui::PushFont(body);
    draw->AddText(ImVec2(left, top + S(34.f)), C_MUTED(),
        "Anúncios, eventos e comunicação da OmniGhost.");
    const ImVec2 panel_min(left, top + S(82.f));
    const ImVec2 panel_max(display.x - left, display.y - S(76.f));
    draw->AddRectFilled(ImVec2(panel_min.x, panel_min.y + CyberTheme::Shadow::SoftOffsetY),
        ImVec2(panel_max.x, panel_max.y + CyberTheme::Shadow::SoftOffsetY),
        CyberTheme::SafeShadowU32(CyberTheme::Shadow::SoftAlpha), CyberTheme::Radius::Md);
    draw->AddRectFilled(panel_min, panel_max, C_CARD(244), CyberTheme::Radius::Md);
    draw->AddRect(panel_min, panel_max, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.46f), CyberTheme::Radius::Md);
    const char* heading = "Ainda não existem notícias";
    const char* detail = "Os anúncios e novidades gerais serão apresentados aqui.";
    const ImVec2 heading_size = ImGui::CalcTextSize(heading);
    const ImVec2 detail_size = ImGui::CalcTextSize(detail);
    const float center = (panel_min.y + panel_max.y) * .5f;
    draw->AddCircle(ImVec2(display.x * .5f, center - S(34.f)), S(17.f),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.42f), 28, S(1.2f));
    draw->AddLine(ImVec2(display.x * .5f - S(7.f), center - S(34.f)),
                  ImVec2(display.x * .5f + S(7.f), center - S(34.f)), C_GOLD(), S(1.5f));
    draw->AddText(ImVec2((display.x - heading_size.x) * .5f, center), C_TEXT(), heading);
    draw->AddText(ImVec2((display.x - detail_size.x) * .5f, center + S(27.f)), C_MUTED(), detail);
    if (body) ImGui::PopFont();
}
