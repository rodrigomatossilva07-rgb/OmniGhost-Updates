// This implementation fragment is included by game_select.cpp inside Launcher's private namespace.
// It is separated by responsibility to keep the launcher coordinator reviewable.

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
    // Shared page signature: it keeps every area of the launcher recognisably
    // OmniGhost without introducing a separate visual language per page.
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
    const float available = ImGui::GetContentRegionAvail().x;
    const bool two = available >= S(760.f);
    const float heroWidth = two ? (available - gap) * .64f : available;
    const float systemWidth = two ? (available - gap) - heroWidth : available;

    CyberWidgets::BeginCard("CONTINUAR", heroWidth);
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
            CyberWidgets::TextLine("PERFIL LOCAL · SESSÃO PROTEGIDA", CyberWidgets::TextTone::Secondary);
            CyberWidgets::TextLineF(CyberWidgets::TextTone::Secondary, "Última sessão · %s · %s",
                SessionResultDisplay(history.lastResult), FormatLastUsed(history.lastUsedUnix).c_str());
            ImGui::Dummy(ImVec2(0, S(8.f)));
            if (IsReadyState(runtime->state)) {
                if (CyberWidgets::GoldButton(Loc::Tr("launcher.action.continue"), ImVec2(S(150.f), S(34.f))))
                    ActivateGame(*runtime);
            } else if (CyberWidgets::CyberButton(Loc::Tr("launcher.action.how_fix"), ImVec2(S(150.f), S(34.f)))) {
                g_help_game = last;
            }
        }
    } else {
        CyberWidgets::EmptyState(Loc::Tr("launcher.last_session"), Loc::Tr("launcher.no_history"));
    }
    CyberWidgets::EndCard();

    if (two) ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(Loc::Tr("launcher.system_health"), systemWidth);
    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    CyberWidgets::HealthRow("DMA", dma.deviceOpen ? Loc::Tr("launcher.connected")
        : (dma.deviceDetected ? Loc::Tr("launcher.device_detected") : Loc::Tr("launcher.device_not_detected")),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    std::string input = std::string(InputDeviceName()) + " · " +
        (InputDeviceConnected() ? Loc::Tr("launcher.connected") : Loc::Tr("launcher.standby"));
    CyberWidgets::HealthRow(Loc::Tr("launcher.input"), input.c_str(), InputDeviceConnected() ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.updates_status"), UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
    if (!dma.dependencyIntegrityOk)
        CyberWidgets::TextLine("Integridade das dependências DMA requer atenção.", CyberWidgets::TextTone::Warning);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0, gap));
    CyberWidgets::BeginCard("ATIVIDADE RECENTE", two ? (available - gap) * .5f : available);
    if (last != GameId::None) {
        const GameDefinition* game = FindGame(last);
        const GameHistory history = GetGameHistory(last);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.game"), game ? game->name : "—");
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.result"), SessionResultDisplay(history.lastResult));
        const std::string when = FormatLastUsed(history.lastUsedUnix);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.when"), when.c_str());
        if (!history.detail.empty())
            CyberWidgets::TextLine(history.detail.c_str(), CyberWidgets::TextTone::Secondary);
    } else {
        CyberWidgets::EmptyState("SEM ATIVIDADE", "Quando iniciares um jogo, a última sessão será mostrada aqui.");
    }
    CyberWidgets::EndCard();

    if (two) ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(Loc::Tr("launcher.games"), two ? (available - gap) * .5f : available);
    std::size_t homeGameCount = 0;
    const GameDefinition* homeGames = Games(homeGameCount);
    std::size_t availableGames = 0;
    for (std::size_t i = 0; i < homeGameCount; ++i) {
        if (!homeGames[i].coming_soon) ++availableGames;
    }
    const std::string availableSummary = std::to_string(availableGames) + " produtos disponíveis";
CyberWidgets::KeyValueRow(Loc::Tr("launcher.games"), availableSummary.c_str());
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.status"), Loc::Tr("launcher.ready_to_explore"));
    CyberWidgets::TextLine(Loc::Tr("launcher.empty_desc"), CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::Button(Loc::Tr("launcher.continue_last"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(165.f), S(33.f))))
        ChangeNavigation(static_cast<int>(NavPage::Library));
    CyberWidgets::EndCard();

    EndControlPage();
}

bool MarketplaceThemeFileDialog(std::filesystem::path& selected) {
    wchar_t path[MAX_PATH]{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = g_overlay_instance ? g_overlay_instance->overlay : nullptr;
    dialog.lpstrFilter = L"Pacotes de tema OmniGhost (*.ogtheme)\0*.ogtheme\0\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"ogtheme";
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) return false;
    selected = path;
    return true;
}

void DrawMarketplace(ImVec2 display) {
    BeginControlPage("##launcher_marketplace", display);
    DrawPageHeading(Loc::Tr("launcher.marketplace"), Loc::Tr("launcher.marketplace.subtitle"));

    const float gap = S(14.f);
    const float available = ImGui::GetContentRegionAvail().x;
    // Keep the catalogue compact even at high UI scales: a single very wide
    // theme card is harder to scan than two portrait-oriented cards.
    const bool twoColumns = available >= S(460.f);
    const float cardWidth = twoColumns ? (available - gap) * .5f : available;

    CyberWidgets::BeginCard("TEMAS E VISUAIS", 0.f);
    CyberWidgets::Badge("INCLUÍDOS", CyberWidgets::TextTone::Success);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::TextLine("O mesmo estilo é aplicado ao launcher, menus dos jogos e janelas do overlay.",
                           CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Para trocar depois, volta aqui e escolhe outro estilo — ou usa Padrão OmniGhost.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    struct MarketplaceStyle {
        const char* title;
        const char* description;
        CyberTheme::ThemeMode mode;
        CyberTheme::AccentPreset accent;
    };
    static constexpr MarketplaceStyle styles[] = {
        { "Padrão OmniGhost", "O visual original escuro com o acento Cyber do OmniGhost.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Cyber },
        { "Obsidian Gold", "Escuro profundo com um acento âmbar mais quente e destacado.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Orange },
        { "Neon Azure", "Escuro, limpo e frio, com destaque azul elétrico.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Blue },
        { "Violet Pulse", "Escuro com identidade roxa para um painel mais expressivo.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Purple },
        { "Matrix Signal", "Escuro, verde e focado em leitura rápida de estado.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Matrix },
        { "Crimson Focus", "Escuro de alto contraste com detalhes vermelhos.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Red },
        { "Arctic Teal", "Escuro e sereno, com uma paleta azul-esverdeada.", CyberTheme::ThemeMode::Dark, CyberTheme::AccentPreset::Teal },
    };

    for (int i = 0; i < static_cast<int>(std::size(styles)); ++i) {
        const auto& style = styles[i];
        if (twoColumns && (i % 2) == 0)
            CyberWidgets::BeginCardRow(2);
        ImGui::PushID(i);
        CyberWidgets::BeginCard(style.title, twoColumns ? CyberWidgets::CardRowHalfWidth() : cardWidth);
        const bool isActive = CyberTheme::GetThemeMode() == style.mode &&
            CyberTheme::GetAccentPreset() == style.accent;
        if (isActive) CyberWidgets::Badge("EM USO", CyberWidgets::TextTone::Success);
        // A compact, data-only preview of the pack.  It mirrors the launcher
        // composition (sidebar, cards and primary action) without loading an
        // external image or allocating textures per frame.
        const auto& accents = CyberTheme::GetAccentPresets();
        const int accentIndex = static_cast<int>(style.accent);
        const ImVec4 accent = accentIndex >= 0 && accentIndex < static_cast<int>(accents.size())
            ? accents[accentIndex].base : CyberTheme::Colors.Gold;
        const ImU32 accentU32 = ImGui::ColorConvertFloat4ToU32(accent);
        const ImU32 accentSoft = CyberTheme::WithAlpha(accent, 0.24f);
        const ImVec2 previewMin = ImGui::GetCursorScreenPos();
        const float previewWidth = ImGui::GetContentRegionAvail().x;
        // Prefer a taller preview over a stretched horizontal strip so each
        // theme remains recognisable in the two-column catalogue.
        const float previewHeight = S(96.f);
        const ImVec2 previewMax(previewMin.x + previewWidth, previewMin.y + previewHeight);
        ImDrawList* previewDraw = ImGui::GetWindowDrawList();
        previewDraw->AddRectFilled(previewMin, previewMax, IM_COL32(9, 11, 16, 255), S(7.f));
        previewDraw->AddRect(previewMin, previewMax, accentSoft, S(7.f), 0, S(1.f));
        const float sidebar = (std::min)(S(62.f), previewWidth * 0.24f);
        previewDraw->AddRectFilled(previewMin, ImVec2(previewMin.x + sidebar, previewMax.y), IM_COL32(15, 18, 25, 255), S(7.f));
        previewDraw->AddRectFilled(ImVec2(previewMin.x + S(9.f), previewMin.y + S(12.f)),
                                   ImVec2(previewMin.x + sidebar - S(9.f), previewMin.y + S(19.f)), accentU32, S(2.f));
        for (int item = 0; item < 3; ++item) {
            const float y = previewMin.y + S(31.f + item * 16.f);
            previewDraw->AddRectFilled(ImVec2(previewMin.x + S(10.f), y),
                                       ImVec2(previewMin.x + sidebar - S(14.f), y + S(5.f)),
                                       item == 0 ? accentSoft : IM_COL32(55, 60, 70, 180), S(2.f));
        }
        const float contentLeft = previewMin.x + sidebar + S(11.f);
        previewDraw->AddRectFilled(ImVec2(contentLeft, previewMin.y + S(12.f)),
                                   ImVec2(previewMax.x - S(12.f), previewMin.y + S(18.f)), IM_COL32(215, 219, 227, 230), S(2.f));
        previewDraw->AddRectFilled(ImVec2(contentLeft, previewMin.y + S(29.f)),
                                   ImVec2(previewMax.x - S(12.f), previewMin.y + S(58.f)), IM_COL32(23, 27, 36, 255), S(4.f));
        previewDraw->AddRectFilled(ImVec2(contentLeft, previewMin.y + S(65.f)),
                                   ImVec2(contentLeft + S(72.f), previewMin.y + S(79.f)), accentU32, S(4.f));
        ImGui::Dummy(ImVec2(previewWidth, previewHeight + S(9.f)));
        CyberWidgets::TextLine(style.description, CyberWidgets::TextTone::Secondary);
        ImGui::Dummy(ImVec2(0.f, S(6.f)));
        if (!isActive && CyberWidgets::Button("USAR ESTE ESTILO",
                                 CyberWidgets::ButtonStyle::Primary,
                                 ImVec2(S(145.f), S(28.f)))) {
            CyberTheme::SetThemeMode(style.mode);
            CyberTheme::SetAccentPreset(style.accent);
            std::string saveError;
            if (app_settings::SaveGlobal(&saveError))
                CyberWidgets::Notify("Estilo aplicado globalmente e guardado.", CyberWidgets::ToastType::Success);
            else
                CyberWidgets::Notify(saveError.empty() ? "O estilo foi aplicado, mas não foi possível guardar." : saveError.c_str(),
                                     CyberWidgets::ToastType::Warning);
        }
        CyberWidgets::EndCard();
        ImGui::PopID();
        if (twoColumns) {
            if ((i % 2) == 0) {
                if (i + 1 == static_cast<int>(std::size(styles))) {
                    CyberWidgets::NextCardColumn();
                    ImGui::Dummy(ImVec2(0.f, 0.f));
                    CyberWidgets::EndCardRow();
                    ImGui::Dummy(ImVec2(0.f, gap));
                } else {
                    CyberWidgets::NextCardColumn();
                }
            } else {
                CyberWidgets::EndCardRow();
                if (i + 1 < static_cast<int>(std::size(styles)))
                    ImGui::Dummy(ImVec2(0.f, gap));
            }
        } else if (i + 1 < static_cast<int>(std::size(styles))) {
            ImGui::Dummy(ImVec2(0.f, gap));
        }
    }

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard("PRESETS", cardWidth);
    CyberWidgets::Badge("EM PREPARAÇÃO", CyberWidgets::TextTone::Info);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::TextLine("Configs prontas por jogo: limpo, detalhado ou leve para PCs menos potentes.",
                           CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Aplicação com um clique; podes sempre ajustar e guardar a tua versão.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard("OS MEUS ITENS", 0.f);
    CyberWidgets::HealthRow("Biblioteca", "Ainda não existem itens desbloqueados", CyberWidgets::HealthStatus::Warning);
    CyberWidgets::TextLine("Quando o catálogo estiver disponível, os teus temas e presets aparecerão aqui.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard("CRIADORES DA COMUNIDADE", 0.f);
    CyberWidgets::TextLine("Podes enviar ideias e temas para revisão antes de serem publicados no Marketplace.",
                           CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine("Depois de baixar um pack .ogtheme do catálogo, importa-o aqui para o usar globalmente.",
                           CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::Button("IMPORTAR E USAR PACK", CyberWidgets::ButtonStyle::Secondary,
                             ImVec2(S(205.f), S(32.f)))) {
        std::filesystem::path path;
        if (MarketplaceThemeFileDialog(path)) {
            std::string error;
            if (CyberTheme::ImportTheme(path, &error)) {
                std::string saveError;
                if (app_settings::SaveGlobal(&saveError))
                    CyberWidgets::Notify("Pack aplicado globalmente e guardado.", CyberWidgets::ToastType::Success);
                else
                    CyberWidgets::Notify(saveError.empty() ? "Pack aplicado, mas não foi possível guardar." : saveError.c_str(),
                                         CyberWidgets::ToastType::Warning);
            }
            else
                CyberWidgets::Notify(error.empty() ? "Não foi possível validar este pack de tema." : error.c_str(),
                                     CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::HealthRow("Catálogo online", "A aguardar serviço seguro de revisão e distribuição", CyberWidgets::HealthStatus::Warning);
    CyberWidgets::EndCard();

    EndControlPage();
}

const char* DiagnosticsGameName(GameId id) {
    const GameDefinition* game = FindGame(id);
    return game ? game->name : "—";
}

GameId DiagnosticsGameId() {
    if (g_detail_game != GameId::None) return g_detail_game;
    if (g_selected != GameId::None) return g_selected;
    return LastPlayedGame();
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

std::string BuildDiagnosticsText() {
    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    const auto system = OmniGhost::Platform::CaptureSystemInfo();
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
    out << "Windows: " << OmniGhost::Platform::FormatSystemInfo(system) << "\n";
    out << Loc::Tr("launcher.renderer") << ": "
        << (rendererOk ? Loc::Tr("launcher.renderer_ready") : Loc::Tr("launcher.renderer_unavailable")) << "\n";
    out << "DMA: " << (dma.deviceOpen ? Loc::Tr("launcher.connected")
        : (dma.deviceDetected ? "detetado" : "não detetado"))
        << " | processo=" << (dma.processInitialized ? Loc::Tr("launcher.attached") : Loc::Tr("launcher.not_attached"))
        << " | dependências=" << (dma.dependencyIntegrityOk ? "OK" : Loc::Tr("launcher.attention")) << "\n";
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

void DrawDiagnostics(ImVec2 display) {
    BeginControlPage("##launcher_diagnostics", display);
    DrawPageHeading(Loc::Tr("launcher.diagnostics"), Loc::Tr("launcher.diagnostics.subtitle"));

    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    const auto system = OmniGhost::Platform::CaptureSystemInfo();
    const GameId gameId = DiagnosticsGameId();
    const GameDefinition* selectedGame = FindGame(gameId);
    const bool rendererOk = g_overlay_instance && g_overlay_instance->device;
    const bool offsetsOk = selectedGame && HasRequiredOffsets(*selectedGame);
    const std::filesystem::path logPath = OmniGhost::SessionLog::CurrentLogPath();
    const std::string logPathUtf8 = OmniGhost::Platform::WideToUtf8(logPath.wstring());
    const std::string windows = OmniGhost::Platform::FormatSystemInfo(system);
    const std::string version = UiFormat::Version(OmniGhost::Version);
    const std::string build = std::string(OmniGhost::BuildInfo::BuildId) + " · " +
        OmniGhost::BuildInfo::Configuration + " · " + OmniGhost::BuildInfo::Architecture;

    const auto updaterHealth = update.status == OmniGhost::Update::Status::Error
        ? CyberWidgets::HealthStatus::Error
        : ((update.status == OmniGhost::Update::Status::Checking ||
            update.status == OmniGhost::Update::Status::Downloading ||
            update.status == OmniGhost::Update::Status::Installing)
            ? CyberWidgets::HealthStatus::Warning : CyberWidgets::HealthStatus::Ok);

    CyberWidgets::BeginCard(Loc::Tr("launcher.system_health"), 0.f);
    CyberWidgets::HealthRow(Loc::Tr("launcher.version"), version.c_str(), CyberWidgets::HealthStatus::Ok);
    CyberWidgets::HealthRow(Loc::Tr("launcher.build"), build.c_str(), BuildHealthStatus());
    CyberWidgets::HealthRow(Loc::Tr("launcher.windows"), windows.c_str(), system.windowsBuild ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(Loc::Tr("launcher.renderer"),
                            rendererOk ? Loc::Tr("launcher.renderer_ready") : Loc::Tr("launcher.renderer_unavailable"),
                            rendererOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::HealthRow("DMA", dma.deviceOpen ? Loc::Tr("launcher.connected")
                                                    : (dma.deviceDetected ? Loc::Tr("launcher.device_detected") : Loc::Tr("launcher.device_not_detected")),
                            dma.deviceOpen ? (dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning)
                                           : CyberWidgets::HealthStatus::Warning);
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
                                     CyberWidgets::TextTone::Error, lastError.c_str());

    ImGui::Dummy(ImVec2(0, S(8.f)));
    const std::string diagnostics = BuildDiagnosticsText();
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
    if (ImGui::CollapsingHeader("DETALHES TÉCNICOS", ImGuiTreeNodeFlags_None)) {
    CyberWidgets::BeginCard(Loc::Tr("launcher.build_metadata"), 0.f);
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.build_id"), OmniGhost::BuildInfo::BuildId);
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.commit_source"), OmniGhost::BuildInfo::CommitId);
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.source_provenance"), OmniGhost::BuildInfo::SourceProvenance);
    const std::string buildTime = UiFormat::DateTime(OmniGhost::BuildInfo::BuildUtc);
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.build_utc"), buildTime.c_str());
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.msvc_tools"), OmniGhost::BuildInfo::ToolchainVersion);
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.compiler"), OmniGhost::BuildInfo::CompilerVersion);
    CyberWidgets::KeyValueRow("Windows SDK", OmniGhost::BuildInfo::WindowsSdkVersion);
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.release_channel"), LocalizedChannelLabel(OmniGhost::BuildInfo::ReleaseChannel));
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.reproducible"),
                              OmniGhost::BuildInfo::Reproducible ? Loc::Tr("launcher.yes") : Loc::Tr("launcher.no"));
    CyberWidgets::EndCard();
    }

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
        OmniGhost::Update::UpdateService::Instance().PrepareRepairAsync();
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0, S(14.f)));
    CyberWidgets::SectionTitle(Loc::Tr("launcher.games"));
    ImGui::Dummy(ImVec2(0, S(6.f)));
    for (GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        const GameDefinition& game = *runtime.definition;
        ImGui::PushID(static_cast<int>(game.launch_id));
        const bool focused = g_detail_game == game.launch_id;
        if (focused)
            ImGui::PushStyleColor(ImGuiCol_ChildBg, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.05f));
        CyberWidgets::BeginCard(game.name, 0.f);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.state"), CardStateLabel(runtime.state));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.runtime_local"), runtime.installed ? Loc::Tr("launcher.installed") : Loc::Tr("launcher.missing"));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.offsets"), HasRequiredOffsets(game) ? Loc::Tr("launcher.status.ready") : Loc::Tr("launcher.missing"));
        const GameHistory history = GetGameHistory(game.launch_id);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.last_session"), SessionResultDisplay(history.lastResult));
        if (!history.detail.empty()) CyberWidgets::TextLine(history.detail.c_str(), CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        if (focused) ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, S(8.f)));
    }
    EndControlPage();
}

const char* SettingsVkName(int vk) {
    if (vk <= 0) return "Nenhuma";
    switch (vk) {
    case VK_INSERT: return "Inserir";
    case VK_HOME: return "Início";
    case VK_END: return "Fim";
    case VK_DELETE: return "Eliminar";
    case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3"; case VK_F4: return "F4";
    case VK_F5: return "F5"; case VK_F6: return "F6"; case VK_F7: return "F7"; case VK_F8: return "F8";
    case VK_F9: return "F9"; case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
    case VK_LBUTTON: return "Rato 1"; case VK_RBUTTON: return "Rato 2";
    case VK_MBUTTON: return "Rato 3"; case VK_XBUTTON1: return "Rato 4"; case VK_XBUTTON2: return "Rato 5";
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
    const char* button = active ? "Pressiona uma tecla..." : SettingsVkName(*vk);
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
    const SettingsNavigationItem navigation[] = {
        {app_settings::SettingsPage::General, Loc::Tr("launcher.settings.general"), "GERAL"},
        {app_settings::SettingsPage::Appearance, Loc::Tr("launcher.settings.appearance"), "APARÊNCIA"},
        {app_settings::SettingsPage::Overlay, Loc::Tr("launcher.settings.overlay"), "APARÊNCIA"},
        {app_settings::SettingsPage::Input, Loc::Tr("launcher.settings.input"), "CONTROLOS"},
        {app_settings::SettingsPage::Licenses, Loc::Tr("launcher.settings.licenses"), "SISTEMA"},
        {app_settings::SettingsPage::Language, Loc::Tr("launcher.language"), "SISTEMA"},
        {app_settings::SettingsPage::Diagnostics, Loc::Tr("launcher.diagnostics"), "SISTEMA"},
        {app_settings::SettingsPage::About, Loc::Tr("launcher.settings.about"), "SISTEMA"}
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
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.68f)), "%s", item.group);
            ImGui::Dummy(ImVec2(0.f, S(4.f)));
            previousGroup = item.group;
        }
        const bool selected = page == item.page;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Button, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.16f));
        if (CyberWidgets::CyberButton(item.label, ImVec2(navWidth - S(8.f), S(36.f))))
            page = item.page;
        if (selected) ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.f, S(3.f)));
    }
    ImGui::EndChild();

    ImGui::SameLine(0.f, S(16.f));
    ImGui::BeginChild("##settings_content", ImVec2(0.f, 0.f), false);
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
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.reduce_motion"), &app_settings::config.reduce_motion);
        const ImU32 accentBefore = app_settings::config.color_primary;
        CyberWidgets::ColorEditU32(Loc::Tr("launcher.accent"), &app_settings::config.color_primary);
        if (accentBefore != app_settings::config.color_primary) { changed = true; CyberTheme::ApplyTheme(); }
        CyberWidgets::EndCard();

        CyberWidgets::CardGap();
        CyberWidgets::BeginCard("Efeitos e desempenho", 0.f);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.auto_performance"), &app_settings::config.auto_performance);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.manual_performance"), &app_settings::config.performance_mode);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.particles"), &app_settings::config.particles);
        changed |= CyberWidgets::ToggleSwitch(Loc::Tr("launcher.snow_effect"), &app_settings::config.snow_effect);
        CyberWidgets::KeyValueRow("Chuva digital", "Subtil · textura de fundo");
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Overlay: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.overlay"), S(760.f));
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
        CyberWidgets::BeginCard("Indicadores da sobreposição", S(760.f));
        const ImU32 fpsColorBefore = app_settings::config.color_fps;
        CyberWidgets::ColorEditU32("Cor do contador FPS", &app_settings::config.color_fps);
        if (fpsColorBefore != app_settings::config.color_fps) changed = true;
        CyberWidgets::KeyValueRow("Composição", app_settings::config.vsync ? "Sincronizada com o monitor" : "Sem sincronização vertical");
        CyberWidgets::KeyValueRow("Monitor de destino", monitor == 0 ? "Automático" : monitorLabels[monitor].c_str());
        CyberWidgets::TextLine("Estas opções afetam apenas a apresentação do menu e dos indicadores locais.", CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Input: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.input"), 0.f);
        changed |= SettingsHotkey(Loc::Tr("launcher.menu"), &app_settings::config.menu_bind);
        changed |= SettingsHotkey("ESP", &app_settings::config.hotkey_esp);
        changed |= SettingsHotkey(Loc::Tr("launcher.aim"), &app_settings::config.hotkey_aim);
        changed |= SettingsHotkey(Loc::Tr("launcher.vehicle"), &app_settings::config.hotkey_vehicle);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::Devices:
        InputDevicesCard::Draw();
        break;
    case app_settings::SettingsPage::Licenses: {
        const auto license = OmniGhost::Licensing::GetSnapshot();
        const bool remote = license.remoteServiceConfigured;
        const bool accessActive = remote ? license.remoteAuthenticated : license.localLicenseValid;
        std::size_t gameCount = 0;
        const GameDefinition* gameList = Games(gameCount);
        std::size_t integratedGames = 0;
        for (std::size_t index = 0; index < gameCount; ++index)
            if (!gameList[index].coming_soon) ++integratedGames;

        // Compact two-panel command centre: related information stays grouped
        // instead of stretching a pair of values over the full launcher width.
        CyberWidgets::BeginCardRow(2);
        const float licenseColumnWidth = CyberWidgets::CardRowHalfWidth();
        CyberWidgets::BeginCard("Estado da licença", licenseColumnWidth);
        CyberWidgets::Badge(
            accessActive ? (remote ? "SESSÃO KEYAUTH ATIVA" : "ACESSO LOCAL ATIVO") : "AÇÃO NECESSÁRIA",
            accessActive ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
        ImGui::Dummy(ImVec2(0, S(6.f)));
        CyberWidgets::KeyValueRow("Modo", remote ? Loc::Tr("launcher.remote_auth") : Loc::Tr("launcher.local_compat"));
        CyberWidgets::KeyValueRow("Estado", remote
            ? (license.remoteAuthenticated ? Loc::Tr("launcher.authenticated") : Loc::Tr("launcher.no_session"))
            : OmniGhost::Licensing::StateLabel(license.localState));
        CyberWidgets::KeyValueRow("Armazenamento", remote ? Loc::Tr("launcher.remote_session") : (license.protectedStorage
            ? Loc::Tr("launcher.protected_storage") : Loc::Tr("launcher.unprotected_missing")));
        std::size_t grantedGames = 0;
        for (std::size_t index = 0; index < gameCount; ++index) {
            if (!gameList[index].coming_soon && OmniGhost::Licensing::HasGameAccess(gameList[index].id))
                ++grantedGames;
        }
        const std::string coverage = accessActive
            ? std::to_string(grantedGames) + " de " + std::to_string(integratedGames) + " " + std::string(Loc::Tr("launcher.games")) + " " + std::string(Loc::Tr("launcher.coverage"))
            : std::string(Loc::Tr("launcher.no_key")) + " " + std::string(Loc::Tr("launcher.games")) + " " + std::string(Loc::Tr("launcher.coverage"));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.coverage"), coverage.c_str());
        CyberWidgets::TextLine(remote
            ? "A sessão KeyAuth controla o acesso aos jogos desta instalação."
            : "A conta e a licença são independentes. O acesso é revisto imediatamente após cada alteração.",
            CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();

        CyberWidgets::NextCardColumn();
        CyberWidgets::BeginCard("Acessos por produto", licenseColumnWidth);
        for (std::size_t index = 0; index < gameCount; ++index) {
            const GameDefinition& game = gameList[index];
            if (game.coming_soon) continue;
            const bool granted = OmniGhost::Licensing::HasGameAccess(game.id);
            const std::string duration = granted ? OmniGhost::Licensing::GameAccessDuration(game.id) : std::string{};
            const std::string status = granted
                ? (duration.empty() ? "Acesso ativo" : "Acesso ativo · " + duration)
                : "Sem acesso";
            CyberWidgets::HealthRow(game.name,
                status.c_str(),
                granted ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
        }
        CyberWidgets::EndCard();
        CyberWidgets::EndCardRow();

        CyberWidgets::BeginCard(remote ? "Licenciamento KeyAuth" : "Ativação local", 0.f);
        if (remote) {
            CyberWidgets::InlineMessage(license.remoteAuthenticated
                ? "A sessão KeyAuth está ativa. Cada subscrição da conta desbloqueia apenas os produtos respetivos."
                : "Inicia sessão, cria uma conta ou ativa uma key na página de autenticação para desbloquear os jogos.",
                license.remoteAuthenticated ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
            if (license.remoteAuthenticated && !license.remoteEntitlements.empty()) {
                std::string plans;
                for (const auto& entitlement : license.remoteEntitlements) {
                    if (!plans.empty()) plans += ", ";
                    plans += entitlement.name;
                }
                CyberWidgets::KeyValueRow("Planos KeyAuth", plans.c_str());
            }
            if (license.remoteAuthenticated) {
                ImGui::Dummy(ImVec2(0, S(8.f)));
                CyberWidgets::TextLine("Adicionar uma licença à conta", CyberWidgets::TextTone::Primary);
                const bool enter = CyberWidgets::InputField("##settings_remote_license_key", g_license_input,
                    sizeof(g_license_input), "Nova licença", ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue,
                    -1.f, true);
                ImGui::BeginDisabled(g_license_operation_pending);
                if (CyberWidgets::GoldButton(
                        g_license_operation_pending ? "A processar...##upgrade_license" : "Adicionar licença",
                        ImVec2(S(170.f), S(34.f))) || (enter && !g_license_operation_pending)) {
                    std::string key(g_license_input);
                    SecureZeroMemory(g_license_input, sizeof(g_license_input));
                    StartRemoteLicenseUpgrade(std::move(key));
                }
                ImGui::EndDisabled();
                CyberWidgets::TextLine(
                    "A key é associada à conta KeyAuth atual; os acessos existentes mantêm-se.",
                    CyberWidgets::TextTone::Secondary);
            }
        } else if (!license.localLicenseValid) {
            CyberWidgets::TextLine(
                "Introduz uma licença válida ou cria temporariamente o acesso local desta instalação.",
                CyberWidgets::TextTone::Secondary);
            ImGui::Dummy(ImVec2(0, S(8.f)));
            const bool enter = CyberWidgets::InputField("##settings_license_key", g_license_input,
                sizeof(g_license_input), "Licença", ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue,
                -1.f, true);
            ImGui::BeginDisabled(g_license_operation_pending);
            if (CyberWidgets::GoldButton(
                    g_license_operation_pending ? "A processar...##validate_license" : "Validar licença",
                    ImVec2(S(155.f), S(34.f))) || (enter && !g_license_operation_pending)) {
                std::string key(g_license_input);
                SecureZeroMemory(g_license_input, sizeof(g_license_input));
                StartLicenseOperation(false, std::move(key));
            }
            ImGui::SameLine(0.f, S(10.f));
            if (CyberWidgets::GhostButton("Criar Licença##temporary_license", ImVec2(S(155.f), S(34.f)))) {
                StartLicenseOperation(true);
            }
            ImGui::EndDisabled();
            CyberWidgets::TextLine(
                "Ferramenta temporária do proprietário. Remover antes da distribuição final a clientes.",
                CyberWidgets::TextTone::Warning);
        } else {
            CyberWidgets::InlineMessage(
                "Licença local ativa. Todos os jogos integrados estão autorizados nesta instalação.",
                CyberWidgets::TextTone::Success);
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
        for (int i = 0; i < languageCount; ++i) languages.push_back(Loc::LanguageName(i));
        int language = static_cast<int>(app_settings::config.language);
        if (CyberWidgets::Combo(Loc::Tr("launcher.language"), &language, languages.data(), languageCount)) { app_settings::config.language = static_cast<app_settings::Language>(language); changed = true; }
        CyberWidgets::KeyValueRow("Idioma atual", Loc::LanguageName(language));
        CyberWidgets::KeyValueRow("Idiomas disponíveis", std::to_string(languageCount).c_str());
        CyberWidgets::EndCard();
        CyberWidgets::CardGap();
        CyberWidgets::BeginCard("Cobertura da interface", 0.f);
        CyberWidgets::HealthRow("Launcher e biblioteca", Loc::Tr("launcher.device_detected"), CyberWidgets::HealthStatus::Ok);
        CyberWidgets::HealthRow("Definições e diagnóstico", Loc::Tr("launcher.device_detected"), CyberWidgets::HealthStatus::Ok);
        CyberWidgets::TextLine("Os nomes próprios de jogos, dispositivos e tecnologias mantêm a designação oficial.", CyberWidgets::TextTone::Secondary);
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
        if (CyberWidgets::CyberButton("Criar diagnóstico.txt", ImVec2(S(205.f), S(33.f)))) {
            const std::filesystem::path output = OmniGhost::Paths::Executable().parent_path() / L"diagnostico.txt";
            std::ofstream file(output, std::ios::binary | std::ios::trunc);
            const std::string report = BuildDiagnosticsText();
            file.write(report.data(), static_cast<std::streamsize>(report.size()));
            file.flush();
            if (file.good()) {
                PushToast("diagnostico.txt criado junto ao OmniGhost.exe", C_GREEN(), ToastAction::None, nullptr);
                ShellExecuteW(nullptr, L"open", output.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else {
                PushToast("Não foi possível criar diagnostico.txt", C_RED(), ToastAction::None, nullptr);
            }
        }
        CyberWidgets::HealthRow("Sistema operativo", Loc::Tr("launcher.create_diagnostics"), CyberWidgets::HealthStatus::Ok);
        CyberWidgets::HealthRow("Renderizador e janela", Loc::Tr("launcher.create_diagnostics"), CyberWidgets::HealthStatus::Ok);
        CyberWidgets::HealthRow("DMA, input e offsets", Loc::Tr("launcher.create_diagnostics"), CyberWidgets::HealthStatus::Ok);
        CyberWidgets::HealthRow("Atualizador, build e sessão", Loc::Tr("launcher.create_diagnostics"), CyberWidgets::HealthStatus::Ok);
        CyberWidgets::EndCard();
        break;
    }
    case app_settings::SettingsPage::About: {
        CyberWidgets::BeginCard(Loc::Tr("launcher.settings.about"), 0.f);
        const std::string aboutVersion = UiFormat::Version(OmniGhost::Version);
        CyberWidgets::KeyValueRow("OmniGhost", aboutVersion.c_str());
        const bool localBuild = std::string_view(OmniGhost::BuildInfo::BuildId).find("local-source") != std::string_view::npos;
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.build"), localBuild
            ? "Compilação local (não publicada)" : OmniGhost::BuildInfo::BuildId);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.channel"), LocalizedChannelLabel(OmniGhost::BuildInfo::ReleaseChannel));
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.architecture"), OmniGhost::BuildInfo::Architecture);
        CyberWidgets::KeyValueRow(Loc::Tr("launcher.commit_source"), localBuild
            ? "Origem local de desenvolvimento" : OmniGhost::BuildInfo::CommitId);
        CyberWidgets::KeyValueRow("MSVC", OmniGhost::BuildInfo::ToolchainVersion);
        CyberWidgets::KeyValueRow("Windows SDK", OmniGhost::BuildInfo::WindowsSdkVersion);
        CyberWidgets::TextLine(Loc::Tr("launcher.about_metadata_help"), CyberWidgets::TextTone::Secondary);
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
    const auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const auto license = OmniGhost::Licensing::GetSnapshot();

    const bool remote = license.remoteServiceConfigured;
    const std::string identity = remote ? license.remoteUsername : MaskEmail(auth.CurrentEmail());
    const bool accessActive = remote ? license.remoteAuthenticated : license.localLicenseValid;
    CyberWidgets::BeginCardRow(2);
    const float columnWidth = CyberWidgets::CardRowHalfWidth();
    CyberWidgets::BeginCard(Loc::Tr("launcher.profile"), columnWidth);
    CyberWidgets::Badge("SESSÃO PRIVADA", CyberWidgets::TextTone::Success);
    ImGui::Dummy(ImVec2(0, S(7.f)));
    CyberWidgets::KeyValueRow(remote ? "Utilizador" : Loc::Tr("launcher.email"), identity.empty() ? "—" : identity.c_str());
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.authentication"), remote ? "KeyAuth" : Loc::Tr("launcher.local_dpapi"));
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.remember_me"), remote ? "Sessão atual" : (auth.RememberMe() ? Loc::Tr("launcher.enabled") : Loc::Tr("launcher.disabled")));
    CyberWidgets::TextLine(remote ? "As credenciais não são guardadas pelo launcher." : "O endereço completo não é exposto nesta interface.", CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard(Loc::Tr("launcher.license"), columnWidth);
    CyberWidgets::Badge(accessActive ? "ACESSO ATIVO" : "AÇÃO NECESSÁRIA",
        accessActive ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0, S(7.f)));
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.state"), remote
        ? (license.remoteAuthenticated ? "Sessão KeyAuth válida" : "Sem sessão KeyAuth")
        : OmniGhost::Licensing::StateLabel(license.localState));
    CyberWidgets::KeyValueRow(Loc::Tr("launcher.storage"), remote ? "KeyAuth" : (license.protectedStorage ? "Windows DPAPI" : Loc::Tr("launcher.unprotected_missing")));
    CyberWidgets::TextLine(remote ? "O acesso aos jogos depende da sessão KeyAuth atual." : Loc::Tr("launcher.license_separate"), CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    ImGui::Dummy(ImVec2(0, S(14.f)));
    CyberWidgets::BeginCard("AÇÕES DA CONTA", 0.f);
    CyberWidgets::TextLine("Ações locais. Nenhuma credencial ou chave é exposta nesta página.", CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0, S(8.f)));
    if (CyberWidgets::CyberButton(Loc::Tr("launcher.refresh_state"), ImVec2(S(140.f), S(32.f))))
        OmniGhost::Licensing::Refresh();
    ImGui::SameLine();
    if (CyberWidgets::GoldButton("Gerir licenças", ImVec2(S(140.f), S(32.f)))) {
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
    CyberWidgets::TextLine("SESSÃO", CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::DangerButton(Loc::Tr("launcher.logout"), ImVec2(S(160.f), S(34.f)))) {
        OmniGhost::Licensing::LogoutRemote();
        OmniGhost::Auth::LocalAuthService::Instance().Logout(true);
        g_logout_requested = true;
    }
    EndControlPage();
}
