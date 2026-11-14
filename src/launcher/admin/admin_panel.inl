#include "platform/strprotect/strprotect.h"





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
    const HINSTANCE result = ShellExecuteW(nullptr, OGS_WCSTR("open"), url, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32)
        return true;
    CyberWidgets::NotifyAdmin(failure, CyberWidgets::ToastType::Error);
    return false;
}

const char* SectionTitle(Section section) {
    switch (section) {
    case Section::Overview: return OGS_CSTR("Visão geral");
    case Section::Users: return OGS_CSTR("Utilizadores");
    case Section::Licenses: return OGS_CSTR("Licenças");
    case Section::Analytics: return OGS_CSTR("Analytics");
    case Section::Games: return OGS_CSTR("Jogos e offsets");
    case Section::Builds: return OGS_CSTR("Builds e releases");
    case Section::Security: return OGS_CSTR("Segurança e auditoria");
    case Section::Developer: return OGS_CSTR("Ferramentas");
    case Section::Support: return OGS_CSTR("Suporte e operações");
    }
    return OGS_CSTR("Admin");
}

void DrawSectionNavigation() {
    static const struct Entry { Section section; std::string label; } entries[] = {
        {Section::Overview, OGS_LIT("VISÃO GERAL")},
        {Section::Users, OGS_LIT("UTILIZADORES")},
        {Section::Licenses, OGS_LIT("LICENÇAS")},
        {Section::Analytics, OGS_LIT("ANALYTICS")},
        {Section::Games, OGS_LIT("JOGOS / OFFSETS")},
        {Section::Builds, OGS_LIT("BUILDS / RELEASES")},
        {Section::Security, OGS_LIT("SEGURANÇA")},
        {Section::Developer, OGS_LIT("FERRAMENTAS")},
        {Section::Support, OGS_LIT("SUPORTE")},
    };

    UiState& state = State();
    CyberWidgets::Badge(OGS_LIT("ADMIN CONTROL"), CyberWidgets::TextTone::Accent);
    ImGui::Dummy(ImVec2(0.f, S(9.f)));
    for (const Entry& entry : entries) {
        const bool selected = state.section == entry.section;
        ImGui::PushID(static_cast<int>(entry.section));
        if (selected) {
            const ImVec4 selectedColor = CyberTheme::Mix(CyberTheme::Colors.Card, CyberTheme::Colors.Gold, .16f);
            ImGui::PushStyleColor(ImGuiCol_Header, selectedColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selectedColor);
        }
        if (ImGui::Selectable(entry.label.c_str(), selected, 0,
                              ImVec2(ImGui::GetContentRegionAvail().x, S(34.f))))
            state.section = entry.section;
        if (selected)
            ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
}

void DrawRemoteBoundary(const char* title, const char* description, bool keyAuth = true) {
    CyberWidgets::BeginCard(title, 0.f);
    CyberWidgets::Badge(OGS_LIT("SERVIÇO REMOTO NECESSÁRIO"), CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::TextLine(description, CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine(
        OGS_LIT("O cliente não guarda Seller API keys, tokens de CI ou credenciais administrativas."),
        CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0.f, S(9.f)));
    if (keyAuth && CyberWidgets::GoldButton(OGS_LIT("ABRIR KEYAUTH"), ImVec2(S(160.f), S(34.f))))
        OpenExternal(OGS_WIDE("https://keyauth.cc/app/"), OGS_LIT("Não foi possível abrir o painel KeyAuth."));
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

    CyberWidgets::BeginCard(OGS_LIT("SESSÃO ADMINISTRATIVA"), width);
    CyberWidgets::Badge(OGS_LIT("AUTORIZADA"), CyberWidgets::TextTone::Success);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::KeyValueRow(OGS_LIT("Operador"), license.remoteUsername.empty() ? "—" : license.remoteUsername.c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Subscrição"), OGS_LIT("Admin"));
    CyberWidgets::KeyValueRow(OGS_LIT("Permissões de produto"), OGS_LIT("Todos os jogos"));
    CyberWidgets::HealthRow(OGS_LIT("Sessão KeyAuth"), license.remoteAuthenticated ? OGS_LIT("Autenticada") : OGS_LIT("Inválida"),
        license.remoteAuthenticated ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::EndCard();

    if (two) ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(OGS_LIT("ESTADO OPERACIONAL"), width);
    CyberWidgets::HealthRow("DMA", dma.deviceOpen ? OGS_LIT("Ligado") : (dma.deviceDetected ? OGS_LIT("Detetado") : OGS_LIT("Indisponível")),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(OGS_LIT("Integridade DMA"), dma.dependencyIntegrityOk ? OGS_LIT("Validada") : OGS_LIT("Requer atenção"),
        dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::HealthRow(OGS_LIT("Atualizações"), UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error
            ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard(OGS_LIT("RESUMO LOCAL"), 0.f);
    CyberWidgets::KeyValueRow(OGS_LIT("Jogos instalados"), std::to_string(installed).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Jogos ativos"), std::to_string(running).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Sessões registadas neste dispositivo"), std::to_string(sessions).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Build"), OmniGhost::BuildInfo::BuildId);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard(OGS_LIT("AÇÕES RÁPIDAS"), 0.f);
    if (CyberWidgets::Button(OGS_LIT("KEYAUTH"), CyberWidgets::ButtonStyle::Primary, ImVec2(S(130.f), S(34.f))))
        OpenExternal(OGS_WIDE("https://keyauth.cc/app/"), OGS_LIT("Não foi possível abrir o painel KeyAuth."));
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_LIT("GITHUB ACTIONS"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(175.f), S(34.f))))
        OpenExternal(OGS_WIDE("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/actions"),
                     OGS_LIT("Não foi possível abrir o GitHub Actions."));
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_LIT("VERIFICAR UPDATE"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(175.f), S(34.f))))
        OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
    CyberWidgets::EndCard();
}

void DrawAdminUsers() {
    UiState& state = State();
    CyberWidgets::BeginCard(OGS_LIT("PESQUISA DE UTILIZADORES"), 0.f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint(OGS_CSTR("##admin_user_filter"), OGS_CSTR("Username, email ou HWID..."),
                             state.userFilter, sizeof(state.userFilter));
    CyberWidgets::TextLine(OGS_LIT("A sessão atual é mostrada localmente. A listagem global permanece no KeyAuth."),
                           CyberWidgets::TextTone::Secondary);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    const auto license = OmniGhost::Licensing::GetSnapshot();
    CyberWidgets::BeginCard(OGS_LIT("OPERADOR ATUAL"), 0.f);
    CyberWidgets::KeyValueRow(OGS_LIT("Username"), license.remoteUsername.empty() ? "—" : license.remoteUsername.c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Perfil"), OGS_LIT("Administrador"));
    CyberWidgets::HealthRow(OGS_LIT("Autenticação"), license.remoteAuthenticated ? OGS_LIT("Ativa") : OGS_LIT("Expirada"),
        license.remoteAuthenticated ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary(OGS_LIT("GESTÃO DE CONTAS"),
        OGS_LIT("Listar utilizadores, banir, remover bans, repor HWID, ações em massa e impersonation exigem autorização server-side e audit log."));
}

void DrawAdminLicenses() {
    UiState& state = State();
    const auto license = OmniGhost::Licensing::GetSnapshot();
    CyberWidgets::BeginCard(OGS_LIT("SUBSCRIÇÕES DA SESSÃO"), 0.f);
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint(OGS_CSTR("##admin_license_filter"), OGS_CSTR("Filtrar subscrições visíveis..."),
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
        const std::string remaining = entitlement.remaining.empty() ? OGS_LIT("Ativa") : entitlement.remaining;
        CyberWidgets::HealthRow(name.c_str(), remaining.c_str(), CyberWidgets::HealthStatus::Ok);
    }
    if (!shown)
        CyberWidgets::EmptyState(OGS_LIT("SEM RESULTADOS"), OGS_LIT("Nenhuma subscrição da sessão corresponde ao filtro."));
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary(OGS_LIT("OPERAÇÕES DE LICENCIAMENTO"),
        OGS_LIT("Gerar, revogar, prolongar ou transferir licenças e criar promo codes são operações remotas privilegiadas."));
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

    CyberWidgets::BeginCard(OGS_LIT("UTILIZAÇÃO LOCAL"), 0.f);
    CyberWidgets::Badge(OGS_LIT("DADOS DESTE DISPOSITIVO"), CyberWidgets::TextTone::Info);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::KeyValueRow(OGS_LIT("Sessões"), std::to_string(totalSessions).c_str());
    const std::string duration = FormatActiveDuration(totalSeconds);
    CyberWidgets::KeyValueRow(OGS_LIT("Tempo total de menu"), duration.c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Jogo mais utilizado"), mostUsed);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    for (const GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        const GameHistory history = GetGameHistory(runtime.definition->launch_id);
        ImGui::PushID(runtime.definition->id);
        CyberWidgets::BeginCard(runtime.definition->name, 0.f);
        CyberWidgets::KeyValueRow(OGS_LIT("Sessões"), std::to_string(history.sessionCount).c_str());
        const std::string gameDuration = FormatActiveDuration(history.totalActiveSeconds);
        CyberWidgets::KeyValueRow(OGS_LIT("Tempo acumulado"), gameDuration.c_str());
        CyberWidgets::KeyValueRow(OGS_LIT("Último resultado"), SessionResultDisplay(history.lastResult));
        CyberWidgets::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, S(8.f)));
    }

    DrawRemoteBoundary(OGS_LIT("ANALYTICS GLOBAL"),
        OGS_LIT("DAU/WAU/MAU, receita, geografia, crashes e feature usage requerem telemetria consentida e um backend agregado."), false);
}

const char* OffsetStateText(OmniGhost::OffsetAuto::CompatibilityState state) {
    using StateType = OmniGhost::OffsetAuto::CompatibilityState;
    switch (state) {
    case StateType::Checking: return OGS_CSTR("A verificar");
    case StateType::Ready: return OGS_CSTR("Prontos");
    case StateType::Outdated: return OGS_CSTR("Desatualizados");
    case StateType::Unsupported: return OGS_CSTR("Sem atualização automática");
    default: return OGS_CSTR("Ainda não validados");
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
        CyberWidgets::HealthRow(OGS_LIT("Instalação"), runtime.installed ? OGS_LIT("Detetada") : OGS_LIT("Não detetada"),
            runtime.installed ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
        CyberWidgets::HealthRow(OGS_LIT("Acesso"), OmniGhost::Licensing::HasGameAccess(game.id) ? OGS_LIT("Autorizado") : OGS_LIT("Sem licença"),
            OmniGhost::Licensing::HasGameAccess(game.id) ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
        CyberWidgets::HealthRow(OGS_LIT("Runtime"), CardStateLabel(runtime.state),
            runtime.state == CardState::Error || runtime.state == CardState::LaunchFailed
                ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
        CyberWidgets::HealthRow(OGS_LIT("Offsets"), OffsetStateText(compatibility.state), OffsetHealth(compatibility.state));
        if (!compatibility.detail.empty())
            CyberWidgets::TextLine(compatibility.detail.c_str(), CyberWidgets::TextTone::Secondary);
        const bool canRefresh = !g_offset_refresh_pending &&
            OmniGhost::OffsetAuto::SupportsAutomaticRefresh(active);
        if (CyberWidgets::Button(OGS_LIT("ATUALIZAR OFFSETS"), CyberWidgets::ButtonStyle::Secondary,
                                 ImVec2(S(180.f), S(32.f)), canRefresh))
            RequestOffsetRefresh(game.launch_id, false);
        CyberWidgets::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, S(9.f)));
    }
    DrawRemoteBoundary(OGS_LIT("ROLLOUT E FEATURE FLAGS"),
        OGS_LIT("Publicação de offsets, canary rollout e perfis oficiais precisam de artefactos validados por SHA-256 e de um serviço de configuração autenticado."), false);
}

void DrawAdminBuildInfo() {
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    CyberWidgets::BeginCard(OGS_LIT("BUILD ATUAL"), 0.f);
    CyberWidgets::KeyValueRow(OGS_LIT("Versão"), OmniGhost::BuildInfo::AppVersion);
    CyberWidgets::KeyValueRow(OGS_LIT("Build ID"), OmniGhost::BuildInfo::BuildId);
    CyberWidgets::KeyValueRow(OGS_LIT("Commit"), OmniGhost::BuildInfo::CommitId);
    CyberWidgets::KeyValueRow(OGS_LIT("Configuração"), OmniGhost::BuildInfo::Configuration);
    CyberWidgets::KeyValueRow(OGS_LIT("Arquitetura"), OmniGhost::BuildInfo::Architecture);
    CyberWidgets::KeyValueRow(OGS_LIT("Canal"), OmniGhost::BuildInfo::ReleaseChannel);
    CyberWidgets::KeyValueRow(OGS_LIT("Toolchain"), OmniGhost::BuildInfo::ToolchainVersion);
    CyberWidgets::HealthRow(OGS_LIT("Reprodutível"), OmniGhost::BuildInfo::Reproducible ? "Sim" : OGS_LIT("Não confirmado"),
        OmniGhost::BuildInfo::Reproducible ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_LIT("UPDATE CLIENT"), 0.f);
    CyberWidgets::HealthRow(OGS_LIT("Estado"), UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error
            ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
    CyberWidgets::KeyValueRow(OGS_LIT("Instalada"), update.installedVersion.empty() ? "—" : update.installedVersion.c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Disponível"), update.availableVersion.empty() ? "—" : update.availableVersion.c_str());
    if (CyberWidgets::Button(OGS_LIT("VERIFICAR AGORA"), CyberWidgets::ButtonStyle::Primary, ImVec2(S(165.f), S(34.f))))
        OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_LIT("GITHUB ACTIONS"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(170.f), S(34.f))))
        OpenExternal(OGS_WIDE("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/actions"),
                     OGS_LIT("Não foi possível abrir o GitHub Actions."));
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_LIT("RELEASES"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(120.f), S(34.f))))
        OpenExternal(OGS_WIDE("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases"),
                     OGS_LIT("Não foi possível abrir os releases."));
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    const auto changelog = ChangelogService::Instance().GetSnapshot();
    CyberWidgets::BeginCard(OGS_LIT("RELEASES RECENTES"), 0.f);
    if (changelog.releases.empty()) {
        CyberWidgets::EmptyState(OGS_LIT("SEM DADOS"), OGS_LIT("Atualiza o changelog para consultar releases publicadas."));
    } else {
        const std::size_t limit = (std::min<std::size_t>)(changelog.releases.size(), 5u);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto& release = changelog.releases[index];
            CyberWidgets::KeyValueRow(release.version.c_str(), release.title.c_str());
        }
    }
    if (CyberWidgets::Button(OGS_LIT("ATUALIZAR LISTA"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(155.f), S(32.f))))
        ChangelogService::Instance().RefreshAsync(true);
    CyberWidgets::EndCard();
}

void DrawAdminSecurity() {
    const bool protectedFeatures = OmniGhost::Security::SecurityManager::Instance().CanUseProtectedFeatures();
    const auto dma = mem.GetDiagnosticsSnapshot();
    CyberWidgets::BeginCard(OGS_LIT("POSTURA DE SEGURANÇA"), 0.f);
    CyberWidgets::HealthRow(OGS_LIT("Funcionalidades protegidas"), protectedFeatures ? OGS_LIT("Autorizadas") : OGS_LIT("Bloqueadas"),
        protectedFeatures ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::HealthRow(OGS_LIT("Dependências DMA"), dma.dependencyIntegrityOk ? OGS_LIT("Íntegras") : OGS_LIT("Falha de integridade"),
        dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::KeyValueRow(OGS_LIT("Session generation"), std::to_string(dma.sessionGeneration).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Process generation"), std::to_string(dma.processGeneration).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Falhas de leitura"), std::to_string(dma.readFailureCount).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Chamadas bloqueadas"), std::to_string(dma.blockedDataCalls).c_str());
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_LIT("EVENTOS RECENTES"), 0.f);
    const auto events = OmniGhost::SessionLog::RecentEvents(12);
    if (events.empty()) {
        CyberWidgets::EmptyState(OGS_LIT("SEM EVENTOS"), OGS_LIT("Não existem eventos registados nesta sessão."));
    } else {
        for (const std::string& event : events) {
            const std::string safe = OmniGhost::SessionLog::RedactForSupport(event);
            CyberWidgets::TextLine(safe.c_str(), CyberWidgets::TextTone::Secondary);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary(OGS_LIT("AUDITORIA REMOTA"),
        OGS_LIT("Bans de IP, tentativas de login e timelines por utilizador pertencem ao backend. Eventos locais não concedem autoridade remota."), false);
}

void DrawAdminDevTools() {
    UiState& state = State();
    const auto dma = mem.GetDiagnosticsSnapshot();
    CyberWidgets::BeginCard(OGS_LIT("DMA / TRANSPORTE"), 0.f);
    CyberWidgets::HealthRow(OGS_LIT("Dispositivo"), dma.deviceOpen ? OGS_LIT("Aberto") : (dma.deviceDetected ? OGS_LIT("Detetado") : OGS_LIT("Ausente")),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::KeyValueRow(OGS_LIT("PID associado"), std::to_string(dma.processId).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Reads"), std::to_string(dma.readRequestCount).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Scatter batches"), std::to_string(dma.scatterReadBatchCount).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("Scatter handles vivos"), std::to_string(dma.scatterHandlesLive).c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("VMM p95"), (std::to_string(dma.vmmLatencyP95Ms) + " ms").c_str());
    CyberWidgets::KeyValueRow(OGS_LIT("VMM p99"), (std::to_string(dma.vmmLatencyP99Ms) + " ms").c_str());
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_LIT("FERRAMENTAS LOCAIS"), 0.f);
    if (CyberWidgets::Button(OGS_LIT("ATUALIZAR CONFIGS"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(175.f), S(34.f)))) {
        config_manager::RefreshList();
        CyberWidgets::NotifyAdmin(OGS_LIT("Lista de configurações atualizada."), CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
#if defined(NDEBUG)
    state.showImGuiMetrics = false;
    constexpr bool metricsAvailable = false;
#else
    constexpr bool metricsAvailable = true;
#endif
    if (CyberWidgets::Button(state.showImGuiMetrics ? OGS_LIT("FECHAR IMGUI METRICS") : OGS_LIT("IMGUI METRICS"),
                             CyberWidgets::ButtonStyle::Ghost, ImVec2(S(190.f), S(34.f)), metricsAvailable))
        state.showImGuiMetrics = !state.showImGuiMetrics;
#if defined(NDEBUG)
    CyberWidgets::InlineMessage(
        OGS_LIT("Inspeção arbitrária de memória, simulação de falhas e force-crash não são expostos em builds Release."),
        CyberWidgets::TextTone::Info);
#else
    CyberWidgets::InlineMessage(OGS_LIT("Build de desenvolvimento: ImGui Metrics está disponível para diagnóstico local."),
                                CyberWidgets::TextTone::Warning);
#endif
    CyberWidgets::EndCard();

#if !defined(NDEBUG)
    if (state.showImGuiMetrics)
        ImGui::ShowMetricsWindow(&state.showImGuiMetrics);
#endif
}

void DrawAdminSupport() {
    CyberWidgets::BeginCard(OGS_LIT("DIAGNÓSTICO E SUPORTE"), 0.f);
    CyberWidgets::TextLine(OGS_LIT("Os pacotes de diagnóstico são limitados e redigidos; não incluem credenciais nem licenças."),
                           CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::Button(OGS_LIT("COPIAR DIAGNÓSTICO"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(190.f), S(34.f)))) {
        const std::string diagnostics = BuildDiagnosticsText();
        CyberWidgets::CopyToClipboard(diagnostics.c_str(), OGS_LIT("Diagnóstico copiado"));
    }
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_LIT("ABRIR LOGS"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(135.f), S(34.f))))
        ShellExecuteW(nullptr, OGS_WCSTR("open"), OmniGhost::Paths::Logs().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_LIT("CRIAR PACOTE"), CyberWidgets::ButtonStyle::Primary, ImVec2(S(145.f), S(34.f)))) {
        std::filesystem::path packagePath;
        std::string error;
        if (OmniGhost::Diagnostics::CreatePackage(BuildDiagnosticsText(), packagePath, error)) {
            CyberWidgets::NotifyAdmin(OGS_LIT("Pacote de diagnóstico criado."), CyberWidgets::ToastType::Success);
            ShellExecuteW(nullptr, OGS_WCSTR("open"), packagePath.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            CyberWidgets::NotifyAdmin(error.empty() ? OGS_LIT("Não foi possível criar o pacote.") : error.c_str(),
                                 CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary(OGS_LIT("OPERAÇÕES E COMUNICAÇÕES"),
        OGS_LIT("Tickets, anúncios, FAQ e janelas de manutenção precisam de um serviço editorial autenticado e histórico de auditoria."), false);
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

}

void DrawAdmin(ImVec2 display) {
    BeginControlPage(OGS_LIT("##launcher_admin"), display);
    DrawPageHeading(Loc::Tr("launcher.admin"), Loc::Tr("launcher.admin.subtitle"));

    if (!OmniGhost::Licensing::IsAdmin()) {
        CyberWidgets::BeginCard(OGS_LIT("ACESSO NEGADO"), 0.f);
        CyberWidgets::HealthRow(OGS_LIT("Admin"), OGS_LIT("Subscrição administrativa necessária"),
                                CyberWidgets::HealthStatus::Error);
        CyberWidgets::TextLine(OGS_LIT("A sessão deixou de possuir autorização. Reautentica a conta para continuar."),
                               CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        EndControlPage();
        return;
    }

    const float sidebarWidth = S(220.f);
    const float gap = S(14.f);
    ImGui::BeginChild(OGS_CSTR("##admin_navigation"), ImVec2(sidebarWidth, -1.f), true,
                      ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetScrollX(0.0f);
    AdminPanel::DrawSectionNavigation();
    ImGui::EndChild();
    ImGui::SameLine(0.f, gap);
    ImGui::BeginChild(OGS_CSTR("##admin_content"), ImVec2(0.f, -1.f), false);
    AdminPanel::DrawContent();
    ImGui::EndChild();

    EndControlPage();
}
