#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Hotkeys {

    enum class Action {
        ToggleMenu,
        ToggleESP,
        ToggleAim,
        ToggleVehicle,
        ToggleConfigMenu,
        ReloadConfig,
        SaveConfig,
        TakeScreenshot,
        ToggleOverlay,
        TogglePerformanceMode,
        ToggleCrosshair,
        PanicButton,
        ReinitDMA,
        TestInputDevice,
        Custom1,
        Custom2,
        Custom3,
        Count
    };

    struct Hotkey {
        Action action;
        int vk_code = 0;          // Virtual key code
        int modifiers = 0;        // Modifiers (Ctrl=1, Shift=2, Alt=4, Win=8)
        std::string label;        // Display name
        std::string description;  // Help text
        bool enabled = true;
    };

    struct HotkeyPress {
        int vk_code;
        int modifiers;
        bool operator==(const HotkeyPress& other) const {
            return vk_code == other.vk_code && modifiers == other.modifiers;
        }
    };

    void Initialize();
    void Shutdown();
    void Update();
    
    // Register default hotkeys
    void RegisterDefaults();
    
    // Get hotkey for action
    const Hotkey& GetHotkey(Action action);
    
    // Set hotkey for action
    bool SetHotkey(Action action, int vk_code, int modifiers);
    
    // Clear hotkey for action
    void ClearHotkey(Action action);
    
    // Check if hotkey is pressed
    bool IsPressed(Action action);
    bool WasPressed(Action action); // Single-frame press
    
    // Get all hotkeys
    const std::vector<Hotkey>& GetAllHotkeys();
    
    // Conflict detection
    bool HasConflict(const Hotkey& hk, Action exclude = Action::Count);
    std::vector<Action> GetConflicts(const Hotkey& hk);
    
    // Import/Export
    std::string ExportHotkeys();
    bool ImportHotkeys(const std::string& json);
    bool ExportToFile(const char* path);
    bool ImportFromFile(const char* path);
    
    // Reset to defaults
    void ResetToDefaults();
    
    // Draw hotkey configuration UI
    void DrawHotkeyConfig();
    
    // Helper: Get key name
    const char* GetKeyName(int vk_code);
    const char* GetModifiersName(int modifiers);
    std::string GetHotkeyDisplayString(const Hotkey& hk);
    
    // Callback registration
    using HotkeyCallback = std::function<void()>;
    void RegisterCallback(Action action, HotkeyCallback callback);
    void UnregisterCallback(Action action);

    // Per-feature toggle keys.  These are intentionally independent from the
    // fixed application shortcuts above: a feature only gets a key after the
    // user captures one on its own toggle row.
    void RegisterFeatureToggle(const std::string& id, bool* value);
    bool BeginFeatureCapture(const std::string& id);
    void SetFeatureKey(const std::string& id, int vk_code);
    bool IsCapturingFeature(const std::string& id);
    int FeatureKey(const std::string& id);
    std::string SerializeFeatureToggles();
    bool DeserializeFeatureToggle(const std::string& key, const std::string& value);

} // namespace Hotkeys
