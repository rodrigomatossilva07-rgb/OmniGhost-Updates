#include "auth/launcher_account_service.h"

#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using OmniGhost::Auth::ILauncherApiTransport;
using OmniGhost::Auth::ILauncherSessionStore;
using OmniGhost::Auth::LauncherAccountService;
using OmniGhost::Auth::LauncherApiResponse;
using OmniGhost::Auth::StoredLauncherSession;

struct RequestRecord {
    std::string method;
    std::string path;
    std::string body;
    std::string token;
    std::string deviceId;
};

class FakeTransport final : public ILauncherApiTransport {
public:
    LauncherApiResponse Request(std::string_view method, std::string_view path,
                                std::string_view body, std::string_view token,
                                std::string_view deviceId) override {
        requests.push_back({std::string(method), std::string(path), std::string(body),
                            std::string(token), std::string(deviceId)});
        if (responses.empty()) return {0, {}, "missing fake response"};
        auto response = std::move(responses.front());
        responses.pop_front();
        return response;
    }

    std::deque<LauncherApiResponse> responses;
    std::vector<RequestRecord> requests;
};

class MemoryStore final : public ILauncherSessionStore {
public:
    bool Save(const StoredLauncherSession& value) override {
        session = value;
        present = saveSucceeds;
        return saveSucceeds;
    }
    bool Load(StoredLauncherSession& value) override {
        if (!present) return false;
        value = session;
        return true;
    }
    void Clear() noexcept override {
        present = false;
        session = {};
        ++clearCalls;
    }

    StoredLauncherSession session;
    bool present{};
    bool saveSucceeds{true};
    int clearCalls{};
};

[[noreturn]] void Fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Require(bool condition, const char* message) {
    if (!condition) Fail(message);
}

std::string LoginResponse(std::string licenses, std::string token = "ogls_token_one",
                          bool admin = false) {
    return "{\"ok\":true,\"account\":{\"email\":\"owner@example.com\",\"isAdmin\":" +
        std::string(admin ? "true" : "false") + "},\"licenses\":" + licenses +
        ",\"sessionToken\":\"" + token +
        "\",\"sessionExpiresAt\":\"2099-10-19T18:15:00.000Z\"}";
}

std::string ReadyLicense(std::string id, std::string product) {
    return "{\"id\":\"" + id + "\",\"product\":\"" + product +
        "\",\"plan\":\"lifetime\",\"keyauth_state\":\"ready\"," 
        "\"expires_at\":null,\"real_expires_at\":null}";
}

void InvalidLoginDoesNotSendRequest() {
    auto transport = std::make_shared<FakeTransport>();
    auto store = std::make_shared<MemoryStore>();
    LauncherAccountService service(transport, store);
    const auto result = service.Login("invalid", "");
    Require(!result.Ok(), "invalid login must fail");
    Require(transport->requests.empty(), "invalid login must not reach the network");
    Require(!store->present, "invalid login must not persist a session");
}

void AccountWithoutLicenseIsRejected() {
    auto transport = std::make_shared<FakeTransport>();
    auto store = std::make_shared<MemoryStore>();
    transport->responses.push_back({200, LoginResponse("[]"), {}});
    LauncherAccountService service(transport, store);
    const auto result = service.Login("owner@example.com", "correct-password");
    Require(!result.Ok(), "account without active license must fail");
    Require(result.diagnosticCode == "website.no_active_license", "missing license code must be stable");
    Require(!service.IsAuthenticated(), "license-less account must not remain authenticated");
}

void MultipleLicensesRequireExplicitSelectionAndCarryAdminClaim() {
    auto transport = std::make_shared<FakeTransport>();
    auto store = std::make_shared<MemoryStore>();
    const std::string licenses = "[" + ReadyLicense("license-cs2", "cs2") + "," +
        ReadyLicense("license-fivem", "fivem") + "]";
    transport->responses.push_back({200, LoginResponse(licenses, "ogls_multi", true), {}});
    LauncherAccountService service(transport, store);
    const auto result = service.Login("owner@example.com", "correct-password");
    Require(result.Ok(), "multiple valid licenses must authenticate");
    auto snapshot = service.Snapshot();
    Require(snapshot.licenses.size() == 2, "all licenses must be presented");
    Require(snapshot.selectedLicenseId.empty(), "multiple licenses must not be selected arbitrarily");
    Require(snapshot.isAdmin, "server admin claim must be retained");
    Require(service.SelectLicense("license-fivem"), "valid license selection must succeed");
    Require(service.HasProduct("cs2") && service.HasProduct("fivem"),
            "all owned products must remain available");
    Require(store->session.selectedLicenseId == "license-fivem", "selection must be persisted securely");
}

void WebsiteAdminCanUseEveryGameWithoutSyntheticLicenses() {
    auto transport = std::make_shared<FakeTransport>();
    auto store = std::make_shared<MemoryStore>();
    transport->responses.push_back({200, LoginResponse("[]", "ogls_admin", true), {}});
    LauncherAccountService service(transport, store);
    Require(service.Login("admin@example.com", "launcher-password").Ok(),
            "server-authorized admin must authenticate without a synthetic license");
    Require(service.HasProduct("cs2") && service.HasProduct("fivem") &&
            service.HasProduct("warzone") && service.HasProduct("fortnite"),
            "server-authorized admin must receive universal game access");
    Require(service.ProductDuration("cs2") == "Administrador",
            "admin access must be identified independently from a paid plan");
}

void RevokedAndSuspendedLicensesAreRejected() {
    for (const char* state : {"revoked", "suspended"}) {
        auto transport = std::make_shared<FakeTransport>();
        auto store = std::make_shared<MemoryStore>();
        const std::string license = "[{\"id\":\"blocked\",\"product\":\"cs2\"," 
            "\"plan\":\"lifetime\",\"keyauth_state\":\"" + std::string(state) +
            "\",\"expires_at\":null,\"real_expires_at\":null}]";
        transport->responses.push_back({200, LoginResponse(license), {}});
        LauncherAccountService service(transport, store);
        Require(!service.Login("owner@example.com", "correct-password").Ok(),
                "blocked license must be rejected");
        Require(!service.IsAuthenticated(), "blocked license must not authenticate");
    }
}

void RenewalRotatesTokenAndUnauthorizedRenewalClearsIt() {
    auto transport = std::make_shared<FakeTransport>();
    auto store = std::make_shared<MemoryStore>();
    const std::string licenses = "[" + ReadyLicense("license-cs2", "cs2") + "]";
    transport->responses.push_back({200, LoginResponse(licenses, "ogls_old"), {}});
    LauncherAccountService service(transport, store);
    Require(service.Login("owner@example.com", "correct-password").Ok(), "initial login must pass");
    transport->responses.push_back({200,
        "{\"ok\":true,\"sessionToken\":\"ogls_new\"," 
        "\"sessionExpiresAt\":\"2099-10-19T19:15:00.000Z\"}", {}});
    Require(service.Renew().Ok(), "renewal must pass");
    Require(transport->requests.back().token == "ogls_old", "renewal must authenticate with old token");
    Require(store->session.token == "ogls_new", "rotated token must replace old credential");
    transport->responses.push_back({401, "{\"error\":\"token_expired\"}", {}});
    Require(!service.Renew().Ok(), "expired token renewal must fail");
    Require(!service.IsAuthenticated(), "401 must clear authenticated state");
    Require(!store->present, "401 must delete stored credential");
}

void DeviceMismatchAndLogoutAreHandled() {
    {
        auto transport = std::make_shared<FakeTransport>();
        auto store = std::make_shared<MemoryStore>();
        transport->responses.push_back({403, "{\"error\":\"device_mismatch\"}", {}});
        LauncherAccountService service(transport, store);
        const auto result = service.Login("owner@example.com", "correct-password");
        Require(!result.Ok(), "device mismatch must fail");
        Require(!service.IsAuthenticated(), "device mismatch must not authenticate");
    }
    {
        auto transport = std::make_shared<FakeTransport>();
        auto store = std::make_shared<MemoryStore>();
        const std::string licenses = "[" + ReadyLicense("license-cs2", "cs2") + "]";
        transport->responses.push_back({200, LoginResponse(licenses), {}});
        transport->responses.push_back({204, {}, {}});
        LauncherAccountService service(transport, store);
        Require(service.Login("owner@example.com", "correct-password").Ok(), "logout setup login must pass");
        service.Logout();
        Require(!service.IsAuthenticated() && !store->present, "logout must clear local session first");
        Require(transport->requests.back().method == "DELETE", "logout must revoke remote session");
        Require(transport->requests.back().path == "/api/launcher/session", "logout path must be exact");
    }
}

void RegistrationUsesDedicatedLauncherContract() {
    auto transport = std::make_shared<FakeTransport>();
    auto store = std::make_shared<MemoryStore>();
    const std::string licenses = "[" + ReadyLicense("license-cs2", "cs2") + "]";
    transport->responses.push_back({200, LoginResponse(licenses), {}});
    LauncherAccountService service(transport, store);
    Require(service.Register("owner@example.com", "launcher-password", "purchased-license").Ok(),
            "valid launcher registration must pass");
    Require(transport->requests.size() == 1, "registration must send one request");
    Require(transport->requests[0].path == "/api/launcher/register", "registration route must be exact");
    Require(transport->requests[0].body.find("\"licenseKey\":\"purchased-license\"") != std::string::npos,
            "registration must submit the purchased license");
    Require(store->session.token == "ogls_token_one", "registration must only persist the returned token");
}

void RegistrationErrorsAreSafeAndActionable() {
    auto transport = std::make_shared<FakeTransport>();
    auto store = std::make_shared<MemoryStore>();
    transport->responses.push_back({403, "{\"error\":\"launcher_license_not_owned\"}", {}});
    LauncherAccountService service(transport, store);
    const auto result = service.Register(
        "owner@example.com", "launcher-password", "purchased-license");
    Require(!result.Ok(), "an unowned license must fail registration");
    Require(result.userMessage.find("não pertence") != std::string::npos,
            "registration must explain the ownership failure without exposing an internal code");
}

}

int main() {
    InvalidLoginDoesNotSendRequest();
    AccountWithoutLicenseIsRejected();
    MultipleLicensesRequireExplicitSelectionAndCarryAdminClaim();
    WebsiteAdminCanUseEveryGameWithoutSyntheticLicenses();
    RevokedAndSuspendedLicensesAreRejected();
    RenewalRotatesTokenAndUnauthorizedRenewalClearsIt();
    DeviceMismatchAndLogoutAreHandled();
    RegistrationUsesDedicatedLauncherContract();
    RegistrationErrorsAreSafeAndActionable();
    std::cout << "PASS: launcher account service regression suite\n";
    return 0;
}
