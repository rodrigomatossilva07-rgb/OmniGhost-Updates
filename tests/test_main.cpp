#include "auth/keyauth_gateway.h"
#include "auth/local_auth_service.h"
#include "config/app_settings.h"
#include "platform/path_security.h"
#include "platform/radar_access.h"
#include "platform/app_paths.h"
#include "platform/embedded_offsets.h"
#include "platform/embedded_resources.h"
#include "platform/startup_state.h"
#include "platform/shutdown_coordinator.h"
#include "globals.h"
#include "updater/install_engine.h"
#include "updater/update_types.h"
#include "updater/http_policy.h"
#include "launcher/update_time_utils.h"

#include <chrono>
#include <algorithm>
#include <Windows.h>
#include <array>
#include <atomic>
#include <thread>
#include <vector>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
namespace fs = std::filesystem;
using OmniGhost::Auth::LicenseResult;
using OmniGhost::Auth::LicenseStatus;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void TestShutdownCoordinator() {
    using namespace OmniGhost::Platform;
    ShutdownCoordinator coordinator;
    std::vector<int> order;
    Require(coordinator.Register(ShutdownComponent::Ui, 100, [&] { order.push_back(1); }),
        "UI shutdown registration rejected");
    Require(coordinator.Register(ShutdownComponent::Hardware, 600, [&] { order.push_back(6); }),
        "hardware shutdown registration rejected");
    Require(coordinator.Register(ShutdownComponent::Updater, 800, [&] { order.push_back(8); }),
        "updater shutdown registration rejected");
    // Component registration is unique: a later registration replaces rather
    // than duplicates the callback.
    Require(coordinator.Register(ShutdownComponent::Ui, 100, [&] { order.push_back(2); }),
        "UI shutdown replacement rejected");
    const auto generation = coordinator.BeginSession([&] { order.push_back(10); });
    Require(generation != 0, "invalid shutdown session generation");
    Require(coordinator.ShutdownAll("unit-test"), "first process shutdown did not execute");
    Require((order == std::vector<int>{10, 8, 6, 2}), "shutdown ordering or uniqueness changed");
    Require(coordinator.State() == ShutdownState::Complete, "shutdown did not reach Complete");
    Require(!coordinator.ShutdownAll("unit-test-repeat"), "repeated shutdown executed callbacks twice");
    Require(!coordinator.EndSession(generation, "late-session-end"),
        "late session teardown executed twice");
    Require(!coordinator.Register(ShutdownComponent::Radar, 900, [] {}),
        "registration accepted after shutdown");

    ShutdownCoordinator sessionCoordinator;
    int sessionRuns = 0;
    const auto sessionGeneration = sessionCoordinator.BeginSession([&] { ++sessionRuns; });
    Require(sessionCoordinator.EndSession(sessionGeneration, "normal"),
        "normal session teardown rejected");
    Require(!sessionCoordinator.EndSession(sessionGeneration, "duplicate"),
        "session teardown was not idempotent");
    Require(sessionRuns == 1, "session callback did not run exactly once");
}

void TestUpdateTimeUtilities() {
    int year = 0, month = 0, day = 0;
    Require(LauncherUpdates::UpdateTime::ParseIsoDatePrefix("2026-08-26T12:00:00Z", year, month, day),
            "valid update date rejected");
    Require(year == 2026 && month == 8 && day == 26, "update date parsed incorrectly");
    Require(!LauncherUpdates::UpdateTime::ParseIsoDatePrefix("2026-02-30", year, month, day),
            "invalid calendar date accepted");
    Require(LauncherUpdates::UpdateTime::DateDisplay("2026-08-26") == "26/08/2026",
            "update date display changed during extraction");
}

struct FakeProvider final : OmniGhost::Auth::IRemoteLicenseProvider {
    LicenseResult next{LicenseStatus::Valid, "ok", "test.valid", {}};
    int calls{};
    LicenseResult Authenticate(std::string_view, std::string_view, std::stop_token) override {
        ++calls;
        return next;
    }
};

std::string Manifest(std::string appId = "com.omnighost.launcher",
                     std::string architecture = "x64",
                     std::string urlScheme = "https") {
    return "{\"schemaVersion\":1,\"appId\":\"" + appId +
        "\",\"channel\":\"stable\",\"version\":\"1.1.0\","
        "\"minimumSupportedVersion\":\"1.0.0\",\"mandatory\":false,"
        "\"publishedAt\":\"2026-08-21T00:00:00Z\","
        "\"releaseNotesUrl\":\"https://github.com/example/release\",\"packages\":[{"
        "\"platform\":\"windows\",\"architecture\":\"" + architecture +
        "\",\"fileName\":\"OmniGhost.zip\",\"url\":\"" + urlScheme +
        "://github.com/example/releases/download/v1.1.0/OmniGhost.zip\","
        "\"size\":42,\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}]}";
}

void TestVersions() {
    using OmniGhost::Update::SemVersion;
    const auto a = SemVersion::Parse("1.10.0");
    const auto b = SemVersion::Parse("1.9.0");
    const auto stable = SemVersion::Parse("2.0.0");
    const auto beta = SemVersion::Parse("2.0.0-beta.1");
    Require(a && b && stable && beta, "valid SemVer rejected");
    Require(a->Compare(*b) > 0, "1.10.0 must be newer than 1.9.0");
    Require(beta->Compare(*stable) < 0, "prerelease must be older than stable");
    Require(!SemVersion::Parse("1.02.0"), "leading zero accepted");
}

void TestStartupStateMachine() {
    using OmniGhost::Startup::State;
    OmniGhost::Startup::StateMachine machine;
    Require(machine.Current() == State::Boot, "startup state does not begin at Boot");
    Require(!machine.Transition(State::CheckingInstance, State::InitializingRuntime),
            "startup accepted a transition from the wrong state");
    Require(machine.Transition(State::Boot, State::CheckingInstance),
            "startup rejected Boot -> CheckingInstance");
    Require(machine.Transition(State::CheckingInstance, State::InitializingRuntime),
            "startup rejected CheckingInstance -> InitializingRuntime");
    Require(machine.Transition(State::InitializingRuntime, State::Authenticating),
            "startup rejected InitializingRuntime -> Authenticating");
    Require(machine.Transition(State::Authenticating, State::CheckingHardware),
            "startup rejected Authenticating -> CheckingHardware");
    Require(machine.Transition(State::CheckingHardware, State::Ready),
            "startup rejected CheckingHardware -> Ready");
    machine.Fail();
    Require(machine.Current() == State::Failed, "startup failure was not terminal");
    Require(!machine.Transition(State::Failed, State::Ready),
            "startup escaped its terminal failure state");
}

void TestActiveGameIndexContract() {
    // OffsetAuto stores one compatibility record per ActiveGame. Keep this
    // contract visible so adding a game cannot silently reintroduce the Apex
    // out-of-bounds corruption that froze the launcher mutex.
    constexpr std::array games = {
        ActiveGame::FiveM, ActiveGame::CS2, ActiveGame::Rust,
        ActiveGame::Warzone, ActiveGame::Valorant, ActiveGame::Fortnite,
        ActiveGame::Apex
    };
    for (std::size_t index = 0; index < games.size(); ++index)
        Require(static_cast<std::size_t>(games[index]) == index,
                "ActiveGame values are no longer contiguous");
}

void TestManifest() {
    OmniGhost::Update::Configuration config;
    config.allowedPackageUrlPrefix = "https://github.com/example/releases/download/";
    Require(OmniGhost::Update::ParseAndValidateManifest(Manifest(), config).manifest.has_value(), "valid manifest rejected");
    Require(!OmniGhost::Update::ParseAndValidateManifest("{}", config).manifest, "incomplete manifest accepted");
    Require(!OmniGhost::Update::ParseAndValidateManifest(Manifest("wrong.app"), config).manifest, "wrong appId accepted");
    Require(!OmniGhost::Update::ParseAndValidateManifest(Manifest("com.omnighost.launcher", "arm64"), config).manifest, "wrong architecture accepted");
    Require(!OmniGhost::Update::ParseAndValidateManifest(Manifest("com.omnighost.launcher", "x64", "http"), config).manifest, "non-HTTPS URL accepted");
}

void TestUpdaterFailurePolicy() {
    using namespace OmniGhost::Update::HttpPolicy;
    Require(IsTlsOnlyUrl("https://github.com/example/release"), "valid HTTPS update URL rejected");
    Require(!IsTlsOnlyUrl("http://github.com/example/release"), "HTTP update URL accepted");
    Require(!IsTlsOnlyUrl("https://user:secret@github.com/release"), "credential-bearing URL accepted");
    Require(IsRetryableStatus(408) && IsRetryableStatus(429) && IsRetryableStatus(503),
            "transient updater status is not retryable");
    Require(!IsRetryableStatus(400) && !IsRetryableStatus(404),
            "permanent updater status was marked retryable");
    Require(ClassifyFailure(0, "A ligação excedeu o tempo limite") == ErrorClass::Timeout,
            "timeout updater failure misclassified");
    Require(ClassifyFailure(0, "DNS NAME_NOT_RESOLVED") == ErrorClass::Dns,
            "DNS updater failure misclassified");
    Require(ClassifyFailure(0, "TLS SECURE_FAILURE") == ErrorClass::Tls,
            "TLS updater failure misclassified");
    Require(ClassifyFailure(404, "not found") == ErrorClass::Http,
            "HTTP updater failure misclassified");
    Require(RetryDelayMilliseconds(0) == 500 && RetryDelayMilliseconds(2) == 2000,
            "updater retry backoff changed unexpectedly");
    Require(RetryDelayMilliseconds(0, 60) == 30000,
            "Retry-After was not bounded");
}

void TestKeyAuthContract() {
    const auto now = std::chrono::steady_clock::now();
    auto provider = std::make_shared<FakeProvider>();
    OmniGhost::Auth::LicenseGateway gateway(provider);
    Require(gateway.Authenticate("key", "hwid", false, now).Ok(), "valid license rejected");
    gateway.Logout();
    Require(!gateway.IsAuthenticated(), "logout did not clear session");

    for (const auto status : {LicenseStatus::Invalid, LicenseStatus::Expired,
                              LicenseStatus::Revoked, LicenseStatus::HwidMismatch}) {
        provider->next = {status, "denied", "test.denied", {}};
        OmniGhost::Auth::LicenseGateway isolated(provider);
        Require(isolated.Authenticate("key", "hwid", false, now).status == status, "license error was not preserved");
    }

    provider->next = {LicenseStatus::Invalid, "denied", "test.invalid", {}};
    OmniGhost::Auth::LicenseGateway locked(provider);
    for (int i = 0; i < 5; ++i) (void)locked.Authenticate("key", "hwid", false, now);
    Require(locked.Authenticate("key", "hwid", false, now).status == LicenseStatus::LockedOut, "lockout not enforced");

    provider->next = {LicenseStatus::Timeout, "timeout", "test.timeout", {}};
    OmniGhost::Auth::LicenseGateway offline(provider);
    offline.SetOfflineGrantForTesting("hwid", now - std::chrono::seconds(1));
    Require(offline.Authenticate("key", "hwid", true, now).status == LicenseStatus::OfflineCacheExpired,
            "expired offline cache accepted");
}

void TestPathPolicy() {
    using OmniGhost::Platform::IsSafeStaticRequestTarget;
    Require(IsSafeStaticRequestTarget("/"), "root target rejected");
    Require(IsSafeStaticRequestTarget("/radar/index.html"), "normal target rejected");
    Require(!IsSafeStaticRequestTarget("/../secret"), "path traversal accepted");
    Require(!IsSafeStaticRequestTarget("/%2e%2e/secret"), "encoded traversal accepted");
    Require(!IsSafeStaticRequestTarget("C:\\Windows\\file"), "absolute Windows path accepted");
}

void TestRadarLanAccess() {
    using namespace OmniGhost::RadarAccess;
    std::string first;
    std::string second;
    std::string error;
    Require(GenerateSessionToken(first, error), "radar token generation failed");
    Require(GenerateSessionToken(second, error), "second radar token generation failed");
    Require(first.size() == 64 && second.size() == 64, "radar token is not 256-bit hex");
    Require(first != second, "radar session tokens were reused");

    const std::string valid =
        "GET /api/live HTTP/1.1\r\nHost: 192.168.1.5:8080\r\n"
        "Origin: http://192.168.1.5:8080\r\nAuthorization: Bearer " + first + "\r\n\r\n";
    Require(HasValidBearerToken(valid, first), "valid radar bearer token rejected");
    Require(HasSafeSameOrigin(valid), "same-origin radar request rejected");
    Require(!HasValidBearerToken(valid, second), "incorrect radar token accepted");

    const std::string foreignOrigin =
        "GET /api/live HTTP/1.1\r\nHost: 192.168.1.5:8080\r\n"
        "Origin: http://example.test\r\nAuthorization: Bearer " + first + "\r\n\r\n";
    Require(!HasSafeSameOrigin(foreignOrigin), "foreign radar Origin accepted");
    Require(!HasValidBearerToken("GET /api/live HTTP/1.1\r\nHost: x\r\n\r\n", first),
            "missing radar Authorization accepted");

    const std::string publicRequest =
        "GET /api/live HTTP/1.1\r\nHost: example.trycloudflare.com\r\n"
        "Origin: https://example.trycloudflare.com\r\nAuthorization: Bearer " + first + "\r\n\r\n";
    Require(HasSafeSameOrigin(publicRequest), "same-origin HTTPS radar request rejected");
    const std::string websocketRequest =
        "GET /cs2_webradar HTTP/1.1\r\nHost: example.trycloudflare.com\r\n"
        "Origin: https://example.trycloudflare.com\r\nSec-WebSocket-Protocol: omnighost-radar." + first + "\r\n\r\n";
    Require(HasValidWebSocketProtocolToken(websocketRequest, first),
            "valid radar WebSocket protocol token rejected");
    Require(!HasValidWebSocketProtocolToken(websocketRequest, second),
            "incorrect radar WebSocket protocol token accepted");
}

struct TemporaryTree {
    fs::path root;
    ~TemporaryTree() {
        std::error_code error;
        fs::remove_all(root, error);
    }
};

void WriteTestFile(const fs::path& path, std::string_view content) {
    std::error_code error;
    fs::create_directories(path.parent_path(), error);
    Require(!error, "failed to create test directory");
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    Require(static_cast<bool>(stream), "failed to create test file");
    stream.write(content.data(), static_cast<std::streamsize>(content.size()));
    Require(static_cast<bool>(stream), "failed to write test file");
}

std::string ReadTestFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    Require(static_cast<bool>(stream), "failed to read test file");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void TestCorruptLocalAccountRecovery() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryTree tree{fs::temp_directory_path() / ("OmniGhost-auth-test-" + unique)};
    const fs::path authPath = tree.root / L"auth.dat";
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    auth.SetStoragePathForTesting(authPath);

    WriteTestFile(authPath, "not-a-dpapi-account");
    auth.Initialize();
    Require(!auth.HasAccount() && !auth.IsAuthenticated(),
            "corrupt auth.dat was accepted");

    std::error_code error;
    fs::remove(authPath, error);
    const auto registered = auth.Register(" owner@example.com ", "correct-horse", true);
    Require(registered.Ok() && auth.IsAuthenticated(), "isolated local account registration failed");
    std::string encrypted = ReadTestFile(authPath);
    Require(encrypted.size() > 8 && encrypted.substr(0, 4) == "OGA1",
            "local account was not stored in the protected envelope");

    encrypted[encrypted.size() / 2] ^= 0x5a;
    WriteTestFile(authPath, encrypted);
    auth.Initialize();
    Require(!auth.HasAccount() && !auth.IsAuthenticated(),
            "tampered DPAPI auth.dat was accepted");
    auth.SetStoragePathForTesting({});
}

void TestCorruptSettingsRecovery() {
    app_settings::Config settings{};
    std::string error;
    Require(!app_settings::ParseForTesting("schema=999\nappearance.ui_scale=99\n", settings, &error),
            "unsupported settings schema accepted");
    Require(!error.empty(), "unsupported settings schema produced no diagnostic");

    error.clear();
    const std::string valid =
        "schema=2\n"
        "language=99\n"
        "appearance.ui_scale=99\n"
        "overlay.black_level=-50\n"
        "overlay.monitor=999\n"
        "input.menu_bind=999\n";
    Require(app_settings::ParseForTesting(valid, settings, &error),
            "recoverable settings values were rejected");
    Require(settings.ui_scale == 1.25f && settings.black_level == 0.0f &&
            settings.monitor_index == 15 && settings.menu_bind == 255,
            "settings bounds were not clamped");
    Require(settings.language == app_settings::Language::IT,
            "settings language bound was not clamped");

    std::string oversized(129 * 1024, 'x');
    Require(!app_settings::ParseForTesting(oversized, settings, &error),
            "oversized settings file accepted");
}

void PrepareInstallTrees(const fs::path& target, const fs::path& staging) {
    WriteTestFile(target / L"OmniGhost.exe", "version-1.0.0");
    WriteTestFile(target / L"data" / L"engine.bin", "old-engine");
    WriteTestFile(target / L"Configs" / L"profile.ogcfg", "user-profile");
    WriteTestFile(target / L"auth.dat", "encrypted-account");
    WriteTestFile(target / L"user.sqlite", "user-database");

    WriteTestFile(staging / L"OmniGhost.exe", "version-1.1.0");
    WriteTestFile(staging / L"data" / L"engine.bin", "new-engine");
    WriteTestFile(staging / L"Configs" / L"profile.ogcfg", "packaged-default");
    WriteTestFile(staging / L"auth.dat", "must-not-replace-account");
    WriteTestFile(staging / L"user.sqlite", "must-not-replace-database");
}

void TestInstallAndRollback() {
    const auto unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    TemporaryTree tree{fs::temp_directory_path() / ("OmniGhost-update-test-" + unique)};
    const fs::path target = fs::absolute(tree.root / L"target");
    const fs::path staging = fs::absolute(tree.root / L"staging");
    const fs::path backup = fs::absolute(tree.root / L"backup");
    PrepareInstallTrees(target, staging);

    Require(OmniGhost::Paths::IsProtectedInstallPath(L"auth.dat"), "auth.dat is not protected");
    Require(OmniGhost::Paths::IsProtectedInstallPath(L"license.dat"), "license.dat is not protected");
    Require(OmniGhost::Paths::IsProtectedInstallPath(L"user.sqlite"), "database is not protected");

    OmniGhost::Update::InstallOptions options{
        staging, target, backup, L"OmniGhost.exe", {}};
    const auto installed = OmniGhost::Update::InstallWithRollback(options);
    Require(installed.success, "isolated update installation failed");
    Require(ReadTestFile(target / L"OmniGhost.exe") == "version-1.1.0", "new executable not installed");
    Require(ReadTestFile(target / L"data" / L"engine.bin") == "new-engine", "new runtime not installed");
    Require(ReadTestFile(target / L"Configs" / L"profile.ogcfg") == "user-profile", "configuration was replaced");
    Require(ReadTestFile(target / L"auth.dat") == "encrypted-account", "account data was replaced");
    Require(ReadTestFile(target / L"user.sqlite") == "user-database", "database was replaced");
    Require(ReadTestFile(backup / L"OmniGhost.exe") == "version-1.0.0", "backup does not contain old executable");

    std::error_code error;
    fs::remove_all(tree.root, error);
    PrepareInstallTrees(target, staging);
    OmniGhost::Update::InstallOptions failing{
        staging, target, backup, L"OmniGhost.exe",
        [](const std::string& stage) { return stage == "copy"; }};
    const auto rolledBack = OmniGhost::Update::InstallWithRollback(failing);
    Require(!rolledBack.success && rolledBack.rolledBack, "injected install failure did not roll back");
    Require(ReadTestFile(target / L"OmniGhost.exe") == "version-1.0.0", "rollback did not restore executable");
    Require(ReadTestFile(target / L"data" / L"engine.bin") == "old-engine", "rollback did not restore runtime");
    Require(ReadTestFile(target / L"Configs" / L"profile.ogcfg") == "user-profile", "rollback changed configuration");
    Require(ReadTestFile(target / L"auth.dat") == "encrypted-account", "rollback changed account data");

    // Current customer packages contain only OmniGhost.exe. Updating them must
    // not delete/rewrite the private LocalAppData runtime; the new executable
    // repairs an individual dependency later only if its embedded hash differs.
    fs::remove_all(tree.root, error);
    WriteTestFile(target / L"OmniGhost.exe", "single-exe-old");
    WriteTestFile(target / L"libs" / L"vmm.dll", "keep-vmm");
    WriteTestFile(target / L"data" / L"runtime.dat", "keep-data");
    WriteTestFile(target / L"resources" / L"logo.png", "keep-resource");
    WriteTestFile(staging / L"OmniGhost.exe", "single-exe-new");
    const auto singleInstalled = OmniGhost::Update::InstallWithRollback(
        {staging, target, backup, L"OmniGhost.exe", {}});
    Require(singleInstalled.success, "single-EXE update installation failed");
    Require(ReadTestFile(target / L"OmniGhost.exe") == "single-exe-new", "single-EXE update did not replace executable");
    Require(ReadTestFile(target / L"libs" / L"vmm.dll") == "keep-vmm", "single-EXE update changed libs");
    Require(ReadTestFile(target / L"data" / L"runtime.dat") == "keep-data", "single-EXE update changed data");
    Require(ReadTestFile(target / L"resources" / L"logo.png") == "keep-resource", "single-EXE update changed resources");
    std::string rollbackError;
    Require(OmniGhost::Update::RollbackInstallation(
        {staging, target, backup, L"OmniGhost.exe", {}}, rollbackError),
        "single-EXE rollback failed");
    Require(ReadTestFile(target / L"OmniGhost.exe") == "single-exe-old", "single-EXE rollback did not restore executable");
    Require(ReadTestFile(target / L"libs" / L"vmm.dll") == "keep-vmm", "single-EXE rollback changed libs");

    // A running/locked executable must fail closed without damaging either the
    // installed file or mutable user data.
    fs::remove_all(tree.root, error);
    WriteTestFile(target / L"OmniGhost.exe", "locked-old");
    WriteTestFile(target / L"auth.dat", "keep-auth");
    WriteTestFile(staging / L"OmniGhost.exe", "locked-new");
    HANDLE locked = CreateFileW((target / L"OmniGhost.exe").c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Require(locked != INVALID_HANDLE_VALUE, "failed to lock updater test executable");
    const auto lockedResult = OmniGhost::Update::InstallWithRollback(
        {staging, target, backup, L"OmniGhost.exe", {}});
    CloseHandle(locked);
    Require(!lockedResult.success, "updater replaced a locked executable");
    Require(ReadTestFile(target / L"OmniGhost.exe") == "locked-old",
            "locked update damaged the installed executable");
    Require(ReadTestFile(target / L"auth.dat") == "keep-auth",
            "locked update damaged auth.dat");
}

std::uint32_t FnvPath(std::string_view path) {
    return OmniGhost::EmbeddedOffsets::HashOffsetPath(path);
}

void Put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value)); out.push_back(static_cast<std::uint8_t>(value >> 8));
}
void Put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void Put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) out.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
}
void PutString(std::vector<std::uint8_t>& out, std::string_view text) {
    Put16(out, static_cast<std::uint16_t>(text.size())); out.insert(out.end(), text.begin(), text.end());
}
std::vector<std::uint8_t> PackLiteral(OmniGhost::EmbeddedOffsets::ByteView input) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < input.size();) {
        const std::size_t len = (std::min<std::size_t>)(128, input.size() - i);
        out.push_back(static_cast<std::uint8_t>(len - 1));
        out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(i), input.begin() + static_cast<std::ptrdiff_t>(i + len));
        i += len;
    }
    return out;
}
std::array<std::uint8_t, 32> TestSha256(OmniGhost::EmbeddedOffsets::ByteView bytes) {
    std::array<std::uint8_t, 32> digest{};
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE hash=nullptr; DWORD objLen=0, got=0;
    Require(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "sha open failed");
    Require(BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &got, 0) >= 0, "sha prop failed");
    std::vector<std::uint8_t> obj(objLen);
    Require(BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) >= 0, "sha create failed");
    if (!bytes.empty()) Require(BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0, "sha data failed");
    Require(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0, "sha finish failed");
    BCryptDestroyHash(hash); BCryptCloseAlgorithmProvider(alg, 0);
    return digest;
}
std::vector<std::uint8_t> MakeOffsetBlob(OmniGhost::EmbeddedOffsets::Game game) {
    std::vector<std::uint8_t> payload;
    Put32(payload, 0x504E534Fu); Put16(payload, 1); Put16(payload, 0); Put32(payload, 1);
    const bool fortnite = game == OmniGhost::EmbeddedOffsets::Game::Fortnite;
    PutString(payload, fortnite ? "fortnite" : "warzone");
    PutString(payload, "test-build"); PutString(payload, fortnite ? "123" : "");
    PutString(payload, fortnite ? "FortniteClient-Win64-Shipping.exe" : "cod.exe");
    PutString(payload, "unit-test"); PutString(payload, "2026-08-24"); PutString(payload, "test");
    Put32(payload, 1); Put32(payload, FnvPath("offsets.test")); Put32(payload, 0); Put64(payload, 0x1234);
    auto compressed = PackLiteral(payload); const auto digest = TestSha256(payload);
    std::vector<std::uint8_t> blob;
    Put32(blob, 0x464F474Fu); Put16(blob, 1); Put16(blob, static_cast<std::uint16_t>(game));
    blob.push_back(1); blob.push_back(0); blob.push_back(0); blob.push_back(0);
    Put32(blob, static_cast<std::uint32_t>(compressed.size())); Put32(blob, static_cast<std::uint32_t>(payload.size()));
    blob.insert(blob.end(), digest.begin(), digest.end()); blob.insert(blob.end(), compressed.begin(), compressed.end());
    return blob;
}
void TestEmbeddedOffsetBlob() {
    using namespace OmniGhost::EmbeddedOffsets;
    Snapshot snapshot; Diagnostics diag;
    auto valid = MakeOffsetBlob(Game::Warzone);
    Require(ParseEmbeddedOffsetBlob(valid, Game::Warzone, snapshot, diag), "valid embedded offset blob rejected");
    std::uint64_t value=0; Require(snapshot.TryGet("offsets.test", value) && value == 0x1234, "embedded offset lookup failed");

    Require(MetadataMatches(snapshot, "warzone", "test-build", "", "cod.exe"), "embedded identity match failed");
    Require(!MetadataMatches(snapshot, "warzone", "wrong-build", "", "cod.exe"), "wrong embedded build accepted");
    Require(!MetadataMatches(snapshot, "fortnite", "test-build", "", "cod.exe"), "wrong embedded game identity accepted");

    auto badMagic=valid; badMagic[0]^=0xFF; Require(!ParseEmbeddedOffsetBlob(badMagic, Game::Warzone, snapshot, diag), "bad magic accepted");
    auto badVersion=valid; badVersion[4]=2; Require(!ParseEmbeddedOffsetBlob(badVersion, Game::Warzone, snapshot, diag), "unsupported version accepted");
    Require(!ParseEmbeddedOffsetBlob(OmniGhost::EmbeddedOffsets::ByteView(valid.data(), 12), Game::Warzone, snapshot, diag), "truncated header accepted");
    auto truncated=valid; truncated.pop_back(); Require(!ParseEmbeddedOffsetBlob(truncated, Game::Warzone, snapshot, diag), "truncated payload accepted");
    auto corrupt=valid; corrupt.back()^=1; Require(!ParseEmbeddedOffsetBlob(corrupt, Game::Warzone, snapshot, diag), "corrupt compressed/integrity data accepted");
    auto wrongSha=valid; wrongSha[20]^=1; Require(!ParseEmbeddedOffsetBlob(wrongSha, Game::Warzone, snapshot, diag), "wrong SHA accepted");
    Require(!ParseEmbeddedOffsetBlob(valid, Game::Fortnite, snapshot, diag), "wrong game accepted");
    auto oversized=valid; oversized[16]=0xFF; oversized[17]=0xFF; oversized[18]=0xFF; oversized[19]=0x7F;
    Require(!ParseEmbeddedOffsetBlob(oversized, Game::Warzone, snapshot, diag), "oversized declared payload accepted");
    Require(ResourceIdForGame(static_cast<Game>(999)) == 0, "unknown game resource id accepted");
}

std::vector<std::uint8_t> MakeGenericResourceBlob(
    std::string_view content, OmniGhost::EmbeddedCompression compression) {
    std::vector<std::uint8_t> raw(content.begin(), content.end());
    std::vector<std::uint8_t> stored = compression == OmniGhost::EmbeddedCompression::PackBits
        ? PackLiteral(OmniGhost::EmbeddedOffsets::ByteView(raw.data(), raw.size())) : raw;
    const auto digest = TestSha256(OmniGhost::EmbeddedOffsets::ByteView(raw.data(), raw.size()));
    std::vector<std::uint8_t> blob;
    Put32(blob, 0x5352474Fu); Put16(blob, 1); Put16(blob, 0);
    blob.push_back(static_cast<std::uint8_t>(compression));
    blob.push_back(0); blob.push_back(0); blob.push_back(0);
    Put32(blob, static_cast<std::uint32_t>(raw.size()));
    Put32(blob, static_cast<std::uint32_t>(stored.size()));
    blob.insert(blob.end(), digest.begin(), digest.end());
    blob.insert(blob.end(), stored.begin(), stored.end());
    return blob;
}

void TestEmbeddedResourceBlob() {
    using namespace OmniGhost;
    std::vector<std::uint8_t> output;
    EmbeddedResourceDiagnostics diagnostics;
    const auto raw = MakeGenericResourceBlob("raw-resource", EmbeddedCompression::None);
    Require(ParseEmbeddedResourceBlob({raw.data(), raw.size()}, output, diagnostics),
            "valid raw embedded resource rejected");
    Require(std::string(output.begin(), output.end()) == "raw-resource", "raw resource changed");

    const auto compressed = MakeGenericResourceBlob(
        "compressed-resource-content", EmbeddedCompression::PackBits);
    Require(ParseEmbeddedResourceBlob({compressed.data(), compressed.size()}, output, diagnostics),
            "valid compressed embedded resource rejected");

    auto badMagic = raw; badMagic[0] ^= 0xff;
    Require(!ParseEmbeddedResourceBlob({badMagic.data(), badMagic.size()}, output, diagnostics), "bad resource magic accepted");
    auto badVersion = raw; badVersion[4] = 2;
    Require(!ParseEmbeddedResourceBlob({badVersion.data(), badVersion.size()}, output, diagnostics), "bad resource version accepted");
    Require(!ParseEmbeddedResourceBlob({raw.data(), 20}, output, diagnostics), "truncated resource header accepted");
    auto truncated = raw; truncated.pop_back();
    Require(!ParseEmbeddedResourceBlob({truncated.data(), truncated.size()}, output, diagnostics), "truncated resource payload accepted");
    auto badSize = raw; badSize[16] ^= 1;
    Require(!ParseEmbeddedResourceBlob({badSize.data(), badSize.size()}, output, diagnostics), "invalid resource size accepted");
    auto oversized = raw; oversized[12] = 1; oversized[13] = 0; oversized[14] = 0; oversized[15] = 5;
    Require(!ParseEmbeddedResourceBlob({oversized.data(), oversized.size()}, output, diagnostics), "oversized resource accepted");
    auto corruptCompressed = compressed; corruptCompressed.back() ^= 1;
    Require(!ParseEmbeddedResourceBlob({corruptCompressed.data(), corruptCompressed.size()}, output, diagnostics), "corrupt compressed resource accepted");
    auto badSha = raw; badSha[20] ^= 1;
    Require(!ParseEmbeddedResourceBlob({badSha.data(), badSha.size()}, output, diagnostics), "resource SHA mismatch accepted");
    auto unsupported = raw; unsupported[8] = 99;
    Require(!ParseEmbeddedResourceBlob({unsupported.data(), unsupported.size()}, output, diagnostics), "unsupported compression accepted");
    Require(FindEmbeddedResource(static_cast<EmbeddedResourceId>(999)) == nullptr, "unknown resource id accepted");
    EmbeddedResourceDiagnostics missing;
    Require(!LoadEmbeddedResource(static_cast<EmbeddedResourceId>(999), &missing), "missing mandatory resource accepted");
}

}

int main() {
    try {
        TestVersions();
        TestUpdateTimeUtilities();
        TestStartupStateMachine();
        TestShutdownCoordinator();
        TestActiveGameIndexContract();
        TestManifest();
        TestUpdaterFailurePolicy();
        TestKeyAuthContract();
        TestCorruptLocalAccountRecovery();
        TestCorruptSettingsRecovery();
        TestPathPolicy();
        TestRadarLanAccess();
        TestInstallAndRollback();
        TestEmbeddedOffsetBlob();
        TestEmbeddedResourceBlob();
        std::cout << "OmniGhost tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "OmniGhost tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
