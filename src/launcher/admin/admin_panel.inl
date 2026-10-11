// Included by game_select_pages.inl inside Launcher's private namespace.
// Remote administration deliberately stays server-side: this panel exposes
// trustworthy local diagnostics and links to the official control planes, but
// never embeds KeyAuth Seller credentials in the distributable executable.

namespace AdminPanel {

enum class Section : int {
    Overview,
    Users,
    Licenses,
    Analytics,
    Games,
    Builds,
    Security,
    Developer,
    Support,
};

struct UiState {
    Section section{Section::Overview};
    char userFilter[96]{};
    char licenseFilter[96]{};
    bool showImGuiMetrics{};
};

UiState& State() {
    static UiState state;
    return state;
}

bool OpenExternal(const wchar_t* url, const char* failure) {
    const HINSTANCE result = ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32)
        return true;
    CyberWidgets::NotifyAdmin(failure, CyberWidgets::ToastType::Error);
    return false;
}

const char* SectionTitle(Section section) {
    switch (section) {
    case Section::Overview: return "Visão geral";
    case Section::Users: return "Utilizadores";
    case Section::Licenses: return "Licenças";
    case Section::Analytics: return "Analytics";
    case Section::Games: return "Jogos e offsets";
    case Section::Builds: return "Builds e releases";
    case Section::Security: return "Segurança e auditoria";
    case Section::Developer: return "Ferramentas";
    case Section::Support: return "Suporte e operações";
    }
    return "Admin";
}

void DrawSectionNavigation() {
    static constexpr struct Entry { Section section; const char* label; } entries[] = {
        {Section::Overview, "VISÃO GERAL"},
        {Section::Users, "UTILIZADORES"},
        {Section::Licenses, "LICENÇAS"},
        {Section::Analytics, "ANALYTICS"},
        {Section::Games, "JOGOS / OFFSETS"},
        {Section::Builds, "BUILDS / RELEASES"},
        {Section::Security, "SEGURANÇA"},
        {Section::Developer, "FERRAMENTAS"},
        {Section::Support, "SUPORTE"},
    };

    UiState& state = State();
    CyberWidgets::Badge("ADMIN CONTROL", CyberWidgets::TextTone::Accent);
    ImGui::Dummy(ImVec2(0.f, S(9.f)));
    for (const Entry& entry : entries) {
        const bool selected = state.section == entry.section;
        ImGui::PushID(static_cast<int>(entry.section));
        if (selected) {
            const ImVec4 selectedColor = CyberTheme::Mix(CyberTheme::Colors.Card, CyberTheme::Colors.Gold, .16f);
            ImGui::PushStyleColor(ImGuiCol_Header, selectedColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selectedColor);
        }
        if (ImGui::Selectable(entry.label, selected, 0, ImVec2(-1.f, S(34.f))))
            state.section = entry.section;
        if (selected)
            ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
}

void DrawRemoteBoundary(const char* title, const char* description, bool keyAuth = true) {
    CyberWidgets::BeginCard(title, 0.f);
    CyberWidgets::Badge("SERVIÇO REMOTO NECESSÁRIO", CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::TextLine(description, CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine(
        "O cliente não guarda Seller API keys, tokens de CI ou credenciais administrativas.",
        CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0.f, S(9.f)));
    if (keyAuth && CyberWidgets::GoldButton("ABRIR KEYAUTH", ImVec2(S(160.f), S(34.f))))
        OpenExternal(L"https://keyauth.cc/app/", "Não foi possível abrir o painel KeyAuth.");
    CyberWidgets::EndCard();
}

void DrawAdminOverview() {
    const auto license = OmniGhost::Licensing::GetSnapshot();
    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    std::size_t installed = 0;
    std::size_t running = 0;
    std::uint64_t sessions = 0;
    for (const GameRuntime& game : g_games) {
        installed += game.installed ? 1u : 0u;
        running += game.state == CardState::Running ? 1u : 0u;
        if (game.definition)
            sessions += GetGameHistory(game.definition->launch_id).sessionCount;
    }

    const float gap = S(12.f);
    const float available = ImGui::GetContentRegionAvail().x;
    const bool two = available >= S(720.f);
    const float width = two ? (available - gap) * .5f : available;

    CyberWidgets::BeginCard("SESSÃO ADMINISTRATIVA", width);
    CyberWidgets::Badge("AUTORIZADA", CyberWidgets::TextTone::Success);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::KeyValueRow("Operador", license.remoteUsername.empty() ? "—" : license.remoteUsername.c_str());
    CyberWidgets::KeyValueRow("Subscrição", "Admin");
    CyberWidgets::KeyValueRow("Permissões de produto", "Todos os jogos");
    CyberWidgets::HealthRow("Sessão KeyAuth", license.remoteAuthenticated ? "Autenticada" : "Inválida",
        license.remoteAuthenticated ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::EndCard();

    if (two) ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard("ESTADO OPERACIONAL", width);
    CyberWidgets::HealthRow("DMA", dma.deviceOpen ? "Ligado" : (dma.deviceDetected ? "Detetado" : "Indisponível"),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow("Integridade DMA", dma.dependencyIntegrityOk ? "Validada" : "Requer atenção",
        dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::HealthRow("Atualizações", UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error
            ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard("RESUMO LOCAL", 0.f);
    CyberWidgets::KeyValueRow("Jogos instalados", std::to_string(installed).c_str());
    CyberWidgets::KeyValueRow("Jogos ativos", std::to_string(running).c_str());
    CyberWidgets::KeyValueRow("Sessões registadas neste dispositivo", std::to_string(sessions).c_str());
    CyberWidgets::KeyValueRow("Build", OmniGhost::BuildInfo::BuildId);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard("AÇÕES RÁPIDAS", 0.f);
    if (CyberWidgets::Button("KEYAUTH", CyberWidgets::ButtonStyle::Primary, ImVec2(S(130.f), S(34.f))))
        OpenExternal(L"https://keyauth.cc/app/", "Não foi possível abrir o painel KeyAuth.");
    ImGui::SameLine();
    if (CyberWidgets::Button("GITHUB ACTIONS", CyberWidgets::ButtonStyle::Secondary, ImVec2(S(175.f), S(34.f))))
        OpenExternal(L"https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/actions",
                     "Não foi possível abrir o GitHub Actions.");
    ImGui::SameLine();
    if (CyberWidgets::Button("VERIFICAR UPDATE", CyberWidgets::ButtonStyle::Ghost, ImVec2(S(175.f), S(34.f))))
        OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
    CyberWidgets::EndCard();
}

void DrawAdminUsers() {
    UiState& state = State();
    CyberWidgets::BeginCard("PESQUISA DE UTILIZADORES", 0.f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##admin_user_filter", "Username, email ou HWID...",
                             state.userFilter, sizeof(state.userFilter));
    CyberWidgets::TextLine("A sessão atual é mostrada localmente. A listagem global permanece no KeyAuth.",
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    const auto license = OmniGhost::Licensing::GetSnapshot();
    CyberWidgets::BeginCard("OPERADOR ATUAL", 0.f);
    CyberWidgets::KeyValueRow("Username", license.remoteUsername.empty() ? "—" : license.remoteUsername.c_str());
    CyberWidgets::KeyValueRow("Perfil", "Administrador");
    CyberWidgets::HealthRow("Autenticação", license.remoteAuthenticated ? "Ativa" : "Expirada",
        license.remoteAuthenticated ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary("GESTÃO DE CONTAS",
        "Listar utilizadores, banir, remover bans, repor HWID, ações em massa e impersonation exigem autorização server-side e audit log.");
}

void DrawAdminLicenses() {
    UiState& state = State();
    const auto license = OmniGhost::Licensing::GetSnapshot();
    CyberWidgets::BeginCard("SUBSCRIÇÕES DA SESSÃO", 0.f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##admin_license_filter", "Filtrar subscrições visíveis...",
                             state.licenseFilter, sizeof(state.licenseFilter));
    bool shown = false;
    std::string filter = state.licenseFilter;
    std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& entitlement : license.remoteEntitlements) {
        std::string name = entitlement.name;
        std::string normalized = name;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (!filter.empty() && normalized.find(filter) == std::string::npos)
            continue;
        shown = true;
        const std::string remaining = entitlement.remaining.empty() ? "Ativa" : entitlement.remaining;
        CyberWidgets::HealthRow(name.c_str(), remaining.c_str(), CyberWidgets::HealthStatus::Ok);
    }
    if (!shown)
        CyberWidgets::EmptyState("SEM RESULTADOS", "Nenhuma subscrição da sessão corresponde ao filtro.");
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary("OPERAÇÕES DE LICENCIAMENTO",
        "Gerar, revogar, prolongar ou transferir licenças e criar promo codes são operações remotas privilegiadas.");
}

void DrawAdminAnalytics() {
    std::uint64_t totalSessions = 0;
    std::uint64_t totalSeconds = 0;
    std::uint32_t mostSessions = 0;
    const char* mostUsed = "—";
    for (const GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        const GameHistory history = GetGameHistory(runtime.definition->launch_id);
        totalSessions += history.sessionCount;
        totalSeconds += history.totalActiveSeconds;
        if (history.sessionCount > mostSessions) {
            mostSessions = history.sessionCount;
            mostUsed = runtime.definition->name;
        }
    }

    CyberWidgets::BeginCard("UTILIZAÇÃO LOCAL", 0.f);
    CyberWidgets::Badge("DADOS DESTE DISPOSITIVO", CyberWidgets::TextTone::Info);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::KeyValueRow("Sessões", std::to_string(totalSessions).c_str());
    const std::string duration = FormatActiveDuration(totalSeconds);
    CyberWidgets::KeyValueRow("Tempo total de menu", duration.c_str());
    CyberWidgets::KeyValueRow("Jogo mais utilizado", mostUsed);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    for (const GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        const GameHistory history = GetGameHistory(runtime.definition->launch_id);
        ImGui::PushID(runtime.definition->id);
        CyberWidgets::BeginCard(runtime.definition->name, 0.f);
        CyberWidgets::KeyValueRow("Sessões", std::to_string(history.sessionCount).c_str());
        const std::string gameDuration = FormatActiveDuration(history.totalActiveSeconds);
        CyberWidgets::KeyValueRow("Tempo acumulado", gameDuration.c_str());
        CyberWidgets::KeyValueRow("Último resultado", SessionResultDisplay(history.lastResult));
        CyberWidgets::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, S(8.f)));
    }

    DrawRemoteBoundary("ANALYTICS GLOBAL",
        "DAU/WAU/MAU, receita, geografia, crashes e feature usage requerem telemetria consentida e um backend agregado.", false);
}

const char* OffsetStateText(OmniGhost::OffsetAuto::CompatibilityState state) {
    using StateType = OmniGhost::OffsetAuto::CompatibilityState;
    switch (state) {
    case StateType::Checking: return "A verificar";
    case StateType::Ready: return "Prontos";
    case StateType::Outdated: return "Desatualizados";
    case StateType::Unsupported: return "Sem atualização automática";
    default: return "Ainda não validados";
    }
}

CyberWidgets::HealthStatus OffsetHealth(OmniGhost::OffsetAuto::CompatibilityState state) {
    using StateType = OmniGhost::OffsetAuto::CompatibilityState;
    if (state == StateType::Ready) return CyberWidgets::HealthStatus::Ok;
    if (state == StateType::Outdated) return CyberWidgets::HealthStatus::Error;
    return CyberWidgets::HealthStatus::Warning;
}

void DrawAdminGameConfig() {
    for (const GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        const GameDefinition& game = *runtime.definition;
        const ActiveGame active = ToActiveGame(game.launch_id);
        const auto compatibility = OmniGhost::OffsetAuto::Compatibility(active);
        ImGui::PushID(game.id);
        CyberWidgets::BeginCard(game.name, 0.f);
        CyberWidgets::HealthRow("Instalação", runtime.installed ? "Detetada" : "Não detetada",
            runtime.installed ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
        CyberWidgets::HealthRow("Acesso", OmniGhost::Licensing::HasGameAccess(game.id) ? "Autorizado" : "Sem licença",
            OmniGhost::Licensing::HasGameAccess(game.id) ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
        CyberWidgets::HealthRow("Runtime", CardStateLabel(runtime.state),
            runtime.state == CardState::Error || runtime.state == CardState::LaunchFailed
                ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
        CyberWidgets::HealthRow("Offsets", OffsetStateText(compatibility.state), OffsetHealth(compatibility.state));
        if (!compatibility.detail.empty())
            CyberWidgets::TextLine(compatibility.detail.c_str(), CyberWidgets::TextTone::Secondary);
        const bool canRefresh = !g_offset_refresh_pending &&
            OmniGhost::OffsetAuto::SupportsAutomaticRefresh(active);
        if (CyberWidgets::Button("ATUALIZAR OFFSETS", CyberWidgets::ButtonStyle::Secondary,
                                 ImVec2(S(180.f), S(32.f)), canRefresh))
            RequestOffsetRefresh(game.launch_id, false);
        CyberWidgets::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, S(9.f)));
    }
    DrawRemoteBoundary("ROLLOUT E FEATURE FLAGS",
        "Publicação de offsets, canary rollout e perfis oficiais precisam de artefactos assinados e de um serviço de configuração autenticado.", false);
}

void DrawAdminBuildInfo() {
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    CyberWidgets::BeginCard("BUILD ATUAL", 0.f);
    CyberWidgets::KeyValueRow("Versão", OmniGhost::BuildInfo::AppVersion);
    CyberWidgets::KeyValueRow("Build ID", OmniGhost::BuildInfo::BuildId);
    CyberWidgets::KeyValueRow("Commit", OmniGhost::BuildInfo::CommitId);
    CyberWidgets::KeyValueRow("Configuração", OmniGhost::BuildInfo::Configuration);
    CyberWidgets::KeyValueRow("Arquitetura", OmniGhost::BuildInfo::Architecture);
    CyberWidgets::KeyValueRow("Canal", OmniGhost::BuildInfo::ReleaseChannel);
    CyberWidgets::KeyValueRow("Toolchain", OmniGhost::BuildInfo::ToolchainVersion);
    CyberWidgets::HealthRow("Reprodutível", OmniGhost::BuildInfo::Reproducible ? "Sim" : "Não confirmado",
        OmniGhost::BuildInfo::Reproducible ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard("UPDATE CLIENT", 0.f);
    CyberWidgets::HealthRow("Estado", UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error
            ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
    CyberWidgets::KeyValueRow("Instalada", update.installedVersion.empty() ? "—" : update.installedVersion.c_str());
    CyberWidgets::KeyValueRow("Disponível", update.availableVersion.empty() ? "—" : update.availableVersion.c_str());
    if (CyberWidgets::Button("VERIFICAR AGORA", CyberWidgets::ButtonStyle::Primary, ImVec2(S(165.f), S(34.f))))
        OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
    ImGui::SameLine();
    if (CyberWidgets::Button("GITHUB ACTIONS", CyberWidgets::ButtonStyle::Secondary, ImVec2(S(170.f), S(34.f))))
        OpenExternal(L"https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/actions",
                     "Não foi possível abrir o GitHub Actions.");
    ImGui::SameLine();
    if (CyberWidgets::Button("RELEASES", CyberWidgets::ButtonStyle::Ghost, ImVec2(S(120.f), S(34.f))))
        OpenExternal(L"https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases",
                     "Não foi possível abrir os releases.");
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    const auto changelog = ChangelogService::Instance().GetSnapshot();
    CyberWidgets::BeginCard("RELEASES RECENTES", 0.f);
    if (changelog.releases.empty()) {
        CyberWidgets::EmptyState("SEM DADOS", "Atualiza o changelog para consultar releases publicadas.");
    } else {
        const std::size_t limit = (std::min<std::size_t>)(changelog.releases.size(), 5u);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto& release = changelog.releases[index];
            CyberWidgets::KeyValueRow(release.version.c_str(), release.title.c_str());
        }
    }
    if (CyberWidgets::Button("ATUALIZAR LISTA", CyberWidgets::ButtonStyle::Secondary, ImVec2(S(155.f), S(32.f))))
        ChangelogService::Instance().RefreshAsync(true);
    CyberWidgets::EndCard();
}

void DrawAdminSecurity() {
    const bool protectedFeatures = OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures();
    const auto dma = mem.GetDiagnosticsSnapshot();
    CyberWidgets::BeginCard("POSTURA DE SEGURANÇA", 0.f);
    CyberWidgets::HealthRow("Funcionalidades protegidas", protectedFeatures ? "Autorizadas" : "Bloqueadas",
        protectedFeatures ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::HealthRow("Dependências DMA", dma.dependencyIntegrityOk ? "Íntegras" : "Falha de integridade",
        dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::KeyValueRow("Session generation", std::to_string(dma.sessionGeneration).c_str());
    CyberWidgets::KeyValueRow("Process generation", std::to_string(dma.processGeneration).c_str());
    CyberWidgets::KeyValueRow("Falhas de leitura", std::to_string(dma.readFailureCount).c_str());
    CyberWidgets::KeyValueRow("Chamadas bloqueadas", std::to_string(dma.blockedDataCalls).c_str());
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard("EVENTOS RECENTES", 0.f);
    const auto events = OmniGhost::SessionLog::RecentEvents(12);
    if (events.empty()) {
        CyberWidgets::EmptyState("SEM EVENTOS", "Não existem eventos registados nesta sessão.");
    } else {
        for (const std::string& event : events) {
            const std::string safe = OmniGhost::SessionLog::RedactForSupport(event);
            CyberWidgets::TextLine(safe.c_str(), CyberWidgets::TextTone::Secondary);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary("AUDITORIA REMOTA",
        "Bans de IP, tentativas de login e timelines por utilizador pertencem ao backend. Eventos locais não concedem autoridade remota.", false);
}

void DrawAdminDevTools() {
    UiState& state = State();
    const auto dma = mem.GetDiagnosticsSnapshot();
    CyberWidgets::BeginCard("DMA / TRANSPORTE", 0.f);
    CyberWidgets::HealthRow("Dispositivo", dma.deviceOpen ? "Aberto" : (dma.deviceDetected ? "Detetado" : "Ausente"),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::KeyValueRow("PID associado", std::to_string(dma.processId).c_str());
    CyberWidgets::KeyValueRow("Reads", std::to_string(dma.readRequestCount).c_str());
    CyberWidgets::KeyValueRow("Scatter batches", std::to_string(dma.scatterReadBatchCount).c_str());
    CyberWidgets::KeyValueRow("Scatter handles vivos", std::to_string(dma.scatterHandlesLive).c_str());
    CyberWidgets::KeyValueRow("VMM p95", (std::to_string(dma.vmmLatencyP95Ms) + " ms").c_str());
    CyberWidgets::KeyValueRow("VMM p99", (std::to_string(dma.vmmLatencyP99Ms) + " ms").c_str());
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard("FERRAMENTAS LOCAIS", 0.f);
    if (CyberWidgets::Button("ATUALIZAR CONFIGS", CyberWidgets::ButtonStyle::Secondary, ImVec2(S(175.f), S(34.f)))) {
        config_manager::RefreshList();
        CyberWidgets::NotifyAdmin("Lista de configurações atualizada.", CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
#if defined(NDEBUG)
    state.showImGuiMetrics = false;
    constexpr bool metricsAvailable = false;
#else
    constexpr bool metricsAvailable = true;
#endif
    if (CyberWidgets::Button(state.showImGuiMetrics ? "FECHAR IMGUI METRICS" : "IMGUI METRICS",
                             CyberWidgets::ButtonStyle::Ghost, ImVec2(S(190.f), S(34.f)), metricsAvailable))
        state.showImGuiMetrics = !state.showImGuiMetrics;
#if defined(NDEBUG)
    CyberWidgets::InlineMessage(
        "Inspeção arbitrária de memória, simulação de falhas e force-crash não são expostos em builds Release.",
        CyberWidgets::TextTone::Info);
#else
    CyberWidgets::InlineMessage("Build de desenvolvimento: ImGui Metrics está disponível para diagnóstico local.",
                                CyberWidgets::TextTone::Warning);
#endif
    CyberWidgets::EndCard();

#if !defined(NDEBUG)
    if (state.showImGuiMetrics)
        ImGui::ShowMetricsWindow(&state.showImGuiMetrics);
#endif
}

void DrawAdminSupport() {
    CyberWidgets::BeginCard("DIAGNÓSTICO E SUPORTE", 0.f);
    CyberWidgets::TextLine("Os pacotes de diagnóstico são limitados e redigidos; não incluem credenciais nem licenças.",
                           CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::Button("COPIAR DIAGNÓSTICO", CyberWidgets::ButtonStyle::Secondary, ImVec2(S(190.f), S(34.f)))) {
        const std::string diagnostics = BuildDiagnosticsText();
        CyberWidgets::CopyToClipboard(diagnostics.c_str(), "Diagnóstico copiado");
    }
    ImGui::SameLine();
    if (CyberWidgets::Button("ABRIR LOGS", CyberWidgets::ButtonStyle::Ghost, ImVec2(S(135.f), S(34.f))))
        ShellExecuteW(nullptr, L"open", OmniGhost::Paths::Logs().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (CyberWidgets::Button("CRIAR PACOTE", CyberWidgets::ButtonStyle::Primary, ImVec2(S(145.f), S(34.f)))) {
        std::filesystem::path packagePath;
        std::string error;
        if (OmniGhost::Diagnostics::CreatePackage(BuildDiagnosticsText(), packagePath, error)) {
            CyberWidgets::NotifyAdmin("Pacote de diagnóstico criado.", CyberWidgets::ToastType::Success);
            ShellExecuteW(nullptr, L"open", packagePath.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            CyberWidgets::NotifyAdmin(error.empty() ? "Não foi possível criar o pacote." : error.c_str(),
                                 CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary("OPERAÇÕES E COMUNICAÇÕES",
        "Tickets, anúncios, FAQ e janelas de manutenção precisam de um serviço editorial autenticado e histórico de auditoria.", false);
}

void DrawContent() {
    const Section section = State().section;
    CyberWidgets::SectionTitle(SectionTitle(section));
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    switch (section) {
    case Section::Overview: DrawAdminOverview(); break;
    case Section::Users: DrawAdminUsers(); break;
    case Section::Licenses: DrawAdminLicenses(); break;
    case Section::Analytics: DrawAdminAnalytics(); break;
    case Section::Games: DrawAdminGameConfig(); break;
    case Section::Builds: DrawAdminBuildInfo(); break;
    case Section::Security: DrawAdminSecurity(); break;
    case Section::Developer: DrawAdminDevTools(); break;
    case Section::Support: DrawAdminSupport(); break;
    }
}

} // namespace AdminPanel

void DrawAdmin(ImVec2 display) {
    BeginControlPage("##launcher_admin", display);
    DrawPageHeading(Loc::Tr("launcher.admin"), Loc::Tr("launcher.admin.subtitle"));

    if (!OmniGhost::Licensing::IsAdmin()) {
        CyberWidgets::BeginCard("ACESSO NEGADO", 0.f);
        CyberWidgets::HealthRow("Admin", "Subscrição administrativa necessária",
                                CyberWidgets::HealthStatus::Error);
        CyberWidgets::TextLine("A sessão deixou de possuir autorização. Reautentica a conta para continuar.",
                               CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        EndControlPage();
        return;
    }

    const float sidebarWidth = S(205.f);
    const float gap = S(14.f);
    ImGui::BeginChild("##admin_navigation", ImVec2(sidebarWidth, -1.f), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    AdminPanel::DrawSectionNavigation();
    ImGui::EndChild();
    ImGui::SameLine(0.f, gap);
    ImGui::BeginChild("##admin_content", ImVec2(0.f, -1.f), false);
    AdminPanel::DrawContent();
    ImGui::EndChild();

    EndControlPage();
}
