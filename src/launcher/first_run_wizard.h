#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <chrono>

namespace Launcher::FirstRun {

enum class WizardStep : uint8_t {
    Welcome = 0,
    LicenseAgreement = 1,
    DeviceDetection = 2,
    DeviceTest = 3,
    DMAConfiguration = 4,
    AccountSetup = 5,
    ThemeSelection = 6,
    FeatureOverview = 7,
    Complete = 8,
    Error = 9
};

enum class DeviceType : uint8_t {
    None = 0,
    FTDI = 1,
    PCIe = 2,
    USB = 3,
    Network = 4
};

enum class TestResult : uint8_t {
    Pending = 0,
    Running = 1,
    Passed = 2,
    Failed = 3,
    Skipped = 4
};

struct DeviceInfo {
    std::string name;
    std::string vendor;
    std::string deviceId;
    std::string serialNumber;
    DeviceType type = DeviceType::None;
    bool isDMA = false;
    bool isInput = false;
    std::string driverVersion;
    std::string firmwareVersion;
    bool isConnected = false;
    TestResult testResult = TestResult::Pending;
    std::string testError;
    std::chrono::system_clock::time_point lastTested;
};

struct WizardState {
    WizardStep currentStep = WizardStep::Welcome;
    bool licenseAccepted = false;
    std::vector<DeviceInfo> detectedDevices;
    int selectedDeviceIndex = -1;
    bool deviceTestCompleted = false;
    bool deviceTestPassed = false;
    std::string dmaConfigPath;
    bool useExternalDMA = false;
    bool skipDMA = false;
    std::string email;
    std::string licenseKey;
    int selectedTheme = 0;
    bool enableAnimations = true;
    bool enableTelemetry = false;
    bool enableAutoUpdates = true;
    bool showTips = true;
    std::string errorMessage;
    bool isProcessing = false;
    std::chrono::steady_clock::time_point stepStartTime;
    int previousStep = -1;
};

struct FirstRunConfig {
    bool enableMarketplace = true;
    bool enableTelemetry = false;
    bool enableAutoUpdates = true;
    bool showTips = true;
    int defaultTheme = 0;
    std::string defaultLanguage = "pt";
    bool enableAnimations = true;
    int animationIntensity = 2;
    bool enableDMA = true;
    bool autoDetectDMA = true;
    std::string defaultGame = "";
};

class FirstRunWizard {
public:
    static FirstRunWizard& Instance();

    void Initialize();
    void Shutdown();
    void Reset();

    WizardState& GetState();
    const WizardState& GetState() const;

    void NextStep();
    void PreviousStep();
    void GoToStep(WizardStep step);
    void SetError(const std::string& error);
    void ClearError();

    bool DetectDevices();
    bool TestSelectedDevice();
    bool TestAllDevices();
    bool TestDMADevice(int deviceIndex);
    bool TestInputDevice(int deviceIndex);
    bool ValidateDMAConnection();
    bool SaveConfiguration();
    bool LoadConfiguration();

    void SetDeviceTestCallback(std::function<void(int, TestResult, const std::string&)> callback);
    void SetStepChangeCallback(std::function<void(WizardStep, WizardStep)> callback);

    const FirstRunConfig& GetConfig() const;
    void SetConfig(const FirstRunConfig& config);

    bool IsComplete() const;
    bool IsFirstRun() const;
    void MarkFirstRunComplete();

    void Draw();

private:
    FirstRunWizard() = default;
    WizardState state_;
    FirstRunConfig config_;
    std::function<void(int, TestResult, const std::string&)> deviceTestCallback_;
    std::function<void(WizardStep, WizardStep)> stepChangeCallback_;
    bool isInitialized_ = false;
    std::chrono::steady_clock::time_point wizardStartTime_;
};

bool IsFirstRunComplete();
void MarkFirstRunComplete();
void RunFirstRunWizard();

} // namespace Launcher::FirstRun