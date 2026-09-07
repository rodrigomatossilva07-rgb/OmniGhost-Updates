#pragma once
#include "../../ImGui/imgui.h"
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>

namespace Gameplay::SpectatorList {

    // Spectator info
    struct SpectatorInfo {
        uint64_t id = 0;
        std::string name;
        std::string platform_name;  // Steam, Discord, etc.
        uint64_t target_id = 0;     // Who they're spectating (0 = none/unknown)
        std::string target_name;
        int mode = 0;               // Spectator mode (0=death, 1=free, 2=player, etc.)
        bool is_friendly = false;
        bool is_streamer = false;
        bool is_admin = false;
        std::chrono::steady_clock::time_point first_seen;
        std::chrono::steady_clock::time_point last_update;
        std::string platform_id;    // SteamID64, etc.
        int ping = -1;
        std::string country;
    };

    // Spectator list configuration
    struct SpectatorConfig {
        bool enabled = true;
        bool show_on_hud = true;
        bool show_in_menu = true;
        
        // Display
        struct DisplayConfig {
            bool show_names = true;
            bool show_target = true;
            bool show_mode = true;
            bool show_ping = false;
            bool show_country = false;
            bool show_platform = true;
            bool show_duration = true;
            
            float max_distance = 500.0f;
            bool only_when_spectating_me = false;
            bool highlight_friends = true;
            bool highlight_streamers = true;
            bool highlight_admins = true;
        } display;
        
        // Alerts
        struct AlertConfig {
            bool notify_new_spectator = true;
            bool notify_spectating_me = true;
            bool notify_streamer = true;
            bool notify_admin = true;
            bool sound_alert = true;
            ImU32 alert_color = IM_COL32(255, 100, 100, 255);
            float alert_duration = 5.0f;
        } alerts;
        
        // Filtering
        std::vector<std::string> ignored_names;
        std::vector<uint64_t> ignored_ids;
        std::vector<int> ignored_teams;
        
        // UI
        ImVec2 window_size = ImVec2(350, 400);
        ImVec2 window_pos = ImVec2(50, 50);
        bool window_pinned = false;
        float background_alpha = 0.9f;
        ImU32 background_color = IM_COL32(15, 15, 20, 230);
        ImU32 header_color = IM_COL32(212, 175, 55, 255);
        ImU32 row_color_friendly = IM_COL32(80, 255, 100, 255);
        ImU32 row_color_enemy = IM_COL32(255, 80, 80, 255);
        ImU32 row_color_spectator = IM_COL32(255, 200, 80, 255);
        ImU32 row_color_streamer = IM_COL32(255, 100, 255, 255);
        ImU32 row_color_admin = IM_COL32(255, 255, 100, 255);
        
        // Performance
        int update_interval_ms = 1000;
    };

    // Spectator list manager
    class SpectatorManager {
    public:
        static SpectatorManager& Instance();
        
        void SetConfig(const SpectatorConfig& config) { config_ = config; }
        const SpectatorConfig& GetConfig() const { return config_; }
        
        // Update from game
        void UpdateSpectators(const std::vector<SpectatorInfo>& spectators);
        void AddSpectator(const SpectatorInfo& info);
        void RemoveSpectator(uint64_t id);
        void ClearSpectators();
        
        // Get spectators
        const std::vector<SpectatorInfo>& GetSpectators() const { return spectators_; }
        std::vector<SpectatorInfo> GetSpectatorsWatchingMe() const;
        std::vector<SpectatorInfo> GetSpectatorsByTeam(int team) const;
        
        // Check specific
        bool IsSpectatedBy(uint64_t id) const;
        bool IsBeingWatched() const;
        int GetSpectatorCount() const { return (int)spectators_.size(); }
        int GetSpectatorsWatchingMeCount() const;
        
        // UI
        void DrawHUD(ImDrawList* draw_list);
        void DrawMenu();
        void DrawAlerts(ImDrawList* draw_list);
        
        // Alerts
        void CheckAlerts();
        void ClearAlerts();
        
        // Serialization
        std::string SerializeConfig() const;
        bool DeserializeConfig(const std::string& data);
        
    private:
        SpectatorConfig config_;
        std::vector<SpectatorInfo> spectators_;
        std::vector<uint64_t> alert_ids_;
        std::chrono::steady_clock::time_point last_update_;
        std::chrono::steady_clock::time_point last_alert_check_;
        
        void CheckNewSpectators();
        void CleanupOldSpectators();
        void TriggerAlert(const SpectatorInfo& info, const std::string& reason);
        ImU32 GetRowColor(const SpectatorInfo& info) const;
        void DrawSpectatorRow(ImDrawList* draw_list, const SpectatorInfo& info, const ImVec2& pos, float width);
    };
    
    // Preset configurations
    SpectatorConfig GetDefaultSpectatorConfig();
    SpectatorConfig GetCompetitiveSpectatorConfig();
    SpectatorConfig GetStreamerSpectatorConfig();
    
    // Serialization
    std::string SerializeSpectatorConfig(const SpectatorConfig& config);
    bool DeserializeSpectatorConfig(const std::string& data, SpectatorConfig& config);

} // namespace Gameplay::SpectatorList