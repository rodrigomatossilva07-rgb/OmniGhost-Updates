#pragma once
#include "../updater/http_client.h"
#include <functional>
#include <string>
#include <string_view>
#include <optional>
#include <vector>

namespace LauncherAdmin {

struct AdminConfig {
    std::string baseUrl;          // e.g. "https://admin.omnighost.store"
    std::string apiKey;           // format: "actor.secret" (Bearer token)
};

struct AdminResponse {
    int statusCode = 0;
    std::string body;             // raw JSON string
    std::string error;
    bool success() const { return statusCode >= 200 && statusCode < 300 && error.empty(); }

    // Helper to parse JSON (requires nlohmann/json.hpp in cpp)
    class JsonView {
        std::string& str;
    public:
        explicit JsonView(std::string& s) : str(s) {}
        // Parse and return nlohmann::json - implementation in cpp
        void* parse(); // Returns nlohmann::json*
    };
    JsonView json() { return JsonView(body); }
};

class AdminApiClient {
public:
    explicit AdminApiClient(const AdminConfig& config);

    // Users
    AdminResponse ListUsers(std::string_view filter = "", int page = 1, int pageSize = 50);
    AdminResponse GetUser(std::string_view username);
    AdminResponse BanUser(std::string_view username, std::string_view reason);
    AdminResponse UnbanUser(std::string_view username);
    AdminResponse ResetHwid(std::string_view username);
    AdminResponse BulkUserAction(const std::vector<std::string>& usernames, std::string_view action, std::string_view reason);
    AdminResponse ExportUsersCsv(std::string_view filter = "");

    // Licenses
    AdminResponse ListLicenses(std::string_view filter = "", int page = 1, int pageSize = 50);
    AdminResponse GenerateLicenses(int days, int level, int amount, std::string_view mask = "", std::string_view note = "");
    AdminResponse BanLicense(std::string_view key, std::string_view reason, bool userToo = false);
    AdminResponse UnbanLicense(std::string_view key);
    AdminResponse DeleteLicense(std::string_view key, bool userToo = false);
    AdminResponse AssignLicense(std::string_view key, std::string_view username);
    AdminResponse ExtendLicense(std::string_view key, int days, std::string_view note = "");
    AdminResponse TransferLicense(std::string_view key, std::string_view newUsername, std::string_view note = "");
    AdminResponse CreatePromoCode(std::string_view code, int days, int level, int maxUses, std::string_view note = "");
    AdminResponse ExportLicensesCsv(std::string_view filter = "");

    // Security
    AdminResponse ListIpBans();
    AdminResponse CreateIpBan(std::string_view ip, std::string_view reason);
    AdminResponse DeleteIpBan(std::string_view ip);
    AdminResponse ListLoginAttempts(std::string_view filter = "", int page = 1, int pageSize = 50);

    // Support
    AdminResponse ListTickets(std::string_view filter = "", int page = 1, int pageSize = 50);
    AdminResponse GetTicket(std::string_view id);
    AdminResponse RespondToTicket(std::string_view id, std::string_view response, bool close = false);
    AdminResponse CloseTicket(std::string_view id);
    AdminResponse ListAnnouncements();
    AdminResponse CreateAnnouncement(std::string_view title, std::string_view body, std::string_view severity, const std::vector<std::string>& targetGames);
    AdminResponse UpdateAnnouncement(std::string_view id, std::optional<std::string> title, std::optional<std::string> body,
                                     std::optional<std::string> severity, std::optional<std::vector<std::string>> targetGames, std::optional<bool> isActive);
    AdminResponse DeleteAnnouncement(std::string_view id);
    AdminResponse ListMaintenanceWindows();
    AdminResponse CreateMaintenanceWindow(std::string_view title, std::string_view description,
                                          int64_t startsAt, int64_t endsAt, const std::vector<std::string>& affectedGames);
    AdminResponse DeleteMaintenanceWindow(std::string_view id);

    // Analytics
    AdminResponse GetAnalyticsSummary();

    // Audit log
    AdminResponse GetAuditLog(int limit = 100);

    // Capabilities
    AdminResponse GetCapabilities();

    // Config hash for auto-reconnect
    size_t GetConfigHash() const;

private:
    AdminConfig config_;
    std::unique_ptr<OmniGhost::Update::IHttpClient> http_;
    std::atomic_bool cancelFlag_{false};

    std::string BuildUrl(std::string_view path) const;
    AdminResponse Request(std::string_view method, std::string_view path, std::string_view body = "");
    AdminResponse RequestInternal(std::string_view method, const std::string& url, const std::string& body);
    void AddAuthHeaders(std::string& headers) const;
};

}