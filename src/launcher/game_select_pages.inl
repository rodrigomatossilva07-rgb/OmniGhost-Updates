
const char* LauncherText(const char* pt, const char* en) {
    return app_settings::config.language == app_settings::Language::PT ? pt : en;
}
void BeginControlPage(const char* id, ImVec2 display) {
    ImGui::SetNextWindowPos(ImVec2(S(24.f), S(76.f)));
    ImGui::SetNextWindowSize(ImVec2(display.x - S(48.f), display.y - S(124.f)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(6.f), S(6.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus);
}

void EndControlPage() {
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void DrawPageHeading(const char* title, const char* subtitle) {


    if (ImFont* font = CyberFonts::GetTitleFont()) ImGui::PushFont(font);
    ImGui::TextColored(CyberTheme::Colors.Text, "%s", title);
    if (CyberFonts::GetTitleFont()) ImGui::PopFont();
    if (subtitle && *subtitle)
        ImGui::TextColored(CyberTheme::Colors.TextDisabled, "%s", subtitle);
    ImGui::Dummy(ImVec2(0.f, S(10.f)));
    const ImVec2 lineStart = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(lineStart, ImVec2(lineStart.x + S(42.f), lineStart.y),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.62f), S(1.f));
    ImGui::Dummy(ImVec2(0.f, S(10.f)));
}

void DrawHome(ImVec2 display) {
    BeginControlPage("##launcher_home", display);
    DrawPageHeading(Loc::Tr("launcher.home"), Loc::Tr("launcher.home.subtitle"));

    const float gap = S(14.f);
    const float verticalExpansion = std::clamp(
        (ImGui::GetContentRegionAvail().y - S(560.f)) / 6.0f, 0.0f, S(24.f));
    const float overviewExpansion = verticalExpansion * 0.45f;
    const float available = ImGui::GetContentRegionAvail().x;
    const bool two = available >= S(760.f);
    const auto account = OmniGhost::Licensing::GetSnapshot();
    std::size_t catalogCount = 0;
    const GameDefinition* catalog = Games(catalogCount);
    std::size_t licensedCount = 0;
    std::size_t readyCount = 0;
    std::uint64_t totalSessions = 0;
    std::uint64_t totalSeconds = 0;
    for (std::size_t index = 0; index < catalogCount; ++index) {
        const GameHistory history = GetGameHistory(catalog[index].launch_id);
        totalSessions += history.sessionCount;
        totalSeconds += history.totalActiveSeconds;
        if (!catalog[index].coming_soon && OmniGhost::Licensing::HasGameAccess(catalog[index].id)) {
            ++licensedCount;
            if (GameRuntime* runtime = FindRuntime(catalog[index].launch_id); runtime && IsReadyState(runtime->state))
                ++readyCount;
        }
    }
    const float overviewWidth = available >= S(900.f) ? (available - gap * 3.f) * .25f
        : (available - gap) * .5f;
    CyberWidgets::BeginCard(Loc::Tr("launcher.account"), overviewWidth);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::Badge(account.websiteAccount ? "OMNIGHOST" : "KEYAUTH",
        account.remoteAuthenticated ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
    CyberWidgets::TextLine(account.remoteUsername.empty() ? LauncherText("Sessão local", "Local session") : account.remoteUsername.c_str(),
        CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::EndCard();
    ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(Loc::Tr("launcher.library"), overviewWidth);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Primary, LauncherText("%zu produtos autorizados", "%zu authorised products"), licensedCount);
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, LauncherText("%zu prontos para iniciar", "%zu ready to start"), readyCount);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::EndCard();
    if (available >= S(900.f)) ImGui::SameLine(0.f, gap); else ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard(LauncherText("Sessão", "Session"), overviewWidth);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::TextLineF(CyberWidgets::TextTone::Primary, LauncherText("%llu sessões registadas", "%llu recorded sessions"), static_cast<unsigned long long>(totalSessions));
    CyberWidgets::TextLine(FormatActiveDuration(totalSeconds).c_str(), CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::EndCard();
    ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(LauncherText("Segurança", "Security"), overviewWidth);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::Badge(account.remoteAuthenticated ? LauncherText("SESSÃO ATIVA", "SESSION ACTIVE") : LauncherText("SESSÃO INATIVA", "SESSION INACTIVE"),
        account.remoteAuthenticated ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
    CyberWidgets::TextLine(account.websiteAccount ? LauncherText("Conta OmniGhost", "OmniGhost account") : LauncherText("Conta KeyAuth", "KeyAuth account"), CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0.f, overviewExpansion * 0.5f));
    CyberWidgets::EndCard();
    ImGui::Dummy(ImVec2(0.f, gap));

    const float heroWidth = two ? (available - gap) * .64f : available;
    const float systemWidth = two ? (available - gap) - heroWidth : available;

    CyberWidgets::BeginCard(Loc::Tr("launcher.action.continue"), heroWidth);
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    const GameId last = LastPlayedGame();
    if (last != GameId::None) {
        const GameDefinition* game = FindGame(last);
        GameRuntime* runtime = FindRuntime(last);
        const GameHistory history = GetGameHistory(last);
        if (game && runtime) {
            CyberWidgets::Badge(IsReadyState(runtime->state) ? "PRONTO PARA CONTINUAR" : CardStateLabel(runtime->state),
                IsReadyState(runtime->state) ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
            ImGui::Dummy(ImVec2(0, S(5.f)));
            CyberWidgets::TextLine(game->name, CyberWidgets::TextTone::Primary);
            CyberWidgets::TextLine(account.websiteAccount ? LauncherText("PERFIL REMOTO", "REMOTE PROFILE") : LauncherText("PERFIL LOCAL", "LOCAL PROFILE"), CyberWidgets::TextTone::Secondary);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, LauncherText("Última sessão · %s · %s", "Last session · %s · %s"),
                SessionResultDisplay(history.lastResult), FormatLastUsed(history.lastUsedUnix).c_str());
            if (history.sessionCount)
                CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, LauncherText("Última duração · %s", "Last duration · %s"),
                    FormatActiveDuration(history.lastSessionSeconds).c_str());
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
                LauncherText("%u sessões · %s de menu ativo", "%u sessions · %s active menu time"), history.sessionCount,
                FormatActiveDuration(history.totalActiveSeconds).c_str());
            ImGui::Dummy(ImVec2(0, S(8.f)));
            if (IsReadyState(runtime->state)) {
                if (CyberWidgets::GoldButton(Loc::Tr("launcher.action.continue"), ImVec2(S(150.f), S(34.f))))
                    ActivateGame(*runtime);
            } else if (CyberWidgets::CyberButton(Loc::Tr("launcher.action.how_fix"), ImVec2(S(150.f), S(34.f)))) {
                g_help_game = last;
            }
        }
    } else {
        CyberWidgets::EmptyState(Loc::Tr("launcher.last_session"),
            MostRecentHistoryGame() == GameId::None
                ? LauncherText("Abre um menu para começar o teu histórico de sessões.", "Open a menu to start your session history.")
                : LauncherText("Ativa 'Recordar o último jogo' para continuar daqui.", "Enable 'Remember last game' to continue here."));
    }
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    CyberWidgets::EndCard();

    if (two) ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(Loc::Tr("launcher.system_health"), systemWidth);
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    CyberWidgets::HealthRow("DMA", dma.deviceOpen ? Loc::Tr("launcher.connected")
        : (dma.deviceDetected ? Loc::Tr("launcher.device_detected") : Loc::Tr("launcher.device_not_detected")),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    std::string input = std::string(InputDeviceName()) + " · " +
        (InputDeviceConnected() ? Loc::Tr("launcher.connected") : Loc::Tr("launcher.standby"));
    CyberWidgets::HealthRow(Loc::Tr("launcher.input"), input.c_str(), InputDeviceConnected() ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.updates_status"), UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error ? CyberWidgets::HealthStatus::Error :
        (update.status == OmniGhost::Update::Status::UpToDate || update.status == OmniGhost::Update::Status::Completed
            ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning));
    if (!dma.dependencyIntegrityOk)
        CyberWidgets::TextLine(LauncherText("Integridade das dependências DMA requer atenção.", "DMA dependency integrity requires attention."), CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0, gap));
    CyberWidgets::BeginCard(LauncherText("Atividade recente", "Recent activity"), two ? (available - gap) * .5f : available);
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    const GameId recent = MostRecentHistoryGame();
    if (recent != GameId::None) {
        const GameDefinition* game = FindGame(recent);
        const GameHistory history = GetGameHistory(recent);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.game"), game ? game->name : "—");
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.result"), SessionResultDisplay(history.lastResult));
        const std::string when = FormatLastUsed(history.lastUsedUnix);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.when"), when.c_str());
        const std::string totalTime = FormatActiveDuration(history.totalActiveSeconds);
        const std::string lastTime = FormatActiveDuration(history.lastSessionSeconds);
        CyberWidgets::KeyValueRow(LauncherText("Tempo total", "Total time"), totalTime.c_str());
        if (history.sessionCount)
            CyberWidgets::KeyValueRow(LauncherText("Última duração", "Last duration"), lastTime.c_str());
        if (!history.detail.empty())
            CyberWidgets::TextLine(history.detail.c_str(), CyberWidgets::TextTone::Secondary);
    } else {
        CyberWidgets::EmptyState(LauncherText("SEM ATIVIDADE", "NO ACTIVITY"), LauncherText("Quando iniciares um jogo, a última sessão será mostrada aqui.", "Your latest session will appear here when you start a game."));
    }
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    CyberWidgets::EndCard();

    if (two) ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(Loc::Tr("launcher.games"), two ? (available - gap) * .5f : available);
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    std::size_t homeGameCount = 0;
    const GameDefinition* homeGames = Games(homeGameCount);
    std::size_t availableGames = 0;
    for (std::size_t i = 0; i < homeGameCount; ++i) {
        if (!homeGames[i].coming_soon) ++availableGames;
    }
    const std::string availableSummary = std::to_string(availableGames) + LauncherText(" produtos disponíveis", " products available");
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.games"), availableSummary.c_str());
    std::size_t shown = 0;
    for (std::size_t index = 0; index < homeGameCount; ++index) {
        const GameDefinition& game = homeGames[index];
        if (game.coming_soon || !OmniGhost::Licensing::HasGameAccess(game.id)) continue;
        GameRuntime* runtime = FindRuntime(game.launch_id);
        CyberWidgets::HealthRow(game.name, runtime ? CardStateLabel(runtime->state) : Loc::Tr("launcher.unavailable"),
            runtime && IsReadyState(runtime->state) ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
        ++shown;
    }
    if (!shown) CyberWidgets::TextLine(LauncherText("Não existem produtos ativos nesta conta.", "There are no active products on this account."), CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0.f, S(6.f)));
    GameRuntime* lastRuntime = FindRuntime(last);
    if (CyberWidgets::Button(Loc::Tr("launcher.continue_last"), CyberWidgets::ButtonStyle::Secondary,
            ImVec2(S(165.f), S(33.f)), lastRuntime != nullptr))
        ActivateGame(*lastRuntime);
    ImGui::SameLine();
    if (CyberWidgets::Button(Loc::Tr("launcher.diagnostics"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(140.f), S(33.f))))
        ChangeNavigation(static_cast<int>(NavPage::Diagnostics));
    ImGui::Dummy(ImVec2(0.f, verticalExpansion * 0.5f));
    CyberWidgets::EndCard();

    EndControlPage();
}

void DrawMarketplace(ImVec2 display) {
    BeginControlPage("##launcher_marketplace", display);
    DrawPageHeading(Loc::Tr("launcher.marketplace"), Loc::Tr("launcher.marketplace.subtitle"));

    static int marketplaceTab = 0;
    const char* tabLabels[] = { "Config Hub", LauncherText("Temas", "Themes"),
        LauncherText("Os meus itens", "My items"), LauncherText("Criadores", "Creators") };
    const float tabsAvailable = ImGui::GetContentRegionAvail().x;
    const bool inlineTabs = tabsAvailable >= S(650.f);
    const float tabWidth = inlineTabs ? (std::min)(S(165.f), (tabsAvailable - S(24.f)) / 4.f)
                                      : (tabsAvailable - S(8.f)) / 2.f;
    for (int index = 0; index < 4; ++index) {
        if (index && (inlineTabs || index % 2 != 0)) ImGui::SameLine(0.f, S(8.f));
        ImGui::PushID(index);
        if (CyberWidgets::Button(tabLabels[index], marketplaceTab == index
                ? CyberWidgets::ButtonStyle::Primary : CyberWidgets::ButtonStyle::Secondary,
                ImVec2(tabWidth, S(38.f)))) marketplaceTab = index;
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0.f, S(12.f)));

    const float gap = S(10.f);
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns = available >= S(1140.f) ? 3 : (available >= S(700.f) ? 2 : 1);
    const float cardWidth = (available - gap * static_cast<float>(columns - 1)) / static_cast<float>(columns);

    if (marketplaceTab == 0) {
    static int configGame = 0;
    static int configCategory = 0;
    static int configSort = 0;
    static char configSearch[81]{};
    static char configStyle[25]{};
    static bool compatibleOnly = true;
    static bool configLoaded = false;
    static bool configAttempted = false;
    static int loadedGame = -1;
    const char* configGames[] = { "FiveM", "CS2", "Fortnite", "Warzone" };
    const char* configGameIds[] = { "fivem", "cs2", "fortnite", "warzone" };
    const char* configCategories[] = { LauncherText("Destaques", "Featured"), LauncherText("Oficiais", "Official"), LauncherText("Verificadas", "Verified"), LauncherText("Populares", "Popular") };
    const char* configSorts[] = { LauncherText("Populares", "Popular"), LauncherText("Recentes", "Recent"), LauncherText("Avaliação", "Rating") };
    const char* configSortValue = configSort == 1 ? "recent" : configSort == 2 ? "rating" : "popular";

    CyberWidgets::BeginCard(LauncherText("Configurações verificadas", "Verified configurations"), 0.f);
    CyberWidgets::Badge("CONFIG HUB", CyberWidgets::TextTone::Accent);
    ImGui::Dummy(ImVec2(0.f, S(6.f)));
    CyberWidgets::TextLine(LauncherText("Presets oficiais e da comunidade que passaram pela validação técnica e moderação.", "Official and community presets that passed technical checks and moderation."), CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine(LauncherText("Guardar cria uma cópia independente na tua biblioteca; atualizações do autor nunca alteram a tua cópia.", "Saving creates an independent library copy; author updates do not change it."), CyberWidgets::TextTone::Secondary);
    const float filterAvailable = (std::min)(ImGui::GetContentRegionAvail().x, S(960.f));
    const bool inlineFilters = filterAvailable >= S(720.f);
    const float filterWidth = inlineFilters
        ? (filterAvailable - S(20.f)) / 3.f
        : filterAvailable;
    const auto drawMarketplaceFilter = [filterWidth](const char* id, const char* label,
                                                      int* value, const char* const* items,
                                                      int count) {
        ImGui::BeginChild(id, ImVec2(filterWidth, S(40.f)), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings);
        CyberWidgets::Combo(label, value, items, count);
        ImGui::EndChild();
    };
    drawMarketplaceFilter("##market_game_filter", LauncherText("Jogo", "Game"), &configGame, configGames, 4);
    if (inlineFilters) ImGui::SameLine(0.f, S(10.f));
    drawMarketplaceFilter("##market_category_filter", LauncherText("Categoria", "Category"), &configCategory,
                          configCategories, 4);
    if (inlineFilters) ImGui::SameLine(0.f, S(10.f));
    drawMarketplaceFilter("##market_sort_filter", LauncherText("Ordenar", "Sort"), &configSort, configSorts, 3);
    if (loadedGame >= 0 && loadedGame != configGame) {
        configLoaded = false;
        configAttempted = false;
        loadedGame = -1;
    }
    CyberWidgets::InputField("##marketplace_config_search", configSearch, sizeof(configSearch), LauncherText("Pesquisar configuração ou autor", "Search configuration or author"), 0, filterAvailable);
    CyberWidgets::InputField("##marketplace_config_style", configStyle, sizeof(configStyle), LauncherText("Etiqueta/estilo", "Tag/style"), 0, filterAvailable);
    CyberWidgets::ToggleSwitch(LauncherText("Apenas compatíveis", "Compatible only"), &compatibleOnly);
    if (CyberWidgets::GoldButton(LauncherText("ATUALIZAR CONFIG HUB", "REFRESH CONFIG HUB"), ImVec2(S(195.f), S(34.f)))) {
        configAttempted = true;
        configLoaded = config_manager::RefreshCommunityConfigsForGame(configGameIds[configGame],
            config_manager::CommunityScope::Marketplace, configSearch, configSortValue);
        loadedGame = configGame;
        CyberWidgets::Notify(config_manager::last_status.c_str(), configLoaded ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
    }
    CyberWidgets::EndCard();

    if (!configLoaded) {
        ImGui::Dummy(ImVec2(0.f, gap));
        CyberWidgets::BeginCard(LauncherText("Configurações", "Configurations"), 0.f);
        CyberWidgets::EmptyState(configAttempted ? LauncherText("SERVIÇO INDISPONÍVEL", "SERVICE UNAVAILABLE")
                                                   : LauncherText("EXPLORAR CONFIGURAÇÕES", "EXPLORE CONFIGURATIONS"),
            configAttempted ? LauncherText("Não foi possível carregar as configurações. Verifica o serviço e tenta novamente.", "Configurations could not be loaded. Check the service and try again.")
                            : LauncherText("Escolhe os filtros e carrega em Atualizar Config Hub.", "Choose filters and select Refresh Config Hub."));
        if (configAttempted && !config_manager::last_status.empty())
            CyberWidgets::TextLine(config_manager::last_status.c_str(), CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
    }
    if (configLoaded) {
        ImGui::Dummy(ImVec2(0.f, gap));
        int shownConfigs = 0;
        const auto& configs = config_manager::CommunityConfigs();
        for (std::size_t i = 0; i < configs.size(); ++i) {
            const auto& item = configs[i];
            bool styleMatches = configStyle[0] == 0;
            for (const auto& tag : item.tags) if (tag.find(configStyle) != std::string::npos) styleMatches = true;
            const bool categoryMatch = configCategory == 0 ? (item.official || item.downloads >= 25 || (item.rating_count >= 3 && item.rating >= 4.f)) :
                configCategory == 1 ? item.official : configCategory == 2 ? (item.verified && !item.official) : true;
            if (!categoryMatch || !styleMatches || (compatibleOnly && !item.compatible)) continue;
            ImGui::PushID(static_cast<int>(i) + 5000);
            CyberWidgets::BeginCard(item.title.c_str(), cardWidth);
            CyberWidgets::Badge(item.official ? LauncherText("OFICIAL OMNIGHOST", "OFFICIAL OMNIGHOST") : LauncherText("VERIFICADA", "VERIFIED"),
                item.official ? CyberWidgets::TextTone::Accent : CyberWidgets::TextTone::Success);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "%s · %s · v%d", item.author.c_str(), item.game.c_str(), item.version);
            CyberWidgets::TextLineF(item.compatible ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning,
                LauncherText("Schema %d · Launcher mínimo %s", "Schema %d · Minimum launcher %s"), item.schema_version, item.min_launcher_version.c_str());
            CyberWidgets::TextLine(item.description.empty() ? LauncherText("Sem descrição.", "No description.") : item.description.c_str(), CyberWidgets::TextTone::Secondary);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Accent, LauncherText("%d downloads · %.1f/5 · %d favoritos", "%d downloads · %.1f/5 · %d favourites"),
                item.downloads, item.rating, item.favorites);
            if (CyberWidgets::GoldButton(LauncherText("GUARDAR", "SAVE"), ImVec2(S(100.f), S(31.f)))) {
                const bool ok = config_manager::DownloadCommunityToLibrary(item.id);
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
            }
            ImGui::SameLine();
            if (CyberWidgets::CyberButton(item.favorite ? LauncherText("FAVORITA", "FAVOURITED") : LauncherText("FAVORITO", "FAVOURITE"), ImVec2(S(125.f), S(31.f)))) {
                const bool ok = config_manager::ToggleCommunityFavorite(item.id, !item.favorite);
                if (ok) configLoaded = config_manager::RefreshCommunityConfigsForGame(configGameIds[configGame],
                    config_manager::CommunityScope::Marketplace, configSearch, configSortValue);
                CyberWidgets::Notify(config_manager::last_status.c_str(), ok ? CyberWidgets::ToastType::Success : CyberWidgets::ToastType::Error);
            }
            CyberWidgets::EndCard();
            ImGui::PopID();
            ++shownConfigs;
            if ((shownConfigs % columns) != 0) ImGui::SameLine(0.f, gap);
            else ImGui::Dummy(ImVec2(0.f, gap));
        }
        if (shownConfigs && (shownConfigs % columns) != 0) ImGui::NewLine();
        if (!shownConfigs) {
            CyberWidgets::BeginCard(LauncherText("Sem resultados", "No results"), 0.f);
            CyberWidgets::EmptyState(LauncherText("SEM CONFIGURAÇÕES", "NO CONFIGURATIONS"), LauncherText("Ainda não existem configurações verificadas nesta categoria.", "There are no verified configurations in this category yet."));
            CyberWidgets::EndCard();
        }
    }
    }

    if (marketplaceTab == 1) {
    ImGui::Dummy(ImVec2(0.f, gap));

    CyberWidgets::BeginCard(LauncherText("Temas e visuais", "Themes and visuals"), 0.f);
    CyberWidgets::Badge(LauncherText("INCLUÍDOS", "INCLUDED"), CyberWidgets::TextTone::Success);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::TextLine(LauncherText("O mesmo estilo é aplicado ao launcher, menus dos jogos e janelas do overlay.", "The same style applies to the launcher, game menus and overlay windows."),
                           CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine(LauncherText("Para trocar depois, escolhe outro estilo ou usa Padrão OmniGhost.", "To change it later, choose another style or use the OmniGhost default."),
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    struct MarketplaceStyle {
        const char* title;
        const char* description;
        CyberTheme::ThemeMode mode;
        CyberTheme::AccentPreset accent;
        CyberTheme::VisualStyle visual;
    };
    static constexpr MarketplaceStyle styles[] = {
        { "Padrão OmniGhost", "Técnico, equilibrado e com a identidade original.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Cyber, CyberTheme::VisualStyle::Omni },
		{ "Obsidian Ember", "Painéis rectos, densos e acabamento laranja incandescente.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Orange, CyberTheme::VisualStyle::Obsidian },
        { "Neon Azure", "Interface leve, arredondada e profundidade azul.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Blue, CyberTheme::VisualStyle::Azure },
        { "Violet Pulse", "Cartões suaves, espaços amplos e movimento orgânico.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Purple, CyberTheme::VisualStyle::Violet },
        { "Matrix Signal", "Terminal angular, compacto e grelha digital.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Matrix, CyberTheme::VisualStyle::Matrix },
        { "Crimson Focus", "Controlos afiados e superfícies vermelhas intensas.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Red, CyberTheme::VisualStyle::Crimson },
        { "Arctic Teal", "Layout aberto, frio e minimalista em vidro teal.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Teal, CyberTheme::VisualStyle::Arctic },
    };

    for (int i = 0; i < static_cast<int>(std::size(styles)); ++i) {
        const auto& style = styles[i];
        ImGui::PushID(i);
        const bool isActive = CyberTheme::GetThemeMode() == style.mode &&
            CyberTheme::GetAccentPreset() == style.accent &&
            CyberTheme::GetVisualStyle() == style.visual;
        const auto& accents = CyberTheme::GetAccentPresets();
        const int accentIndex = static_cast<int>(style.accent);
        const ImVec4 accent = accentIndex >= 0 && accentIndex < static_cast<int>(accents.size())
            ? accents[accentIndex].base : CyberTheme::Colors.Gold;
        const ImU32 accentU32 = ImGui::ColorConvertFloat4ToU32(accent);
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const float height = S(250.f);
        const bool clicked = ImGui::InvisibleButton("##theme", ImVec2(cardWidth, height));
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 max(min.x + cardWidth, min.y + height);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rounding = style.visual == CyberTheme::VisualStyle::Matrix ? 0.f : S(style.visual == CyberTheme::VisualStyle::Violet ? 14.f : 7.f);
        dl->AddRectFilled(min, max, hovered ? C_CARD_TOP(252) : C_CARD(246), rounding);
        dl->AddRect(min, max, CyberTheme::WithAlpha(accent, isActive ? .82f : (hovered ? .55f : .24f)), rounding, 0, S(isActive ? 2.f : 1.f));
        dl->AddRectFilled(ImVec2(min.x, min.y), ImVec2(min.x + S(5.f), max.y), accentU32, rounding, ImDrawFlags_RoundCornersLeft);
        const ImVec2 previewMin(min.x + S(14.f), min.y + S(12.f));
        const ImVec2 previewMax(max.x - S(14.f), min.y + S(143.f));
        const float previewWidth = previewMax.x - previewMin.x;
        const float previewRadius = style.visual == CyberTheme::VisualStyle::Matrix ? 0.f :
            (style.visual == CyberTheme::VisualStyle::Violet ? S(13.f) : S(6.f));
        dl->AddRectFilled(previewMin, previewMax, C_BG(250), previewRadius);
        dl->PushClipRect(previewMin, previewMax, true);
        if (style.visual == CyberTheme::VisualStyle::Matrix) {
            for (float gx = previewMin.x + S(15.f); gx < previewMax.x; gx += S(22.f))
                dl->AddLine(ImVec2(gx, previewMin.y), ImVec2(gx, previewMax.y), CyberTheme::WithAlpha(accent, .13f));
            for (float gy = previewMin.y + S(12.f); gy < previewMax.y; gy += S(22.f))
                dl->AddLine(ImVec2(previewMin.x, gy), ImVec2(previewMax.x, gy), CyberTheme::WithAlpha(accent, .10f));
        } else if (style.visual == CyberTheme::VisualStyle::Violet || style.visual == CyberTheme::VisualStyle::Azure || style.visual == CyberTheme::VisualStyle::Arctic) {
            dl->AddCircleFilled(ImVec2(previewMax.x - S(28.f), previewMin.y + S(24.f)), S(72.f), CyberTheme::WithAlpha(accent, .10f), 32);
            dl->AddCircle(ImVec2(previewMax.x - S(28.f), previewMin.y + S(24.f)), S(51.f), CyberTheme::WithAlpha(accent, .17f), 32);
        } else if (style.visual == CyberTheme::VisualStyle::Crimson) {
            dl->AddLine(ImVec2(previewMax.x - S(95.f), previewMin.y), ImVec2(previewMax.x, previewMin.y + S(95.f)), CyberTheme::WithAlpha(accent, .25f), S(2.f));
        } else if (style.visual == CyberTheme::VisualStyle::Obsidian) {
            for (float y = previewMin.y + S(18.f); y < previewMax.y; y += S(24.f))
                dl->AddLine(ImVec2(previewMin.x, y), ImVec2(previewMax.x, y), CyberTheme::WithAlpha(accent, .08f));
        }
        const float navWidth = previewWidth * (style.visual == CyberTheme::VisualStyle::Obsidian ? .24f : .19f);
        dl->AddRectFilled(ImVec2(previewMin.x + S(7.f), previewMin.y + S(7.f)),
            ImVec2(previewMin.x + navWidth, previewMax.y - S(7.f)), CyberTheme::WithAlpha(accent, .16f), previewRadius);
        for (int line = 0; line < 4; ++line)
            dl->AddRectFilled(ImVec2(previewMin.x + S(13.f), previewMin.y + S(20.f + line * 19.f)),
                ImVec2(previewMin.x + navWidth - S(6.f), previewMin.y + S(24.f + line * 19.f)), CyberTheme::WithAlpha(accent, line == 0 ? .85f : .22f), S(2.f));
        const float panelX = previewMin.x + navWidth + S(9.f);
        dl->AddRectFilled(ImVec2(panelX, previewMin.y + S(12.f)), ImVec2(previewMax.x - S(9.f), previewMax.y - S(10.f)),
            CyberTheme::WithAlpha(accent, .10f), previewRadius);
        dl->AddRectFilled(ImVec2(panelX + S(10.f), previewMin.y + S(23.f)), ImVec2(panelX + (previewMax.x - panelX) * .65f, previewMin.y + S(28.f)), accentU32, S(2.f));
        for (int row = 0; row < 3; ++row) {
            const float y = previewMin.y + S(43.f + row * 23.f);
            dl->AddRectFilled(ImVec2(panelX + S(10.f), y), ImVec2(previewMax.x - S(20.f), y + S(14.f)), CyberTheme::WithAlpha(accent, .11f), previewRadius * .5f);
            dl->AddRectFilled(ImVec2(previewMax.x - S(48.f), y + S(4.f)), ImVec2(previewMax.x - S(25.f), y + S(10.f)), CyberTheme::WithAlpha(accent, .65f), S(3.f));
        }
        dl->PopClipRect();
        ImFont* previewTitle = CyberFonts::GetBodyFont();
        if (previewTitle) dl->AddText(previewTitle, S(18.f), ImVec2(min.x + S(16.f), min.y + S(153.f)), C_TEXT(), style.title);
        else dl->AddText(ImVec2(min.x + S(16.f), min.y + S(153.f)), C_TEXT(), style.title);
        dl->PushClipRect(ImVec2(min.x + S(15.f), min.y + S(177.f)), ImVec2(max.x - S(12.f), min.y + S(211.f)), true);
        dl->AddText(ImGui::GetFont(), S(14.f), ImVec2(min.x + S(16.f), min.y + S(179.f)), C_MUTED(), style.description, nullptr, cardWidth - S(32.f));
        dl->PopClipRect();
        dl->AddText(ImVec2(min.x + S(16.f), min.y + S(218.f)), isActive ? C_GREEN() : C_MUTED(), isActive ? LauncherText("ATIVO", "ACTIVE") : LauncherText("APLICAR TEMA", "APPLY THEME"));
        if (hovered) ImGui::SetTooltip("%s", style.description);
        if (!isActive && clicked) {
            CyberTheme::SetThemeMode(style.mode);
            CyberTheme::SetAccentPreset(style.accent);
            CyberTheme::SetVisualStyle(style.visual);
            std::string saveError;
            if (app_settings::SaveGlobal(&saveError))
                CyberWidgets::Notify(LauncherText("Tema aplicado e guardado.", "Theme applied and saved."), CyberWidgets::ToastType::Success);
            else
                CyberWidgets::Notify(saveError.empty() ? LauncherText("O tema foi aplicado, mas não foi possível guardar.", "The theme was applied but could not be saved.") : saveError.c_str(),
                                     CyberWidgets::ToastType::Warning);
        }
        ImGui::PopID();
        if ((i % columns) != columns - 1 && i + 1 < static_cast<int>(std::size(styles)))
            ImGui::SameLine(0.f, gap);
        else if (i + 1 < static_cast<int>(std::size(styles)))
            ImGui::Dummy(ImVec2(0.f, gap));
    }
    }

    if (marketplaceTab == 2) {
    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard(LauncherText("Os meus itens", "My items"), 0.f);
    CyberWidgets::EmptyState(LauncherText("A TUA COLEÇÃO", "YOUR COLLECTION"),
        LauncherText("Os conteúdos da comunidade associados à tua conta aparecerão aqui quando o serviço estiver disponível.",
                     "Community content linked to your account will appear here when the service is available."));
    CyberWidgets::EndCard();
    if (CyberWidgets::Button(LauncherText("Explorar temas", "Explore themes"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(170.f), S(36.f))))
        marketplaceTab = 1;
    }

    if (marketplaceTab == 3) {
    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard(LauncherText("Criadores da comunidade", "Community creators"), 0.f);
    CyberWidgets::Badge(LauncherText("REVISÃO OBRIGATÓRIA", "REVIEW REQUIRED"), CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0.f, S(6.f)));
    CyberWidgets::TextLine(LauncherText("Cria um pack .ogtheme e envia-o para avaliação antes de poder entrar no Marketplace.", "Create an .ogtheme pack and submit it for review before it can enter the Marketplace."),
                           CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine(LauncherText("O launcher não aceita importações locais: apenas temas revistos e publicados podem ser aplicados.", "The launcher accepts only reviewed and published themes."),
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::TextLine(LauncherText("1. Cria o ficheiro   2. Envia ao suporte   3. Aguarda a revisão", "1. Create the file   2. Send it to support   3. Await review"), CyberWidgets::TextTone::Accent);
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
	static bool openThemeGuide = false;
    if (CyberWidgets::Button(LauncherText("COMO CRIAR UM TEMA", "HOW TO CREATE A THEME"), CyberWidgets::ButtonStyle::Primary,
							 ImVec2(S(205.f), S(34.f))))
		openThemeGuide = true;
    CyberWidgets::EndCard();


	if (openThemeGuide) {
		ImGui::OpenPopup("CRIAR TEMA DA COMUNIDADE");
		openThemeGuide = false;
	}

    ImGui::SetNextWindowSize(ImVec2(S(650.f), S(520.f)), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("CRIAR TEMA DA COMUNIDADE", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        CyberWidgets::Badge("GUIA .OGTHEME", CyberWidgets::TextTone::Info);
        ImGui::Dummy(ImVec2(0.f, S(8.f)));
        CyberWidgets::TextLine("1. Cria um ficheiro de texto UTF-8 e muda a extensão para .ogtheme.", CyberWidgets::TextTone::Primary);
        CyberWidgets::TextLine("2. Usa apenas os campos visuais suportados no exemplo abaixo.", CyberWidgets::TextTone::Primary);
        CyberWidgets::TextLine("3. Envia o ficheiro e uma captura do tema ao suporte OmniGhost para revisão.", CyberWidgets::TextTone::Primary);
        CyberWidgets::TextLine("4. Só depois da aprovação o tema poderá ser publicado e associado às contas.", CyberWidgets::TextTone::Primary);
        ImGui::Dummy(ImVec2(0.f, S(10.f)));
        static char example[] =
            "{\n"
            "  \"schema_version\": 1,\n"
            "  \"theme_mode\": 0,\n"
            "  \"accent_preset\": 99,\n"
            "  \"visual_style\": 0,\n"
            "  \"custom_accent\": { \"r\": 0.20, \"g\": 0.65, \"b\": 1.00, \"a\": 1.00 },\n"
            "  \"ui_scale\": 1.0,\n"
            "  \"high_contrast\": false\n"
            "}";
        ImGui::InputTextMultiline("##ogtheme_example", example, sizeof(example),
            ImVec2(-1.f, S(205.f)), ImGuiInputTextFlags_ReadOnly);
        if (CyberWidgets::Button("COPIAR EXEMPLO", CyberWidgets::ButtonStyle::Secondary, ImVec2(S(160.f), S(34.f))))
            CyberWidgets::CopyToClipboard(example, "Exemplo .ogtheme copiado.");
        ImGui::SameLine();
        if (CyberWidgets::Button("FECHAR", CyberWidgets::ButtonStyle::Ghost, ImVec2(S(110.f), S(34.f))))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    }

    EndControlPage();
}

#include "admin/admin_panel.inl"

const char* DiagnosticsGameName(GameId id) {
    const GameDefinition* game = FindGame(id);
    return game ? game->name : "—";
}

GameId DiagnosticsGameId() {
    const auto licensed = [](GameId id) {
        const GameDefinition* game = FindGame(id);
        return game && OmniGhost::Licensing::HasGameAccess(game->id);
    };
    if (g_detail_game != GameId::None && licensed(g_detail_game)) return g_detail_game;
    if (g_selected != GameId::None && licensed(g_selected)) return g_selected;
    const GameId last = LastPlayedGame();
    if (last != GameId::None && licensed(last)) return last;
    for (const GameRuntime& runtime : g_games) {
        if (runtime.definition && OmniGhost::Licensing::HasGameAccess(runtime.definition->id))
            return runtime.definition->launch_id;
    }
    return GameId::None;
}

CyberWidgets::HealthStatus BuildHealthStatus() {
    const std::string_view buildId = OmniGhost::BuildInfo::BuildId;
    const std::string_view commit = OmniGhost::BuildInfo::CommitId;
    const std::string_view toolchain = OmniGhost::BuildInfo::ToolchainVersion;
    if (buildId.empty() || commit.empty()) return CyberWidgets::HealthStatus::Error;
    if (buildId == "development" || commit == "untracked-source" ||
        toolchain == "unknown" || !OmniGhost::BuildInfo::Reproducible)
        return CyberWidgets::HealthStatus::Warning;
    return CyberWidgets::HealthStatus::Ok;
}

std::string FriendlyBuildLabel() {
    const std::string_view id = OmniGhost::BuildInfo::BuildId;
    const bool local = id.starts_with("local-") || id == "development";
    std::string label = local ? "Compilação de desenvolvimento" : "Versão oficial";
    label += " · ";
    label += OmniGhost::BuildInfo::Architecture[0] ? OmniGhost::BuildInfo::Architecture : "x64";
    return label;
}





struct SystemSnapshot {
    OmniGhost::Platform::SystemInfo info{};
    std::string formatted;
};

const SystemSnapshot& CachedSystemSnapshot() {
    static SystemSnapshot snapshot;
    static double nextRefresh = 0.0;
    const double now = ImGui::GetTime();
    if (now >= nextRefresh) {
        snapshot.info = OmniGhost::Platform::CaptureSystemInfo();
        snapshot.formatted = OmniGhost::Platform::FormatSystemInfo(snapshot.info);
        nextRefresh = now + 2.0;
    }
    return snapshot;
}

std::string BuildDiagnosticsText() {
    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    const SystemSnapshot& system = CachedSystemSnapshot();
    const GameId gameId = DiagnosticsGameId();
    const GameDefinition* game = FindGame(gameId);
    const bool rendererOk = g_overlay_instance && g_overlay_instance->device;
    const bool offsetsOk = game && HasRequiredOffsets(*game);
    const std::filesystem::path logPath = OmniGhost::SessionLog::CurrentLogPath();
    OmniGhost::Support::ErrorCode supportCode = OmniGhost::Support::ErrorCode::None;
    if (!rendererOk)
        supportCode = OmniGhost::Support::ErrorCode::RendererUnavailable;
    else if (!dma.dependencyIntegrityOk)
        supportCode = OmniGhost::Support::ErrorCode::DmaUnavailable;
    else if (game && !offsetsOk)
        supportCode = OmniGhost::Support::ErrorCode::OffsetsUnsupported;
    else if (update.status == OmniGhost::Update::Status::Error)
        supportCode = OmniGhost::Support::ErrorCode::UpdaterFailed;
    else if (!InputDeviceConnected())
        supportCode = OmniGhost::Support::ErrorCode::InputUnavailable;

    std::ostringstream out;
    out << "OmniGhost · " << Loc::Tr("launcher.diagnostics") << "\n";
    out << "Código de suporte: " << OmniGhost::Support::Code(supportCode)
        << " · " << OmniGhost::Support::Message(supportCode) << "\n";
    out << Loc::Tr("launcher.version") << ": " << UiFormat::Version(OmniGhost::Version) << "\n";
    out << Loc::Tr("launcher.build") << ": " << OmniGhost::BuildInfo::BuildId << " | "
        << OmniGhost::BuildInfo::Configuration << " | " << OmniGhost::BuildInfo::Architecture << "\n";
    out << Loc::Tr("launcher.commit_source") << ": " << OmniGhost::BuildInfo::CommitId << "\n";
    out << Loc::Tr("launcher.build_utc") << ": " << UiFormat::DateTime(OmniGhost::BuildInfo::BuildUtc) << "\n";
    out << Loc::Tr("launcher.msvc_tools") << ": " << OmniGhost::BuildInfo::ToolchainVersion << "\n";
    out << Loc::Tr("launcher.compiler") << ": " << OmniGhost::BuildInfo::CompilerVersion << "\n";
    out << "Windows SDK: " << OmniGhost::BuildInfo::WindowsSdkVersion << "\n";
    out << Loc::Tr("launcher.release_channel") << ": " << LocalizedChannelLabel(OmniGhost::BuildInfo::ReleaseChannel) << "\n";
    out << "Windows: " << system.formatted << "\n";
    out << Loc::Tr("launcher.renderer") << ": "
        << (rendererOk ? Loc::Tr("launcher.renderer_ready") : Loc::Tr("launcher.renderer_unavailable")) << "\n";
    out << "DMA: " << (dma.deviceOpen ? Loc::Tr("launcher.connected")
        : (dma.deviceDetected ? "detetado" : "não detetado"))
        << " | processo=" << (dma.processInitialized ? Loc::Tr("launcher.attached") : Loc::Tr("launcher.not_attached"))
        << " | dependências=" << (dma.dependencyIntegrityOk ? "OK" : Loc::Tr("launcher.attention")) << "\n";
    if (game) {
        const GameHistory history = GetGameHistory(gameId);
        if (!history.detail.empty())
            out << "Último diagnóstico de arranque: " << history.detail << "\n";
    }
    out << Loc::Tr("launcher.input") << ": " << InputDeviceName() << " | "
        << (InputDeviceConnected() ? Loc::Tr("launcher.connected") : Loc::Tr("launcher.standby")) << "\n";
    out << Loc::Tr("launcher.active_game") << ": " << DiagnosticsGameName(gameId) << "\n";
    out << Loc::Tr("launcher.offsets") << ": "
        << (game ? (offsetsOk ? Loc::Tr("launcher.status.ready") : Loc::Tr("launcher.missing"))
                 : Loc::Tr("launcher.no_game_selected")) << "\n";
    out << Loc::Tr("launcher.updater") << ": " << UpdateStatusText(update) << "\n";
    if (!update.errorStage.empty()) out << Loc::Tr("launcher.updater_stage") << ": " << update.errorStage << "\n";
    out << Loc::Tr("launcher.log_path") << ": " << OmniGhost::Platform::WideToUtf8(logPath.wstring()) << "\n";
    const std::string lastError = OmniGhost::SessionLog::LastErrorId();
    out << Loc::Tr("launcher.last_error") << ": "
        << (lastError.empty() ? Loc::Tr("launcher.none") : lastError) << "\n";
    out << Loc::Tr("launcher.session") << ": " << OmniGhost::SessionLog::CurrentSessionId() << "\n";
    return OmniGhost::SessionLog::RedactForSupport(out.str());
}





std::string CachedDiagnosticsText() {
    static std::string cached;
    static double nextRebuild = 0.0;
    const double now = ImGui::GetTime();
    if (now >= nextRebuild || cached.empty()) {
        cached = BuildDiagnosticsText();
        nextRebuild = now + 2.0;
    }
    return cached;
}

void DrawDiagnostics(ImVec2 display) {
    BeginControlPage("##launcher_diagnostics", display);
    DrawPageHeading(Loc::Tr("launcher.diagnostics"), Loc::Tr("launcher.diagnostics.subtitle"));
    ImGui::BeginChild("##diagnostics_content",
        ImVec2((std::min)(ImGui::GetContentRegionAvail().x, S(1120.f)), 0.f), false);

    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();


    const SystemSnapshot& system = CachedSystemSnapshot();
    const GameId gameId = DiagnosticsGameId();
    const GameDefinition* selectedGame = FindGame(gameId);
    const bool rendererOk = g_overlay_instance && g_overlay_instance->device;
    const bool offsetsOk = selectedGame && HasRequiredOffsets(*selectedGame);
    const std::filesystem::path logPath = OmniGhost::SessionLog::CurrentLogPath();
    const std::string logPathUtf8 = OmniGhost::Platform::WideToUtf8(logPath.wstring());
    const std::string& windows = system.formatted;
    const std::string version = UiFormat::Version(OmniGhost::Version);
    const std::string build = FriendlyBuildLabel();

    const auto updaterHealth = update.status == OmniGhost::Update::Status::Error
        ? CyberWidgets::HealthStatus::Error
        : ((update.status == OmniGhost::Update::Status::UpToDate ||
            update.status == OmniGhost::Update::Status::Completed)
            ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);

    CyberWidgets::BeginCard(Loc::Tr("launcher.system_health"), 0.f);
    CyberWidgets::TextLine(app_settings::config.language == app_settings::Language::PT
        ? "SISTEMA E APLICAÇÃO" : "SYSTEM AND APPLICATION", CyberWidgets::TextTone::Accent);
    CyberWidgets::HealthRow(Loc::Tr("launcher.version"), version.c_str(), CyberWidgets::HealthStatus::Ok);
    CyberWidgets::HealthRow(Loc::Tr("launcher.build"), build.c_str(), BuildHealthStatus());
    CyberWidgets::HealthRow(Loc::Tr("launcher.windows"), windows.c_str(), system.info.windowsBuild ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.renderer"),
                            rendererOk ? Loc::Tr("launcher.renderer_ready") : Loc::Tr("launcher.renderer_unavailable"),
                            rendererOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    ImGui::Separator();
    CyberWidgets::TextLine(app_settings::config.language == app_settings::Language::PT
        ? "DISPOSITIVOS E JOGO" : "DEVICES AND GAME", CyberWidgets::TextTone::Accent);
    CyberWidgets::HealthRow("DMA", dma.deviceOpen ? Loc::Tr("launcher.connected")
                                                    : (dma.deviceDetected ? Loc::Tr("launcher.device_detected") : Loc::Tr("launcher.device_not_detected")),
                            dma.deviceOpen ? (dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning)
                                           : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(LauncherText("Leitura do processo", "Process reading"),
                            dma.processInitialized ? LauncherText("Validada nesta sessão", "Validated this session") : LauncherText("Ainda não validada", "Not yet validated"),
                            dma.processInitialized ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.input"), InputDeviceConnected() ? InputDeviceName() : Loc::Tr("launcher.standby"),
                            InputDeviceConnected() ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.active_game"), DiagnosticsGameName(gameId),
                            selectedGame ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.offsets"),
                            selectedGame ? (offsetsOk ? Loc::Tr("launcher.status.ready") : Loc::Tr("launcher.missing"))
                                         : Loc::Tr("launcher.no_game_selected"),
                            selectedGame ? (offsetsOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error)
                                         : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.updater"), UpdateStatusText(update), updaterHealth);
    CyberWidgets::HealthRow(Loc::Tr("launcher.log_path"),
                            logPathUtf8.empty() ? Loc::Tr("launcher.unavailable") : logPathUtf8.c_str(),
                            logPathUtf8.empty() ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);

    const std::string lastError = OmniGhost::SessionLog::LastErrorId();
    if (!lastError.empty())
        CyberWidgets::InlineMessage((std::string(Loc::Tr("launcher.last_error")) + ": " + lastError).c_str(),
                                     CyberWidgets::TextTone::Warning, lastError.c_str());

    if (!dma.deviceDetected || !InputDeviceConnected())
        CyberWidgets::TextLine(app_settings::config.language == app_settings::Language::PT
            ? "Verifica as ligações dos dispositivos e volta a tentar." : "Check the device connections and try again.",
            CyberWidgets::TextTone::Secondary);

    ImGui::Dummy(ImVec2(0, S(8.f)));

    const std::string diagnostics = CachedDiagnosticsText();
    if (CyberWidgets::Button(Loc::Tr("launcher.copy_diagnostics"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(170.f), S(34.f))))
        CyberWidgets::CopyToClipboard(diagnostics.c_str(), Loc::Tr("launcher.diagnostics_copied"));
    ImGui::SameLine();
    if (CyberWidgets::Button(Loc::Tr("launcher.open_logs_folder"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(190.f), S(34.f))))
        ShellExecuteW(nullptr, L"open", OmniGhost::Paths::Logs().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (CyberWidgets::Button(Loc::Tr("launcher.create_diagnostics_package"), CyberWidgets::ButtonStyle::Primary, ImVec2(S(225.f), S(34.f)))) {
        std::filesystem::path packagePath;
        std::string error;
        if (OmniGhost::Diagnostics::CreatePackage(diagnostics, packagePath, error)) {
            CyberWidgets::Notify(Loc::Tr("launcher.diagnostics_package_created"), CyberWidgets::ToastType::Success);
            ShellExecuteW(nullptr, L"open", packagePath.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            CyberWidgets::Notify(error.empty() ? Loc::Tr("launcher.diagnostics_package_failed") : error.c_str(), CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0, S(14.f)));
    CyberWidgets::BeginCard(Loc::Tr("launcher.installation"), 0.f);
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.integrity"),
        update.integrityStatus == OmniGhost::Update::IntegrityStatus::Healthy ? Loc::Tr("launcher.integrity_verified") :
        update.integrityStatus == OmniGhost::Update::IntegrityStatus::Damaged ? Loc::Tr("launcher.integrity_repair_needed") :
        update.integrityStatus == OmniGhost::Update::IntegrityStatus::Checking ? Loc::Tr("launcher.integrity_checking") :
        update.integrityStatus == OmniGhost::Update::IntegrityStatus::ManifestMissing ? Loc::Tr("launcher.integrity_manifest_missing") :
        update.integrityStatus == OmniGhost::Update::IntegrityStatus::Error ? Loc::Tr("launcher.error") : Loc::Tr("launcher.integrity_not_checked"));
    if (!update.integrityMessage.empty())
        CyberWidgets::TextLine(update.integrityMessage.c_str(), CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0, S(8.f)));
    const bool integrityBusy = update.integrityStatus == OmniGhost::Update::IntegrityStatus::Checking;
    if (CyberWidgets::LoadingButton(Loc::Tr("launcher.verify_installation"), Loc::Tr("launcher.verifying"), integrityBusy,
            CyberWidgets::ButtonStyle::Secondary, ImVec2(S(160.f), S(34.f))))
        OmniGhost::Update::UpdateService::Instance().VerifyInstallationAsync();
    ImGui::SameLine();
    if (CyberWidgets::LoadingButton(Loc::Tr("launcher.repair_installation"), Loc::Tr("launcher.preparing_repair"), update.repairMode,
            CyberWidgets::ButtonStyle::Secondary, ImVec2(S(165.f), S(34.f))))
        g_confirm_repair = true;
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0, S(14.f)));
    CyberWidgets::SectionTitle(Loc::Tr("launcher.games"));
    ImGui::Dummy(ImVec2(0, S(6.f)));
    for (GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        const GameDefinition& game = *runtime.definition;
        if (!OmniGhost::Licensing::HasGameAccess(game.id)) continue;
        ImGui::PushID(static_cast<int>(game.launch_id));
        const bool focused = g_detail_game == game.launch_id;
        if (focused) ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
        const std::string heading = std::string(game.name) + "  ·  " + CardStateLabel(runtime.state);
        if (!ImGui::CollapsingHeader(heading.c_str())) {
            ImGui::PopID();
            continue;
        }
        if (focused)
            ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.05f));
        CyberWidgets::BeginCard(game.name, 0.f);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.state"), CardStateLabel(runtime.state));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.runtime_local"), runtime.installed ? Loc::Tr("launcher.installed") : Loc::Tr("launcher.missing"));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.offsets"), HasRequiredOffsets(game) ? Loc::Tr("launcher.status.ready") : Loc::Tr("launcher.missing"));
        const GameHistory history = GetGameHistory(game.launch_id);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.last_session"), SessionResultDisplay(history.lastResult));
        const std::string totalTime = FormatActiveDuration(history.totalActiveSeconds);
        const std::string lastTime = FormatActiveDuration(history.lastSessionSeconds);
        CyberWidgets::KeyValueRow(LauncherText("Sessões com menu ativo", "Sessions with active menu"), std::to_string(history.sessionCount).c_str());
        CyberWidgets::KeyValueRow(LauncherText("Tempo total", "Total time"), totalTime.c_str());
        if (history.sessionCount)
            CyberWidgets::KeyValueRow(LauncherText("Última duração", "Last duration"), lastTime.c_str());
        for (const auto& session : history.recentSessions) {
            const std::string when = FormatLastUsed(session.endedUnix);
            const std::string duration = FormatActiveDuration(session.activeSeconds);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary,
                "%s · %s · %s", when.c_str(), duration.c_str(),
                SessionResultDisplay(session.result));
        }
        if (!history.detail.empty()) CyberWidgets::TextLine(history.detail.c_str(), CyberWidgets::TextTone::Secondary);
        if (runtime.state != CardState::ComingSoon && !IsReadyState(runtime.state) &&
            CyberWidgets::Button(Loc::Tr("launcher.action.how_fix"), CyberWidgets::ButtonStyle::Secondary,
                ImVec2(S(150.f), S(34.f))))
            g_help_game = game.launch_id;
        CyberWidgets::EndCard();
        if (focused) ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, S(8.f)));
    }
    ImGui::EndChild();
    EndControlPage();
}

const char* SettingsVkName(int vk) {
    if (vk <= 0) return LauncherText("Nenhuma", "None");
    switch (vk) {
    case VK_INSERT: return LauncherText("Inserir", "Insert");
    case VK_HOME: return LauncherText("Início", "Home");
    case VK_END: return LauncherText("Fim", "End");
    case VK_DELETE: return LauncherText("Eliminar", "Delete");
    case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3"; case VK_F4: return "F4";
    case VK_F5: return "F5"; case VK_F6: return "F6"; case VK_F7: return "F7"; case VK_F8: return "F8";
    case VK_F9: return "F9"; case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
    case VK_LBUTTON: return LauncherText("Rato 1", "Mouse 1"); case VK_RBUTTON: return LauncherText("Rato 2", "Mouse 2");
    case VK_MBUTTON: return LauncherText("Rato 3", "Mouse 3"); case VK_XBUTTON1: return LauncherText("Rato 4", "Mouse 4"); case VK_XBUTTON2: return LauncherText("Rato 5", "Mouse 5");
    default: break;
    }
    static char text[24]{};
    std::snprintf(text, sizeof(text), "VK 0x%02X", vk);
    return text;
}

bool SettingsHotkey(const char* label, int* vk) {
    static int* capture = nullptr;
    static double ignoreUntil = 0.0;
    ImGui::PushID(label);
    CyberWidgets::TextLine(label, CyberWidgets::TextTone::Primary);
    ImGui::SameLine(S(180.f));
    const bool active = capture == vk;
    const char* button = active ? LauncherText("Pressiona uma tecla...", "Press a key...") : SettingsVkName(*vk);
    const bool clicked = CyberWidgets::CyberButton(button, ImVec2(S(180.f), S(31.f)));
    bool changed = false;
    if (clicked) {
        if (active) capture = nullptr;
        else { capture = vk; ignoreUntil = ImGui::GetTime() + 0.25; }
    }
    if (active && ImGui::GetTime() >= ignoreUntil) {
        for (int key = 1; key < 256; ++key) {
            if (GetAsyncKeyState(key) & 0x8000) {
                *vk = key == VK_ESCAPE ? 0 : key;
                capture = nullptr;
                changed = true;
                break;
            }
        }
    }
    ImGui::PopID();
    return changed;
}

bool SettingsFileDialog(bool save, std::filesystem::path& selected) {
    wchar_t path[MAX_PATH]{};
    if (save)
        wcscpy_s(path, L"OmniGhost-settings.ogsettings");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_overlay_instance ? g_overlay_instance->overlay : nullptr;
    dialog.lpstrFilter = L"Definições OmniGhost (*.ogsettings)\0*.ogsettings\0Todos os ficheiros (*.*)\0*.*\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"ogsettings";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!ok) return false;
    selected = path;
    return true;
}

void ApplyUpdaterSettings() {
#if defined(OMNIGHOST_PUBLISH_BUILD)
    const auto channel = OmniGhost::Update::Channel::Stable;
#else
    const auto channel = app_settings::config.update_channel == app_settings::UpdateChannel::Beta
        ? OmniGhost::Update::Channel::Beta : OmniGhost::Update::Channel::Stable;
#endif
    OmniGhost::Update::UpdateService::Instance().ApplyUserPreferences(
        channel,
        app_settings::config.update_auto_download,
        app_settings::config.update_install_on_exit);
}

void SaveGlobalSettings(bool changed) {
    if (!changed) return;
    std::string error;
    if (!app_settings::SaveGlobal(&error))
        PushToast(error.empty() ? Loc::Tr("launcher.settings_save_failed") : error.c_str(), C_RED(), ToastAction::None, nullptr);
}

void DrawSettings(ImVec2 display) {
    app_settings::SettingsPage& page = g_settings_page;
    static app_settings::SettingsPage resetPage = app_settings::SettingsPage::General;
    static bool requestPageReset = false;
    static bool requestGlobalReset = false;

    BeginControlPage("##launcher_settings", display);
    DrawPageHeading(Loc::Tr("launcher.settings"), Loc::Tr("launcher.settings.description"));

    struct SettingsNavigationItem {
        app_settings::SettingsPage page;
        const char* label;
        const char* group;
    };
    const bool settingsPt = app_settings::config.language == app_settings::Language::PT;
    const SettingsNavigationItem navigation[] = {
        {app_settings::SettingsPage::General, Loc::Tr("launcher.settings.general"), settingsPt ? "GERAL" : "GENERAL"},
        {app_settings::SettingsPage::Appearance, Loc::Tr("launcher.settings.appearance"), settingsPt ? "APARÊNCIA" : "APPEARANCE"},
        {app_settings::SettingsPage::Overlay, Loc::Tr("launcher.settings.overlay"), settingsPt ? "APARÊNCIA" : "APPEARANCE"},
        {app_settings::SettingsPage::Input, Loc::Tr("launcher.settings.input"), settingsPt ? "CONTROLOS" : "CONTROLS"},
        {app_settings::SettingsPage::Licenses, Loc::Tr("launcher.settings.licenses"), settingsPt ? "SISTEMA" : "SYSTEM"},
        {app_settings::SettingsPage::Language, Loc::Tr("launcher.language"), settingsPt ? "SISTEMA" : "SYSTEM"},
        {app_settings::SettingsPage::Diagnostics, Loc::Tr("launcher.diagnostics"), settingsPt ? "SISTEMA" : "SYSTEM"},
        {app_settings::SettingsPage::About, Loc::Tr("launcher.settings.about"), settingsPt ? "SISTEMA" : "SYSTEM"}
    };
    if (page == app_settings::SettingsPage::Devices)
        page = app_settings::SettingsPage::General;
    const float navWidth = S(176.f);
    ImGui::BeginChild("##settings_nav", ImVec2(navWidth, 0.f), false);
    const char* previousGroup = nullptr;
    for (const SettingsNavigationItem& item : navigation) {
        if (!previousGroup || std::strcmp(previousGroup, item.group) != 0) {
            if (previousGroup) ImGui::Dummy(ImVec2(0.f, S(8.f)));
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.84f)), "%s", item.group);
            ImGui::Dummy(ImVec2(0.f, S(4.f)));
            previousGroup = item.group;
        }
        const bool selected = page == item.page;
        if (CyberWidgets::CyberButton(item.label, ImVec2(navWidth - S(8.f), S(36.f))))
            page = item.page;
        if (selected) {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRect(min, max,
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.75f), CyberTheme::Metrics::ControlRounding, 0, S(1.5f));
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, min.y + S(6.f)),
                ImVec2(min.x + S(3.f), max.y - S(6.f)), C_GOLD(), S(1.f));
        }
        ImGui::Dummy(ImVec2(0.f, S(3.f)));
    }
    ImGui::EndChild();

    ImGui::SameLine(0.f, S(16.f));
    ImGui::BeginChild("##settings_content", ImVec2((std::min)(ImGui::GetContentRegionAvail().x, S(1120.f)), 0.f), false);
    bool changed = false;
    bool updaterChanged = false;

    switch (page) {
    case app_settings::SettingsPage::General: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.general"), 0.f);
        const char* starts[] = {Loc::Tr("launcher.home"), Loc::Tr("launcher.library"), Loc::Tr("launcher.last_game")};
        int start = static_cast<int>(app_settings::config.start_behavior);
        if (CyberWidgets::Combo(Loc::Tr("launcher.start_behavior"), &start, starts, 3)) {
            app_settings::config.start_behavior = static_cast<app_settings::StartBehavior>(start); changed = true;
        }
        if (CyberWidgets::ToggleSwitch(Loc::Tr("launcher.remember_last_game"), &app_settings::config.remember_last_game)) {
            changed = true;
            if (!app_settings::config.remember_last_game) ForgetLastPlayedGame();
        }
        const char* minimize[] = {Loc::Tr("launcher.minimize"), Loc::Tr("launcher.keep_open")};
        int minBehavior = static_cast<int>(app_settings::config.minimize_behavior);
        if (CyberWidgets::Combo(Loc::Tr("launcher.minimize_behavior"), &minBehavior, minimize, 2)) {
            app_settings::config.minimize_behavior = static_cast<app_settings::MinimizeBehavior>(minBehavior); changed = true;
        }
        const char* close[] = {Loc::Tr("launcher.exit_app"), Loc::Tr("launcher.minimize_taskbar")};
        int closeBehavior = static_cast<int>(app_settings::config.close_behavior);
        if (CyberWidgets::Combo(Loc::Tr("launcher.close_behavior"), &closeBehavior, close, 2)) {
            app_settings::config.close_behavior = static_cast<app_settings::CloseBehavior>(closeBehavior); changed = true;
        }
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.auto_save_game_changes"), &app_settings::config.auto_save);
        CyberWidgets::EndCard();

        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(Loc::Tr("launcher.import_export"), 0.f);
        if (CyberWidgets::CyberButton(Loc::Tr("launcher.export_settings"), ImVec2(S(165.f), S(33.f)))) {
            std::filesystem::path path;
            if (SettingsFileDialog(true, path)) {
                std::string error;
                if (app_settings::ExportGlobal(path, &error)) PushToast(Loc::Tr("launcher.settings_exported"), C_GREEN(), ToastAction::None, nullptr);
                else PushToast(error.c_str(), C_RED(), ToastAction::None, nullptr);
            }
        }
        ImGui::SameLine();
        if (CyberWidgets::CyberButton(Loc::Tr("launcher.import_settings"), ImVec2(S(165.f), S(33.f)))) {
            std::filesystem::path path;
            if (SettingsFileDialog(false, path)) {
                std::string error;
                if (app_settings::ImportGlobal(path, &error)) {
                    ApplyUpdaterSettings();
                    CyberTheme::ApplyTheme();
                    PushToast(Loc::Tr("launcher.settings_imported"), C_GREEN(), ToastAction::None, nullptr);
                } else PushToast(error.c_str(), C_RED(), ToastAction::None, nullptr);
            }
        }
        CyberWidgets::EndCard();

        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(Loc::Tr("launcher.reset"), 0.f);
        CyberWidgets::TextLine(Loc::Tr("launcher.reset_global_help"), CyberWidgets::TextTone::Warning);
        if (CyberWidgets::DangerButton(Loc::Tr("launcher.reset_global"), ImVec2(S(145.f), S(33.f)))) requestGlobalReset = true;
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Appearance: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.appearance"), 0.f);
        const char* scales[] = {"80%", "90%", "100%", "110%", "125%"};
        const float scaleValues[] = {.80f, .90f, 1.00f, 1.10f, 1.25f};
        int selected = 2; float best = 100.f;
        for (int i = 0; i < 5; ++i) { const float d = std::fabs(scaleValues[i] - app_settings::config.ui_scale); if (d < best) { best = d; selected = i; } }
        if (CyberWidgets::Combo(Loc::Tr("launcher.ui_scale"), &selected, scales, 5)) { app_settings::config.ui_scale = scaleValues[selected]; changed = true; }
        const char* levels[] = {Loc::Tr("launcher.effect.off"), Loc::Tr("launcher.effect.subtle"), Loc::Tr("launcher.effect.full")};
        int animation = static_cast<int>(app_settings::config.animation_intensity);
        if (CyberWidgets::Combo(Loc::Tr("launcher.animation"), &animation, levels, 3)) { app_settings::config.animation_intensity = static_cast<app_settings::EffectLevel>(animation); changed = true; }
        const ImU32 accentBefore = app_settings::config.color_primary;
        CyberWidgets::ColorEditU32(Loc::Tr("launcher.accent"), &app_settings::config.color_primary);
        if (accentBefore != app_settings::config.color_primary) { changed = true; CyberTheme::ApplyTheme(); }
        CyberWidgets::EndCard();

        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(LauncherText("Efeitos e desempenho", "Effects and performance"), 0.f);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.auto_performance"), &app_settings::config.auto_performance);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.manual_performance"), &app_settings::config.performance_mode);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.particles"), &app_settings::config.particles);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.snow_effect"), &app_settings::config.snow_effect);
        CyberWidgets::KeyValueRow(LauncherText("Chuva digital", "Digital rain"), LauncherText("Subtil · textura de fundo", "Subtle · background texture"));
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Overlay: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.overlay"), (std::min)(ImGui::GetContentRegionAvail().x, S(760.f)));
        if (CyberWidgets::SliderFloat(Loc::Tr("launcher.background_opacity"), &app_settings::config.black_level, 0.f, 100.f, "%.0f%%")) changed = true;
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.show_fps"), &app_settings::config.show_fps);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.hotkey_overlay"), &app_settings::config.show_hotkey_overlay);
        changed |= CyberWidgets::ToggleSwitch("VSync", &app_settings::config.vsync);

        auto monitors = OmniGhost::Platform::EnumerateMonitors();
        std::vector<std::string> monitorLabels; monitorLabels.emplace_back(Loc::Tr("launcher.monitor_auto"));
        for (std::size_t i = 0; i < monitors.size(); ++i) {
            const int w = monitors[i].rect.right - monitors[i].rect.left;
            const int h = monitors[i].rect.bottom - monitors[i].rect.top;
            char text[96]{}; std::snprintf(text, sizeof(text), "%s %zu%s · %dx%d", Loc::Tr("launcher.monitor"),
                i + 1, monitors[i].primary ? (std::string(" · ") + Loc::Tr("launcher.primary")).c_str() : "", w, h);
            monitorLabels.emplace_back(text);
        }
        std::vector<const char*> monitorItems; for (auto& label : monitorLabels) monitorItems.push_back(label.c_str());
        int monitor = app_settings::config.monitor_index + 1;
        if (monitor < 0 || monitor >= static_cast<int>(monitorItems.size())) monitor = 0;
        if (CyberWidgets::Combo(Loc::Tr("launcher.monitor"), &monitor, monitorItems.data(), static_cast<int>(monitorItems.size()))) { app_settings::config.monitor_index = monitor - 1; changed = true; }
        CyberWidgets::EndCard();
        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(LauncherText("Indicadores da sobreposição", "Overlay indicators"), (std::min)(ImGui::GetContentRegionAvail().x, S(760.f)));
        const ImU32 fpsColorBefore = app_settings::config.color_fps;
        CyberWidgets::ColorEditU32(LauncherText("Cor do contador FPS", "FPS counter colour"), &app_settings::config.color_fps);
        if (fpsColorBefore != app_settings::config.color_fps) changed = true;
        CyberWidgets::KeyValueRow(LauncherText("Composição", "Display sync"), app_settings::config.vsync ? LauncherText("Sincronizada com o monitor", "Synchronised with monitor") : LauncherText("Sem sincronização vertical", "Vertical sync off"));
        CyberWidgets::KeyValueRow(LauncherText("Monitor de destino", "Target monitor"), monitor == 0 ? LauncherText("Automático", "Automatic") : monitorLabels[monitor].c_str());
        CyberWidgets::TextLine(LauncherText("Estas opções afetam apenas a apresentação do menu e dos indicadores locais.", "These options affect only the menu and local indicators."), CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Input: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.input"), 0.f);
        CyberWidgets::TextLine(app_settings::config.language == app_settings::Language::PT
            ? "Atalho para mostrar ou ocultar o menu. Clica na tecla para a alterar."
            : "Shortcut to show or hide the menu. Click the key to change it.", CyberWidgets::TextTone::Secondary);
        ImGui::Dummy(ImVec2(0.f, S(6.f)));
        changed |= SettingsHotkey(Loc::Tr("launcher.menu"), &app_settings::config.menu_bind);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Devices:
        InputDevicesCard::Draw();
        break;
    case app_settings::SettingsPage::Licenses: {
        const auto license = OmniGhost::Licensing::GetSnapshot();
        const bool accessActive = license.remoteAuthenticated;
        std::size_t gameCount = 0;
        const GameDefinition* gameList = Games(gameCount);
        std::size_t integratedGames = 0;
        for (std::size_t index = 0; index < gameCount; ++index)
            if (!gameList[index].coming_soon) ++integratedGames;



        const bool wideLicenses = ImGui::GetContentRegionAvail().x >= S(720.f);
        CyberWidgets::BeginCardRow(wideLicenses ? 2 : 1);
        const float licenseColumnWidth = CyberWidgets::CardRowHalfWidth();
        CyberWidgets::BeginCard(LauncherText("Estado da licença", "License status"), licenseColumnWidth);
        CyberWidgets::Badge(
            accessActive ? LauncherText("SESSÃO ATIVA", "SESSION ACTIVE") : LauncherText("AÇÃO NECESSÁRIA", "ACTION REQUIRED"),
            accessActive ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
        ImGui::Dummy(ImVec2(0, S(6.f)));
        CyberWidgets::KeyValueRow(LauncherText("Modo", "Mode"), license.websiteAccount ? "OmniGhost" : "KeyAuth");
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.state"), license.remoteAuthenticated ? Loc::Tr("launcher.authenticated") : Loc::Tr("launcher.no_session"));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.storage"), Loc::Tr("launcher.remote_session"));
        std::size_t grantedGames = 0;
        for (std::size_t index = 0; index < gameCount; ++index) {
            if (!gameList[index].coming_soon && OmniGhost::Licensing::HasGameAccess(gameList[index].id))
                ++grantedGames;
        }
        const std::string coverage = std::to_string(grantedGames) + " / " + std::to_string(integratedGames);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.coverage"), coverage.c_str());
        CyberWidgets::TextLine(LauncherText("A sessão atual controla o acesso aos jogos desta instalação.", "The current session controls access to games on this installation."),
            CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();

        if (wideLicenses) CyberWidgets::NextCardColumn();
        CyberWidgets::BeginCard(LauncherText("Acessos por produto", "Access by product"), licenseColumnWidth);
        for (std::size_t index = 0; index < gameCount; ++index) {
            const GameDefinition& game = gameList[index];
            if (game.coming_soon) continue;
            const bool granted = OmniGhost::Licensing::HasGameAccess(game.id);
            const std::string duration = granted ? OmniGhost::Licensing::GameAccessDuration(game.id) : std::string{};
            const std::string status = granted
                ? (duration.empty() ? LauncherText("Acesso ativo", "Access active") : std::string(LauncherText("Ativo · ", "Active · ")) + duration)
                : LauncherText("Sem acesso", "No access");
            CyberWidgets::HealthRow(game.name,
                status.c_str(),
                granted ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
        }
        CyberWidgets::EndCard();
        CyberWidgets::EndCardRow();

        CyberWidgets::BeginCard(LauncherText("Licenciamento", "Licensing"), 0.f);
        {
            CyberWidgets::InlineMessage(license.remoteAuthenticated
                ? LauncherText("A sessão está ativa. Cada subscrição desbloqueia os produtos respetivos.", "The session is active. Each subscription unlocks its products.")
                : LauncherText("Inicia sessão ou cria uma conta para desbloquear os jogos.", "Sign in or create an account to unlock games."),
                license.remoteAuthenticated ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
            if (license.remoteAuthenticated && !license.remoteEntitlements.empty()) {
                std::string plans;
                std::vector<std::string> shownPlans;
                for (const auto& entitlement : license.remoteEntitlements) {
                    if (std::find(shownPlans.begin(), shownPlans.end(), entitlement.name) != shownPlans.end()) continue;
                    shownPlans.push_back(entitlement.name);
                    if (!plans.empty()) plans += ", ";
                    plans += entitlement.name;
                }
                CyberWidgets::KeyValueRow(LauncherText("Planos ativos", "Active plans"), plans.c_str());
            }
            if (license.remoteAuthenticated) {
                ImGui::Dummy(ImVec2(0, S(8.f)));
                CyberWidgets::TextLine(LauncherText("Adicionar uma licença à conta", "Add a license to your account"), CyberWidgets::TextTone::Primary);
                ImGui::BeginDisabled(g_license_operation_pending);
                const bool enter = CyberWidgets::InputField("##settings_remote_license_key", g_license_input,
                    sizeof(g_license_input), LauncherText("Nova licença", "New license"), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue,
                    -1.f, false);
                ImGui::EndDisabled();
                if (CyberWidgets::LoadingButton(LauncherText("Adicionar licença", "Add license"),
                        LauncherText("A processar...", "Processing..."), g_license_operation_pending,
                        CyberWidgets::ButtonStyle::Primary, ImVec2(S(180.f), S(34.f))) || (enter && !g_license_operation_pending)) {
                    std::string key(g_license_input);
                    SecureZeroMemory(g_license_input, sizeof(g_license_input));
                    StartRemoteLicenseUpgrade(std::move(key));
                }
                CyberWidgets::TextLine(
                    LauncherText("A licença é associada à conta atual; os acessos existentes mantêm-se.", "The license is added to the current account; existing access is retained."),
                    CyberWidgets::TextTone::Secondary);
            }
        }
        if (!g_license_feedback.empty()) {
            ImGui::Dummy(ImVec2(0, S(7.f)));
            CyberWidgets::InlineMessage(g_license_feedback.c_str(),
                g_license_feedback_success ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Error);
        }
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Updates: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.updates"), 0.f);
#if defined(OMNIGHOST_PUBLISH_BUILD)
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.release_channel"), Loc::Tr("launcher.stable_publish_locked"));
#elif defined(OMNIGHOST_DEV_BUILD)
        const char* channels[] = {Loc::Tr("launcher.stable"), "Beta", "Development (DEV Configs)"};
        int channel = static_cast<int>(app_settings::config.update_channel);
        if (CyberWidgets::Combo(Loc::Tr("launcher.release_channel"), &channel, channels, 3)) { app_settings::config.update_channel = static_cast<app_settings::UpdateChannel>(channel); changed = updaterChanged = true; }
#else
        const char* channels[] = {Loc::Tr("launcher.stable"), "Beta"};
        int channel = static_cast<int>(app_settings::config.update_channel);
        if (CyberWidgets::Combo(Loc::Tr("launcher.release_channel"), &channel, channels, 2)) { app_settings::config.update_channel = static_cast<app_settings::UpdateChannel>(channel); changed = updaterChanged = true; }
#endif
        if (CyberWidgets::ToggleSwitch(Loc::Tr("launcher.auto_check"), &app_settings::config.update_auto_check)) changed = updaterChanged = true;
        if (CyberWidgets::ToggleSwitch(Loc::Tr("launcher.auto_download"), &app_settings::config.update_auto_download)) changed = updaterChanged = true;
        if (CyberWidgets::ToggleSwitch(Loc::Tr("launcher.install_on_exit"), &app_settings::config.update_install_on_exit)) changed = updaterChanged = true;
        const auto snapshot = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.updater"), UpdateStatusText(snapshot));
        if (!snapshot.availableVersion.empty()) {
            const std::string available = UiFormat::Version(snapshot.availableVersion);
            CyberWidgets::KeyValueRow(Loc::Tr("launcher.available"), available.c_str());
        }
        if (CyberWidgets::GoldButton(Loc::Tr("launcher.check_now"), ImVec2(S(145.f), S(33.f)))) OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
        CyberWidgets::TextLine(Loc::Tr("launcher.update_channel_help"), CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Language: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.language"), 0.f);
        const int languageCount = Loc::LanguageCount();
        std::vector<const char*> languages; languages.reserve(languageCount);
        for (int i = 0; i < languageCount; ++i)
            languages.push_back(Loc::LanguageName(static_cast<Loc::Language>(i)));
        int language = static_cast<int>(app_settings::config.language);
        if (CyberWidgets::Combo(Loc::Tr("launcher.language"), &language, languages.data(), languageCount)) { app_settings::config.language = static_cast<app_settings::Language>(language); changed = true; }
        CyberWidgets::KeyValueRow(LauncherText("Idioma atual", "Current language"), Loc::LanguageName(static_cast<Loc::Language>(language)));
        CyberWidgets::KeyValueRow(LauncherText("Idiomas disponíveis", "Available languages"), std::to_string(languageCount).c_str());
        CyberWidgets::EndCard();
        CyberWidgets::CardGap();
        CyberWidgets::BeginCard(LauncherText("Cobertura da interface", "Interface coverage"), 0.f);
        const bool pt = app_settings::config.language == app_settings::Language::PT;
        CyberWidgets::TextLine(pt ? "O idioma é aplicado imediatamente e guardado para a próxima sessão."
                                  : "The language is applied immediately and saved for the next session.", CyberWidgets::TextTone::Primary);
        CyberWidgets::TextLine(pt ? "Alguns textos podem aparecer em português; nomes de jogos e dispositivos mantêm a designação oficial."
                                  : "Some text may appear in Portuguese; game and device names retain their official names.", CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Diagnostics: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.diagnostics"), 0.f);
        CyberWidgets::TextLine(Loc::Tr("launcher.diagnostics_help"), CyberWidgets::TextTone::Secondary);
        if (CyberWidgets::GoldButton(Loc::Tr("launcher.open_diagnostics"), ImVec2(S(175.f), S(33.f)))) ChangeNavigation(static_cast<int>(NavPage::Diagnostics));
        ImGui::SameLine();
        if (CyberWidgets::CyberButton(Loc::Tr("launcher.open_logs_folder"), ImVec2(S(190.f), S(33.f)))) {
            const auto logs = OmniGhost::Paths::LocalData() / L"logs";
            ShellExecuteW(nullptr, L"open", logs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        if (CyberWidgets::CyberButton(LauncherText("Criar diagnóstico.txt", "Create diagnostic.txt"), ImVec2(S(205.f), S(33.f)))) {
            const std::filesystem::path output = OmniGhost::Paths::Executable().parent_path() / L"diagnostico.txt";
            std::ofstream file(output, std::ios::binary | std::ios::trunc);
            const std::string report = BuildDiagnosticsText();
            file.write(report.data(), static_cast<std::streamsize>(report.size()));
            file.flush();
            if (file.good()) {
                PushToast(LauncherText("diagnostico.txt criado junto ao OmniGhost.exe", "diagnostico.txt created next to OmniGhost.exe"), C_GREEN(), ToastAction::None, nullptr);
                ShellExecuteW(nullptr, L"open", output.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                PushToast(LauncherText("Não foi possível criar diagnostico.txt", "Could not create diagnostico.txt"), C_RED(), ToastAction::None, nullptr);
            }
        }
        ImGui::Dummy(ImVec2(0.f, S(8.f)));
        CyberWidgets::TextLine(LauncherText("O relatório inclui o sistema, o renderizador, os dispositivos e as atualizações.", "The report includes the system, renderer, devices and updates."), CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::About: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.about"), 0.f);
        const std::string aboutVersion = UiFormat::Version(OmniGhost::Version);
        CyberWidgets::KeyValueRow("OmniGhost", aboutVersion.c_str());
        const std::string friendlyBuild = FriendlyBuildLabel();
        const bool localBuild = std::string_view(OmniGhost::BuildInfo::BuildId).starts_with("local-");
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.build"), friendlyBuild.c_str());
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.channel"), LocalizedChannelLabel(OmniGhost::BuildInfo::ReleaseChannel));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.architecture"), OmniGhost::BuildInfo::Architecture);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.commit_source"), localBuild
            ? LauncherText("Origem local de desenvolvimento", "Local development build") : OmniGhost::BuildInfo::CommitId);
        CyberWidgets::KeyValueRow("MSVC", OmniGhost::BuildInfo::ToolchainVersion);
        CyberWidgets::KeyValueRow("Windows SDK", OmniGhost::BuildInfo::WindowsSdkVersion);
        CyberWidgets::TextLine(Loc::Tr("launcher.about_metadata_help"), CyberWidgets::TextTone::Secondary);
        if (CyberWidgets::CyberButton(app_settings::config.language == app_settings::Language::PT
                ? "Abrir acordo de licença" : "Open License Agreement", ImVec2(S(202.f), S(34.f))))
            Onboarding::OpenLicenseAgreement();
        CyberWidgets::EndCard();
        break;
    }
    }

    if (updaterChanged) ApplyUpdaterSettings();
    SaveGlobalSettings(changed);

    const bool resettable = page != app_settings::SettingsPage::Devices &&
        page != app_settings::SettingsPage::Licenses &&
        page != app_settings::SettingsPage::Diagnostics && page != app_settings::SettingsPage::About;
    if (resettable) {
        ImGui::Dummy(ImVec2(0.f, S(12.f)));
        if (CyberWidgets::CyberButton(Loc::Tr("launcher.reset_this_page"), ImVec2(S(165.f), S(32.f)))) { resetPage = page; requestPageReset = true; }
    }

    if (requestPageReset) {
        CyberWidgets::OpenModal("##settings_reset_page");
        char resetMessage[220]{};
        const char* resetLabel = Loc::Tr("launcher.settings");
        for (const SettingsNavigationItem& item : navigation) {
            if (item.page == resetPage) { resetLabel = item.label; break; }
        }
        std::snprintf(resetMessage, sizeof(resetMessage),
            "Repor apenas %s para os valores predefinidos?", resetLabel);
        const auto result = CyberWidgets::ConfirmModal(
            "##settings_reset_page", "Repor esta página", resetMessage,
            "Repor página", "Cancelar", CyberWidgets::ButtonStyle::Destructive, S(440.f));
        if (result == CyberWidgets::ModalResult::Cancelled) {
            requestPageReset = false;
        } else if (result == CyberWidgets::ModalResult::Confirmed) {
            app_settings::ResetPage(resetPage);
            if (resetPage == app_settings::SettingsPage::Updates) ApplyUpdaterSettings();
            if (resetPage == app_settings::SettingsPage::Appearance) {
                CyberTheme::ApplyTheme();
            }
            requestPageReset = false;
            PushToast(Loc::Tr("launcher.page_reset"), C_GOLD(), ToastAction::None, nullptr);
        }
    }
    if (requestGlobalReset) {
        CyberWidgets::OpenModal("##settings_reset_global");
        const auto result = CyberWidgets::ConfirmModal(
            "##settings_reset_global", Loc::Tr("launcher.global_reset_title"),
            Loc::Tr("launcher.global_reset_message"),
            Loc::Tr("launcher.reset_global"), Loc::Tr("launcher.cancel"), CyberWidgets::ButtonStyle::Destructive, S(470.f));
        if (result == CyberWidgets::ModalResult::Cancelled) {
            requestGlobalReset = false;
        } else if (result == CyberWidgets::ModalResult::Confirmed) {
            app_settings::ResetGlobal();
            ApplyUpdaterSettings();
            CyberTheme::ApplyTheme();
            requestGlobalReset = false;
            PushToast(Loc::Tr("launcher.global_settings_reset"), C_GOLD(), ToastAction::None, nullptr);
        }
    }

    ImGui::EndChild();
    EndControlPage();
}

void DrawAccount(ImVec2 display) {
    BeginControlPage("##launcher_account", display);
    DrawPageHeading(Loc::Tr("launcher.account"), Loc::Tr("launcher.account.subtitle"));
    const auto license = OmniGhost::Licensing::GetSnapshot();

    const std::string identity = license.remoteUsername;
    const bool accessActive = license.remoteAuthenticated;
    const bool wideAccount = ImGui::GetContentRegionAvail().x >= S(720.f);
    CyberWidgets::BeginCardRow(wideAccount ? 2 : 1);
    const float columnWidth = CyberWidgets::CardRowHalfWidth();
    CyberWidgets::BeginCard(Loc::Tr("launcher.profile"), columnWidth);
    CyberWidgets::Badge(accessActive ? LauncherText("SESSÃO ATIVA", "SESSION ACTIVE") : LauncherText("SESSÃO INATIVA", "SESSION INACTIVE"),
        accessActive ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0, S(7.f)));
    CyberWidgets::KeyValueRow(LauncherText("Utilizador", "User"), identity.empty() ? "—" : identity.c_str());
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.authentication"), license.websiteAccount ? "OmniGhost" : "KeyAuth");
    CyberWidgets::TextLine(LauncherText("O acesso aos jogos depende da sessão atual.", "Game access depends on the current session."), CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    if (wideAccount) CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard(Loc::Tr("launcher.license"), columnWidth);
    CyberWidgets::Badge(accessActive ? LauncherText("ACESSO ATIVO", "ACCESS ACTIVE") : LauncherText("AÇÃO NECESSÁRIA", "ACTION REQUIRED"),
        accessActive ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0, S(7.f)));
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.state"), license.remoteAuthenticated ? LauncherText("Sessão autenticada", "Authenticated session") : LauncherText("Sem sessão ativa", "No active session"));
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.authentication"), license.websiteAccount ? "OmniGhost" : "KeyAuth");
    CyberWidgets::TextLine(LauncherText("Consulta as licenças associadas a esta conta nas definições.", "View the licenses linked to this account in settings."), CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    ImGui::Dummy(ImVec2(0, S(14.f)));
    CyberWidgets::BeginCard(LauncherText("Ações da conta", "Account actions"), 0.f);
    CyberWidgets::TextLine(LauncherText("Nenhuma credencial ou chave é exposta nesta página.", "No credentials or license keys are shown on this page."), CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0, S(8.f)));
    if (CyberWidgets::CyberButton(Loc::Tr("launcher.refresh_state"), ImVec2(S(140.f), S(32.f))))
        OmniGhost::Licensing::Refresh();
    ImGui::SameLine();
    if (CyberWidgets::GoldButton(LauncherText("Gerir licenças", "Manage licenses"), ImVec2(S(160.f), S(32.f)))) {
        g_settings_page = app_settings::SettingsPage::Licenses;
        ChangeNavigation(static_cast<int>(NavPage::Settings));
    }
    ImGui::SameLine();
    if (CyberWidgets::CyberButton(Loc::Tr("launcher.open_local_folder"), ImVec2(S(150.f), S(32.f)))) {
        const auto folder = OmniGhost::Paths::LocalData();
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0, S(14.f)));
    CyberWidgets::TextLine(LauncherText("SESSÃO", "SESSION"), CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::DangerButton(Loc::Tr("launcher.logout"), ImVec2(S(160.f), S(34.f))))
        g_confirm_logout = true;
    EndControlPage();
}

