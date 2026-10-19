#include "auth/keyauth_gateway.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class SuccessfulProvider final : public OmniGhost::Auth::IRemoteLicenseProvider {
public:
    OmniGhost::Auth::LicenseResult Authenticate(
        std::string_view, std::string_view, std::stop_token) override {
        return Success();
    }

    OmniGhost::Auth::LicenseResult Login(
        std::string_view, std::string_view, std::stop_token) override {
        ++loginCalls;
        return Success();
    }

    bool IsConfigured() const noexcept override { return true; }

    std::string GetModulePath() const override {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                                static_cast<DWORD>(path.size()));
        if (length == 0 || length >= path.size()) return {};
        path.resize(length);

        const int required = WideCharToMultiByte(CP_UTF8, 0, path.data(),
                                                  static_cast<int>(path.size()),
                                                  nullptr, 0, nullptr, nullptr);
        if (required <= 0) return {};
        std::string utf8(static_cast<std::size_t>(required), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, path.data(), static_cast<int>(path.size()),
                                utf8.data(), required, nullptr, nullptr) != required) {
            return {};
        }
        return utf8;
    }

    int loginCalls{};

private:
    static OmniGhost::Auth::LicenseResult Success() {
        return {OmniGhost::Auth::LicenseStatus::Valid, "", "test.ok", {}};
    }
};

}

int main() {
    auto provider = std::make_shared<SuccessfulProvider>();
    OmniGhost::Auth::LicenseGateway gateway(provider);

    const auto result = gateway.Login("test-user", "test-password");
    Require(result.Ok(), "normal login must pass the clear lockout state");
    Require(result.userMessage.empty(), "clear lockout must produce an empty message safely");
    Require(provider->loginCalls == 1, "provider login must be called exactly once");
    Require(gateway.IsAuthenticated(), "successful login must authenticate the gateway");

    std::cout << "PASS: auth gateway clear-lockout regression\n";
    return 0;
}
