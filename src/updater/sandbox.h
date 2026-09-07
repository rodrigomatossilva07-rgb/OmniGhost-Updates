#pragma once

#include <string>
#include <string_view>
#include <functional>
#include <memory>
#include <future>
#include <chrono>

namespace OmniGhost::Updater {

// ============================================================
// Updater Sandbox / Isolation
// ============================================================

class UpdaterSandbox {
public:
    struct Config {
        // Process isolation
        bool useJobObject = true;
        bool useAppContainer = false;
        bool restrictNetwork = true;
        bool restrictFileSystem = true;
        bool restrictRegistry = true;
        
        // Resource limits
        size_t maxMemoryMB = 256;
        size_t maxCpuPercent = 50;
        std::chrono::seconds maxExecutionTime = std::chrono::minutes(10);
        
        // Security
        bool dropPrivileges = true;
        bool blockChildProcesses = true;
        std::wstring workingDirectory;
    };
    
    struct Result {
        int exitCode = -1;
        std::string stdoutOutput;
        std::string stderrOutput;
        bool timedOut = false;
        bool killedByPolicy = false;
        std::string errorMessage;
    };
    
    explicit UpdaterSandbox(Config config = {}) : config_(std::move(config)) {}
    ~UpdaterSandbox() = default;
    
    // Execute updater in isolated environment
    Result Execute(std::wstring_view executablePath, std::wstring_view arguments) noexcept;
    
    // Execute with callback for progress
    template <typename ProgressCallback>
    Result ExecuteWithProgress(std::wstring_view executablePath, 
                               std::wstring_view arguments,
                               ProgressCallback&& progressCallback) noexcept {
        // Simplified - would implement progress tracking
        return Execute(executablePath, arguments);
    }
    
    // Check if sandbox is available
    [[nodiscard]] static bool IsSandboxSupported() noexcept;
    
    // Get sandbox capabilities
    [[nodiscard]] static std::string GetSandboxCapabilities() noexcept;

private:
    Config config_;
    
    // Job object management
    HANDLE CreateJobObject() noexcept;
    void ConfigureJobObject(HANDLE job) noexcept;
    void AssignProcessToJob(HANDLE job, HANDLE process) noexcept;
    
    // AppContainer setup
    HANDLE CreateAppContainerToken() noexcept;
    
    // Resource monitoring
    void MonitorResources(HANDLE process, HANDLE job) noexcept;
    
    Config config_;
};

// ============================================================
// Secure Updater Process Launcher
// ============================================================

class SecureUpdaterLauncher {
public:
    struct LaunchConfig {
        std::wstring executablePath;
        std::wstring arguments;
        std::wstring workingDirectory;
        std::chrono::seconds timeout = std::chrono::minutes(10);
        bool requireElevation = false;
        bool runAsUser = true;
    };
    
    struct LaunchResult {
        bool success = false;
        DWORD exitCode = 0;
        std::string errorMessage;
        DWORD processId = 0;
    };
    
    static LaunchResult Launch(const LaunchConfig& config) noexcept;
    
    // Verify binary signature before launch
    static bool VerifyBinarySignature(std::wstring_view path) noexcept;
    
    // Verify file hash
    static bool VerifyFileHash(std::wstring_view path, std::string_view expectedSha256) noexcept;
    
private:
    static bool CreateRestrictedToken(HANDLE& token) noexcept;
    static void ApplySecurityAttributes(SECURITY_ATTRIBUTES& sa) noexcept;
};

// ============================================================
// Self-Update Integrity
// ============================================================

class SelfUpdateVerifier {
public:
    struct VerificationResult {
        bool valid = false;
        std::string errorMessage;
        std::string version;
        std::string sha256;
        std::string signature;
    };
    
    // Verify update package before installation
    static VerificationResult VerifyPackage(std::wstring_view packagePath) noexcept;
    
    // Verify digital signature
    static bool VerifyAuthenticodeSignature(std::wstring_view filePath) noexcept;
    
    // Verify embedded certificate
    static bool VerifyCertificateChain(std::wstring_view filePath) noexcept;
    
    // Verify hash matches expected
    static bool VerifyHash(std::wstring_view filePath, std::string_view expectedSha256) noexcept;
    
    // Verify publisher
    static bool VerifyPublisher(std::wstring_view filePath, std::wstring_view expectedPublisher) noexcept;
};

} // namespace OmniGhost::Updater