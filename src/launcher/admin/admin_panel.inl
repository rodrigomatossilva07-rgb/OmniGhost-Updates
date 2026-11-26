#include "platform/strprotect/strprotect.h"
#include "../admin_api_client.h"
#include "game_select.h"
#include "imgui.h"

#include <chrono>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <optional>
#include <sstream>

// Forward declaration from game_select.cpp
std::string BuildDiagnosticsText();

// Include Diagnostics for CreatePackage
#include "../platform/diagnostics_package.h"

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

    // Users
    int userPage = 1;
    int userPageSize = 50;
    std::string selectedUser;
    bool userDetailsOpen = false;

    // Licenses
    int licensePage = 1;
    int licensePageSize = 50;

    // Security
    int ipBanPage = 1;
    int loginAttemptPage = 1;

    // Support
    int ticketPage = 1;
    int ticketPageSize = 50;
    std::string selectedTicket;
    bool ticketDetailsOpen = false;

    // Announcements
    std::string newAnnTitle;
    std::string newAnnBody;
    std::string newAnnSeverity = "info";
    std::vector<std::string> newAnnGames;

    // Maintenance
    std::string newMaintTitle;
    std::string newMaintDesc;
    int64_t newMaintStart = 0;
    int64_t newMaintEnd = 0;
    std::vector<std::string> newMaintGames;

    // Promo code
    std::string newPromoCode;
    int newPromoDays = 30;
    int newPromoLevel = 1;
    int newPromoMaxUses = 100;
    std::string newPromoNote;

    // License extend/transfer
    std::string selectedLicenseKey;
    int extendDays = 30;
    std::string extendNote;
    std::string transferUsername;
    std::string transferNote;

    // Client config
    std::string adminApiUrl;
    std::string adminApiKey;
    bool configSaved = false;
};

UiState& State() {
    static UiState state;
    return state;
}

static LauncherAdmin::AdminApiClient* GetApiClient() {
    static LauncherAdmin::AdminApiClient* client = nullptr;
    auto& state = State();
    if (!client || client->GetConfigHash() != std::hash<std::string>{}(state.adminApiUrl + "|" + state.adminApiKey)) {
        if (state.adminApiUrl.empty() || state.adminApiKey.empty()) return nullptr;
        LauncherAdmin::AdminConfig cfg;
        cfg.baseUrl = state.adminApiUrl;
        cfg.apiKey = state.adminApiKey;
        delete client;
        client = new LauncherAdmin::AdminApiClient(cfg);
    }
    return client;
}

bool OpenExternal(const wchar_t* url, const char* failure) {
    const HINSTANCE result = ShellExecuteW(nullptr, OGS_WCSTR("open"), url, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) return true;
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
    static const struct Entry { Section section; const char* label; } entries[] = {
        {Section::Overview, OGS_CSTR("VISÃO GERAL")},
        {Section::Users, OGS_CSTR("UTILIZADORES")},
        {Section::Licenses, OGS_CSTR("LICENÇAS")},
        {Section::Analytics, OGS_CSTR("ANALYTICS")},
        {Section::Games, OGS_CSTR("JOGOS / OFFSETS")},
        {Section::Builds, OGS_CSTR("BUILDS / RELEASES")},
        {Section::Security, OGS_CSTR("SEGURANÇA")},
        {Section::Developer, OGS_CSTR("FERRAMENTAS")},
        {Section::Support, OGS_CSTR("SUPORTE")},
    };

    UiState& state = State();
    CyberWidgets::Badge(OGS_CSTR("ADMIN CONTROL"), CyberWidgets::TextTone::Accent);
    ImGui::Dummy(ImVec2(0.f, S(9.f)));
    for (const Entry& entry : entries) {
        const bool selected = state.section == entry.section;
        ImGui::PushID(static_cast<int>(entry.section));
        if (selected) {
            const ImVec4 selectedColor = CyberTheme::Mix(CyberTheme::Colors.Card, CyberTheme::Colors.Gold, .16f);
            ImGui::PushStyleColor(ImGuiCol_Header, selectedColor);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selectedColor);
        }
        if (ImGui::Selectable(entry.label, selected, 0, ImVec2(ImGui::GetContentRegionAvail().x, S(34.f))))
            state.section = entry.section;
        if (selected) ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
}

void DrawRemoteBoundary(const char* title, const char* description, bool keyAuth = true) {
    CyberWidgets::BeginCard(title, 0.f);
    CyberWidgets::Badge(OGS_CSTR("SERVIÇO REMOTO NECESSÁRIO"), CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::TextLine(description, CyberWidgets::TextTone::Primary);
    CyberWidgets::TextLine(
        OGS_CSTR("O cliente não guarda Seller API keys, tokens de CI ou credenciais administrativas."),
        CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0.f, S(9.f)));
    if (keyAuth && CyberWidgets::GoldButton(OGS_CSTR("ABRIR KEYAUTH"), ImVec2(S(160.f), S(34.f))))
        OpenExternal(OGS_WCSTR("https://keyauth.cc/app/"), OGS_CSTR("Não foi possível abrir o painel KeyAuth."));
    CyberWidgets::EndCard();
}

void DrawApiConfig() {
    UiState& state = State();
    CyberWidgets::BeginCard(OGS_CSTR("CONFIGURAÇÃO DA ADMIN API"), 0.f);
    CyberWidgets::TextLine(OGS_CSTR("Configure a URL e a chave da Admin API para ativar as funcionalidades remotas."), CyberWidgets::TextTone::Secondary);
    ImGui::Dummy(ImVec2(0.f, S(9.f)));

    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint(OGS_CSTR("##admin_api_url"), OGS_CSTR("https://admin.omnighost.store"), &state.adminApiUrl)) {
        state.adminApiUrl.shrink_to_fit();
    }
    ImGui::Dummy(ImVec2(0.f, S(6.f)));
    ImGui::SetNextItemWidth(-1.f);
    if (ImGui::InputTextWithHint(OGS_CSTR("##admin_api_key"), OGS_CSTR("actor.secret (Bearer token)"), &state.adminApiKey, ImGuiInputTextFlags_Password)) {
        state.adminApiKey.shrink_to_fit();
    }
    ImGui::Dummy(ImVec2(0.f, S(9.f)));

    bool hasConfig = !state.adminApiUrl.empty() && !state.adminApiKey.empty();
    if (CyberWidgets::Button(hasConfig ? OGS_CSTR("RECONECTAR") : OGS_CSTR("SALVAR E CONECTAR"),
                             CyberWidgets::ButtonStyle::Primary, ImVec2(S(180.f), S(34.f)), hasConfig)) {
        state.configSaved = true;
    }
    ImGui::SameLine();
    CyberWidgets::TextLine(hasConfig ? OGS_CSTR("✓ Configurado") : OGS_CSTR("✗ Não configurado"),
                           hasConfig ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Error);
    CyberWidgets::EndCard();
}

void DrawAdminOverview() {
    const auto license = OmniGhost::Licensing::GetSnapshot();
    const auto dma = mem.GetDiagnosticsSnapshot();
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    std::size_t installed = 0, running = 0;
    std::uint64_t sessions = 0;
    for (const GameRuntime& game : g_games) {
        installed += game.installed ? 1u : 0u;
        running += game.state == CardState::Running ? 1u : 0u;
        if (game.definition) sessions += GetGameHistory(game.definition->launch_id).sessionCount;
    }

    const float gap = S(12.f);
    const float available = ImGui::GetContentRegionAvail().x;
    const bool two = available >= S(720.f);
    const float width = two ? (available - gap) * .5f : available;

    CyberWidgets::BeginCard(OGS_CSTR("SESSÃO ADMINISTRATIVA"), width);
    CyberWidgets::Badge(OGS_CSTR("AUTORIZADA"), CyberWidgets::TextTone::Success);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::KeyValueRow(OGS_CSTR("Operador"), license.remoteUsername.empty() ? "—" : license.remoteUsername.c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Subscrição"), OGS_CSTR("Admin"));
    CyberWidgets::KeyValueRow(OGS_CSTR("Permissões de produto"), OGS_CSTR("Todos os jogos"));
    CyberWidgets::HealthRow(OGS_CSTR("Sessão KeyAuth"), license.remoteAuthenticated ? OGS_CSTR("Autenticada") : OGS_CSTR("Inválida"),
        license.remoteAuthenticated ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::EndCard();

    if (two) ImGui::SameLine(0.f, gap);
    CyberWidgets::BeginCard(OGS_CSTR("ESTADO OPERACIONAL"), width);
    CyberWidgets::HealthRow("DMA", dma.deviceOpen ? OGS_CSTR("Ligado") : (dma.deviceDetected ? OGS_CSTR("Detetado") : OGS_CSTR("Indisponível")),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::HealthRow(OGS_CSTR("Integridade DMA"), dma.dependencyIntegrityOk ? OGS_CSTR("Validada") : OGS_CSTR("Requer atenção"),
        dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::HealthRow(OGS_CSTR("Atualizações"), UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard(OGS_CSTR("RESUMO LOCAL"), 0.f);
    CyberWidgets::KeyValueRow(OGS_CSTR("Jogos instalados"), std::to_string(installed).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Jogos ativos"), std::to_string(running).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Sessões registadas neste dispositivo"), std::to_string(sessions).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Build"), OmniGhost::BuildInfo::BuildId);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, gap));
    CyberWidgets::BeginCard(OGS_CSTR("AÇÕES RÁPIDAS"), 0.f);
    if (CyberWidgets::Button(OGS_CSTR("KEYAUTH"), CyberWidgets::ButtonStyle::Primary, ImVec2(S(130.f), S(34.f))))
        OpenExternal(OGS_WCSTR("https://keyauth.cc/app/"), OGS_CSTR("Não foi possível abrir o painel KeyAuth."));
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("GITHUB ACTIONS"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(175.f), S(34.f))))
        OpenExternal(OGS_WCSTR("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/actions"), OGS_CSTR("Não foi possível abrir o GitHub Actions."));
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("VERIFICAR UPDATE"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(175.f), S(34.f))))
        OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
    CyberWidgets::EndCard();

    // Admin API Status
    ImGui::Dummy(ImVec2(0.f, gap));
    auto* api = GetApiClient();
    CyberWidgets::BeginCard(OGS_CSTR("ADMIN API STATUS"), 0.f);
    if (api) {
        auto caps = api->GetCapabilities();
        if (caps.success()) {
            CyberWidgets::Badge(OGS_CSTR("CONECTADO"), CyberWidgets::TextTone::Success);
            ImGui::Dummy(ImVec2(0.f, S(5.f)));
            CyberWidgets::KeyValueRow(OGS_CSTR("Base URL"), State().adminApiUrl.c_str());
            CyberWidgets::KeyValueRow(OGS_CSTR("Capacidades"), OGS_CSTR("Users, Licenses, Security, Support, Analytics"));
        } else {
            CyberWidgets::Badge(OGS_CSTR("ERRO DE CONEXÃO"), CyberWidgets::TextTone::Error);
            CyberWidgets::TextLine(caps.error.c_str(), CyberWidgets::TextTone::Error);
        }
    } else {
        CyberWidgets::Badge(OGS_CSTR("NÃO CONFIGURADO"), CyberWidgets::TextTone::Warning);
        CyberWidgets::TextLine(OGS_CSTR("Configure a Admin API acima para ativar funcionalidades remotas."), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
}

// ─── USERS ───
void DrawAdminUsers() {
    UiState& state = State();
    auto* api = GetApiClient();

    DrawApiConfig();
    if (!api) return;

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("PESQUISA DE UTILIZADORES"), 0.f);
    ImGui::SetNextItemWidth(S(300.f));
    ImGui::InputTextWithHint(OGS_CSTR("##admin_user_filter"), OGS_CSTR("Username, email ou HWID..."), state.userFilter, sizeof(state.userFilter));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(80.f));
    ImGui::InputInt(OGS_CSTR("##user_page"), &state.userPage);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(80.f));
    ImGui::InputInt(OGS_CSTR("##user_page_size"), &state.userPageSize);
    state.userPage = std::max(1, state.userPage);
    state.userPageSize = std::clamp(state.userPageSize, 10, 200);
    ImGui::SameLine();
    if (CyberWidgets::GoldButton(OGS_CSTR("BUSCAR"), ImVec2(S(100.f), S(28.f)))) {}
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));

    auto response = api->ListUsers(state.userFilter, state.userPage, state.userPageSize);
    if (!response.success()) {
        CyberWidgets::BeginCard(OGS_CSTR("ERRO"), 0.f);
        CyberWidgets::TextLine(response.error.c_str(), CyberWidgets::TextTone::Error);
        CyberWidgets::EndCard();
        return;
    }

    auto respJson = ParseAdminResponse(response);
    if (!respJson) {
        CyberWidgets::BeginCard(OGS_CSTR("ERRO"), 0.f);
        CyberWidgets::TextLine(OGS_CSTR("Falha ao parsear resposta JSON"), CyberWidgets::TextTone::Error);
        CyberWidgets::EndCard();
        return;
    }
    auto items = JsonGetArray(respJson, "items");
    int total = JsonGetInt(respJson, "total", 0);
    int page = JsonGetInt(respJson, "page", 1);
    int pageSize = JsonGetInt(respJson, "pageSize", 50);

    CyberWidgets::BeginCard(OGS_CSTR("LISTA DE UTILIZADORES"), 0.f);
    CyberWidgets::KeyValueRow(OGS_CSTR("Total"), std::to_string(total).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Página"), (std::to_string(page) + " / " + std::to_string((total + pageSize - 1) / pageSize)).c_str());
    ImGui::Dummy(ImVec2(0.f, S(6.f)));

    if (ImGui::BeginTable("##users_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, S(400.f)))) {
        ImGui::TableSetupColumn(OGS_CSTR("Username"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(OGS_CSTR("Email"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(OGS_CSTR("IP"), ImGuiTableColumnFlags_WidthFixed, S(120.f));
        ImGui::TableSetupColumn(OGS_CSTR("HWID"), ImGuiTableColumnFlags_WidthFixed, S(140.f));
        ImGui::TableSetupColumn(OGS_CSTR("Status"), ImGuiTableColumnFlags_WidthFixed, S(100.f));
        ImGui::TableSetupColumn(OGS_CSTR("Ações"), ImGuiTableColumnFlags_WidthFixed, S(200.f));
        ImGui::TableHeadersRow();

        int itemCount = items ? JsonArraySize(items) : 0;
        for (int i = 0; i < itemCount; ++i) {
            ParsedJson* user = JsonArrayGet(items, i);
            if (!user) continue;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string username = JsonGetString(user, "username", "—");
            ImGui::Text("%s", username.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", JsonGetString(user, "email", "—"));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%s", JsonGetString(user, "ip", "—"));
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%s", JsonGetString(user, "hwid", "—"));
            ImGui::TableSetColumnIndex(4);
            bool banned = JsonGetBool(user, "banned", false);
            FreeParsedJson(user);
            if (banned) {
                CyberWidgets::Badge(OGS_CSTR("BANIDO"), CyberWidgets::TextTone::Error);
            } else {
                CyberWidgets::Badge(OGS_CSTR("ATIVO"), CyberWidgets::TextTone::Success);
            }
            ImGui::TableSetColumnIndex(5);
            ImGui::PushID(username.c_str());
            if (ImGui::SmallButton(OGS_CSTR("Detalhes"))) {
                state.selectedUser = username;
                state.userDetailsOpen = true;
            }
            ImGui::SameLine();
            if (banned) {
                if (ImGui::SmallButton(OGS_CSTR("Desbanir"))) {
                    auto resp = api->UnbanUser(username);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Utilizador desbanido"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                }
            } else {
                if (ImGui::SmallButton(OGS_CSTR("Banir"))) {
                    ImGui::OpenPopup("BanUserPopup");
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(OGS_CSTR("Reset HWID"))) {
                auto resp = api->ResetHwid(username);
                if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("HWID resetado"), CyberWidgets::ToastType::Success);
                else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
            }
            ImGui::PopID();

            if (ImGui::BeginPopupModal("BanUserPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char reason[256]{};
                ImGui::Text("Banir utilizador: %s", username.c_str());
                ImGui::InputTextWithHint("##ban_reason", "Motivo...", reason, sizeof(reason));
                ImGui::Dummy(ImVec2(0, S(10.f)));
                if (ImGui::Button("Confirmar", ImVec2(S(100.f), 0))) {
                    auto resp = api->BanUser(username, reason);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Utilizador banido"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                    reason[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancelar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::EndTable();
    }
    CyberWidgets::EndCard();

    // Bulk actions
    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("AÇÕES EM MASSA"), 0.f);
    static char bulkUsers[1024]{};
    static char bulkReason[256]{};
    static int bulkAction = 0;
    ImGui::InputTextMultiline("##bulk_users", bulkUsers, sizeof(bulkUsers), ImVec2(-1, S(80.f)), ImGuiInputTextFlags_AllowTabInput);
    ImGui::Dummy(ImVec2(0.f, S(6.f)));
    ImGui::SetNextItemWidth(S(150.f));
    const char* actions[] = {"ban", "unban", "reset-hwid"};
    ImGui::Combo("##bulk_action", &bulkAction, actions, IM_ARRAYSIZE(actions));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(250.f));
    ImGui::InputTextWithHint("##bulk_reason", "Motivo (obrigatório para ban)", bulkReason, sizeof(bulkReason));
    ImGui::SameLine();
    if (CyberWidgets::GoldButton(OGS_CSTR("EXECUTAR"), ImVec2(S(120.f), S(28.f)))) {
        std::vector<std::string> users;
        std::stringstream ss(bulkUsers);
        std::string line;
        while (std::getline(ss, line)) {
            line.erase(std::remove_if(line.begin(), line.end(), ::isspace), line.end());
            if (!line.empty()) users.push_back(line);
        }
        if (!users.empty()) {
            auto resp = api->BulkUserAction(users, actions[bulkAction], bulkReason);
            if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Ação em massa concluída"), CyberWidgets::ToastType::Success);
            else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    // User details modal
    if (state.userDetailsOpen && !state.selectedUser.empty()) {
        ImGui::OpenPopup("UserDetails");
        state.userDetailsOpen = false;
    }
    if (ImGui::BeginPopupModal("UserDetails", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Detalhes do utilizador: %s", state.selectedUser.c_str());
        ImGui::Separator();
        auto detailResp = api->GetUser(state.selectedUser);
        if (detailResp.success()) {
            ParsedJson* u = ParseAdminResponse(detailResp);
            ImGui::Text("Email: %s", JsonGetString(u, "email", "—"));
            ImGui::Text("IP: %s", JsonGetString(u, "ip", "—"));
            ImGui::Text("HWID: %s", JsonGetString(u, "hwid", "—"));
            ImGui::Text("Criado: %s", JsonGetString(u, "createdate", "—"));
            ImGui::Text("Último login: %s", JsonGetString(u, "lastlogin", "—"));
            ImGui::Text("Banido: %s", JsonGetBool(u, "banned", false) ? "Sim" : "Não");
            ImGui::Text("Subscrições:");
            if (ParsedJson* subscriptions = JsonGetArray(u, "subscriptions")) {
                for (int i = 0; i < JsonArraySize(subscriptions); ++i) {
                    ParsedJson* sub = JsonArrayGet(subscriptions, i);
                    const std::string name = JsonGetString(sub, "subscription", "—");
                    const std::string level = JsonGetString(sub, "level", "—");
                    const std::string expiry = JsonGetString(sub, "expiry", "—");
                    ImGui::BulletText("%s (nível %s, expira %s)", name.c_str(), level.c_str(), expiry.c_str());
                    FreeParsedJson(sub);
                }
                FreeParsedJson(subscriptions);
            }
            FreeParsedJson(u);
        } else {
            ImGui::TextColored(CyberTheme::Colors.Error, "Erro: %s", detailResp.error.c_str());
        }
        ImGui::Dummy(ImVec2(0, S(10.f)));
        if (ImGui::Button("Fechar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ─── LICENSES ───
void DrawAdminLicenses() {
    UiState& state = State();
    auto* api = GetApiClient();

    DrawApiConfig();
    if (!api) return;

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("GERAR LICENÇAS"), 0.f);
    static int genDays = 30, genLevel = 1, genAmount = 1;
    static char genMask[128]{}, genNote[256]{};
    ImGui::SetNextItemWidth(S(100.f)); ImGui::InputInt("Dias", &genDays, 1, 30); genDays = std::clamp(genDays, 1, 3650);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(100.f)); ImGui::InputInt("Nível", &genLevel, 1, 10); genLevel = std::clamp(genLevel, 1, 100);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(100.f)); ImGui::InputInt("Quantidade", &genAmount, 1, 10); genAmount = std::clamp(genAmount, 1, 100);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(200.f)); ImGui::InputTextWithHint("##gen_mask", "Máscara (opcional)", genMask, sizeof(genMask));
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(250.f)); ImGui::InputTextWithHint("##gen_note", "Nota (opcional)", genNote, sizeof(genNote));
    ImGui::SameLine();
    if (CyberWidgets::GoldButton(OGS_CSTR("GERAR"), ImVec2(S(120.f), S(28.f)))) {
        auto resp = api->GenerateLicenses(genDays, genLevel, genAmount, genMask, genNote);
        if (resp.success()) {
            CyberWidgets::NotifyAdmin(OGS_CSTR("Licenças geradas com sucesso"), CyberWidgets::ToastType::Success);
            genMask[0] = genNote[0] = '\0';
        } else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("CRIAR PROMO CODE"), 0.f);
    ImGui::SetNextItemWidth(S(150.f)); ImGui::InputTextWithHint("##promo_code", "Código (ex: SUMMER2024)", &state.newPromoCode);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("Dias", &state.newPromoDays); state.newPromoDays = std::clamp(state.newPromoDays, 1, 3650);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("Nível", &state.newPromoLevel); state.newPromoLevel = std::clamp(state.newPromoLevel, 1, 100);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("Max usos", &state.newPromoMaxUses); state.newPromoMaxUses = std::clamp(state.newPromoMaxUses, 1, 10000);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(250.f)); ImGui::InputTextWithHint("##promo_note", "Nota", &state.newPromoNote);
    ImGui::SameLine();
    if (CyberWidgets::GoldButton(OGS_CSTR("CRIAR"), ImVec2(S(120.f), S(28.f)))) {
        if (!state.newPromoCode.empty()) {
            auto resp = api->CreatePromoCode(state.newPromoCode, state.newPromoDays, state.newPromoLevel, state.newPromoMaxUses, state.newPromoNote);
            if (resp.success()) {
                CyberWidgets::NotifyAdmin(OGS_CSTR("Promo code criado"), CyberWidgets::ToastType::Success);
                state.newPromoCode.clear(); state.newPromoNote.clear();
            } else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("FILTRO DE LICENÇAS"), 0.f);
    ImGui::SetNextItemWidth(S(300.f));
    ImGui::InputTextWithHint("##license_filter", "Filtrar por key, owner, note...", state.licenseFilter, sizeof(state.licenseFilter));
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("##lic_page", &state.licensePage); state.licensePage = std::max(1, state.licensePage);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("##lic_page_size", &state.licensePageSize); state.licensePageSize = std::clamp(state.licensePageSize, 10, 200);
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("EXPORTAR CSV"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(140.f), S(28.f)))) {
        auto resp = api->ExportLicensesCsv(state.licenseFilter);
        if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("CSV exportado"), CyberWidgets::ToastType::Success);
        else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));

    auto response = api->ListLicenses(state.licenseFilter, state.licensePage, state.licensePageSize);
    if (!response.success()) {
        CyberWidgets::BeginCard(OGS_CSTR("ERRO"), 0.f);
        CyberWidgets::TextLine(response.error.c_str(), CyberWidgets::TextTone::Error);
        CyberWidgets::EndCard();
        return;
    }

    auto respJson = ParseAdminResponse(response);
    if (!respJson) {
        CyberWidgets::BeginCard(OGS_CSTR("ERRO"), 0.f);
        CyberWidgets::TextLine(OGS_CSTR("Falha ao parsear resposta JSON"), CyberWidgets::TextTone::Error);
        CyberWidgets::EndCard();
        return;
    }
    auto items = JsonGetArray(respJson, "items");
    int total = JsonGetInt(respJson, "total", 0);
    int page = JsonGetInt(respJson, "page", 1);
    int pageSize = JsonGetInt(respJson, "pageSize", 50);

    CyberWidgets::BeginCard(OGS_CSTR("LISTA DE LICENÇAS"), 0.f);
    CyberWidgets::KeyValueRow(OGS_CSTR("Total"), std::to_string(total).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Página"), (std::to_string(page) + " / " + std::to_string((total + pageSize - 1) / pageSize)).c_str());
    ImGui::Dummy(ImVec2(0.f, S(6.f)));

    if (ImGui::BeginTable("##licenses_table", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, S(400.f)))) {
        ImGui::TableSetupColumn(OGS_CSTR("Key"), ImGuiTableColumnFlags_WidthFixed, S(180.f));
        ImGui::TableSetupColumn(OGS_CSTR("Status"), ImGuiTableColumnFlags_WidthFixed, S(80.f));
        ImGui::TableSetupColumn(OGS_CSTR("Nível"), ImGuiTableColumnFlags_WidthFixed, S(60.f));
        ImGui::TableSetupColumn(OGS_CSTR("Expira"), ImGuiTableColumnFlags_WidthFixed, S(120.f));
        ImGui::TableSetupColumn(OGS_CSTR("Owner"), ImGuiTableColumnFlags_WidthFixed, S(140.f));
        ImGui::TableSetupColumn(OGS_CSTR("Nota"), ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn(OGS_CSTR("Ações"), ImGuiTableColumnFlags_WidthFixed, S(280.f));
        ImGui::TableHeadersRow();

        int itemCount = items ? JsonArraySize(items) : 0;
        for (int i = 0; i < itemCount; ++i) {
            ParsedJson* key = JsonArrayGet(items, i);
            if (!key) continue;
            ImGui::TableNextRow();
            const std::string keyStr = JsonGetString(key, "key", "—");
            ImGui::TableSetColumnIndex(0); ImGui::Text("%s", keyStr.c_str());
            ImGui::TableSetColumnIndex(1);
            const std::string status = JsonGetString(key, "status", "—");
            if (status == "banned") CyberWidgets::Badge(OGS_CSTR("BANIDA"), CyberWidgets::TextTone::Error);
            else if (status == "used") CyberWidgets::Badge(OGS_CSTR("USADA"), CyberWidgets::TextTone::Info);
            else CyberWidgets::Badge(OGS_CSTR("LIVRE"), CyberWidgets::TextTone::Success);
            ImGui::TableSetColumnIndex(2); ImGui::Text("%s", JsonGetString(key, "level", "—"));
            ImGui::TableSetColumnIndex(3); ImGui::Text("%s", JsonGetString(key, "expiry", "—"));
            ImGui::TableSetColumnIndex(4); ImGui::Text("%s", JsonGetString(key, "owner", "—"));
            ImGui::TableSetColumnIndex(5); ImGui::Text("%s", JsonGetString(key, "note", "—"));
            FreeParsedJson(key);
            ImGui::TableSetColumnIndex(6);
            ImGui::PushID(keyStr.c_str());
            if (status == "banned") {
                if (ImGui::SmallButton(OGS_CSTR("Desbanir"))) {
                    auto resp = api->UnbanLicense(keyStr);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Licença desbanida"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                }
            } else {
                if (ImGui::SmallButton(OGS_CSTR("Banir"))) {
                    ImGui::OpenPopup("BanLicensePopup");
                }
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(OGS_CSTR("Atribuir"))) {
                state.selectedLicenseKey = keyStr;
                ImGui::OpenPopup("AssignLicensePopup");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(OGS_CSTR("Prolongar"))) {
                state.selectedLicenseKey = keyStr;
                state.extendDays = 30;
                state.extendNote.clear();
                ImGui::OpenPopup("ExtendLicensePopup");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(OGS_CSTR("Transferir"))) {
                state.selectedLicenseKey = keyStr;
                state.transferUsername.clear();
                state.transferNote.clear();
                ImGui::OpenPopup("TransferLicensePopup");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(OGS_CSTR("Eliminar"))) {
                ImGui::OpenPopup("DeleteLicensePopup");
            }
            ImGui::PopID();

            // Popups
            if (ImGui::BeginPopupModal("BanLicensePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char reason[256]{}; static bool userToo = false;
                ImGui::Text("Banir licença: %s", keyStr.c_str());
                ImGui::InputTextWithHint("##ban_lic_reason", "Motivo...", reason, sizeof(reason));
                ImGui::Checkbox("Banir utilizador também", &userToo);
                if (ImGui::Button("Confirmar", ImVec2(S(100.f), 0))) {
                    auto resp = api->BanLicense(keyStr, reason, userToo);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Licença banida"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                    reason[0] = '\0'; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancelar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("AssignLicensePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static char username[128]{};
                ImGui::Text("Atribuir licença %s a:", keyStr.c_str());
                ImGui::InputTextWithHint("##assign_user", "Username", username, sizeof(username));
                if (ImGui::Button("Confirmar", ImVec2(S(100.f), 0))) {
                    auto resp = api->AssignLicense(keyStr, username);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Licença atribuída"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                    username[0] = '\0'; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancelar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("ExtendLicensePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Prolongar licença: %s", state.selectedLicenseKey.c_str());
                ImGui::InputInt("Dias adicionais", &state.extendDays, 1, 30); state.extendDays = std::clamp(state.extendDays, 1, 3650);
                ImGui::InputTextWithHint("##extend_note", "Nota (opcional)", &state.extendNote);
                if (ImGui::Button("Confirmar", ImVec2(S(100.f), 0))) {
                    auto resp = api->ExtendLicense(state.selectedLicenseKey, state.extendDays, state.extendNote);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Licença prolongada"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancelar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("TransferLicensePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text("Transferir licença %s para:", state.selectedLicenseKey.c_str());
                ImGui::InputTextWithHint("##transfer_user", "Novo username", &state.transferUsername);
                ImGui::InputTextWithHint("##transfer_note", "Nota (opcional)", &state.transferNote);
                if (ImGui::Button("Confirmar", ImVec2(S(100.f), 0))) {
                    auto resp = api->TransferLicense(state.selectedLicenseKey, state.transferUsername, state.transferNote);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Licença transferida"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancelar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (ImGui::BeginPopupModal("DeleteLicensePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                static bool userToo = false;
                ImGui::Text("Eliminar licença: %s", keyStr.c_str());
                ImGui::Checkbox("Eliminar utilizador também", &userToo);
                if (ImGui::Button("Confirmar", ImVec2(S(100.f), 0))) {
                    auto resp = api->DeleteLicense(keyStr, userToo);
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Licença eliminada"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if (ImGui::Button("Cancelar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }
        ImGui::EndTable();
    }
    CyberWidgets::EndCard();
}

// ─── ANALYTICS ───
void DrawAdminAnalytics() {
    std::uint64_t totalSessions = 0, totalSeconds = 0;
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

    CyberWidgets::BeginCard(OGS_CSTR("UTILIZAÇÃO LOCAL"), 0.f);
    CyberWidgets::Badge(OGS_CSTR("DADOS DESTE DISPOSITIVO"), CyberWidgets::TextTone::Info);
    ImGui::Dummy(ImVec2(0.f, S(7.f)));
    CyberWidgets::KeyValueRow(OGS_CSTR("Sessões"), std::to_string(totalSessions).c_str());
    const std::string duration = FormatActiveDuration(totalSeconds);
    CyberWidgets::KeyValueRow(OGS_CSTR("Tempo total de menu"), duration.c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Jogo mais utilizado"), mostUsed);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    for (const GameRuntime& runtime : g_games) {
        if (!runtime.definition) continue;
        const GameHistory history = GetGameHistory(runtime.definition->launch_id);
        ImGui::PushID(runtime.definition->id);
        CyberWidgets::BeginCard(runtime.definition->name, 0.f);
        CyberWidgets::KeyValueRow(OGS_CSTR("Sessões"), std::to_string(history.sessionCount).c_str());
        const std::string gameDuration = FormatActiveDuration(history.totalActiveSeconds);
        CyberWidgets::KeyValueRow(OGS_CSTR("Tempo acumulado"), gameDuration.c_str());
        CyberWidgets::KeyValueRow(OGS_CSTR("Último resultado"), SessionResultDisplay(history.lastResult));
        CyberWidgets::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, S(8.f)));
    }

    // Remote analytics
    auto* api = GetApiClient();
    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("ANALYTICS GLOBAL (KEYAUTH)"), 0.f);
    if (api) {
        auto resp = api->GetAnalyticsSummary();
        if (resp.success()) {
            ParsedJson* a = ParseAdminResponse(resp);
            CyberWidgets::KeyValueRow(OGS_CSTR("Utilizadores totais"), std::to_string(JsonGetInt(a, "users.total", 0)).c_str());
            CyberWidgets::KeyValueRow(OGS_CSTR("Utilizadores banidos"), std::to_string(JsonGetInt(a, "users.banned", 0)).c_str());
            CyberWidgets::KeyValueRow(OGS_CSTR("Licenças totais"), std::to_string(JsonGetInt(a, "licenses.total", 0)).c_str());
            CyberWidgets::KeyValueRow(OGS_CSTR("Licenças usadas"), std::to_string(JsonGetInt(a, "licenses.used", 0)).c_str());
            CyberWidgets::KeyValueRow(OGS_CSTR("Licenças livres"), std::to_string(JsonGetInt(a, "licenses.unused", 0)).c_str());
            FreeParsedJson(a);
        } else {
            CyberWidgets::TextLine(resp.error.c_str(), CyberWidgets::TextTone::Error);
        }
    } else {
        CyberWidgets::TextLine(OGS_CSTR("Configure a Admin API para ver analytics remotos."), CyberWidgets::TextTone::Secondary);
    }
    CyberWidgets::EndCard();
}

// ─── GAMES / OFFSETS ───
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
        CyberWidgets::HealthRow(OGS_CSTR("Instalação"), runtime.installed ? OGS_CSTR("Detetada") : OGS_CSTR("Não detetada"),
            runtime.installed ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
        CyberWidgets::HealthRow(OGS_CSTR("Acesso"), OmniGhost::Licensing::HasGameAccess(game.id) ? OGS_CSTR("Autorizado") : OGS_CSTR("Sem licença"),
            OmniGhost::Licensing::HasGameAccess(game.id) ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
        CyberWidgets::HealthRow(OGS_CSTR("Runtime"), CardStateLabel(runtime.state),
            runtime.state == CardState::Error || runtime.state == CardState::LaunchFailed
                ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
        CyberWidgets::HealthRow(OGS_CSTR("Offsets"), OffsetStateText(compatibility.state), OffsetHealth(compatibility.state));
        if (!compatibility.detail.empty())
            CyberWidgets::TextLine(compatibility.detail.c_str(), CyberWidgets::TextTone::Secondary);
        const bool canRefresh = !g_offset_refresh_pending && OmniGhost::OffsetAuto::SupportsAutomaticRefresh(active);
        if (CyberWidgets::Button(OGS_CSTR("ATUALIZAR OFFSETS"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(180.f), S(32.f)), canRefresh))
            RequestOffsetRefresh(game.launch_id, false);
        CyberWidgets::EndCard();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0.f, S(9.f)));
    }
    DrawRemoteBoundary(OGS_CSTR("ROLLOUT E FEATURE FLAGS"),
        OGS_CSTR("Publicação de offsets, canary rollout e perfis oficiais precisam de artefactos validados por SHA-256 e de um serviço de configuração autenticado."), false);
}

// ─── BUILDS / RELEASES ───
void DrawAdminBuildInfo() {
    const auto update = OmniGhost::Update::UpdateService::Instance().GetSnapshot();
    CyberWidgets::BeginCard(OGS_CSTR("BUILD ATUAL"), 0.f);
    CyberWidgets::KeyValueRow(OGS_CSTR("Versão"), OmniGhost::BuildInfo::AppVersion);
    CyberWidgets::KeyValueRow(OGS_CSTR("Build ID"), OmniGhost::BuildInfo::BuildId);
    CyberWidgets::KeyValueRow(OGS_CSTR("Commit"), OmniGhost::BuildInfo::CommitId);
    CyberWidgets::KeyValueRow(OGS_CSTR("Configuração"), OmniGhost::BuildInfo::Configuration);
    CyberWidgets::KeyValueRow(OGS_CSTR("Arquitetura"), OmniGhost::BuildInfo::Architecture);
    CyberWidgets::KeyValueRow(OGS_CSTR("Canal"), OmniGhost::BuildInfo::ReleaseChannel);
    CyberWidgets::KeyValueRow(OGS_CSTR("Toolchain"), OmniGhost::BuildInfo::ToolchainVersion);
    CyberWidgets::HealthRow(OGS_CSTR("Reprodutível"), OmniGhost::BuildInfo::Reproducible ? "Sim" : OGS_CSTR("Não confirmado"),
        OmniGhost::BuildInfo::Reproducible ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("UPDATE CLIENT"), 0.f);
    CyberWidgets::HealthRow(OGS_CSTR("Estado"), UpdateStatusText(update),
        update.status == OmniGhost::Update::Status::Error ? CyberWidgets::HealthStatus::Error : CyberWidgets::HealthStatus::Ok);
    CyberWidgets::KeyValueRow(OGS_CSTR("Instalada"), update.installedVersion.empty() ? "—" : update.installedVersion.c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Disponível"), update.availableVersion.empty() ? "—" : update.availableVersion.c_str());
    if (CyberWidgets::Button(OGS_CSTR("VERIFICAR AGORA"), CyberWidgets::ButtonStyle::Primary, ImVec2(S(165.f), S(34.f))))
        OmniGhost::Update::UpdateService::Instance().CheckAsync(true);
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("GITHUB ACTIONS"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(170.f), S(34.f))))
        OpenExternal(OGS_WCSTR("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/actions"), OGS_CSTR("Não foi possível abrir o GitHub Actions."));
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("RELEASES"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(120.f), S(34.f))))
        OpenExternal(OGS_WCSTR("https://github.com/rodrigomatossilva07-rgb/OmniGhost-Updates/releases"), OGS_CSTR("Não foi possível abrir os releases."));
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    const auto changelog = ChangelogService::Instance().GetSnapshot();
    CyberWidgets::BeginCard(OGS_CSTR("RELEASES RECENTES"), 0.f);
    if (changelog.releases.empty()) {
        CyberWidgets::EmptyState(OGS_CSTR("SEM DADOS"), OGS_CSTR("Atualiza o changelog para consultar releases publicadas."));
    } else {
        const std::size_t limit = (std::min<std::size_t>)(changelog.releases.size(), 5u);
        for (std::size_t index = 0; index < limit; ++index) {
            const auto& release = changelog.releases[index];
            CyberWidgets::KeyValueRow(release.version.c_str(), release.title.c_str());
        }
    }
    if (CyberWidgets::Button(OGS_CSTR("ATUALIZAR LISTA"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(155.f), S(32.f))))
        ChangelogService::Instance().RefreshAsync(true);
    CyberWidgets::EndCard();
}

// ─── SECURITY ───
void DrawAdminSecurity() {
    auto* api = GetApiClient();
    UiState& state = State();

    DrawApiConfig();
    if (!api) return;

    const bool protectedFeatures = true;
    const auto dma = mem.GetDiagnosticsSnapshot();
    CyberWidgets::BeginCard(OGS_CSTR("POSTURA DE SEGURANÇA"), 0.f);
    CyberWidgets::HealthRow(OGS_CSTR("Funcionalidades protegidas"), protectedFeatures ? OGS_CSTR("Autorizadas") : OGS_CSTR("Bloqueadas"),
        protectedFeatures ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::HealthRow(OGS_CSTR("Dependências DMA"), dma.dependencyIntegrityOk ? OGS_CSTR("Íntegras") : OGS_CSTR("Falha de integridade"),
        dma.dependencyIntegrityOk ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Error);
    CyberWidgets::KeyValueRow(OGS_CSTR("Session generation"), std::to_string(dma.sessionGeneration).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Process generation"), std::to_string(dma.processGeneration).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Falhas de leitura"), std::to_string(dma.readFailureCount).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Chamadas bloqueadas"), std::to_string(dma.blockedDataCalls).c_str());
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("EVENTOS LOCAIS RECENTES"), 0.f);
    const auto events = OmniGhost::SessionLog::RecentEvents(12);
    if (events.empty()) {
        CyberWidgets::EmptyState(OGS_CSTR("SEM EVENTOS"), OGS_CSTR("Não existem eventos registados nesta sessão."));
    } else {
        for (const std::string& event : events) {
            const std::string safe = OmniGhost::SessionLog::RedactForSupport(event);
            CyberWidgets::TextLine(safe.c_str(), CyberWidgets::TextTone::Secondary);
        }
    }
    if (CyberWidgets::Button(OGS_CSTR("EXPORTAR EVENTOS LOCAIS"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(200.f), S(28.f)))) {
        std::string allEvents;
        for (const auto& e : OmniGhost::SessionLog::RecentEvents(1000)) allEvents += e + "\n";
        CyberWidgets::CopyToClipboard(allEvents.c_str(), OGS_CSTR("Eventos locais copiados"));
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    // IP Bans
    CyberWidgets::BeginCard(OGS_CSTR("IP BANS (KEYAUTH)"), 0.f);
    auto ipBansResp = api->ListIpBans();
    if (ipBansResp.success()) {
        auto respJson = ParseAdminResponse(ipBansResp);
        auto bans = respJson ? JsonGetArray(respJson, "items") : nullptr;
        int total = bans ? JsonArraySize(bans) : 0;
        CyberWidgets::KeyValueRow(OGS_CSTR("Total"), std::to_string(total).c_str());
        ImGui::Dummy(ImVec2(0.f, S(6.f)));
        if (ImGui::BeginTable("##ipbans", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(OGS_CSTR("IP"), ImGuiTableColumnFlags_WidthFixed, S(150.f));
            ImGui::TableSetupColumn(OGS_CSTR("Motivo"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(OGS_CSTR("Criado em"), ImGuiTableColumnFlags_WidthFixed, S(180.f));
            ImGui::TableSetupColumn(OGS_CSTR("Ação"), ImGuiTableColumnFlags_WidthFixed, S(100.f));
            ImGui::TableHeadersRow();
            int banCount = bans ? JsonArraySize(bans) : 0;
            for (int i = 0; i < banCount; ++i) {
                ParsedJson* ban = JsonArrayGet(bans, i);
                if (!ban) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", JsonGetString(ban, "ip", "—"));
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", JsonGetString(ban, "reason", "—"));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", JsonGetString(ban, "created", "—"));
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(JsonGetString(ban, "ip", ""));
                if (ImGui::SmallButton(OGS_CSTR("Remover"))) {
                    auto resp = api->DeleteIpBan(JsonGetString(ban, "ip", ""));
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("IP ban removido"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                }
                ImGui::PopID();
                FreeParsedJson(ban);
            }
            ImGui::EndTable();
        }
        if (respJson) FreeParsedJson(respJson);
    } else {
        CyberWidgets::TextLine(ipBansResp.error.c_str(), CyberWidgets::TextTone::Error);
    }
    ImGui::Dummy(ImVec2(0.f, S(6.f)));
    static char newIp[64]{}, newIpReason[256]{};
    ImGui::SetNextItemWidth(S(150.f)); ImGui::InputTextWithHint("##new_ip", "IP address", newIp, sizeof(newIp));
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(300.f)); ImGui::InputTextWithHint("##new_ip_reason", "Motivo", newIpReason, sizeof(newIpReason));
    ImGui::SameLine();
    if (CyberWidgets::GoldButton(OGS_CSTR("BANIR IP"), ImVec2(S(120.f), S(28.f)))) {
        if (strlen(newIp) > 0 && strlen(newIpReason) > 0) {
            auto resp = api->CreateIpBan(newIp, newIpReason);
            if (resp.success()) { CyberWidgets::NotifyAdmin(OGS_CSTR("IP banido"), CyberWidgets::ToastType::Success); newIp[0] = newIpReason[0] = '\0'; }
            else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    // Login attempts
    CyberWidgets::BeginCard(OGS_CSTR("TENTATIVAS DE LOGIN (KEYAUTH)"), 0.f);
    ImGui::SetNextItemWidth(S(300.f));
    ImGui::InputTextWithHint("##login_filter", "Filtrar por username, IP...", state.userFilter, sizeof(state.userFilter));
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("##login_page", &state.loginAttemptPage); state.loginAttemptPage = std::max(1, state.loginAttemptPage);
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("BUSCAR"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(100.f), S(28.f)))) {}
    ImGui::Dummy(ImVec2(0.f, S(6.f)));
    auto loginResp = api->ListLoginAttempts(state.userFilter, state.loginAttemptPage, 50);
    if (loginResp.success()) {
        auto respJson = ParseAdminResponse(loginResp);
        auto logs = respJson ? JsonGetArray(respJson, "items") : nullptr;
        if (ImGui::BeginTable("##logins", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, S(300.f)))) {
            ImGui::TableSetupColumn(OGS_CSTR("Username"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(OGS_CSTR("IP"), ImGuiTableColumnFlags_WidthFixed, S(120.f));
            ImGui::TableSetupColumn(OGS_CSTR("Sucesso"), ImGuiTableColumnFlags_WidthFixed, S(80.f));
            ImGui::TableSetupColumn(OGS_CSTR("Timestamp"), ImGuiTableColumnFlags_WidthFixed, S(180.f));
            ImGui::TableSetupColumn(OGS_CSTR("Detalhes"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            int logCount = logs ? JsonArraySize(logs) : 0;
            for (int i = 0; i < logCount; ++i) {
                ParsedJson* log = JsonArrayGet(logs, i);
                if (!log) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", JsonGetString(log, "username", "—"));
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", JsonGetString(log, "ip", "—"));
                ImGui::TableSetColumnIndex(2);
                bool ok = JsonGetBool(log, "success", false);
                CyberWidgets::Badge(ok ? OGS_CSTR("OK") : OGS_CSTR("FALHA"), ok ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Error);
                ImGui::TableSetColumnIndex(3); ImGui::Text("%s", JsonGetString(log, "timestamp", "—"));
                ImGui::TableSetColumnIndex(4); ImGui::Text("%s", JsonGetString(log, "details", "—"));
                FreeParsedJson(log);
            }
            ImGui::EndTable();
        }
        if (respJson) FreeParsedJson(respJson);
    } else {
        CyberWidgets::TextLine(loginResp.error.c_str(), CyberWidgets::TextTone::Error);
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary(OGS_CSTR("AUDITORIA REMOTA"),
        OGS_CSTR("Bans de IP, tentativas de login e timelines por utilizador pertencem ao backend. Eventos locais não concedem autoridade remota."), false);
}

// ─── DEVELOPER TOOLS ───
void DrawAdminDevTools() {
    UiState& state = State();
    const auto dma = mem.GetDiagnosticsSnapshot();
    CyberWidgets::BeginCard(OGS_CSTR("DMA / TRANSPORTE"), 0.f);
    CyberWidgets::HealthRow(OGS_CSTR("Dispositivo"), dma.deviceOpen ? OGS_CSTR("Aberto") : (dma.deviceDetected ? OGS_CSTR("Detetado") : OGS_CSTR("Ausente")),
        dma.deviceOpen ? CyberWidgets::HealthStatus::Ok : CyberWidgets::HealthStatus::Warning);
    CyberWidgets::KeyValueRow(OGS_CSTR("PID associado"), std::to_string(dma.processId).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Reads"), std::to_string(dma.readRequestCount).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Scatter batches"), std::to_string(dma.scatterReadBatchCount).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("Scatter handles vivos"), std::to_string(dma.scatterHandlesLive).c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("VMM p95"), (std::to_string(dma.vmmLatencyP95Ms) + " ms").c_str());
    CyberWidgets::KeyValueRow(OGS_CSTR("VMM p99"), (std::to_string(dma.vmmLatencyP99Ms) + " ms").c_str());
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    CyberWidgets::BeginCard(OGS_CSTR("FERRAMENTAS LOCAIS"), 0.f);
    if (CyberWidgets::Button(OGS_CSTR("ATUALIZAR CONFIGS"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(175.f), S(34.f)))) {
        config_manager::RefreshList();
        CyberWidgets::NotifyAdmin(OGS_CSTR("Lista de configurações atualizada."), CyberWidgets::ToastType::Success);
    }
    ImGui::SameLine();
#if defined(NDEBUG)
    state.showImGuiMetrics = false;
    constexpr bool metricsAvailable = false;
#else
    constexpr bool metricsAvailable = true;
#endif
    if (CyberWidgets::Button(state.showImGuiMetrics ? OGS_CSTR("FECHAR IMGUI METRICS") : OGS_CSTR("IMGUI METRICS"),
                             CyberWidgets::ButtonStyle::Ghost, ImVec2(S(190.f), S(34.f)), metricsAvailable))
        state.showImGuiMetrics = !state.showImGuiMetrics;
#if defined(NDEBUG)
    CyberWidgets::InlineMessage(
        OGS_CSTR("Inspeção arbitrária de memória, simulação de falhas e force-crash não são expostos em builds Release."),
        CyberWidgets::TextTone::Info);
#else
    CyberWidgets::InlineMessage(OGS_CSTR("Build de desenvolvimento: ImGui Metrics está disponível para diagnóstico local."),
                                CyberWidgets::TextTone::Warning);
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    if (CyberWidgets::Button(OGS_CSTR("MEMORY INSPECTOR"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(180.f), S(30.f)))) {
        // Open memory inspector window (would need separate implementation)
        CyberWidgets::NotifyAdmin(OGS_CSTR("Memory Inspector: implementar janela separada"), CyberWidgets::ToastType::Info);
    }
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("SIMULAR FALHA DMA"), CyberWidgets::ButtonStyle::Warning, ImVec2(S(180.f), S(30.f)))) {
        // Simulate DMA read failure
        CyberWidgets::NotifyAdmin(OGS_CSTR("Simulação de falha DMA: implementar injeção de erro"), CyberWidgets::ToastType::Warning);
    }
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("FORCE CRASH"), CyberWidgets::ButtonStyle::Error, ImVec2(S(140.f), S(30.f)))) {
        if (ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyDown(ImGuiMod_Shift)) {
            *(volatile int*)0 = 0; // intentional crash for testing
        } else {
            CyberWidgets::NotifyAdmin(OGS_CSTR("Pressione Ctrl+Shift+Click para confirmar crash"), CyberWidgets::ToastType::Error);
        }
    }
#endif
    CyberWidgets::EndCard();

#if !defined(NDEBUG)
    if (state.showImGuiMetrics) ImGui::ShowMetricsWindow(&state.showImGuiMetrics);
#endif
}

// ─── SUPPORT ───
void DrawAdminSupport() {
    auto* api = GetApiClient();
    UiState& state = State();

    DrawApiConfig();
    if (!api) return;

    // Tickets
    CyberWidgets::BeginCard(OGS_CSTR("TICKETS DE SUPORTE"), 0.f);
    ImGui::SetNextItemWidth(S(300.f));
    ImGui::InputTextWithHint("##ticket_filter", "Filtrar por username, assunto...", state.userFilter, sizeof(state.userFilter));
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("##ticket_page", &state.ticketPage); state.ticketPage = std::max(1, state.ticketPage);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(80.f)); ImGui::InputInt("##ticket_page_size", &state.ticketPageSize); state.ticketPageSize = std::clamp(state.ticketPageSize, 10, 200);
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("BUSCAR"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(100.f), S(28.f)))) {}
    ImGui::Dummy(ImVec2(0.f, S(6.f)));

    auto ticketsResp = api->ListTickets(state.userFilter, state.ticketPage, state.ticketPageSize);
    if (ticketsResp.success()) {
        auto respJson = ParseAdminResponse(ticketsResp);
        auto tickets = respJson ? JsonGetArray(respJson, "items") : nullptr;
        if (ImGui::BeginTable("##tickets", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, S(300.f)))) {
            ImGui::TableSetupColumn(OGS_CSTR("ID"), ImGuiTableColumnFlags_WidthFixed, S(80.f));
            ImGui::TableSetupColumn(OGS_CSTR("Utilizador"), ImGuiTableColumnFlags_WidthFixed, S(120.f));
            ImGui::TableSetupColumn(OGS_CSTR("Assunto"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(OGS_CSTR("Status"), ImGuiTableColumnFlags_WidthFixed, S(100.f));
            ImGui::TableSetupColumn(OGS_CSTR("Criado"), ImGuiTableColumnFlags_WidthFixed, S(150.f));
            ImGui::TableSetupColumn(OGS_CSTR("Ações"), ImGuiTableColumnFlags_WidthFixed, S(140.f));
            ImGui::TableHeadersRow();
            int ticketCount = tickets ? JsonArraySize(tickets) : 0;
            for (int i = 0; i < ticketCount; ++i) {
                ParsedJson* ticket = JsonArrayGet(tickets, i);
                if (!ticket) continue;
                ImGui::TableNextRow();
                const std::string id = JsonGetString(ticket, "id", "—");
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", id.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", JsonGetString(ticket, "username", "—"));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", JsonGetString(ticket, "subject", "—"));
                ImGui::TableSetColumnIndex(3);
                const std::string status = JsonGetString(ticket, "status", "open");
                if (status == "closed") CyberWidgets::Badge(OGS_CSTR("FECHADO"), CyberWidgets::TextTone::Secondary);
                else CyberWidgets::Badge(OGS_CSTR("ABERTO"), CyberWidgets::TextTone::Success);
                ImGui::TableSetColumnIndex(4); ImGui::Text("%s", JsonGetString(ticket, "created", "—"));
                ImGui::TableSetColumnIndex(5);
                ImGui::PushID(id.c_str());
                if (ImGui::SmallButton(OGS_CSTR("Ver"))) {
                    state.selectedTicket = id;
                    state.ticketDetailsOpen = true;
                }
                ImGui::SameLine();
                if (status == "open") {
                    if (ImGui::SmallButton(OGS_CSTR("Responder"))) {
                        state.selectedTicket = id;
                        ImGui::OpenPopup("ReplyTicketPopup");
                    }
                }
                ImGui::PopID();
                FreeParsedJson(ticket);
            }
            ImGui::EndTable();
        }
        if (tickets) FreeParsedJson(tickets);
        if (respJson) FreeParsedJson(respJson);
    } else {
        CyberWidgets::TextLine(ticketsResp.error.c_str(), CyberWidgets::TextTone::Error);
    }
    CyberWidgets::EndCard();

    // Ticket reply popup
    if (state.ticketDetailsOpen && !state.selectedTicket.empty()) {
        ImGui::OpenPopup("TicketDetails");
        state.ticketDetailsOpen = false;
    }
    if (ImGui::BeginPopupModal("TicketDetails", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Ticket: %s", state.selectedTicket.c_str());
        ImGui::Separator();
        auto detailResp = api->GetTicket(state.selectedTicket);
        if (detailResp.success()) {
            ParsedJson* t = ParseAdminResponse(detailResp);
            ImGui::Text("Utilizador: %s", JsonGetString(t, "username", "—"));
            ImGui::Text("Email: %s", JsonGetString(t, "email", "—"));
            ImGui::Text("Assunto: %s", JsonGetString(t, "subject", "—"));
            ImGui::Text("Status: %s", JsonGetString(t, "status", "open"));
            ImGui::Text("Criado: %s", JsonGetString(t, "created", "—"));
            ImGui::Separator();
            ImGui::TextWrapped("%s", JsonGetString(t, "message", "—"));
            if (ParsedJson* responses = JsonGetArray(t, "responses")) {
                ImGui::Separator();
                ImGui::Text("Respostas:");
                for (int i = 0; i < JsonArraySize(responses); ++i) {
                    ParsedJson* response = JsonArrayGet(responses, i);
                    const std::string date = JsonGetString(response, "date", "—");
                    const std::string author = JsonGetString(response, "author", "admin");
                    const std::string text = JsonGetString(response, "text", "—");
                    ImGui::BulletText("[%s] %s: %s", date.c_str(), author.c_str(), text.c_str());
                    FreeParsedJson(response);
                }
                FreeParsedJson(responses);
            }
            FreeParsedJson(t);
        } else {
            ImGui::TextColored(CyberTheme::Colors.Error, "Erro: %s", detailResp.error.c_str());
        }
        ImGui::Dummy(ImVec2(0, S(10.f)));
        if (ImGui::Button("Fechar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("ReplyTicketPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static char response[4096]{}; static bool closeTicket = false;
        ImGui::Text("Responder ao ticket %s", state.selectedTicket.c_str());
        ImGui::InputTextMultiline("##ticket_response", response, sizeof(response), ImVec2(S(500.f), S(200.f)));
        ImGui::Checkbox("Fechar ticket após responder", &closeTicket);
        if (ImGui::Button("Enviar", ImVec2(S(100.f), 0))) {
            auto resp = api->RespondToTicket(state.selectedTicket, response, closeTicket);
            if (resp.success()) { CyberWidgets::NotifyAdmin(OGS_CSTR("Resposta enviada"), CyberWidgets::ToastType::Success); response[0] = '\0'; closeTicket = false; ImGui::CloseCurrentPopup(); }
            else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
        }
        ImGui::SameLine(); if (ImGui::Button("Cancelar", ImVec2(S(100.f), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    // Announcements
    CyberWidgets::BeginCard(OGS_CSTR("ANÚNCIOS"), 0.f);
    auto annResp = api->ListAnnouncements();
    if (annResp.success()) {
        auto respJson = ParseAdminResponse(annResp);
        auto anns = respJson ? JsonGetArray(respJson, "items") : nullptr;
        if (ImGui::BeginTable("##announcements", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(OGS_CSTR("Título"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(OGS_CSTR("Severidade"), ImGuiTableColumnFlags_WidthFixed, S(100.f));
            ImGui::TableSetupColumn(OGS_CSTR("Jogos"), ImGuiTableColumnFlags_WidthFixed, S(150.f));
            ImGui::TableSetupColumn(OGS_CSTR("Ativo"), ImGuiTableColumnFlags_WidthFixed, S(80.f));
            ImGui::TableSetupColumn(OGS_CSTR("Ações"), ImGuiTableColumnFlags_WidthFixed, S(100.f));
            ImGui::TableHeadersRow();
            int annCount = anns ? JsonArraySize(anns) : 0;
            for (int i = 0; i < annCount; ++i) {
                ParsedJson* ann = JsonArrayGet(anns, i);
                if (!ann) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", JsonGetString(ann, "title", "—"));
                ImGui::TableSetColumnIndex(1);
                const char* sev = JsonGetString(ann, "severity", "info");
                CyberWidgets::Badge(sev, strcmp(sev, "critical") == 0 ? CyberWidgets::TextTone::Error : (strcmp(sev, "warning") == 0 ? CyberWidgets::TextTone::Warning : CyberWidgets::TextTone::Info));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", JsonGetString(ann, "games", "all"));
                ImGui::TableSetColumnIndex(3);
                bool active = JsonGetBool(ann, "active", true);
                CyberWidgets::Badge(active ? OGS_CSTR("SIM") : OGS_CSTR("NÃO"), active ? CyberWidgets::TextTone::Success : CyberWidgets::TextTone::Secondary);
                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(JsonGetString(ann, "id", ""));
                if (ImGui::SmallButton(OGS_CSTR("Editar"))) {
                    // TODO: edit popup
                }
                ImGui::SameLine();
                if (ImGui::SmallButton(OGS_CSTR("Apagar"))) {
                    auto resp = api->DeleteAnnouncement(JsonGetString(ann, "id", ""));
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Anúncio apagado"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                }
                ImGui::PopID();
                FreeParsedJson(ann);
            }
            ImGui::EndTable();
        }
        if (respJson) FreeParsedJson(respJson);
    } else {
        CyberWidgets::TextLine(annResp.error.c_str(), CyberWidgets::TextTone::Error);
    }
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    ImGui::Separator();
    ImGui::Text("Novo anúncio:");
    ImGui::SetNextItemWidth(S(250.f)); ImGui::InputTextWithHint("##ann_title", "Título", &state.newAnnTitle);
    ImGui::SetNextItemWidth(S(500.f)); ImGui::InputTextMultiline("##ann_body", &state.newAnnBody, ImVec2(-1, S(80.f)));
    int severity = state.newAnnSeverity == "critical" ? 2 : state.newAnnSeverity == "warning" ? 1 : 0;
    ImGui::SetNextItemWidth(S(120.f));
    if (ImGui::Combo("##ann_severity", &severity, "info\0warning\0critical\0"))
        state.newAnnSeverity = severity == 2 ? "critical" : severity == 1 ? "warning" : "info";
    ImGui::SameLine(); ImGui::Text("Jogos (vazio = todos):");
    static std::string announcementGames;
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(200.f)); ImGui::InputTextWithHint("##ann_games", "cs2,fivem,rust...", &announcementGames);
    if (CyberWidgets::GoldButton(OGS_CSTR("CRIAR ANÚNCIO"), ImVec2(S(180.f), S(30.f)))) {
        if (!state.newAnnTitle.empty() && !state.newAnnBody.empty()) {
            std::vector<std::string> games;
            std::istringstream gameStream(announcementGames);
            for (std::string game; std::getline(gameStream, game, ',');) {
                game.erase(std::remove_if(game.begin(), game.end(), [](unsigned char c) { return std::isspace(c); }), game.end());
                if (!game.empty()) games.push_back(game);
            }
            auto resp = api->CreateAnnouncement(state.newAnnTitle, state.newAnnBody, state.newAnnSeverity, games);
            if (resp.success()) { CyberWidgets::NotifyAdmin(OGS_CSTR("Anúncio criado"), CyberWidgets::ToastType::Success); state.newAnnTitle.clear(); state.newAnnBody.clear(); }
            else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    // Maintenance windows
    CyberWidgets::BeginCard(OGS_CSTR("JANELAS DE MANUTENÇÃO"), 0.f);
    auto maintResp = api->ListMaintenanceWindows();
    if (maintResp.success()) {
        auto respJson = ParseAdminResponse(maintResp);
        auto windows = respJson ? JsonGetArray(respJson, "items") : nullptr;
        if (ImGui::BeginTable("##maintenance", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn(OGS_CSTR("Título"), ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(OGS_CSTR("Início"), ImGuiTableColumnFlags_WidthFixed, S(180.f));
            ImGui::TableSetupColumn(OGS_CSTR("Fim"), ImGuiTableColumnFlags_WidthFixed, S(180.f));
            ImGui::TableSetupColumn(OGS_CSTR("Jogos"), ImGuiTableColumnFlags_WidthFixed, S(150.f));
            ImGui::TableSetupColumn(OGS_CSTR("Ações"), ImGuiTableColumnFlags_WidthFixed, S(100.f));
            ImGui::TableHeadersRow();
            int winCount = windows ? JsonArraySize(windows) : 0;
            for (int i = 0; i < winCount; ++i) {
                ParsedJson* w = JsonArrayGet(windows, i);
                if (!w) continue;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", JsonGetString(w, "title", "—"));
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", JsonGetString(w, "starts", "—"));
                ImGui::TableSetColumnIndex(2); ImGui::Text("%s", JsonGetString(w, "ends", "—"));
                ImGui::TableSetColumnIndex(3); ImGui::Text("%s", JsonGetString(w, "games", "all"));
                ImGui::TableSetColumnIndex(4);
                ImGui::PushID(JsonGetString(w, "id", ""));
                if (ImGui::SmallButton(OGS_CSTR("Apagar"))) {
                    auto resp = api->DeleteMaintenanceWindow(JsonGetString(w, "id", ""));
                    if (resp.success()) CyberWidgets::NotifyAdmin(OGS_CSTR("Janela removida"), CyberWidgets::ToastType::Success);
                    else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
                }
                ImGui::PopID();
                FreeParsedJson(w);
            }
            ImGui::EndTable();
        }
        if (respJson) FreeParsedJson(respJson);
    } else {
        CyberWidgets::TextLine(maintResp.error.c_str(), CyberWidgets::TextTone::Error);
    }
    ImGui::Dummy(ImVec2(0.f, S(8.f)));
    ImGui::Separator();
    ImGui::Text("Nova janela de manutenção:");
    ImGui::SetNextItemWidth(S(250.f)); ImGui::InputTextWithHint("##maint_title", "Título", &state.newMaintTitle);
    ImGui::SetNextItemWidth(S(500.f)); ImGui::InputTextMultiline("##maint_desc", &state.newMaintDesc, ImVec2(-1, S(60.f)));
    ImGui::SetNextItemWidth(S(180.f)); ImGui::InputScalar("Início (Unix)", ImGuiDataType_S64, &state.newMaintStart);
    ImGui::SameLine(); ImGui::SetNextItemWidth(S(180.f)); ImGui::InputScalar("Fim (Unix)", ImGuiDataType_S64, &state.newMaintEnd);
    static std::string maintenanceGames;
    ImGui::SetNextItemWidth(S(200.f)); ImGui::InputTextWithHint("##maint_games", "cs2,fivem,rust... (vazio = todos)", &maintenanceGames);
    if (CyberWidgets::GoldButton(OGS_CSTR("CRIAR JANELA"), ImVec2(S(180.f), S(30.f)))) {
        if (!state.newMaintTitle.empty() && !state.newMaintDesc.empty() && state.newMaintStart > 0 && state.newMaintEnd > state.newMaintStart) {
            std::vector<std::string> games;
            std::istringstream gameStream(maintenanceGames);
            for (std::string game; std::getline(gameStream, game, ',');) {
                game.erase(std::remove_if(game.begin(), game.end(), [](unsigned char c) { return std::isspace(c); }), game.end());
                if (!game.empty()) games.push_back(game);
            }
            auto resp = api->CreateMaintenanceWindow(state.newMaintTitle, state.newMaintDesc, state.newMaintStart, state.newMaintEnd, games);
            if (resp.success()) { CyberWidgets::NotifyAdmin(OGS_CSTR("Janela de manutenção criada"), CyberWidgets::ToastType::Success); state.newMaintTitle.clear(); state.newMaintDesc.clear(); state.newMaintStart = state.newMaintEnd = 0; }
            else CyberWidgets::NotifyAdmin(resp.error.c_str(), CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    // Diagnostics
    CyberWidgets::BeginCard(OGS_CSTR("DIAGNÓSTICO E SUPORTE"), 0.f);
    CyberWidgets::TextLine(OGS_CSTR("Os pacotes de diagnóstico são limitados e redigidos; não incluem credenciais nem licenças."), CyberWidgets::TextTone::Secondary);
    if (CyberWidgets::Button(OGS_CSTR("COPIAR DIAGNÓSTICO"), CyberWidgets::ButtonStyle::Secondary, ImVec2(S(190.f), S(34.f)))) {
        const std::string diagnostics = BuildDiagnosticsText();
        CyberWidgets::CopyToClipboard(diagnostics.c_str(), OGS_CSTR("Diagnóstico copiado"));
    }
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("ABRIR LOGS"), CyberWidgets::ButtonStyle::Ghost, ImVec2(S(135.f), S(34.f))))
        ShellExecuteW(nullptr, OGS_WCSTR("open"), OmniGhost::Paths::Logs().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ImGui::SameLine();
    if (CyberWidgets::Button(OGS_CSTR("CRIAR PACOTE"), CyberWidgets::ButtonStyle::Primary, ImVec2(S(145.f), S(34.f)))) {
        std::filesystem::path packagePath;
        std::string error;
        if (OmniGhost::Diagnostics::CreatePackage(BuildDiagnosticsText(), packagePath, error)) {
            CyberWidgets::NotifyAdmin(OGS_CSTR("Pacote de diagnóstico criado."), CyberWidgets::ToastType::Success);
            ShellExecuteW(nullptr, OGS_WCSTR("open"), packagePath.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            CyberWidgets::NotifyAdmin(error.empty() ? OGS_CSTR("Não foi possível criar o pacote.") : error.c_str(), CyberWidgets::ToastType::Error);
        }
    }
    CyberWidgets::EndCard();

    ImGui::Dummy(ImVec2(0.f, S(12.f)));
    DrawRemoteBoundary(OGS_CSTR("OPERAÇÕES E COMUNICAÇÕES"),
        OGS_CSTR("Tickets, anúncios, FAQ e janelas de manutenção precisam de um serviço editorial autenticado e histórico de auditoria."), false);
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
    BeginControlPage(OGS_CSTR("##launcher_admin"), display);
    DrawPageHeading(Loc::Tr("launcher.admin"), Loc::Tr("launcher.admin.subtitle"));

    if (!OmniGhost::Licensing::IsAdmin()) {
        CyberWidgets::BeginCard(OGS_CSTR("ACESSO NEGADO"), 0.f);
        CyberWidgets::HealthRow(OGS_CSTR("Admin"), OGS_CSTR("Subscrição administrativa necessária"), CyberWidgets::HealthStatus::Error);
        CyberWidgets::TextLine(OGS_CSTR("A sessão deixou de possuir autorização. Reautentica a conta para continuar."), CyberWidgets::TextTone::Secondary);
        CyberWidgets::EndCard();
        EndControlPage();
        return;
    }

    const float sidebarWidth = S(220.f);
    const float gap = S(14.f);
    ImGui::BeginChild(OGS_CSTR("##admin_navigation"), ImVec2(sidebarWidth, -1.f), true, ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetScrollX(0.0f);
    AdminPanel::DrawSectionNavigation();
    ImGui::EndChild();
    ImGui::SameLine(0.f, gap);
    ImGui::BeginChild(OGS_CSTR("##admin_content"), ImVec2(0.f, -1.f), false);
    AdminPanel::DrawContent();
    ImGui::EndChild();

    EndControlPage();
}
