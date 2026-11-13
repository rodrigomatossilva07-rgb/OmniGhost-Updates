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
        int vk_code = 0;
        int modifiers = 0;
        std::string label;
        std::string description;
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


    void RegisterDefaults();


    const Hotkey& GetHotkey(Action action);


    bool SetHotkey(Action action, int vk_code, int modifiers);


    void ClearHotkey(Action action);


    bool IsPressed(Action action);
    bool WasPressed(Action action);


    const std::vector<Hotkey>& GetAllHotkeys();


    bool HasConflict(const Hotkey& hk, Action exclude = Action::Count);
    std::vector<Action> GetConflicts(const Hotkey& hk);


    std::string ExportHotkeys();
    bool ImportHotkeys(const std::string& json);
    bool ExportToFile(const char* path);
    bool ImportFromFile(const char* path);



    std::string SerializeActions();
    bool DeserializeAction(const std::string& key, const std::string& value);


    void ResetToDefaults();


    void DrawHotkeyConfig();


    const char* GetKeyName(int vk_code);
    const char* GetModifiersName(int modifiers);
    std::string GetHotkeyDisplayString(const Hotkey& hk);


    using HotkeyCallback = std::function<void()>;
    void RegisterCallback(Action action, HotkeyCallback callback);
    void UnregisterCallback(Action action);




    void RegisterFeatureToggle(const std::string& id, bool* value);
    bool BeginFeatureCapture(const std::string& id);
    void SetFeatureKey(const std::string& id, int vk_code);
    bool IsCapturingFeature(const std::string& id);
    int FeatureKey(const std::string& id);
    std::string SerializeFeatureToggles();
    bool DeserializeFeatureToggle(const std::string& key, const std::string& value);

}
