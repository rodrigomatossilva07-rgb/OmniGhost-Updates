#pragma once
#include "../../ImGui/imgui.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace Gameplay::ProfileManager {

    // Game types
    enum class GameType : int {
        FiveM = 0,
        CS2 = 1,
        Warzone = 3,
        Fortnite = 5,
        Apex = 6,
        Custom = 99
    };

    // Profile types
    enum class ProfileType : int {
        Legit = 0,
        SemiLegit = 1,
        Competitive = 2,
        Rage = 3,
        Sniper = 4,
        Movement = 5,
        Custom = 99
    };

    // Feature categories for profile settings
    enum class FeatureCategory : int {
        Aim = 0,
        ESP = 1,
        Triggerbot = 2,
        Movement = 3,
        Recoil = 4,
        Radar = 5,
        SoundESP = 6,
        Visuals = 7,
        Misc = 8
    };

    // Generic setting value
    struct SettingValue {
        enum class Type : int {
            Bool = 0,
            Int = 1,
            Float = 2,
            String = 3,
            Color = 4,
            Enum = 5,
            Vec2 = 6,
            Vec3 = 7
        } type = Type::Bool;
        
        bool bool_val = false;
        int int_val = 0;
        float float_val = 0.0f;
        std::string string_val;
        ImU32 color_val = 0;
        ImVec2 vec2_val = ImVec2(0, 0);
        ImVec4 vec3_val = ImVec4(0, 0, 0, 0);
    };

    // Profile setting
    struct ProfileSetting {
        std::string key;
        std::string name;
        std::string description;
        FeatureCategory category = FeatureCategory::Misc;
        SettingValue value;
        SettingValue default_value;
        SettingValue min_value;
        SettingValue max_value;
        bool is_advanced = false;
        bool requires_restart = false;
        std::vector<std::string> enum_options; // For enum type
    };

    // Game profile
    struct GameProfile {
        std::string id;              // Unique identifier
        std::string name;            // Display name
        std::string description;
        GameType game = GameType::Custom;
        ProfileType type = ProfileType::Legit;
        std::string author;
        std::string version = "1.0";
        std::chrono::system_clock::time_point created;
        std::chrono::system_clock::time_point modified;
        bool is_builtin = false;
        bool is_active = false;
        std::vector<std::string> tags; // "legit", "competitive", "sniper", etc.
        
        // Settings organized by category
        std::unordered_map<std::string, ProfileSetting> settings;
        
        // Metadata
        int usage_count = 0;
        float rating = 0.0f; // 0-5
        std::string checksum; // For integrity
    };

    // Profile manager
    class ProfileManager {
    public:
        static ProfileManager& Instance();
        
        // Profile management
        GameProfile* CreateProfile(const std::string& name, GameType game, ProfileType type);
        bool DeleteProfile(const std::string& id);
        GameProfile* GetProfile(const std::string& id);
        GameProfile* GetProfileByName(const std::string& name, GameType game);
        std::vector<GameProfile*> GetProfiles(GameType game = GameType::Custom, ProfileType type = ProfileType::Custom);
        std::vector<GameProfile*> GetProfilesByTag(const std::string& tag);
        
        // Active profile
        void SetActiveProfile(GameType game, const std::string& profile_id);
        GameProfile* GetActiveProfile(GameType game) const;
        
        // Settings
        bool SetSetting(GameProfile* profile, const std::string& key, const SettingValue& value);
        SettingValue GetSetting(const GameProfile* profile, const std::string& key) const;
        bool ResetSetting(GameProfile* profile, const std::string& key);
        void ResetAllSettings(GameProfile* profile);
        
        // Import/Export
        std::string ExportProfile(const GameProfile* profile) const;
        GameProfile* ImportProfile(const std::string& data);
        bool ExportToFile(const GameProfile* profile, const char* path);
        GameProfile* ImportFromFile(const char* path);
        
        // Profile inheritance
        GameProfile* CreateChildProfile(const std::string& name, const std::string& parent_id);
        void ApplyParentSettings(GameProfile* child, const GameProfile* parent);
        
        // Community features
        bool UploadProfile(const GameProfile* profile, const std::string& description);
        std::vector<GameProfile*> SearchCommunityProfiles(const std::string& query, GameType game = GameType::Custom);
        bool DownloadProfile(const std::string& profile_id);
        
        // Profile comparison
        std::vector<std::string> CompareProfiles(const GameProfile* a, const GameProfile* b);
        
        // Serialization
        std::string SerializeProfile(const GameProfile* profile) const;
        GameProfile* DeserializeProfile(const std::string& data);
        
        // Auto-save
        void EnableAutoSave(bool enable, float interval = 30.0f);
        
    private:
        std::unordered_map<std::string, GameProfile> profiles_;
        std::unordered_map<GameType, std::string> active_profiles_; // game -> profile_id
        
        std::string GenerateProfileId() const;
        void ApplyProfileToGame(const GameProfile* profile, GameType game);
        void RegisterDefaultSettings(GameProfile* profile, GameType game);
    };
    
    // Profile UI
    class ProfileUI {
    public:
        static void DrawProfileList();
        static void DrawProfileEditor(GameProfile* profile);
        static void DrawProfileSelector(GameType game);
        static void DrawSettingsPanel(GameProfile* profile, FeatureCategory category = FeatureCategory::Misc);
        static void DrawProfileComparison(const GameProfile* a, const GameProfile* b);
        
        // Callbacks
        static std::function<void(GameProfile*)> on_profile_selected;
        static std::function<void(GameProfile*, const std::string&, const SettingValue&)> on_setting_changed;
    };
    
    // Built-in profiles
    void RegisterBuiltinProfiles();
    
    // Helper functions
    const char* GetGameTypeName(GameType game);
    const char* GetProfileTypeName(ProfileType type);
    const char* GetFeatureCategoryName(FeatureCategory cat);
    
    // Serialization
    std::string SerializeSettingValue(const SettingValue& value);
    bool DeserializeSettingValue(const std::string& data, SettingValue& value);
    std::string SerializeProfile(const GameProfile& profile);
    GameProfile* DeserializeProfile(const std::string& data);

} // namespace Gameplay::ProfileManager
