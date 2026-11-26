// nlohmann/json MUST be included FIRST to avoid macro conflicts with STL headers
#include <nlohmann/json.hpp>

#include "admin_api_client.h"
#include "../platform/internet_handle.h"
#include <sstream>
#include <algorithm>

namespace LauncherAdmin {

using json = nlohmann::json;

AdminApiClient::AdminApiClient(const AdminConfig& config)
    : config_(config), http_(std::make_unique<OmniGhost::Update::WinHttpClient>()) {}

std::string AdminApiClient::BuildUrl(std::string_view path) const {
    std::string url = config_.baseUrl;
    if (!url.empty() && url.back() == '/') url.pop_back();
    if (!path.empty() && path.front() != '/') url += "/";
    url += path;
    return url;
}

void AdminApiClient::AddAuthHeaders(std::string& headers) const {
    if (!config_.apiKey.empty()) {
        headers += "Authorization: Bearer " + config_.apiKey + "\r\n";
    }
    headers += "Content-Type: application/json\r\n";
    headers += "Accept: application/json\r\n";
}

AdminResponse AdminApiClient::Request(std::string_view method, std::string_view path, std::string_view body) {
    std::string url = BuildUrl(path);
    std::string bodyStr(body);
    return RequestInternal(method, url, bodyStr);
}

AdminResponse AdminApiClient::RequestInternal(std::string_view method, const std::string& url, const std::string& body) {
    AdminResponse result;
    std::wstring methodW(method.begin(), method.end());

    std::wstring wurl(url.begin(), url.end());
    URL_COMPONENTSW comp{};
    comp.dwStructSize = sizeof(comp);
    comp.dwHostNameLength = 1;
    comp.dwUrlPathLength = 1;
    comp.dwSchemeLength = 1;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &comp)) {
        result.error = "Invalid URL";
        return result;
    }

    std::vector<wchar_t> host(comp.dwHostNameLength + 1);
    std::vector<wchar_t> pathW(comp.dwUrlPathLength + 1);
    comp.lpszHostName = host.data();
    comp.dwHostNameLength = static_cast<DWORD>(host.size());
    comp.lpszUrlPath = pathW.data();
    comp.dwUrlPathLength = static_cast<DWORD>(pathW.size());
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &comp)) {
        result.error = "Invalid URL (crack)";
        return result;
    }

    auto session = OmniGhost::Platform::MakeWinHttpHandle(
        WinHttpOpen(L"OmniGhost-AdminApi/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { result.error = "WinHttpOpen failed"; return result; }
    WinHttpSetTimeouts(session.get(), 8000, 10000, 15000, 15000);

    auto connection = OmniGhost::Platform::MakeWinHttpHandle(
        WinHttpConnect(session.get(), comp.lpszHostName, comp.nPort, 0));
    if (!connection) { result.error = "WinHttpConnect failed"; return result; }

    auto request = OmniGhost::Platform::MakeWinHttpHandle(
        WinHttpOpenRequest(connection.get(), methodW.c_str(), comp.lpszUrlPath, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) { result.error = "WinHttpOpenRequest failed"; return result; }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols));

    std::string headers;
    AddAuthHeaders(headers);
    std::wstring wheaders(headers.begin(), headers.end());
    if (!WinHttpAddRequestHeaders(request.get(), wheaders.c_str(), static_cast<DWORD>(wheaders.size()), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        result.error = "WinHttpAddRequestHeaders failed";
        return result;
    }

    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            body.empty() ? nullptr : const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)) {
        result.error = "WinHttpSendRequest failed";
        return result;
    }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        result.error = "WinHttpReceiveResponse failed";
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
        result.error = "Failed to query status code";
        return result;
    }
    result.statusCode = static_cast<int>(statusCode);

    DWORD available = 0;
    std::string responseBody;
    do {
        if (!WinHttpQueryDataAvailable(request.get(), &available)) break;
        if (available == 0) break;
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), available, &read)) break;
        responseBody.append(buffer.data(), read);
    } while (available > 0);

    result.body = responseBody;

    if (result.statusCode >= 400) {
        try {
            json err = json::parse(responseBody);
            result.error = err.value("error", "HTTP " + std::to_string(result.statusCode));
            if (err.contains("message")) result.error += ": " + err["message"].get<std::string>();
        } catch (...) {
            result.error = "HTTP " + std::to_string(result.statusCode);
        }
    }
    return result;
}

// Users
AdminResponse AdminApiClient::ListUsers(std::string_view filter, int page, int pageSize) {
    std::ostringstream qs;
    qs << "/api/admin/users?page=" << page << "&pageSize=" << pageSize;
    if (!filter.empty()) qs << "&filter=" << filter;
    return Request("GET", qs.str());
}

AdminResponse AdminApiClient::GetUser(std::string_view username) {
    return Request("GET", "/api/admin/users/" + std::string(username));
}

AdminResponse AdminApiClient::BanUser(std::string_view username, std::string_view reason) {
    json body{{"reason", reason}};
    return Request("POST", "/api/admin/users/" + std::string(username) + "/ban", body.dump());
}

AdminResponse AdminApiClient::UnbanUser(std::string_view username) {
    return Request("POST", "/api/admin/users/" + std::string(username) + "/unban", "{}");
}

AdminResponse AdminApiClient::ResetHwid(std::string_view username) {
    return Request("POST", "/api/admin/users/" + std::string(username) + "/reset-hwid", "{}");
}

AdminResponse AdminApiClient::BulkUserAction(const std::vector<std::string>& usernames, std::string_view action, std::string_view reason) {
    json body;
    body["usernames"] = usernames;
    body["action"] = action;
    body["reason"] = reason;
    return Request("POST", "/api/admin/users/bulk", body.dump());
}

AdminResponse AdminApiClient::ExportUsersCsv(std::string_view filter) {
    std::ostringstream qs;
    qs << "/api/admin/users/export";
    if (!filter.empty()) qs << "?filter=" << filter;
    return Request("GET", qs.str());
}

// Licenses
AdminResponse AdminApiClient::ListLicenses(std::string_view filter, int page, int pageSize) {
    std::ostringstream qs;
    qs << "/api/admin/licenses?page=" << page << "&pageSize=" << pageSize;
    if (!filter.empty()) qs << "&filter=" << filter;
    return Request("GET", qs.str());
}

AdminResponse AdminApiClient::GenerateLicenses(int days, int level, int amount, std::string_view mask, std::string_view note) {
    json body;
    body["days"] = days;
    body["level"] = level;
    body["amount"] = amount;
    if (!mask.empty()) body["mask"] = mask;
    if (!note.empty()) body["note"] = note;
    return Request("POST", "/api/admin/licenses/generate", body.dump());
}

AdminResponse AdminApiClient::BanLicense(std::string_view key, std::string_view reason, bool userToo) {
    json body{{"reason", reason}, {"userToo", userToo}};
    return Request("POST", "/api/admin/licenses/" + std::string(key) + "/ban", body.dump());
}

AdminResponse AdminApiClient::UnbanLicense(std::string_view key) {
    return Request("POST", "/api/admin/licenses/" + std::string(key) + "/unban", "{}");
}

AdminResponse AdminApiClient::DeleteLicense(std::string_view key, bool userToo) {
    std::ostringstream qs;
    qs << "/api/admin/licenses/" << key << "?userToo=" << (userToo ? "true" : "false");
    return Request("DELETE", qs.str());
}

AdminResponse AdminApiClient::AssignLicense(std::string_view key, std::string_view username) {
    json body{{"username", username}};
    return Request("POST", "/api/admin/licenses/" + std::string(key) + "/assign", body.dump());
}

AdminResponse AdminApiClient::ExtendLicense(std::string_view key, int days, std::string_view note) {
    json body{{"days", days}};
    if (!note.empty()) body["note"] = note;
    return Request("POST", "/api/admin/licenses/" + std::string(key) + "/extend", body.dump());
}

AdminResponse AdminApiClient::TransferLicense(std::string_view key, std::string_view newUsername, std::string_view note) {
    json body{{"newUsername", newUsername}};
    if (!note.empty()) body["note"] = note;
    return Request("POST", "/api/admin/licenses/" + std::string(key) + "/transfer", body.dump());
}

AdminResponse AdminApiClient::CreatePromoCode(std::string_view code, int days, int level, int maxUses, std::string_view note) {
    json body;
    body["code"] = code;
    body["days"] = days;
    body["level"] = level;
    body["maxUses"] = maxUses;
    if (!note.empty()) body["note"] = note;
    return Request("POST", "/api/admin/promo-codes", body.dump());
}

AdminResponse AdminApiClient::ExportLicensesCsv(std::string_view filter) {
    std::ostringstream qs;
    qs << "/api/admin/licenses/export";
    if (!filter.empty()) qs << "?filter=" << filter;
    return Request("GET", qs.str());
}

// Security
AdminResponse AdminApiClient::ListIpBans() {
    return Request("GET", "/api/admin/security/ip-bans");
}

AdminResponse AdminApiClient::CreateIpBan(std::string_view ip, std::string_view reason) {
    json body{{"ip", ip}, {"reason", reason}};
    return Request("POST", "/api/admin/security/ip-bans", body.dump());
}

AdminResponse AdminApiClient::DeleteIpBan(std::string_view ip) {
    return Request("DELETE", "/api/admin/security/ip-bans/" + std::string(ip));
}

AdminResponse AdminApiClient::ListLoginAttempts(std::string_view filter, int page, int pageSize) {
    std::ostringstream qs;
    qs << "/api/admin/security/login-attempts?page=" << page << "&pageSize=" << pageSize;
    if (!filter.empty()) qs << "&filter=" << filter;
    return Request("GET", qs.str());
}

// Support
AdminResponse AdminApiClient::ListTickets(std::string_view filter, int page, int pageSize) {
    std::ostringstream qs;
    qs << "/api/admin/support/tickets?page=" << page << "&pageSize=" << pageSize;
    if (!filter.empty()) qs << "&filter=" << filter;
    return Request("GET", qs.str());
}

AdminResponse AdminApiClient::GetTicket(std::string_view id) {
    return Request("GET", "/api/admin/support/tickets/" + std::string(id));
}

AdminResponse AdminApiClient::RespondToTicket(std::string_view id, std::string_view response, bool close) {
    json body{{"response", response}, {"close", close}};
    return Request("POST", "/api/admin/support/tickets/" + std::string(id) + "/respond", body.dump());
}

AdminResponse AdminApiClient::CloseTicket(std::string_view id) {
    return Request("POST", "/api/admin/support/tickets/" + std::string(id) + "/close", "{}");
}

AdminResponse AdminApiClient::ListAnnouncements() {
    return Request("GET", "/api/admin/support/announcements");
}

AdminResponse AdminApiClient::CreateAnnouncement(std::string_view title, std::string_view body, std::string_view severity, const std::vector<std::string>& targetGames) {
    json j;
    j["title"] = title;
    j["body"] = body;
    j["severity"] = severity;
    j["targetGames"] = targetGames;
    return Request("POST", "/api/admin/support/announcements", j.dump());
}

AdminResponse AdminApiClient::UpdateAnnouncement(std::string_view id, std::optional<std::string> title, std::optional<std::string> body,
                                                 std::optional<std::string> severity, std::optional<std::vector<std::string>> targetGames, std::optional<bool> isActive) {
    json j;
    if (title) j["title"] = *title;
    if (body) j["body"] = *body;
    if (severity) j["severity"] = *severity;
    if (targetGames) j["targetGames"] = *targetGames;
    if (isActive) j["isActive"] = *isActive;
    return Request("PATCH", "/api/admin/support/announcements/" + std::string(id), j.dump());
}

AdminResponse AdminApiClient::DeleteAnnouncement(std::string_view id) {
    return Request("DELETE", "/api/admin/support/announcements/" + std::string(id));
}

AdminResponse AdminApiClient::ListMaintenanceWindows() {
    return Request("GET", "/api/admin/support/maintenance");
}

AdminResponse AdminApiClient::CreateMaintenanceWindow(std::string_view title, std::string_view description,
                                                      int64_t startsAt, int64_t endsAt, const std::vector<std::string>& affectedGames) {
    json j;
    j["title"] = title;
    j["description"] = description;
    j["startsAt"] = startsAt;
    j["endsAt"] = endsAt;
    j["affectedGames"] = affectedGames;
    return Request("POST", "/api/admin/support/maintenance", j.dump());
}

AdminResponse AdminApiClient::DeleteMaintenanceWindow(std::string_view id) {
    return Request("DELETE", "/api/admin/support/maintenance/" + std::string(id));
}

AdminResponse AdminApiClient::GetAnalyticsSummary() {
    return Request("GET", "/api/admin/analytics/summary");
}

AdminResponse AdminApiClient::GetAuditLog(int limit) {
    std::ostringstream qs;
    qs << "/api/admin/audit-log?limit=" << limit;
    return Request("GET", qs.str());
}

AdminResponse AdminApiClient::GetCapabilities() {
    return Request("GET", "/api/admin/capabilities");
}

size_t AdminApiClient::GetConfigHash() const {
    return std::hash<std::string>{}(config_.baseUrl + "|" + config_.apiKey);
}

// JsonView implementation
void* AdminResponse::JsonView::parse() {
    try {
        return new nlohmann::json(nlohmann::json::parse(str));
    } catch (...) {
        return nullptr;
    }
}

} // namespace LauncherAdmin