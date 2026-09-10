#include "sound_esp.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Gameplay::SoundESP {

    SoundESPManager& SoundESPManager::Instance() {
        static SoundESPManager instance;
        return instance;
    }

    void SoundESPManager::AddEvent(const SoundEvent& event) {
        if (events_.size() >= config_.max_events) {
            // Remove oldest
            events_.erase(events_.begin());
        }
        
        SoundEvent new_event = event;
        events_.push_back(new_event);
    }

    void SoundESPManager::RemoveEvent(uint64_t event_id) {
        events_.erase(
            std::remove_if(events_.begin(), events_.end(),
                [event_id](const SoundEvent& e) { return e.timestamp.time_since_epoch().count() == (long long)event_id; }),
            events_.end());
    }

    void SoundESPManager::ClearEvents() {
        events_.clear();
    }

    void SoundESPManager::Update(float dt) {
        (void)dt;
        CleanupOldEvents();
    }

    void SoundESPManager::CleanupOldEvents() {
        auto now = std::chrono::steady_clock::now();
        float max_age = 10.0f; // 10 seconds max
        
        events_.erase(
            std::remove_if(events_.begin(), events_.end(),
                [&](const SoundEvent& e) {
                    float age = std::chrono::duration<float>(now - e.timestamp).count();
                    return age > max_age;
                }),
            events_.end());
    }

    void SoundESPManager::DrawEvents(ImDrawList* draw_list, const Vec3& camera_pos, const Matrix& view_proj) {
        if (!config_.enabled || !draw_list) return;
        
        for (const auto& event : events_) {
            // Check filters
            if (event.distance > config_.max_distance) continue;
            if (event.volume < config_.min_volume) continue;
            if (std::find(config_.ignored_types.begin(), config_.ignored_types.end(), event.type) != config_.ignored_types.end()) continue;
            
            // World to screen
            ImVec2 screen_pos;
            if (!WorldToScreen(event.position, camera_pos, view_proj, screen_pos)) continue;
            
            DrawEventIcon(draw_list, event, screen_pos);
        }
    }

    void SoundESPManager::DrawDirectionalIndicators(ImDrawList* draw_list, const Vec3& camera_pos, const Matrix& view_proj) {
        if (!config_.directionals.enabled) return;
        
        ImVec2 screen_center = ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
        
        for (const auto& event : events_) {
            if (event.distance > config_.directionals.fade_distance) continue;
            if (event.distance < 5.0f) continue; // Too close
            
            // Calculate direction on screen
            ImVec2 screen_pos;
            if (WorldToScreen(event.position, camera_pos, view_proj, screen_pos)) {
                // On screen - draw at position
                DrawDirectionalArrow(draw_list, event, screen_pos);
            } else if (config_.directionals.show_on_screen) {
                // Off screen - draw at edge
                Vec3 to_event = event.position - camera_pos;
                float angle = atan2f(to_event.x, to_event.z);
                
                float margin = config_.directionals.screen_edge_margin;
                ImVec2 edge_pos(
                    screen_center.x + sinf(angle) * (ImGui::GetIO().DisplaySize.x * 0.5f - margin),
                    screen_center.y - cosf(angle) * (ImGui::GetIO().DisplaySize.y * 0.5f - margin)
                );
                
                DrawDirectionalArrow(draw_list, event, edge_pos);
            }
        }
    }

    void SoundESPManager::DrawEventIcon(ImDrawList* draw_list, const SoundEvent& event, const ImVec2& screen_pos) {
        if (!config_.visual.type_icons[static_cast<int>(event.type)].enabled) return;
        
        auto& icon_cfg = config_.visual.type_icons[static_cast<int>(event.type)];
        
        // Calculate alpha based on age
        auto now = std::chrono::steady_clock::now();
        float age = std::chrono::duration<float>(now - event.timestamp).count();
        float alpha = 1.0f;
        if (age > icon_cfg.fade_time) {
            alpha = 1.0f - (age - icon_cfg.fade_time) / icon_cfg.fade_duration;
            alpha = std::max(0.0f, alpha);
        }
        
        if (alpha <= 0) return;
        
        // Pulse animation
        float pulse = 1.0f;
        if (icon_cfg.pulse) {
            pulse = 0.8f + 0.2f * sinf(age * icon_cfg.pulse_speed * 6.28f);
        }
        
        ImU32 color = icon_cfg.color;
        color = (color & 0xFFFFFF) | (int)(alpha * 255 * pulse) << 24;
        
        ImU32 outline = icon_cfg.outline_color;
        outline = (outline & 0xFFFFFF) | (int)(alpha * 255) << 24;
        
        // Draw icon based on type
        float size = icon_cfg.size.x * pulse;
        
        switch (event.type) {
            case SoundType::Footstep:
                draw_list->AddCircle(screen_pos, size, color, 12, 1.5f);
                draw_list->AddCircle(screen_pos, size + icon_cfg.outline_thickness, outline, 12, icon_cfg.outline_thickness);
                break;
            case SoundType::Gunshot:
                draw_list->AddTriangleFilled(
                    ImVec2(screen_pos.x, screen_pos.y - size),
                    ImVec2(screen_pos.x - size * 0.866f, screen_pos.y + size * 0.5f),
                    ImVec2(screen_pos.x + size * 0.866f, screen_pos.y + size * 0.5f),
                    color);
                if (icon_cfg.outline_thickness > 0) {
                    draw_list->AddTriangle(
                        ImVec2(screen_pos.x, screen_pos.y - size),
                        ImVec2(screen_pos.x - size * 0.866f, screen_pos.y + size * 0.5f),
                        ImVec2(screen_pos.x + size * 0.866f, screen_pos.y + size * 0.5f),
                        outline, icon_cfg.outline_thickness);
                }
                break;
            case SoundType::Explosion:
                draw_list->AddCircleFilled(screen_pos, size * 1.2f, color, 16);
                draw_list->AddCircle(screen_pos, size * 1.2f, outline, 16, icon_cfg.outline_thickness);
                break;
            case SoundType::Reload:
                draw_list->AddRectFilled(
                    ImVec2(screen_pos.x - size * 0.7f, screen_pos.y - size * 0.7f),
                    ImVec2(screen_pos.x + size * 0.7f, screen_pos.y + size * 0.7f),
                    color, 3.0f);
                break;
            default:
                draw_list->AddCircleFilled(screen_pos, size, color, 8);
                break;
        }
        
        // Distance text
        if (config_.directionals.show_distance) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0fm", event.distance);
            draw_list->AddText(ImVec2(screen_pos.x + size + 4, screen_pos.y - 8), 
                IM_COL32(255, 255, 255, (int)(alpha * 200)), buf);
        }
    }

    void SoundESPManager::DrawDirectionalArrow(ImDrawList* draw_list, const SoundEvent& event, const ImVec2& pos) {
        if (!config_.directionals.enabled) return;
        
        auto now = std::chrono::steady_clock::now();
        float age = std::chrono::duration<float>(now - event.timestamp).count();
        float alpha = std::max(0.0f, 1.0f - age / config_.directionals.fade_distance * 50.0f);
        
        ImU32 color = event.is_friendly ? config_.directionals.friendly_color : config_.directionals.color;
        color = (color & 0xFFFFFF) | (int)(alpha * 255) << 24;
        
        // Calculate arrow direction
        float size = config_.directionals.indicator_size;
        // Arrow points toward sound
        ImVec2 tip(pos.x, pos.y - size);
        ImVec2 left(pos.x - size * 0.5f, pos.y + size * 0.3f);
        ImVec2 right(pos.x + size * 0.5f, pos.y + size * 0.3f);
        
        draw_list->AddTriangleFilled(tip, left, right, color);
        
        // Distance
        if (config_.directionals.show_distance) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0fm", event.distance);
            draw_list->AddText(ImVec2(pos.x + size + 4, pos.y - 8), color, buf);
        }
    }

    void SoundESPManager::DrawAudioVisualizer(ImDrawList* draw_list, const ImVec2& pos, const ImVec2& size) {
        if (!config_.audio_visual.enabled) return;
        
        // Draw waveform background
        draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), 
            IM_COL32(20, 20, 30, 200), 4.0f);
        
        // Would draw actual audio data here
        // Placeholder: draw center line
        draw_list->AddLine(
            ImVec2(pos.x, pos.y + size.y * 0.5f),
            ImVec2(pos.x + size.x, pos.y + size.y * 0.5f),
            IM_COL32(60, 60, 80, 200), 1.0f);
    }

    void SoundESPManager::OnGameSoundEvent(const SoundEvent& event) {
        AddEvent(event);
    }

    std::vector<SoundEvent> SoundESPManager::GetEventsByType(SoundType type) const {
        std::vector<SoundEvent> result;
        for (const auto& event : events_) {
            if (event.type == type) result.push_back(event);
        }
        return result;
    }

    bool SoundESPManager::WorldToScreen(const Vec3& world, const Vec3& camera_pos, const Matrix& view_proj, ImVec2& out) {
        // Simplified world to screen
        Vec3 rel = world - camera_pos;
        float w = rel.x * view_proj.m[3] + rel.y * view_proj.m[7] + rel.z * view_proj.m[11] + view_proj.m[15];
        if (w < 0.1f) return false;
        
        float x = rel.x * view_proj.m[0] + rel.y * view_proj.m[4] + rel.z * view_proj.m[8] + view_proj.m[12];
        float y = rel.x * view_proj.m[1] + rel.y * view_proj.m[5] + rel.z * view_proj.m[9] + view_proj.m[13];
        
        out.x = (x / w + 1.0f) * 0.5f * ImGui::GetIO().DisplaySize.x;
        out.y = (1.0f - y / w) * 0.5f * ImGui::GetIO().DisplaySize.y;
        return true;
    }

    std::string SoundESPManager::SerializeConfig() const {
        std::ostringstream out;
        out << config_.enabled << '|'
            << config_.only_when_aiming << '|'
            << config_.visual.show_footsteps << '|'
            << config_.visual.show_gunshots << '|'
            << config_.visual.show_explosions << '|'
            << config_.visual.show_reloads << '|'
            << config_.visual.show_voice << '|'
            << config_.directionals.enabled << '|'
            << config_.directionals.show_on_radar << '|'
            << config_.directionals.show_on_screen << '|'
            << config_.directionals.show_3d << '|'
            << config_.audio_visual.enabled << '|'
            << config_.max_distance << '|'
            << config_.min_volume << '|'
            << config_.max_events << '|'
            << config_.cleanup_interval;
        return out.str();
    }

    bool SoundESPManager::DeserializeConfig(const std::string& data) {
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
        
        read_bool(config_.enabled);
        read_bool(config_.only_when_aiming);
        read_bool(config_.visual.show_footsteps);
        read_bool(config_.visual.show_gunshots);
        read_bool(config_.visual.show_explosions);
        read_bool(config_.visual.show_reloads);
        read_bool(config_.visual.show_voice);
        read_bool(config_.directionals.enabled);
        read_bool(config_.directionals.show_on_radar);
        read_bool(config_.directionals.show_on_screen);
        read_bool(config_.directionals.show_3d);
        read_bool(config_.audio_visual.enabled);
        read_float(config_.max_distance);
        read_float(config_.min_volume);
        read_int(config_.max_events);
        read_float(config_.cleanup_interval);
        
        return true;
    }

    void InitializeDefaultSoundIcons(SoundESPConfig::VisualConfig& visual) {
        // Footsteps
        visual.type_icons[static_cast<int>(SoundType::Footstep)] = SoundESPConfig::VisualConfig::IconConfig{};
        visual.type_icons[static_cast<int>(SoundType::Footstep)].enabled = true;
        visual.type_icons[static_cast<int>(SoundType::Footstep)].color = IM_COL32(255, 255, 255, 255);
        
        // Gunshot
        visual.type_icons[static_cast<int>(SoundType::Gunshot)] = SoundESPConfig::VisualConfig::IconConfig{};
        visual.type_icons[static_cast<int>(SoundType::Gunshot)].enabled = true;
        visual.type_icons[static_cast<int>(SoundType::Gunshot)].color = IM_COL32(255, 100, 100, 255);
        visual.type_icons[static_cast<int>(SoundType::Gunshot)].pulse = true;
        visual.type_icons[static_cast<int>(SoundType::Gunshot)].pulse_speed = 4.0f;
        
        // Explosion
        visual.type_icons[static_cast<int>(SoundType::Explosion)] = SoundESPConfig::VisualConfig::IconConfig{};
        visual.type_icons[static_cast<int>(SoundType::Explosion)].enabled = true;
        visual.type_icons[static_cast<int>(SoundType::Explosion)].color = IM_COL32(255, 150, 0, 255);
        visual.type_icons[static_cast<int>(SoundType::Explosion)].size = ImVec2(28, 28);
        
        // Reload
        visual.type_icons[static_cast<int>(SoundType::Reload)] = SoundESPConfig::VisualConfig::IconConfig{};
        visual.type_icons[static_cast<int>(SoundType::Reload)].enabled = true;
        visual.type_icons[static_cast<int>(SoundType::Reload)].color = IM_COL32(100, 200, 255, 255);
        
        // Voice
        visual.type_icons[static_cast<int>(SoundType::Voice)] = SoundESPConfig::VisualConfig::IconConfig{};
        visual.type_icons[static_cast<int>(SoundType::Voice)].enabled = false;
        visual.type_icons[static_cast<int>(SoundType::Voice)].color = IM_COL32(255, 100, 255, 255);
        
        // Others
        for (int i = 0; i < 11; ++i) {
            if (!visual.type_icons[i].enabled) {
                visual.type_icons[i] = SoundESPConfig::VisualConfig::IconConfig{};
                visual.type_icons[i].enabled = true;
            }
        }
    }

    SoundESPConfig GetLegitSoundESPConfig() {
        SoundESPConfig cfg;
        cfg.enabled = true;
        cfg.max_distance = 150.0f;
        cfg.min_volume = 0.15f;
        cfg.max_events = 32;
        
        cfg.visual.show_footsteps = true;
        cfg.visual.show_gunshots = true;
        cfg.visual.show_explosions = true;
        cfg.visual.show_reloads = true;
        cfg.visual.show_voice = false;
        
        cfg.directionals.enabled = true;
        cfg.directionals.show_on_screen = true;
        cfg.directionals.show_on_radar = true;
        cfg.directionals.fade_distance = 80.0f;
        cfg.directionals.color = IM_COL32(255, 200, 0, 220);
        
        InitializeDefaultSoundIcons(cfg.visual);
        return cfg;
    }

    SoundESPConfig GetCompetitiveSoundESPConfig() {
        SoundESPConfig cfg = GetLegitSoundESPConfig();
        cfg.max_distance = 250.0f;
        cfg.max_events = 64;
        cfg.visual.show_voice = true;
        cfg.directionals.fade_distance = 150.0f;
        cfg.directionals.show_3d = true;
        return cfg;
    }

    SoundESPConfig GetSniperSoundESPConfig() {
        SoundESPConfig cfg = GetCompetitiveSoundESPConfig();
        cfg.max_distance = 500.0f;
        cfg.visual.show_footsteps = false; // Snipers don't need footsteps as much
        cfg.visual.show_gunshots = true;
        cfg.visual.show_explosions = true;
        cfg.directionals.fade_distance = 300.0f;
        return cfg;
    }

    SoundESPConfig GetMinimalSoundESPConfig() {
        SoundESPConfig cfg;
        cfg.enabled = true;
        cfg.max_distance = 100.0f;
        cfg.max_events = 16;
        cfg.visual.show_footsteps = false;
        cfg.visual.show_gunshots = true;
        cfg.visual.show_explosions = true;
        cfg.visual.show_reloads = false;
        cfg.visual.show_voice = false;
        cfg.directionals.enabled = true;
        cfg.directionals.show_on_screen = true;
        cfg.directionals.show_on_radar = false;
        return cfg;
    }

    std::string SerializeSoundESPConfig(const SoundESPConfig& config) {
        std::ostringstream out;
        out << config.enabled << '|'
            << config.only_when_aiming << '|'
            << config.visual.show_footsteps << '|'
            << config.visual.show_gunshots << '|'
            << config.visual.show_explosions << '|'
            << config.visual.show_reloads << '|'
            << config.visual.show_voice << '|'
            << config.directionals.enabled << '|'
            << config.directionals.show_on_radar << '|'
            << config.directionals.show_on_screen << '|'
            << config.directionals.show_3d << '|'
            << config.audio_visual.enabled << '|'
            << config.max_distance << '|'
            << config.min_volume << '|'
            << config.max_events << '|'
            << config.cleanup_interval;
        return out.str();
    }

    bool DeserializeSoundESPConfig(const std::string& data, SoundESPConfig& config) {
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
        
        read_bool(config.enabled);
        read_bool(config.only_when_aiming);
        read_bool(config.visual.show_footsteps);
        read_bool(config.visual.show_gunshots);
        read_bool(config.visual.show_explosions);
        read_bool(config.visual.show_reloads);
        read_bool(config.visual.show_voice);
        read_bool(config.directionals.enabled);
        read_bool(config.directionals.show_on_radar);
        read_bool(config.directionals.show_on_screen);
        read_bool(config.directionals.show_3d);
        read_bool(config.audio_visual.enabled);
        read_float(config.max_distance);
        read_float(config.min_volume);
        read_int(config.max_events);
        read_float(config.cleanup_interval);
        
        InitializeDefaultSoundIcons(config.visual);
        return true;
    }

} // namespace Gameplay::SoundESP
