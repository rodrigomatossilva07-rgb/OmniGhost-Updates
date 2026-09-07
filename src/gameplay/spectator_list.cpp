#include "spectator_list.h"
#include <algorithm>
#include <chrono>
#include <sstream>

namespace Gameplay::SpectatorList {

    SpectatorManager& SpectatorManager::Instance() {
        static SpectatorManager instance;
        return instance;
    }

    void SpectatorManager::UpdateSpectators(const std::vector<SpectatorInfo>& spectators) {
        auto now = std::chrono::steady_clock::now();
        
        // Update existing and add new
        for (const auto& new_spec : spectators) {
            bool found = false;
            for (auto& existing : spectators_) {
                if (existing.id == new_spec.id) {
                    existing = new_spec;
                    existing.last_update = now;
                    found = true;
                    break;
                }
            }
            if (!found) {
                SpectatorInfo info = new_spec;
                info.first_seen = now;
                info.last_update = now;
                spectators_.push_back(info);
                
                // Check alerts for new spectator
                if (config_.alerts.notify_new_spectator) {
                    TriggerAlert(info, "New spectator detected");
                }
                if (info.target_id != 0 && config_.alerts.notify_spectating_me) {
                    // Would need local player ID check
                }
            }
        }
        
        CleanupOldSpectators();
        last_update_ = now;
    }

    void SpectatorManager::AddSpectator(const SpectatorInfo& info) {
        // Check if already exists
        for (auto& existing : spectators_) {
            if (existing.id == info.id) {
                existing = info;
                existing.last_update = std::chrono::steady_clock::now();
                return;
            }
        }
        
        SpectatorInfo new_info = info;
        new_info.first_seen = std::chrono::steady_clock::now();
        new_info.last_update = new_info.first_seen;
        spectators_.push_back(new_info);
    }

    void SpectatorManager::RemoveSpectator(uint64_t id) {
        spectators_.erase(
            std::remove_if(spectators_.begin(), spectators_.end(),
                [id](const SpectatorInfo& s) { return s.id == id; }),
            spectators_.end());
    }

    void SpectatorManager::ClearSpectators() {
        spectators_.clear();
    }

    const std::vector<SpectatorInfo>& SpectatorManager::GetSpectators() const {
        return spectators_;
    }

    std::vector<SpectatorInfo> SpectatorManager::GetSpectatorsWatchingMe() const {
        // Would need local player ID
        std::vector<SpectatorInfo> result;
        for (const auto& spec : spectators_) {
            // if (spec.target_id == local_player_id) result.push_back(spec);
        }
        return result;
    }

    std::vector<SpectatorInfo> SpectatorManager::GetSpectatorsByTeam(int team) const {
        std::vector<SpectatorInfo> result;
        for (const auto& spec : spectators_) {
            // Would need team info
        }
        return result;
    }

    bool SpectatorManager::IsSpectatedBy(uint64_t id) const {
        for (const auto& spec : spectators_) {
            if (spec.id == id && spec.target_id != 0) return true;
        }
        return false;
    }

    bool SpectatorManager::IsBeingWatched() const {
        return !GetSpectatorsWatchingMe().empty();
    }

    int SpectatorManager::GetSpectatorsWatchingMeCount() const {
        return (int)GetSpectatorsWatchingMe().size();
    }

    void SpectatorManager::DrawHUD(ImDrawList* draw_list) {
        if (!config_.enabled || !config_.show_on_hud || spectators_.empty()) return;
        if (!draw_list) return;
        
        ImVec2 pos = config_.window_pos;
        float width = config_.window_size.x;
        float row_height = 24.0f;
        float header_height = 30.0f;
        
        // Background
        ImU32 bg = config_.background_color;
        bg = (bg & 0xFFFFFF) | (int)(config_.background_alpha * 255) << 24;
        
        float height = header_height + spectators_.size() * row_height + 10;
        draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg, 6.0f);
        draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), 
            IM_COL32(80, 80, 100, 180), 6.0f, 0, 1.0f);
        
        // Header
        draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + header_height), 
            config_.header_color, 6.0f, ImDrawFlags_RoundCornersTop);
        draw_list->AddText(ImVec2(pos.x + 10, pos.y + 5), IM_COL32(0, 0, 0, 255), 
            "SPECTATORS");
        char count_buf[32];
        snprintf(count_buf, sizeof(count_buf), "%d", (int)spectators_.size());
        draw_list->AddText(ImVec2(pos.x + width - 40, pos.y + 5), IM_COL32(0, 0, 0, 255), count_buf);
        
        // Rows
        for (size_t i = 0; i < spectators_.size(); ++i) {
            const auto& spec = spectators_[i];
            ImVec2 row_pos(pos.x + 5, pos.y + header_height + 5 + i * row_height);
            ImU32 row_color = GetRowColor(spec);
            
            // Background
            draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + width - 10, row_pos.y + row_height),
                (i % 2 == 0) ? IM_COL32(25, 25, 35, 200) : IM_COL32(35, 35, 45, 200), 3.0f);
            
            // Name
            std::string name = spec.name;
            if (spec.is_streamer) name = "★ " + name;
            if (spec.is_admin) name = "◆ " + name;
            draw_list->AddText(ImVec2(row_pos.x + 5, row_pos.y + 3), row_color, name.c_str());
            
            // Target
            if (config_.display.show_target && spec.target_id != 0) {
                char target_buf[64];
                snprintf(target_buf, sizeof(target_buf), "→ %s", spec.target_name.c_str());
                draw_list->AddText(ImVec2(row_pos.x + 150, row_pos.y + 3), 
                    IM_COL32(200, 200, 200, 200), target_buf);
            }
            
            // Duration
            if (config_.display.show_duration) {
                auto now = std::chrono::steady_clock::now();
                float duration = std::chrono::duration<float>(now - spec.first_seen).count();
                char dur_buf[16];
                if (duration < 60) snprintf(dur_buf, sizeof(dur_buf), "%.0fs", duration);
                else if (duration < 3600) snprintf(dur_buf, sizeof(dur_buf), "%.0fm", duration / 60);
                else snprintf(dur_buf, sizeof(dur_buf), "%.0fh", duration / 3600);
                draw_list->AddText(ImVec2(row_pos.x + width - 70, row_pos.y + 3),
                    IM_COL32(180, 180, 180, 200), dur_buf);
            }
        }
    }

    void SpectatorManager::DrawMenu() {
        if (!ImGui::Begin("Spectator List", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::End();
            return;
        }
        
        ImGui::Text("Spectators: %d", (int)spectators_.size());
        ImGui::Separator();
        
        if (ImGui::BeginTable("##spectators", 4, 
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthFixed, 150);
            ImGui::TableSetupColumn("Mode", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableHeadersRow();
            
            for (const auto& spec : spectators_) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                
                std::string name = spec.name;
                if (spec.is_streamer) name = "★ " + name;
                if (spec.is_admin) name = "◆ " + name;
                ImGui::Text("%s", name.c_str());
                
                ImGui::TableSetColumnIndex(1);
                if (spec.target_id != 0) {
                    ImGui::Text("→ %s", spec.target_name.c_str());
                } else {
                    ImGui::TextDisabled("None");
                }
                
                ImGui::TableSetColumnIndex(2);
                const char* modes[] = {"Death", "Free", "Player", "Fixed", "Follow"};
                if (spec.mode >= 0 && spec.mode < 5) {
                    ImGui::Text("%s", modes[spec.mode]);
                }
                
                ImGui::TableSetColumnIndex(3);
                auto now = std::chrono::steady_clock::now();
                float duration = std::chrono::duration<float>(now - spec.first_seen).count();
                if (duration < 60) ImGui::Text("%.0fs", duration);
                else if (duration < 3600) ImGui::Text("%.0fm", duration / 60);
                else ImGui::Text("%.0fh", duration / 3600);
            }
            ImGui::EndTable();
        }
        
        ImGui::Separator();
        if (ImGui::Button("Clear All")) ClearSpectators();
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            // Would trigger game refresh
        }
        ImGui::End();
    }

    void SpectatorManager::DrawAlerts(ImDrawList* draw_list) {
        // Would draw alert notifications
    }

    void SpectatorManager::CheckAlerts() {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - last_alert_check_).count() < 1.0f) return;
        last_alert_check_ = now;
        
        CheckNewSpectators();
    }

    void SpectatorManager::ClearAlerts() {
        alert_ids_.clear();
    }

    void SpectatorManager::CheckNewSpectators() {
        // Would compare with previous frame
    }

    void SpectatorManager::CleanupOldSpectators() {
        auto now = std::chrono::steady_clock::now();
        float max_age = 30.0f; // 30 seconds
        
        spectators_.erase(
            std::remove_if(spectators_.begin(), spectators_.end(),
                [&](const SpectatorInfo& s) {
                    float age = std::chrono::duration<float>(now - s.last_update).count();
                    return age > max_age;
                }),
            spectators_.end());
    }

    void SpectatorManager::TriggerAlert(const SpectatorInfo& info, const std::string& reason) {
        if (std::find(alert_ids_.begin(), alert_ids_.end(), info.id) != alert_ids_.end()) return;
        
        alert_ids_.push_back(info.id);
        // Would show notification
        // CyberWidgets::Notify(...);
    }

    ImU32 SpectatorManager::GetRowColor(const SpectatorInfo& info) const {
        if (info.is_admin) return config_.row_color_admin;
        if (info.is_streamer) return config_.row_color_streamer;
        if (info.is_friendly) return config_.row_color_friendly;
        if (info.target_id != 0) return config_.row_color_spectator;
        return config_.row_color_enemy;
    }

    void SpectatorManager::DrawSpectatorRow(ImDrawList* draw_list, const SpectatorInfo& info, const ImVec2& pos, float width) {
        // Implementation in DrawHUD
    }

    std::string SpectatorManager::SerializeConfig() const {
        std::ostringstream out;
        out << config_.enabled << '|'
            << config_.show_on_hud << '|'
            << config_.show_in_menu << '|'
            << config_.display.show_names << '|'
            << config_.display.show_target << '|'
            << config_.display.show_mode << '|'
            << config_.display.show_ping << '|'
            << config_.display.show_country << '|'
            << config_.display.show_platform << '|'
            << config_.display.show_duration << '|'
            << config_.display.max_distance << '|'
            << config_.display.only_when_spectating_me << '|'
            << config_.display.highlight_friends << '|'
            << config_.display.highlight_streamers << '|'
            << config_.display.highlight_admins << '|'
            << config_.alerts.notify_new_spectator << '|'
            << config_.alerts.notify_spectating_me << '|'
            << config_.alerts.notify_streamer << '|'
            << config_.alerts.notify_admin << '|'
            << config_.alerts.sound_alert << '|'
            << config_.alerts.alert_color << '|'
            << config_.alerts.alert_duration << '|'
            << config_.window_size.x << '|' << config_.window_size.y << '|'
            << config_.window_pos.x << '|' << config_.window_pos.y << '|'
            << config_.window_pinned << '|'
            << config_.background_alpha << '|'
            << config_.background_color << '|'
            << config_.header_color << '|'
            << config_.row_color_friendly << '|'
            << config_.row_color_enemy << '|'
            << config_.row_color_spectator << '|'
            << config_.row_color_streamer << '|'
            << config_.row_color_admin << '|'
            << config_.update_interval_ms;
        return out.str();
    }

    bool SpectatorManager::DeserializeConfig(const std::string& data) {
        std::istringstream in(data);
        std::string token;
        
        auto read_bool = [&](bool& b) {
            std::getline(in, token, '|');
            b = token == "1";
        };
        auto read_float = [&](float& f) {
            std::getline(in, token, '|');
            f = std::stof(token);
        };
        auto read_int = [&](int& i) {
            std::getline(in, token, '|');
            i = std::stoi(token);
        };
        auto read_uint = [&](ImU32& u) {
            std::getline(in, token, '|');
            u = std::stoul(token);
        };
        
        read_bool(config_.enabled);
        read_bool(config_.show_on_hud);
        read_bool(config_.show_in_menu);
        read_bool(config_.display.show_names);
        read_bool(config_.display.show_target);
        read_bool(config_.display.show_mode);
        read_bool(config_.display.show_ping);
        read_bool(config_.display.show_country);
        read_bool(config_.display.show_platform);
        read_bool(config_.display.show_duration);
        read_float(config_.display.max_distance);
        read_bool(config_.display.only_when_spectating_me);
        read_bool(config_.display.highlight_friends);
        read_bool(config_.display.highlight_streamers);
        read_bool(config_.display.highlight_admins);
        read_bool(config_.alerts.notify_new_spectator);
        read_bool(config_.alerts.notify_spectating_me);
        read_bool(config_.alerts.notify_streamer);
        read_bool(config_.alerts.notify_admin);
        read_bool(config_.alerts.sound_alert);
        read_uint(config_.alerts.alert_color);
        read_float(config_.alerts.alert_duration);
        read_float(config_.window_size.x);
        read_float(config_.window_size.y);
        read_float(config_.window_pos.x);
        read_float(config_.window_pos.y);
        read_bool(config_.window_pinned);
        read_float(config_.background_alpha);
        read_uint(config_.background_color);
        read_uint(config_.header_color);
        read_uint(config_.row_color_friendly);
        read_uint(config_.row_color_enemy);
        read_uint(config_.row_color_spectator);
        read_uint(config_.row_color_streamer);
        read_uint(config_.row_color_admin);
        read_int(config_.update_interval_ms);
        
        return true;
    }

    SpectatorConfig GetDefaultSpectatorConfig() {
        SpectatorConfig cfg;
        cfg.enabled = true;
        cfg.show_on_hud = true;
        cfg.show_in_menu = true;
        
        cfg.display.show_names = true;
        cfg.display.show_target = true;
        cfg.display.show_mode = true;
        cfg.display.show_ping = false;
        cfg.display.show_country = false;
        cfg.display.show_platform = true;
        cfg.display.show_duration = true;
        cfg.display.max_distance = 500.0f;
        cfg.display.only_when_spectating_me = false;
        cfg.display.highlight_friends = true;
        cfg.display.highlight_streamers = true;
        cfg.display.highlight_admins = true;
        
        cfg.alerts.notify_new_spectator = true;
        cfg.alerts.notify_spectating_me = true;
        cfg.alerts.notify_streamer = true;
        cfg.alerts.notify_admin = true;
        cfg.alerts.sound_alert = true;
        cfg.alerts.alert_color = IM_COL32(255, 100, 100, 255);
        cfg.alerts.alert_duration = 5.0f;
        
        cfg.window_size = ImVec2(350, 400);
        cfg.window_pos = ImVec2(50, 50);
        cfg.window_pinned = false;
        cfg.background_alpha = 0.9f;
        cfg.background_color = IM_COL32(15, 15, 20, 230);
        cfg.header_color = IM_COL32(212, 175, 55, 255);
        cfg.row_color_friendly = IM_COL32(80, 255, 100, 255);
        cfg.row_color_enemy = IM_COL32(255, 80, 80, 255);
        cfg.row_color_spectator = IM_COL32(255, 200, 80, 255);
        cfg.row_color_streamer = IM_COL32(255, 100, 255, 255);
        cfg.row_color_admin = IM_COL32(255, 255, 100, 255);
        
        cfg.update_interval_ms = 1000;
        
        return cfg;
    }

    SpectatorConfig GetCompetitiveSpectatorConfig() {
        SpectatorConfig cfg = GetDefaultSpectatorConfig();
        cfg.display.max_distance = 300.0f;
        cfg.alerts.sound_alert = false;
        cfg.window_size = ImVec2(300, 300);
        return cfg;
    }

    SpectatorConfig GetStreamerSpectatorConfig() {
        SpectatorConfig cfg = GetDefaultSpectatorConfig();
        cfg.display.highlight_streamers = true;
        cfg.display.highlight_admins = true;
        cfg.alerts.notify_streamer = true;
        cfg.alerts.notify_admin = true;
        cfg.show_on_hud = true;
        return cfg;
    }

    std::string SerializeSpectatorConfig(const SpectatorConfig& config) {
        return ""; // Handled by manager
    }

    bool DeserializeSpectatorConfig(const std::string& data, SpectatorConfig& config) {
        // Handled by manager
        return true;
    }

} // namespace Gameplay::SpectatorList