#include "first_run_wizard.h"
#include "../platform/app_paths.h"
#include "../config/app_settings.h"
#include "../makcu/makcu_wrapper.h"
#include "../ferrum/ferrum_device.h"
#include "../kmbox/kmbox_net.h"
#include "../platform/hardware_manager.h"
#include "../window/localization.h"
#include "../../DMALibrary/Memory/Memory.h"
#include <Windows.h>
#include <setupapi.h>
#include <devguid.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "setupapi.lib")

namespace Launcher::FirstRun {

namespace {

constexpr const char* kFirstRunConfigFile = "first_run.cfg";
constexpr const char* kWizardConfigSection = "wizard";
constexpr const char* kDeviceTestTimeoutMs = "5000";

std::string GetConfigPath() {
    return (OmniGhost::Paths::ConfigDir() / kFirstRunConfigFile).string();
}

std::string DeviceTypeToString(DeviceType type) {
    switch (type) {
        case DeviceType::FTDI: return "FTDI";
        case DeviceType::PCIe: return "PCIe";
        case DeviceType::USB: return "USB";
        case DeviceType::Network: return "Network";
        default: return "Unknown";
    }
}

std::string TestResultToString(TestResult result) {
    switch (result) {
        case TestResult::Pending: return "Pending";
        case TestResult::Running: return "Running";
        case TestResult::Passed: return "Passed";
        case TestResult::Failed: return "Failed";
        case TestResult::Skipped: return "Skipped";
        default: return "Unknown";
    }
}

std::string DeviceTypeToLocalizedString(DeviceType type) {
    switch (type) {
        case DeviceType::FTDI: return Loc::Tr("wizard.device.ftdi");
        case DeviceType::PCIe: return Loc::Tr("wizard.device.pcie");
        case DeviceType::USB: return Loc::Tr("wizard.device.usb");
        case DeviceType::Network: return Loc::Tr("wizard.device.network");
        default: return Loc::Tr("wizard.device.unknown");
    }
}

std::string TestResultToLocalizedString(TestResult result) {
    switch (result) {
        case TestResult::Pending: return Loc::Tr("wizard.test.pending");
        case TestResult::Running: return Loc::Tr("wizard.test.running");
        case TestResult::Passed: return Loc::Tr("wizard.test.passed");
        case TestResult::Failed: return Loc::Tr("wizard.test.failed");
        case TestResult::Skipped: return Loc::Tr("wizard.test.skipped");
        default: return Loc::Tr("wizard.test.unknown");
    }
}

} // namespace

FirstRunWizard& FirstRunWizard::Instance() {
    static FirstRunWizard instance;
    return instance;
}

void FirstRunWizard::Initialize() {
    if (isInitialized_) return;

    LoadConfiguration();
    state_.currentStep = IsFirstRun() ? WizardStep::Welcome : WizardStep::Complete;
    state_.stepStartTime = std::chrono::steady_clock::now();
    wizardStartTime_ = std::chrono::steady_clock::now();
    isInitialized_ = true;

    std::cout << "[FirstRun] Wizard initialized, first run: " << (IsFirstRun() ? "yes" : "no") << std::endl;
}

void FirstRunWizard::Shutdown() {
    if (!isInitialized_) return;
    SaveConfiguration();
    isInitialized_ = false;
    std::cout << "[FirstRun] Wizard shutdown" << std::endl;
}

void FirstRunWizard::Reset() {
    state_ = WizardState{};
    config_ = FirstRunConfig{};
    state_.currentStep = WizardStep::Welcome;
    state_.stepStartTime = std::chrono::steady_clock::now();
    wizardStartTime_ = std::chrono::steady_clock::now();
    SaveConfiguration();
}

WizardState& FirstRunWizard::GetState() {
    return state_;
}

const WizardState& FirstRunWizard::GetState() const {
    return state_;
}

void FirstRunWizard::NextStep() {
    if (state_.currentStep >= WizardStep::Complete) return;

    WizardStep oldStep = state_.currentStep;
    state_.currentStep = static_cast<WizardStep>(static_cast<int>(state_.currentStep) + 1);
    state_.stepStartTime = std::chrono::steady_clock::now();
    state_.previousStep = static_cast<int>(oldStep);
    ClearError();

    if (stepChangeCallback_) {
        stepChangeCallback_(oldStep, state_.currentStep);
    }

    std::cout << "[FirstRun] Step changed: " << static_cast<int>(oldStep) << " -> " << static_cast<int>(state_.currentStep) << std::endl;

    if (state_.currentStep == WizardStep::DeviceDetection) {
        DetectDevices();
    }
}

void FirstRunWizard::PreviousStep() {
    if (state_.currentStep <= WizardStep::Welcome) return;

    WizardStep oldStep = state_.currentStep;
    state_.currentStep = static_cast<WizardStep>(static_cast<int>(state_.currentStep) - 1);
    state_.stepStartTime = std::chrono::steady_clock::now();
    state_.previousStep = static_cast<int>(oldStep);
    ClearError();

    if (stepChangeCallback_) {
        stepChangeCallback_(oldStep, state_.currentStep);
    }
}

void FirstRunWizard::GoToStep(WizardStep step) {
    if (step == state_.currentStep) return;

    WizardStep oldStep = state_.currentStep;
    state_.previousStep = static_cast<int>(state_.currentStep);
    state_.currentStep = step;
    state_.stepStartTime = std::chrono::steady_clock::now();
    ClearError();

    if (stepChangeCallback_) {
        stepChangeCallback_(oldStep, step);
    }
}

void FirstRunWizard::SetError(const std::string& error) {
    state_.errorMessage = error;
    state_.currentStep = WizardStep::Error;
    std::cerr << "[FirstRun] Error: " << error << std::endl;
}

void FirstRunWizard::ClearError() {
    state_.errorMessage.clear();
}

bool FirstRunWizard::DetectDevices() {
    state_.detectedDevices.clear();
    state_.isProcessing = true;
    state_.errorMessage.clear();

    std::cout << "[FirstRun] Detecting devices..." << std::endl;

    // Detect FTDI devices
    HDEVINFO deviceInfo = SetupDiGetClassDevs(&GUID_DEVINTERFACE_USB_DEVICE, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (deviceInfo != INVALID_HANDLE_VALUE) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(deviceInfo, nullptr, &GUID_DEVINTERFACE_USB_DEVICE, i, &interfaceData); ++i) {
            DWORD requiredSize = 0;
            SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, nullptr, 0, &requiredSize, nullptr);
            if (requiredSize > 0) {
                std::vector<BYTE> buffer(requiredSize);
                auto detailData = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(buffer.data());
                detailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                SP_DEVINFO_DATA devInfoData{};
                devInfoData.cbSize = sizeof(devInfoData);
                if (SetupDiGetDeviceInterfaceDetail(deviceInfo, &interfaceData, detailData, requiredSize, nullptr, &devInfoData)) {
                    char vendorId[256] = {};
                    char productId[256] = {};
                    char serialNumber[256] = {};

                    DWORD regType = 0;
                    DWORD size = sizeof(vendorId);
                    HKEY hKey = SetupDiOpenDevRegKey(deviceInfo, &devInfoData, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
                    if (hKey != INVALID_HANDLE_VALUE) {
                        RegGetValueA(hKey, nullptr, "VID", RRF_RT_REG_SZ, nullptr, vendorId, &size);
                        size = sizeof(productId);
                        RegGetValueA(hKey, nullptr, "PID", RRF_RT_REG_SZ, nullptr, productId, &size);
                        size = sizeof(serialNumber);
                        RegGetValueA(hKey, nullptr, "SerialNumber", RRF_RT_REG_SZ, nullptr, serialNumber, &size);
                        RegCloseKey(hKey);
                    }

                    std::string vendorStr(vendorId);
                    std::string productStr(productId);
                    std::string serialStr(serialNumber);

                    // Check if it's an FTDI device
                    if (vendorStr.find("0403") != std::string::npos || vendorStr.find("FTDI") != std::string::npos) {
                        DeviceInfo device;
                        device.name = "FTDI Device";
                        device.vendor = "FTDI";
                        device.deviceId = productStr;
                        device.serialNumber = serialStr;
                        device.type = DeviceType::FTDI;
                        device.isDMA = true;
                        device.isConnected = true;
                        state_.detectedDevices.push_back(std::move(device));
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(deviceInfo);
    }

    // Detect MAKCU
    if (makcu_wrapper::IsConnected()) {
        DeviceInfo device;
        device.name = "MAKCU";
        device.vendor = "MAKCU";
        device.type = DeviceType::USB;
        device.isInput = true;
        device.isConnected = true;
        state_.detectedDevices.push_back(std::move(device));
    }

    // Detect Ferrum
    if (ferrum_device::IsConnected()) {
        DeviceInfo device;
        device.name = "Ferrum";
        device.vendor = "Ferrum";
        device.type = DeviceType::USB;
        device.isInput = true;
        device.isConnected = true;
        state_.detectedDevices.push_back(std::move(device));
    }

    // Detect KMBox Net
    if (kmbox_net::IsConnected()) {
        DeviceInfo device;
        device.name = "KMBox Net";
        device.vendor = "KMBox";
        device.type = DeviceType::Network;
        device.isInput = true;
        device.isConnected = true;
        state_.detectedDevices.push_back(std::move(device));
    }

    // Detect DMA devices via PnP
    if (mem.vHandle) {
        DeviceInfo device;
        device.name = "DMA Device (FPGA)";
        device.vendor = "FPGA";
        device.type = DeviceType::PCIe;
        device.isDMA = true;
        device.isConnected = true;
        state_.detectedDevices.push_back(std::move(device));
    } else {
        // Passive PnP detection for DMA
        Memory::PassiveDmaPnpResult probe = Memory::ProbePassiveDmaPnp();
        if (probe.candidateFound && probe.pnpStarted) {
            DeviceInfo device;
            device.name = "DMA Device (FPGA - PnP)";
            device.vendor = "FPGA";
            device.type = DeviceType::PCIe;
            device.isDMA = true;
            device.isConnected = true;
            state_.detectedDevices.push_back(std::move(device));
        }
    }

    // Auto-select first DMA device
    for (size_t i = 0; i < state_.detectedDevices.size(); ++i) {
        if (state_.detectedDevices[i].isDMA) {
            state_.selectedDeviceIndex = static_cast<int>(i);
            break;
        }
    }

    state_.isProcessing = false;
    std::cout << "[FirstRun] Detected " << state_.detectedDevices.size() << " devices" << std::endl;
    for (const auto& dev : state_.detectedDevices) {
        std::cout << "  - " << dev.name << " (" << DeviceTypeToString(dev.type) << ") DMA: " << (dev.isDMA ? "yes" : "no") << " Input: " << (dev.isInput ? "yes" : "no") << std::endl;
    }

    return !state_.detectedDevices.empty();
}

bool FirstRunWizard::TestSelectedDevice() {
    if (state_.selectedDeviceIndex < 0 || state_.selectedDeviceIndex >= static_cast<int>(state_.detectedDevices.size())) {
        SetError(Loc::Tr("wizard.error.no_device_selected"));
        return false;
    }

    return TestDMADevice(state_.selectedDeviceIndex) || TestInputDevice(state_.selectedDeviceIndex);
}

bool FirstRunWizard::TestAllDevices() {
    bool allPassed = true;
    for (size_t i = 0; i < state_.detectedDevices.size(); ++i) {
        bool passed = TestDMADevice(static_cast<int>(i)) || TestInputDevice(static_cast<int>(i));
        if (!passed) allPassed = false;
    }
    state_.deviceTestCompleted = true;
    state_.deviceTestPassed = allPassed;
    return allPassed;
}

bool FirstRunWizard::TestDMADevice(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(state_.detectedDevices.size())) return false;

    auto& device = state_.detectedDevices[deviceIndex];
    if (!device.isDMA) {
        device.testResult = TestResult::Skipped;
        device.testError = "Not a DMA device";
        if (deviceTestCallback_) deviceTestCallback_(deviceIndex, TestResult::Skipped, device.testError);
        return false;
    }

    device.testResult = TestResult::Running;
    if (deviceTestCallback_) deviceTestCallback_(deviceIndex, TestResult::Running, "");

    std::cout << "[FirstRun] Testing DMA device: " << device.name << std::endl;

    // Initialize DMA if not already initialized
    if (!mem.vHandle) {
        std::cout << "[FirstRun] Initializing DMA for device test..." << std::endl;
        if (!mem.Init("", true, false)) {
            device.testResult = TestResult::Failed;
            device.testError = "Failed to initialize DMA";
            if (deviceTestCallback_) deviceTestCallback_(deviceIndex, TestResult::Failed, device.testError);
            return false;
        }
    }

    // Try to open a test process or just validate the device is responsive
    bool testPassed = false;
    std::string error;

    // Try to validate the device by attempting a simple memory read
    // This is a lightweight test that doesn't require a game process
    try {
        // Check if we can query the device info
        ULONG64 lcHandle = 0;
        if (mem.vHandle && VMMDLL_ConfigGet(mem.vHandle, VMMDLL_OPT_CORE_LEECHCORE_HANDLE, &lcHandle) && lcHandle) {
            HANDLE hLC = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(lcHandle));
            QWORD fpgaId = 0, verMajor = 0, verMinor = 0;
            bool idOk = LcGetOption(hLC, LC_OPT_FPGA_FPGA_ID, &fpgaId) != FALSE;
            bool verOk = LcGetOption(hLC, LC_OPT_FPGA_VERSION_MAJOR, &verMajor) != FALSE &&
                         LcGetOption(hLC, LC_OPT_FPGA_VERSION_MINOR, &verMinor) != FALSE;

            if (idOk || verOk) {
                testPassed = true;
                device.firmwareVersion = std::to_string(verMajor) + "." + std::to_string(verMinor);
                std::cout << "[FirstRun] DMA device test passed: FPGA ID=" << fpgaId << " FW=" << verMajor << "." << verMinor << std::endl;
            } else {
                error = "Could not query FPGA info";
            }
        } else {
            error = "LeechCore handle not available";
        }
    } catch (const std::exception& ex) {
        testPassed = false;
        error = "Exception: " + std::string(ex.what());
    } catch (...) {
        testPassed = false;
        error = "Unknown exception during DMA test";
    }

    device.testResult = testPassed ? TestResult::Passed : TestResult::Failed;
    device.testError = error;
    device.lastTested = std::chrono::system_clock::now();

    if (deviceTestCallback_) {
        deviceTestCallback_(deviceIndex, device.testResult, error);
    }

    std::cout << "[FirstRun] DMA device test " << (testPassed ? "PASSED" : "FAILED") << ": " << error << std::endl;
    return testPassed;
}

bool FirstRunWizard::TestInputDevice(int deviceIndex) {
    if (deviceIndex < 0 || deviceIndex >= static_cast<int>(state_.detectedDevices.size())) return false;

    auto& device = state_.detectedDevices[deviceIndex];
    if (!device.isInput) {
        device.testResult = TestResult::Skipped;
        device.testError = "Not an input device";
        if (deviceTestCallback_) deviceTestCallback_(deviceIndex, TestResult::Skipped, device.testError);
        return false;
    }

    device.testResult = TestResult::Running;
    if (deviceTestCallback_) deviceTestCallback_(deviceIndex, TestResult::Running, "");

    bool testPassed = false;
    std::string error;

    try {
        if (device.vendor == "MAKCU" && makcu_wrapper::IsConnected()) {
            testPassed = true;
        } else if (device.vendor == "Ferrum" && ferrum_device::IsConnected()) {
            testPassed = true;
        } else if (device.vendor == "KMBox" && kmbox_net::IsConnected()) {
            testPassed = true;
        } else {
            error = "Input device not responding";
        }
    } catch (const std::exception& ex) {
        testPassed = false;
        error = "Exception: " + std::string(ex.what());
    } catch (...) {
        testPassed = false;
        error = "Unknown exception during input test";
    }

    device.testResult = testPassed ? TestResult::Passed : TestResult::Failed;
    device.testError = error;
    device.lastTested = std::chrono::system_clock::now();

    if (deviceTestCallback_) {
        deviceTestCallback_(deviceIndex, device.testResult, error);
    }

    std::cout << "[FirstRun] Input device test " << (testPassed ? "PASSED" : "FAILED") << ": " << error << std::endl;
    return testPassed;
}

bool FirstRunWizard::ValidateDMAConnection() {
    if (!mem.vHandle) return false;

    try {
        ULONG64 lcHandle = 0;
        if (!VMMDLL_ConfigGet(mem.vHandle, VMMDLL_OPT_CORE_LEECHCORE_HANDLE, &lcHandle) || !lcHandle) {
            return false;
        }

        HANDLE hLC = reinterpret_cast<HANDLE>(static_cast<uintptr_t>(lcHandle));
        QWORD fpgaId = 0;
        return LcGetOption(hLC, LC_OPT_FPGA_FPGA_ID, &fpgaId) != FALSE;
    } catch (...) {
        return false;
    }
}

bool FirstRunWizard::SaveConfiguration() {
    std::string configPath = GetConfigPath();
    fs::create_directories(fs::path(configPath).parent_path());

    std::ofstream file(configPath);
    if (!file) return false;

    file << "[wizard]\n";
    file << "completed=" << (IsFirstRunComplete() ? "1" : "0") << "\n";
    file << "license_accepted=" << (state_.licenseAccepted ? "1" : "0") << "\n";
    file << "selected_device=" << state_.selectedDeviceIndex << "\n";
    file << "dma_config_path=" << state_.dmaConfigPath << "\n";
    file << "use_external_dma=" << (state_.useExternalDMA ? "1" : "0") << "\n";
    file << "skip_dma=" << (state_.skipDMA ? "1" : "0") << "\n";
    file << "email=" << state_.email << "\n";
    file << "license_key=" << state_.licenseKey << "\n";
    file << "theme=" << state_.selectedTheme << "\n";
    file << "animations=" << (state_.enableAnimations ? "1" : "0") << "\n";
    file << "telemetry=" << (state_.enableTelemetry ? "1" : "0") << "\n";
    file << "auto_updates=" << (state_.enableAutoUpdates ? "1" : "0") << "\n";
    file << "show_tips=" << (state_.showTips ? "1" : "0") << "\n";

    file << "\n[config]\n";
    file << "enable_marketplace=" << (config_.enableMarketplace ? "1" : "0") << "\n";
    file << "enable_telemetry=" << (config_.enableTelemetry ? "1" : "0") << "\n";
    file << "auto_updates=" << (config_.enableAutoUpdates ? "1" : "0") << "\n";
    file << "show_tips=" << (config_.showTips ? "1" : "0") << "\n";
    file << "default_theme=" << config_.defaultTheme << "\n";
    file << "default_language=" << config_.defaultLanguage << "\n";
    file << "animations=" << (config_.enableAnimations ? "1" : "0") << "\n";
    file << "animation_intensity=" << config_.animationIntensity << "\n";
    file << "enable_dma=" << (config_.enableDMA ? "1" : "0") << "\n";
    file << "auto_detect_dma=" << (config_.autoDetectDMA ? "1" : "0") << "\n";
    file << "default_game=" << config_.defaultGame << "\n";

    return true;
}

bool FirstRunWizard::LoadConfiguration() {
    std::string configPath = GetConfigPath();
    if (!fs::exists(configPath)) return false;

    std::ifstream file(configPath);
    if (!file) return false;

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        size_t eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);

        auto trim = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t\r\n"));
            s.erase(s.find_last_not_of(" \t\r\n") + 1);
        };
        trim(key);
        trim(value);

        if (currentSection == "wizard") {
            if (key == "completed") IsFirstRunComplete() ? void() : MarkFirstRunComplete();
            else if (key == "license_accepted") state_.licenseAccepted = (value == "1");
            else if (key == "selected_device") state_.selectedDeviceIndex = std::stoi(value);
            else if (key == "dma_config_path") state_.dmaConfigPath = value;
            else if (key == "use_external_dma") state_.useExternalDMA = (value == "1");
            else if (key == "skip_dma") state_.skipDMA = (value == "1");
            else if (key == "email") state_.email = value;
            else if (key == "license_key") state_.licenseKey = value;
            else if (key == "theme") state_.selectedTheme = std::stoi(value);
            else if (key == "animations") state_.enableAnimations = (value == "1");
            else if (key == "telemetry") state_.enableTelemetry = (value == "1");
            else if (key == "auto_updates") state_.enableAutoUpdates = (value == "1");
            else if (key == "show_tips") state_.showTips = (value == "1");
        } else if (currentSection == "config") {
            if (key == "enable_marketplace") config_.enableMarketplace = (value == "1");
            else if (key == "enable_telemetry") config_.enableTelemetry = (value == "1");
            else if (key == "auto_updates") config_.enableAutoUpdates = (value == "1");
            else if (key == "show_tips") config_.showTips = (value == "1");
            else if (key == "default_theme") config_.defaultTheme = std::stoi(value);
            else if (key == "default_language") config_.defaultLanguage = value;
            else if (key == "animations") config_.enableAnimations = (value == "1");
            else if (key == "animation_intensity") config_.animationIntensity = std::stoi(value);
            else if (key == "enable_dma") config_.enableDMA = (value == "1");
            else if (key == "auto_detect_dma") config_.autoDetectDMA = (value == "1");
            else if (key == "default_game") config_.defaultGame = value;
        }
    }

    return true;
}

void FirstRunWizard::SetDeviceTestCallback(std::function<void(int, TestResult, const std::string&)> callback) {
    deviceTestCallback_ = std::move(callback);
}

void FirstRunWizard::SetStepChangeCallback(std::function<void(WizardStep, WizardStep)> callback) {
    stepChangeCallback_ = std::move(callback);
}

const FirstRunConfig& FirstRunWizard::GetConfig() const {
    return config_;
}

void FirstRunWizard::SetConfig(const FirstRunConfig& config) {
    config_ = config;
}

bool FirstRunWizard::IsComplete() const {
    return state_.currentStep == WizardStep::Complete;
}

bool FirstRunWizard::IsFirstRun() const {
    return !IsFirstRunComplete();
}

void FirstRunWizard::MarkFirstRunComplete() {
    std::string configPath = GetConfigPath();
    fs::create_directories(fs::path(configPath).parent_path());

    std::ofstream file(configPath, std::ios::app);
    if (file) {
        file << "completed=1\n";
    }
}

void FirstRunWizard::Draw() {
    if (!isInitialized_) return;

    // This would be implemented with ImGui
    // For now, just a placeholder
}

const FirstRunConfig& FirstRunWizard::GetConfig() const {
    return config_;
}

void FirstRunWizard::SetConfig(const FirstRunConfig& config) {
    config_ = config;
}

bool IsFirstRunComplete() {
    std::string configPath = GetConfigPath();
    if (!fs::exists(configPath)) return false;

    std::ifstream file(configPath);
    std::string line;
    while (std::getline(file, line)) {
        if (line.find("completed=1") != std::string::npos) return true;
    }
    return false;
}

void MarkFirstRunComplete() {
    FirstRunWizard::Instance().MarkFirstRunComplete();
}

void RunFirstRunWizard() {
    FirstRunWizard::Instance().Initialize();
}

} // namespace Launcher::FirstRun