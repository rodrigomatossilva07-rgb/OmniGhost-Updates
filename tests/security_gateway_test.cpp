#include "../src/auth/keyauth_gateway.h"
#include "../src/updater/update_types.h"

#include <iostream>
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
