#include "hotkeys.h"
#include "widgets.h"
#include "theme.h"
#include "localization.h"
#include "../config/app_settings.h"
#include "src/games/Fivem/aimbot/aim_type.h"
#include <array>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>

namespace Hotkeys {

    namespace {
        std::array<Hotkey, static_cast<int>(Action::Count)> g_hotkeys;
        std::array<HotkeyPress, static_cast<int>(Action::Count)> g_prev_state;
        std::array<HotkeyPress, static_cast<int>(Action::Count)> g_curr_state;
        std::unordered_map<Action, std::function<void()>> g_callbacks;
        bool g_initialized = false;
        bool g_capturing = false;
        Action g_capture_action = Action::Count;
        struct FeatureToggle {
            bool* value = nullptr;
            int vk_code = 0;
            bool was_down = false;
        };
        std::unordered_map<std::string, FeatureToggle> g_feature_toggles;
        std::string g_feature_capture;

        bool FeatureKeyDown(int vk)
        {
            if (vk <= 0) return false;
            // IsDown includes the input devices configured by the user; the
            // Windows state keeps ordinary primary-PC keyboard use working.
            return (GetAsyncKeyState(vk) & 0x8000) != 0 || aim_type::IsDown(vk);
        }

        std::string HexId(const std::string& id)
        {
            std::ostringstream out;
            out << std::hex << std::hash<std::string>{}(id);
            return out.str();
        }
    }

    void Initialize() {
        if (g_initialized) return;
        RegisterDefaults();
        g_initialized = true;
    }

    void Shutdown() {
        g_callbacks.clear();
        g_initialized = false;
    }

    void Update() {
        if (!g_initialized) return;
        
        // Update current state
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            g_prev_state[i] = g_curr_state[i];
            
            const Hotkey& hk = g_hotkeys[i];
            if (!hk.enabled || hk.vk_code == 0) {
                g_curr_state[i] = {0, 0};
                continue;
            }
            
            bool vk_down = (GetAsyncKeyState(hk.vk_code) & 0x8000) != 0;
            bool ctrl_down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shift_down = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
            bool alt_down = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            bool win_down = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;
            
            int current_modifiers = 0;
            if (ctrl_down) current_modifiers |= 1;
            if (shift_down) current_modifiers |= 2;
            if (alt_down) current_modifiers |= 4;
            if (win_down) current_modifiers |= 8;
            
            // Allow extra modifiers (e.g., if hotkey is Ctrl+S, allow Ctrl+Shift+S)
            bool modifiers_ok = (current_modifiers & hk.modifiers) == hk.modifiers;
            
            g_curr_state[i] = { vk_down ? hk.vk_code : 0, vk_down && modifiers_ok ? current_modifiers : 0 };
        }

        for (auto& [id, feature] : g_feature_toggles) {
            if (!feature.value || feature.vk_code <= 0) continue;
            const bool down = FeatureKeyDown(feature.vk_code);
            if (down && !feature.was_down)
                *feature.value = !*feature.value;
            feature.was_down = down;
        }
        
        // Check for presses and trigger callbacks
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            bool was_down = g_prev_state[i].vk_code != 0;
            bool is_down = g_curr_state[i].vk_code != 0;
            
            if (is_down && !was_down) {
                // Key pressed
                Action action = static_cast<Action>(i);
                auto it = g_callbacks.find(action);
                if (it != g_callbacks.end() && it->second) {
                    it->second();
                }
            }
        }
    }

    void RegisterDefaults() {
        g_hotkeys[static_cast<int>(Action::ToggleMenu)] = { Action::ToggleMenu, VK_INSERT, 0, "Toggle Menu", "Open/close the main menu" };
        g_hotkeys[static_cast<int>(Action::ToggleESP)] = { Action::ToggleESP, 0, 0, "Toggle ESP", "Enable/disable ESP visuals" };
        g_hotkeys[static_cast<int>(Action::ToggleAim)] = { Action::ToggleAim, 0, 0, "Toggle Aim", "Enable/disable aim assist" };
        g_hotkeys[static_cast<int>(Action::ToggleVehicle)] = { Action::ToggleVehicle, 0, 0, "Toggle Vehicle ESP", "Enable/disable vehicle ESP" };
        g_hotkeys[static_cast<int>(Action::ToggleConfigMenu)] = { Action::ToggleConfigMenu, VK_F1, 0, "Config Menu", "Quick access to config menu" };
        g_hotkeys[static_cast<int>(Action::ReloadConfig)] = { Action::ReloadConfig, VK_F5, 0, "Reload Config", "Reload current configuration" };
        g_hotkeys[static_cast<int>(Action::SaveConfig)] = { Action::SaveConfig, 'S', 1, "Save Config", "Save current configuration (Ctrl+S)" };
        g_hotkeys[static_cast<int>(Action::TakeScreenshot)] = { Action::TakeScreenshot, VK_SNAPSHOT, 0, "Screenshot", "Take a screenshot" };
        g_hotkeys[static_cast<int>(Action::ToggleOverlay)] = { Action::ToggleOverlay, VK_F2, 0, "Toggle Overlay", "Show/hide overlay" };
        g_hotkeys[static_cast<int>(Action::TogglePerformanceMode)] = { Action::TogglePerformanceMode, VK_F3, 0, "Performance Mode", "Toggle performance mode" };
        g_hotkeys[static_cast<int>(Action::ToggleCrosshair)] = { Action::ToggleCrosshair, 0, 0, "Toggle Crosshair", "Show/hide crosshair" };
        g_hotkeys[static_cast<int>(Action::PanicButton)] = { Action::PanicButton, VK_END, 0, "Panic Button", "Emergency disable all features" };
        g_hotkeys[static_cast<int>(Action::ReinitDMA)] = { Action::ReinitDMA, 'R', 1, "Reinit DMA", "Reinitialize DMA connection (Ctrl+R)" };
        g_hotkeys[static_cast<int>(Action::TestInputDevice)] = { Action::TestInputDevice, 'T', 1, "Test Input", "Test input device (Ctrl+T)" };
        g_hotkeys[static_cast<int>(Action::Custom1)] = { Action::Custom1, 0, 0, "Custom 1", "Custom action 1" };
        g_hotkeys[static_cast<int>(Action::Custom2)] = { Action::Custom2, 0, 0, "Custom 2", "Custom action 2" };
        g_hotkeys[static_cast<int>(Action::Custom3)] = { Action::Custom3, 0, 0, "Custom 3", "Custom action 3" };
    }

    const Hotkey& GetHotkey(Action action) {
        int idx = static_cast<int>(action);
        if (idx >= 0 && idx < static_cast<int>(Action::Count)) {
            return g_hotkeys[idx];
        }
        static Hotkey empty = { Action::Count, 0, 0, "", "" };
        return empty;
    }

    bool SetHotkey(Action action, int vk_code, int modifiers) {
        int idx = static_cast<int>(action);
        if (idx < 0 || idx >= static_cast<int>(Action::Count)) return false;
        
        // Check for conflicts
        Hotkey new_hk = g_hotkeys[idx];
        new_hk.vk_code = vk_code;
        new_hk.modifiers = modifiers;
        
        if (HasConflict(new_hk, action)) {
            return false; // Conflict detected
        }
        
        g_hotkeys[idx].vk_code = vk_code;
        g_hotkeys[idx].modifiers = modifiers;
        return true;
    }

    void ClearHotkey(Action action) {
        int idx = static_cast<int>(action);
        if (idx >= 0 && idx < static_cast<int>(Action::Count)) {
            g_hotkeys[idx].vk_code = 0;
            g_hotkeys[idx].modifiers = 0;
        }
    }

    bool IsPressed(Action action) {
        int idx = static_cast<int>(action);
        if (idx >= 0 && idx < static_cast<int>(Action::Count)) {
            return g_curr_state[idx].vk_code != 0;
        }
        return false;
    }

    bool WasPressed(Action action) {
        int idx = static_cast<int>(action);
        if (idx >= 0 && idx < static_cast<int>(Action::Count)) {
            return g_curr_state[idx].vk_code != 0 && g_prev_state[idx].vk_code == 0;
        }
        return false;
    }

    const std::vector<Hotkey>& GetAllHotkeys() {
        static std::vector<Hotkey> all;
        all.clear();
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            all.push_back(g_hotkeys[i]);
        }
        return all;
    }

    bool HasConflict(const Hotkey& hk, Action exclude) {
        if (hk.vk_code == 0) return false;
        
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            if (static_cast<Action>(i) == exclude) continue;
            const Hotkey& other = g_hotkeys[i];
            if (other.vk_code == hk.vk_code && other.modifiers == hk.modifiers) {
                return true;
            }
        }
        return false;
    }

    std::vector<Action> GetConflicts(const Hotkey& hk) {
        std::vector<Action> conflicts;
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            const Hotkey& other = g_hotkeys[i];
            if (other.vk_code == hk.vk_code && other.modifiers == hk.modifiers) {
                conflicts.push_back(static_cast<Action>(i));
            }
        }
        return conflicts;
    }

    std::string ExportHotkeys() {
        std::ostringstream out;
        out << "{\n  \"hotkeys\": [\n";
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            const Hotkey& hk = g_hotkeys[i];
            out << "    {\n";
            out << "      \"action\": " << i << ",\n";
            out << "      \"vk_code\": " << hk.vk_code << ",\n";
            out << "      \"modifiers\": " << hk.modifiers << ",\n";
            out << "      \"enabled\": " << (hk.enabled ? "true" : "false") << "\n";
            out << "    }" << (i < static_cast<int>(Action::Count) - 1 ? "," : "") << "\n";
        }
        out << "  ]\n}";
        return out.str();
    }

    bool ImportHotkeys(const std::string& json) {
        try {
            // Simple JSON parsing for hotkeys
            std::istringstream in(json);
            std::string line;
            while (std::getline(in, line)) {
                if (line.find("\"action\"") != std::string::npos) {
                    int action = 0, vk = 0, mods = 0;
                    bool enabled = true;
                    
                    size_t pos = line.find(":");
                    if (pos != std::string::npos) action = std::stoi(line.substr(pos + 1));
                    
                    std::getline(in, line);
                    pos = line.find(":");
                    if (pos != std::string::npos) vk = std::stoi(line.substr(pos + 1));
                    
                    std::getline(in, line);
                    pos = line.find(":");
                    if (pos != std::string::npos) mods = std::stoi(line.substr(pos + 1));
                    
                    std::getline(in, line);
                    pos = line.find(":");
                    if (pos != std::string::npos) enabled = line.find("true") != std::string::npos;
                    
                    if (action >= 0 && action < static_cast<int>(Action::Count)) {
                        g_hotkeys[action].vk_code = vk;
                        g_hotkeys[action].modifiers = mods;
                        g_hotkeys[action].enabled = enabled;
                    }
                }
            }
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ExportToFile(const char* path) {
        try {
            std::ofstream file(path);
            if (!file) return false;
            file << ExportHotkeys();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool ImportFromFile(const char* path) {
        try {
            std::ifstream file(path);
            if (!file) return false;
            std::stringstream buffer;
            buffer << file.rdbuf();
            return ImportHotkeys(buffer.str());
        } catch (...) {
            return false;
        }
    }

    void ResetToDefaults() {
        RegisterDefaults();
    }

    const char* GetKeyName(int vk_code) {
        switch (vk_code) {
            case 0: return "None";
            case VK_LBUTTON: return "LMB";
            case VK_RBUTTON: return "RMB";
            case VK_MBUTTON: return "MMB";
            case VK_XBUTTON1: return "X1";
            case VK_XBUTTON2: return "X2";
            case VK_BACK: return "Backspace";
            case VK_TAB: return "Tab";
            case VK_RETURN: return "Enter";
            case VK_SHIFT: return "Shift";
            case VK_CONTROL: return "Ctrl";
            case VK_MENU: return "Alt";
            case VK_CAPITAL: return "CapsLock";
            case VK_ESCAPE: return "Esc";
            case VK_SPACE: return "Space";
            case VK_PRIOR: return "PageUp";
            case VK_NEXT: return "PageDown";
            case VK_END: return "End";
            case VK_HOME: return "Home";
            case VK_LEFT: return "Left";
            case VK_UP: return "Up";
            case VK_RIGHT: return "Right";
            case VK_DOWN: return "Down";
            case VK_SNAPSHOT: return "PrintScr";
            case VK_INSERT: return "Insert";
            case VK_DELETE: return "Delete";
            case VK_F1: return "F1"; case VK_F2: return "F2"; case VK_F3: return "F3";
            case VK_F4: return "F4"; case VK_F5: return "F5"; case VK_F6: return "F6";
            case VK_F7: return "F7"; case VK_F8: return "F8"; case VK_F9: return "F9";
            case VK_F10: return "F10"; case VK_F11: return "F11"; case VK_F12: return "F12";
            default:
                if (vk_code >= '0' && vk_code <= '9') {
                    static char buf[2] = {0, 0};
                    buf[0] = static_cast<char>(vk_code);
                    return buf;
                }
                if (vk_code >= 'A' && vk_code <= 'Z') {
                    static char buf[2] = {0, 0};
                    buf[0] = static_cast<char>(vk_code);
                    return buf;
                }
                static char buf[16];
                std::snprintf(buf, sizeof(buf), "VK 0x%02X", vk_code);
                return buf;
        }
    }

    const char* GetModifiersName(int modifiers) {
        static char buf[64];
        buf[0] = '\0';
        bool first = true;
        if (modifiers & 1) { if (!first) std::strcat(buf, " + "); std::strcat(buf, "Ctrl"); first = false; }
        if (modifiers & 2) { if (!first) std::strcat(buf, " + "); std::strcat(buf, "Shift"); first = false; }
        if (modifiers & 4) { if (!first) std::strcat(buf, " + "); std::strcat(buf, "Alt"); first = false; }
        if (modifiers & 8) { if (!first) std::strcat(buf, " + "); std::strcat(buf, "Win"); first = false; }
        if (buf[0] == '\0') std::strcpy(buf, "None");
        return buf;
    }

    std::string GetHotkeyDisplayString(const Hotkey& hk) {
        if (hk.vk_code == 0) return "Not bound";
        std::string mods = GetModifiersName(hk.modifiers);
        std::string key = GetKeyName(hk.vk_code);
        if (mods == "None") return key;
        return mods + " + " + key;
    }

    void RegisterCallback(Action action, HotkeyCallback callback) {
        g_callbacks[action] = callback;
    }

    void UnregisterCallback(Action action) {
        g_callbacks.erase(action);
    }

    void RegisterFeatureToggle(const std::string& id, bool* value)
    {
        if (id.empty() || !value) return;
        auto& feature = g_feature_toggles[id];
        feature.value = value;
        const std::string pending = "#pending:" + HexId(id);
        if (const auto it = g_feature_toggles.find(pending); it != g_feature_toggles.end()) {
            feature.vk_code = it->second.vk_code;
            g_feature_toggles.erase(it);
        }
    }

    bool BeginFeatureCapture(const std::string& id)
    {
        if (id.empty()) return false;
        g_feature_capture = id;
        return true;
    }

    void SetFeatureKey(const std::string& id, int vk_code)
    {
        auto it = g_feature_toggles.find(id);
        if (it == g_feature_toggles.end()) return;
        it->second.vk_code = (vk_code > 0 && vk_code < 256) ? vk_code : 0;
        it->second.was_down = false;
        g_feature_capture.clear();
    }

    bool IsCapturingFeature(const std::string& id)
    {
        return !id.empty() && g_feature_capture == id;
    }

    int FeatureKey(const std::string& id)
    {
        const auto it = g_feature_toggles.find(id);
        return it == g_feature_toggles.end() ? 0 : it->second.vk_code;
    }

    std::string SerializeFeatureToggles()
    {
        std::ostringstream out;
        for (const auto& [id, feature] : g_feature_toggles) {
            if (feature.vk_code > 0)
                out << "hotkey.feature." << (id.rfind("#pending:", 0) == 0 ? id.substr(9) : HexId(id)) << "=" << feature.vk_code << "\n";
        }
        return out.str();
    }

    bool DeserializeFeatureToggle(const std::string& key, const std::string& value)
    {
        constexpr const char* prefix = "hotkey.feature.";
        if (key.rfind(prefix, 0) != 0) return false;
        const int vk = std::atoi(value.c_str());
        if (vk <= 0 || vk > 255) return true;
        const std::string savedHash = key.substr(std::strlen(prefix));
        // Widgets register after a config loads; keep the saved hash in a
        // placeholder until its matching row is rendered.
        g_feature_toggles["#pending:" + savedHash].vk_code = vk;
        return true;
    }

    void DrawHotkeyConfig() {
        CyberWidgets::BeginCard("Keyboard Shortcuts");
        CyberWidgets::TextLine("Configure keyboard shortcuts for quick actions. Click a binding to change it.", CyberWidgets::TextTone::Secondary);
        CyberWidgets::CardGap(8.0f);
        
        static bool waiting_for_key = false;
        static Action waiting_action = Action::Count;
        
        if (waiting_for_key) {
            CyberWidgets::TextLine("Press a key combination... (Esc to cancel)", CyberWidgets::TextTone::Warning);
            
            // Check for key press
            for (int vk = 1; vk < 256; ++vk) {
                if (GetAsyncKeyState(vk) & 1) {
                    if (vk == VK_ESCAPE) {
                        waiting_for_key = false;
                        waiting_action = Action::Count;
                    } else {
                        int modifiers = 0;
                        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) modifiers |= 1;
                        if (GetAsyncKeyState(VK_SHIFT) & 0x8000) modifiers |= 2;
                        if (GetAsyncKeyState(VK_MENU) & 0x8000) modifiers |= 4;
                        if ((GetAsyncKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_RWIN) & 0x8000)) modifiers |= 8;
                        
                        // Don't allow modifier-only bindings
                        if (vk != VK_CONTROL && vk != VK_SHIFT && vk != VK_MENU && vk != VK_LWIN && vk != VK_RWIN) {
                            if (SetHotkey(waiting_action, vk, modifiers)) {
                                CyberWidgets::Notify("Hotkey updated", CyberWidgets::ToastType::Success);
                            } else {
                                CyberWidgets::Notify("Hotkey conflicts with another action", CyberWidgets::ToastType::Error);
                            }
                        }
                        waiting_for_key = false;
                        waiting_action = Action::Count;
                    }
                    break;
                }
            }
            CyberWidgets::CardGap(8.0f);
        }
        
        // Hotkey list
        CyberWidgets::BeginSurfaceList("##hotkey_list", 400.0f);
        
        for (int i = 0; i < static_cast<int>(Action::Count); ++i) {
            Hotkey& hk = g_hotkeys[i];
            if (!hk.enabled && hk.vk_code == 0 && i >= static_cast<int>(Action::Custom1)) continue;
            
            ImGui::PushID(i);
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float row_width = ImGui::GetContentRegionAvail().x;
            const float rowH = 36.0f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            
            // Background
            bool hovered = false;
            ImGui::InvisibleButton("##row", ImVec2(row_width, rowH));
            hovered = ImGui::IsItemHovered();
            if (hovered) {
                dl->AddRectFilled(row, ImVec2(row.x + row_width, row.y + rowH), 
                    CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.1f), CyberTheme::Radius::Sm);
            }
            
            // Action label
            dl->AddText(ImVec2(row.x + 10.0f, row.y + 9.0f),
                CyberTheme::U32(CyberTheme::Colors.Text), hk.label.c_str());
            
            // Description
            if (!hk.description.empty()) {
                dl->AddText(ImVec2(row.x + 10.0f, row.y + 22.0f),
                    CyberTheme::U32(CyberTheme::Colors.TextDisabled), hk.description.c_str());
            }
            
            // Current binding
            std::string display = GetHotkeyDisplayString(hk);
            ImVec2 text_size = ImGui::CalcTextSize(display.c_str());
            float btn_x = row.x + row_width - text_size.x - 20.0f;
            if (btn_x < row.x + 200.0f) btn_x = row.x + 200.0f;
            
            bool is_waiting = waiting_for_key && waiting_action == static_cast<Action>(i);
            if (is_waiting) display = "Press key...";
            
            ImVec2 btn_pos(btn_x, row.y + 4.0f);
            ImVec2 btn_end(btn_pos.x + text_size.x + 20.0f, btn_pos.y + 28.0f);
            
            ImU32 btn_color = is_waiting ? CyberTheme::U32(CyberTheme::Colors.Warning) : 
                (hovered ? CyberTheme::U32(CyberTheme::Colors.PanelHover) : CyberTheme::U32(CyberTheme::Colors.Panel));
            
            dl->AddRectFilled(btn_pos, btn_end, btn_color, 4.0f);
            dl->AddRect(btn_pos, btn_end, 
                CyberTheme::WithAlpha(is_waiting ? CyberTheme::Colors.Warning : CyberTheme::Colors.Border, 0.5f), 4.0f);
            dl->AddText(ImVec2(btn_pos.x + 10.0f, btn_pos.y + 6.0f),
                CyberTheme::U32(CyberTheme::Colors.Text), display.c_str());
            
            // Click to edit
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                waiting_for_key = true;
                waiting_action = static_cast<Action>(i);
            }
            
            // Clear button
            if (hk.vk_code != 0) {
                float clear_x = btn_pos.x - 30.0f;
                ImVec2 clear_pos(clear_x, row.y + 4.0f);
                ImVec2 clear_end(clear_x + 24.0f, clear_pos.y + 24.0f);
                dl->AddRectFilled(clear_pos, clear_end, CyberTheme::U32(CyberTheme::Colors.Panel), 4.0f);
                dl->AddRect(clear_pos, clear_end, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.5f), 4.0f);
                dl->AddLine(ImVec2(clear_pos.x + 6.0f, clear_pos.y + 6.0f), 
                    ImVec2(clear_end.x - 6.0f, clear_end.y - 6.0f), CyberTheme::U32(CyberTheme::Colors.TextDisabled), 1.5f);
                dl->AddLine(ImVec2(clear_end.x - 6.0f, clear_pos.y + 6.0f), 
                    ImVec2(clear_pos.x + 6.0f, clear_end.y - 6.0f), CyberTheme::U32(CyberTheme::Colors.TextDisabled), 1.5f);
                
                if (ImGui::IsMouseHoveringRect(clear_pos, clear_end) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    ClearHotkey(static_cast<Action>(i));
                    CyberWidgets::Notify("Hotkey cleared", CyberWidgets::ToastType::Info);
                }
            }
            
            ImGui::Dummy(ImVec2(row_width, rowH + 4.0f));
            ImGui::PopID();
        }
        
        CyberWidgets::EndSurfaceList();
        
        // Reset button
        CyberWidgets::CardGap(8.0f);
        if (CyberWidgets::Button("Reset to Defaults", CyberWidgets::ButtonStyle::Ghost)) {
            ResetToDefaults();
            CyberWidgets::Notify("Hotkeys reset to defaults", CyberWidgets::ToastType::Success);
        }
        ImGui::SameLine();
        if (CyberWidgets::Button("Export", CyberWidgets::ButtonStyle::Secondary)) {
            // Export to file
        }
        ImGui::SameLine();
        if (CyberWidgets::Button("Import", CyberWidgets::ButtonStyle::Secondary)) {
            // Import from file
        }
        
        CyberWidgets::EndCard();
    }

} // namespace Hotkeys
