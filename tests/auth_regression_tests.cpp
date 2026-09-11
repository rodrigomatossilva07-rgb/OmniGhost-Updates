#include "../src/auth/local_auth_service.h"
#include "../src/platform/session_log.h"

#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

// The local-auth service writes operational events and normally resolves the
// user data directory.  These minimal test-only implementations keep the
// suite isolated from the user's real profile and logs.
namespace OmniGhost::SessionLog {
void Write(Severity, Subsystem, std::string_view, std::initializer_list<Field>) {}
}

namespace OmniGhost::Paths {
std::filesystem::path LocalData() { return {}; }
bool EnsureUserDirectories() { return true; }
}

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

std::filesystem::path MakeTestPath() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetTempPathW(MAX_PATH, buffer);
    const auto root = std::filesystem::path(std::wstring(buffer, length)) /
        L"OmniGhost-auth-regression";
    std::error_code error;
    std::filesystem::create_directories(root, error);
    return root / L"auth.dat";
}

void ResetStorage(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    std::filesystem::remove(path.string() + ".tmp", error);
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    auth.SetStoragePathForTesting(path);
    auth.Initialize();
}

void TestRegisterAndLogin(const std::filesystem::path& path) {
    ResetStorage(path);
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();

    Expect(auth.Register("  USER@Example.COM ", "password123", false).Ok(),
           "registration should succeed");
    Expect(auth.HasAccount(), "registration should persist an account");
    Expect(auth.IsAuthenticated(), "registration should authenticate the user");
    Expect(auth.CurrentEmail() == "user@example.com", "email should be normalized");

    auth.Logout();
    Expect(!auth.IsAuthenticated(), "logout should clear the authenticated state");
    Expect(!auth.Login("user@example.com", "wrong-password", false).Ok(),
           "incorrect password must be rejected");
    Expect(auth.Login("USER@example.com", "password123", false).Ok(),
           "valid credentials should log in");
}

void TestRememberMe(const std::filesystem::path& path) {
    ResetStorage(path);
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();

    Expect(auth.Register("remember@example.com", "password123", true).Ok(),
           "remember-me registration should succeed");
    auth.Logout(false);
    auth.Initialize();
    Expect(auth.TryAutoLogin(), "remember-me should restore the session");
    Expect(auth.IsAuthenticated(), "auto-login should authenticate the user");

    auth.Logout(true);
    auth.Initialize();
    Expect(!auth.TryAutoLogin(), "forgetting remember-me must disable auto-login");
}

void TestCorruptStorage(const std::filesystem::path& path) {
    ResetStorage(path);
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << "not a valid OmniGhost auth record";
    }
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    auth.SetStoragePathForTesting(path);
    auth.Initialize();
    Expect(!auth.HasAccount(), "corrupt local auth data must fail closed");
    Expect(!auth.TryAutoLogin(), "corrupt local auth data must not auto-login");
}

} // namespace

int main() {
    const auto path = MakeTestPath();
    TestRegisterAndLogin(path);
    TestRememberMe(path);
    TestCorruptStorage(path);

    std::error_code error;
    std::filesystem::remove(path, error);
    if (failures != 0) return 1;
    std::cout << "PASS: local authentication regressions\n";
    return 0;
}
