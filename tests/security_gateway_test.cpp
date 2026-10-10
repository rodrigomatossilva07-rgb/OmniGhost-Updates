#include "../src/auth/keyauth_gateway.h"
#include "../src/updater/update_types.h"
#include "../src/games/Cs2/radar/radar_http.h"
#include "../src/games/Fivem/game/health_state.h"

#include <iostream>
#include <limits>
#include <memory>
#include <stop_token>
#include <string_view>

namespace {
using namespace OmniGhost::Auth;

class FakeProvider final : public IRemoteLicenseProvider {
public:
    LicenseStatus nextCheck = LicenseStatus::Valid;
    int checkCount = 0;

    LicenseResult Authenticate(std::string_view, std::string_view, std::stop_token) override {
        return {LicenseStatus::Valid, "ok", "test.ok", {}};
    }
    LicenseResult Login(std::string_view, std::string_view, std::stop_token) override {
        return {LicenseStatus::Valid, "ok", "test.ok", {}};
    }
    LicenseResult CheckSession(std::stop_token) override {
        ++checkCount;
        return {nextCheck, "check", "test.check", {}};
    }
    bool IsConfigured() const noexcept override { return true; }
};

bool Expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main() {
    using FiveM::PedHealthState;
    if (!Expect(FiveM::ClassifyPedHealth(125.f) == PedHealthState::Alive,
                "positive FiveM health is alive")) return 1;
    if (!Expect(FiveM::ClassifyPedHealth(0.f) == PedHealthState::Dead,
                "zero FiveM health is dead")) return 1;
    if (!Expect(FiveM::ClassifyPedHealth(-1.f) == PedHealthState::Unknown &&
                FiveM::ClassifyPedHealth(std::numeric_limits<float>::quiet_NaN()) == PedHealthState::Unknown &&
                FiveM::ClassifyPedHealth(1000.f) == PedHealthState::Unknown,
                "invalid FiveM health is unknown")) return 1;
    using CS2::WebRadar::Http::TokenOk;
    if (!Expect(TokenOk("GET /api/live HTTP/1.1\r\naUtHoRiZaTiOn: bEaReR test\r\n\r\n", "test"),
                "HTTP authorization is case insensitive")) return 1;
    if (!Expect(!TokenOk("GET /api/live?token=test HTTP/1.1\r\n\r\n", "test"),
                "URL tokens are rejected")) return 1;
    if (!Expect(!TokenOk("GET /api/live HTTP/1.1\r\nAuthorization: Bearer test\r\n", "test"),
                "fragmented headers are not accepted early")) return 1;
    if (!Expect(!TokenOk("GET /api/live HTTP/1.1\r\nAuthorization: Bearer test\r\nAuthorization: Bearer test\r\n\r\n", "test"),
                "duplicate authorization headers are rejected")) return 1;
    if (!Expect(!TokenOk("GET /api/live HTTP/1.1\r\nAuthorization: Bearer tesT\r\n\r\n", "test"),
                "token bytes remain case sensitive")) return 1;
    if (!Expect(!TokenOk("GET /api/live HTTP/1.1\r\n\r\n", ""),
                "empty configured token fails closed")) return 1;
    const OmniGhost::Update::Configuration updatePolicy{};
    if (!Expect(updatePolicy.requireAuthenticode && updatePolicy.requireManifestSignature,
                "updater trust policy fails closed")) return 1;
    auto provider = std::make_shared<FakeProvider>();
    LicenseGateway gateway(provider);
    if (!Expect(gateway.Login("account", "password").Ok(), "login")) return 1;
    if (!Expect(gateway.IsAuthenticated(), "login grants session")) return 1;

    provider->nextCheck = LicenseStatus::Timeout;
    if (!Expect(gateway.Revalidate().status == LicenseStatus::Timeout, "timeout classified as transient")) return 1;
    if (!Expect(gateway.IsAuthenticated(), "short network outage retains bounded session")) return 1;

    provider->nextCheck = LicenseStatus::Revoked;
    if (!Expect(gateway.Revalidate().status == LicenseStatus::Revoked, "revocation reaches gateway")) return 1;
    if (!Expect(!gateway.IsAuthenticated(), "revoked session denied")) return 1;
    if (!Expect(gateway.Revalidate().status == LicenseStatus::Invalid, "revoked session is not revived")) return 1;

    if (!Expect(gateway.Login("account", "password").Ok(), "fresh login restores session")) return 1;
    provider->nextCheck = LicenseStatus::Valid;
    if (!Expect(gateway.Revalidate().Ok(), "valid remote recheck")) return 1;
    gateway.Logout();
    if (!Expect(!gateway.IsAuthenticated(), "logout denied")) return 1;
    if (!Expect(provider->checkCount == 3, "only expected remote checks performed")) return 1;
    std::cout << "Security gateway tests: PASS\n";
    return 0;
}
