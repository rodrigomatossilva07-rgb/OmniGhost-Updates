#include "web_radar.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Gameplay::WebRadar {

    // WebRadar implementation
    WebRadar::WebRadar() {
        config_ = GetDefaultRadarConfig();
        state_.current_range = config_.default_range;
        state_.target_range = config_.default_range;
        state_.screen_radius = config_.radar_size * 0.5f;
    }

    WebRadar::~WebRadar() = default;

    void WebRadar::Update(float dt) {
        if (!config_.enabled) return;
        
        state_.last_update += dt;
        if (state_.last_update < 1.0f / config_.updates_per_second) return;
        state_.last_update = 0.0f;
        
        UpdateEntities();
        UpdateRange(dt);
        UpdateRotation(dt);
    }

    void WebRadar::UpdateEntities() {
        if (!adapter_) return;
        
        std::vector<RadarEntity> new_entities = adapter_->GetEntities();
        state_.entities.clear();
        state_.entities.reserve(new_entities.size());
        
        Vec3 local_pos = adapter_->GetLocalPlayerPos();
        float local_yaw = adapter_->GetLocalPlayerYaw();
        
        // Update radar center and rotation
        if (config_.follow_player) {
            state_.center_pos = local_pos;
        }
        if (config_.rotate_with_player) {
            state_.target_rotation = -local_yaw; // Negative for radar convention
        }
        
        for (auto& entity : new_entities) {
            if (!entity.active) continue;
            if (entity_filter_ && !entity_filter_(entity)) continue;
            
            // Calculate distance
            entity.distance = local_pos.DistTo(entity.world_pos);
            
            // Filter by distance
            auto& display = config_.entity_displays[static_cast<int>(entity.type)];
            if (entity.distance > display.max_distance) continue;
            if (entity.distance < config_.min_entity_distance) continue;
            
            // Filter teams
            if (std::find(config_.ignored_teams.begin(), config_.ignored_teams.end(), entity.team) != config_.ignored_teams.end()) continue;
            if (std::find(config_.ignored_entities.begin(), config_.ignored_entities.end(), entity.id) != config_.ignored_entities.end()) continue;
            
            // Check visibility if required
            if (display.only_visible && adapter_) {
                if (!adapter_->IsEntityVisible(entity.id)) continue;
            }
            
            // Update icon color based on team
            if (display.show_team_color && !entity.is_friendly && !entity.is_local) {
                entity.icon.color = IM_COL32(255, 80, 80, 255); // Enemy
            } else if (entity.is_friendly) {
                entity.icon.color = config_.friend_color;
            } else if (entity.is_local) {
                entity.icon.color = config_.local_player_color;
            }
            
            state_.entities.push_back(entity);
        }
        
        // Sort by distance (closest first for drawing order)
        std::sort(state_.entities.begin(), state_.entities.end(),
            [](const RadarEntity& a, const RadarEntity& b) {
                return a.distance < b.distance;
            });
        
        // Limit entities
        if (state_.entities.size() > config_.max_entities) {
            state_.entities.resize(config_.max_entities);
        }
    }

    void WebRadar::UpdateRange(float dt) {
        if (state_.current_range != state_.target_range) {
            float diff = state_.target_range - state_.current_range;
            state_.current_range += diff * std::min(1.0f, dt * 8.0f);
            if (fabsf(diff) < 0.5f) {
                state_.current_range = state_.target_range;
            }
        }
    }

    void WebRadar::UpdateRotation(float dt) {
        if (config_.smooth_rotation && state_.rotation != state_.target_rotation) {
            float diff = state_.target_rotation - state_.rotation;
            // Normalize angle difference
            while (diff > 3.14159f) diff -= 6.28318f;
            while (diff < -3.14159f) diff += 6.28318f;
            
            state_.rotation += diff * std::min(1.0f, dt * config_.rotation_speed);
        } else {
            state_.rotation = state_.target_rotation;
        }
    }

    void WebRadar::Draw(ImDrawList* draw_list, const ImVec2& position) {
        DrawAtPosition(draw_list, position, ImVec2(config_.radar_size, config_.radar_size));
    }

    void WebRadar::DrawAtPosition(ImDrawList* draw_list, const ImVec2& position, const ImVec2& size) {
        if (!draw_list || !config_.enabled) return;
        
        state_.screen_center = ImVec2(position.x + size.x * 0.5f, position.y + size.y * 0.5f);
        state_.screen_radius = std::min(size.x, size.y) * 0.5f * 0.95f;
        
        DrawBackground(draw_list);
        DrawGrid(draw_list);
        DrawCompass(draw_list);
        DrawEntities(draw_list);
        DrawLocalPlayer(draw_list);
        DrawPings(draw_list);
    }

    void WebRadar::DrawBackground(ImDrawList* draw_list) {
        // Background circle
        draw_list->AddCircleFilled(state_.screen_center, state_.screen_radius, 
            config_.background_color, 64);
        draw_list->AddCircle(state_.screen_center, state_.screen_radius, 
            config_.border_color, 64, config_.border_color & 0xFF);
        
        // Range rings
        for (int i = 1; i <= 3; ++i) {
            float ring_radius = state_.screen_radius * i / 4.0f;
            draw_list->AddCircle(state_.screen_center, ring_radius, 
                IM_COL32(60, 70, 90, 100), 32, 0.5f);
        }
    }

    void WebRadar::DrawGrid(ImDrawList* draw_list) {
        if (!config_.show_grid) return;
        
        int divs = config_.grid_divisions;
        float step = state_.screen_radius / divs;
        
        // Concentric circles
        for (int i = 1; i <= divs; ++i) {
            float r = step * i;
            draw_list->AddCircle(state_.screen_center, r, config_.grid_color, 32, config_.grid_thickness);
        }
        
        // Cross lines
        draw_list->AddLine(
            ImVec2(state_.screen_center.x - state_.screen_radius, state_.screen_center.y),
            ImVec2(state_.screen_center.x + state_.screen_radius, state_.screen_center.y),
            config_.grid_color, config_.grid_thickness);
        draw_list->AddLine(
            ImVec2(state_.screen_center.x, state_.screen_center.y - state_.screen_radius),
            ImVec2(state_.screen_center.x, state_.screen_center.y + state_.screen_radius),
            config_.grid_color, config_.grid_thickness);
        
        // Diagonal lines
        float diag = state_.screen_radius * 0.7071f;
        draw_list->AddLine(
            ImVec2(state_.screen_center.x - diag, state_.screen_center.y - diag),
            ImVec2(state_.screen_center.x + diag, state_.screen_center.y + diag),
            config_.grid_color, config_.grid_thickness * 0.5f);
        draw_list->AddLine(
            ImVec2(state_.screen_center.x - diag, state_.screen_center.y + diag),
            ImVec2(state_.screen_center.x + diag, state_.screen_center.y - diag),
            config_.grid_color, config_.grid_thickness * 0.5f);
    }

    void WebRadar::DrawCompass(ImDrawList* draw_list) {
        if (!config_.show_compass) return;
        
        float radius = config_.compass_radius;
        ImVec2 center = state_.screen_center;
        
        // Compass ring
        draw_list->AddCircle(center, radius, config_.compass_color, 64, 1.0f);
        
        // Cardinal directions
        if (config_.show_cardinal) {
            const char* dirs[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
            for (int i = 0; i < 8; ++i) {
                float angle = i * 3.14159f / 4.0f;
                ImVec2 pos(center.x + sinf(angle) * (radius + 10), 
                          center.y - cosf(angle) * (radius + 10));
                draw_list->AddText(pos, config_.cardinal_color, dirs[i]);
            }
            
            if (config_.show_degrees) {
                for (int i = 0; i < 360; i += 15) {
                    float angle = i * 3.14159f / 180.0f;
                    float r1 = radius;
                    float r2 = (i % 45 == 0) ? radius - 8 : radius - 4;
                    ImVec2 p1(center.x + sinf(angle) * r1, center.y - cosf(angle) * r1);
                    ImVec2 p2(center.x + sinf(angle) * r2, center.y - cosf(angle) * r2);
                    draw_list->AddLine(p1, p2, config_.compass_color, 1.0f);
                }
            }
        }
        
        // Current heading marker
        float heading_angle = state_.rotation;
        ImVec2 heading_pos(center.x + sinf(heading_angle) * (radius + 15),
                          center.y - cosf(heading_angle) * (radius + 15));
        draw_list->AddTriangleFilled(
            ImVec2(heading_pos.x, heading_pos.y - 8),
            ImVec2(heading_pos.x - 6, heading_pos.y + 4),
            ImVec2(heading_pos.x + 6, heading_pos.y + 4),
            IM_COL32(255, 255, 0, 255));
    }

    void WebRadar::DrawEntities(ImDrawList* draw_list) {
        for (const auto& entity : state_.entities) {
            if (!ShouldDrawEntity(entity)) continue;
            
            ImVec2 radar_pos = WorldToRadarInternal(entity.world_pos);
            if (radar_pos.x == 0 && radar_pos.y == 0) continue;
            
            // Check if on screen
            float dist_from_center = sqrtf(
                (radar_pos.x - state_.screen_center.x) * (radar_pos.x - state_.screen_center.x) +
                (radar_pos.y - state_.screen_center.y) * (radar_pos.y - state_.screen_center.y));
            
            if (dist_from_center > state_.screen_radius + entity.icon.size) continue;
            
            DrawEntityIcon(draw_list, entity, radar_pos);
            
            // Draw label
            auto& display = config_.entity_displays[static_cast<int>(entity.type)];
            if (display.show_names || display.show_distance || display.show_health) {
                DrawEntityLabel(draw_list, entity, radar_pos);
            }
        }
    }

    void WebRadar::DrawLocalPlayer(ImDrawList* draw_list) {
        if (!config_.follow_player) return;
        
        ImVec2 center = state_.screen_center;
        const IconStyle& icon = config_.local_player_icon;
        
        // Draw local player at center
        DrawIcon(draw_list, center, icon);
        
        // Direction indicator
        if (config_.show_local_direction) {
            float angle = state_.rotation;
            float len = config_.local_player_icon.size * 1.5f;
            ImVec2 end(center.x + sinf(angle) * len, center.y - cosf(angle) * len);
            draw_list->AddLine(center, end, config_.local_player_color, 2.0f);
            
            // Arrow head
            ImVec2 arrow1(end.x - sinf(angle - 0.5f) * 6, end.y + cosf(angle - 0.5f) * 6);
            ImVec2 arrow2(end.x - sinf(angle + 0.5f) * 6, end.y + cosf(angle + 0.5f) * 6);
            draw_list->AddTriangleFilled(end, arrow1, arrow2, config_.local_player_color);
        }
    }

    void WebRadar::DrawPings(ImDrawList* draw_list) {
        // Would draw ping markers
    }

    ImVec2 WebRadar::WorldToRadarInternal(const Vec3& world_pos) const {
        Vec3 relative = world_pos - state_.center_pos;
        float dist = relative.Length2D();
        
        if (dist == 0) return state_.screen_center;
        
        // Calculate angle
        float angle = atan2f(relative.x, relative.z) - state_.rotation;
        
        // Scale to radar
        float scale = state_.screen_radius / state_.current_range;
        float radar_dist = std::min(dist * scale, state_.screen_radius * 0.95f);
        
        return ImVec2(
            state_.screen_center.x + sinf(angle) * radar_dist,
            state_.screen_center.y - cosf(angle) * radar_dist
        );
    }

    void WebRadar::DrawEntityIcon(ImDrawList* draw_list, const RadarEntity& entity, const ImVec2& pos) {
        DrawIcon(draw_list, pos, entity.icon);
        
        // Distance indicator for far entities
        auto& display = config_.entity_displays[static_cast<int>(entity.type)];
        if (display.show_distance && entity.distance > 50.0f) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%.0fm", entity.distance);
            ImVec2 text_pos(pos.x + entity.icon.size + 4, pos.y - 8);
            draw_list->AddText(text_pos, IM_COL32(200, 200, 200, 200), buf);
        }
    }

    void WebRadar::DrawEntityLabel(ImDrawList* draw_list, const RadarEntity& entity, const ImVec2& pos) {
        auto& display = config_.entity_displays[static_cast<int>(entity.type)];
        if (!display.show_names && !display.show_distance && !display.show_health) return;
        
        std::string label;
        if (display.show_names && !entity.name.empty()) {
            label = entity.name;
        }
        if (display.show_distance) {
            if (!label.empty()) label += " ";
            char buf[16];
            snprintf(buf, sizeof(buf), "[%.0fm]", entity.distance);
            label += buf;
        }
        if (display.show_health && entity.max_health > 0) {
            if (!label.empty()) label += " ";
            char buf[16];
            snprintf(buf, sizeof(buf), "%d/%d", entity.health, entity.max_health);
            label += buf;
        }
        
        if (label.empty()) return;
        
        ImVec2 text_pos(pos.x + entity.icon.size + 6, pos.y - 10);
        ImU32 text_color = entity.is_local ? config_.local_player_color : 
                          entity.is_friendly ? config_.friend_color : IM_COL32(255, 255, 255, 255);
        
        // Fade by distance
        auto& display_cfg = config_.entity_displays[static_cast<int>(entity.type)];
        float alpha = 1.0f;
        if (entity.distance > display_cfg.fade_distance) {
            alpha = 1.0f - (entity.distance - display_cfg.fade_distance) / 200.0f;
            alpha = std::max(0.2f, alpha);
        }
        text_color = (text_color & 0xFFFFFF) | (int)(alpha * 255) << 24;
        
        draw_list->AddText(text_pos, text_color, label.c_str());
    }

    bool WebRadar::ShouldDrawEntity(const RadarEntity& entity) const {
        auto& display = config_.entity_displays[static_cast<int>(entity.type)];
        return display.enabled && entity.active && entity.distance <= display.max_distance;
    }

    void WebRadar::DrawIcon(ImDrawList* draw_list, const ImVec2& center, const IconStyle& style) {
        float size = style.size;
        ImU32 color = style.color;
        ImU32 outline = style.outline_color;
        
        switch (style.type) {
            case IconStyle::Type::Circle:
                if (style.outline_thickness > 0) {
                    draw_list->AddCircle(center, size + style.outline_thickness, outline, 16, style.outline_thickness);
                }
                draw_list->AddCircleFilled(center, size, color, 16);
                break;
                
            case IconStyle::Type::Square:
                if (style.outline_thickness > 0) {
                    draw_list->AddRect(ImVec2(center.x - size - style.outline_thickness, center.y - size - style.outline_thickness),
                                     ImVec2(center.x + size + style.outline_thickness, center.y + size + style.outline_thickness),
                                     outline, 2.0f, 0, style.outline_thickness);
                }
                draw_list->AddRectFilled(ImVec2(center.x - size, center.y - size),
                                       ImVec2(center.x + size, center.y + size), color, 2.0f);
                break;
                
            case IconStyle::Type::Triangle: {
                ImVec2 p1(center.x, center.y - size);
                ImVec2 p2(center.x - size * 0.866f, center.y + size * 0.5f);
                ImVec2 p3(center.x + size * 0.866f, center.y + size * 0.5f);
                if (style.outline_thickness > 0) {
                    draw_list->AddTriangle(p1, p2, p3, outline, style.outline_thickness);
                }
                draw_list->AddTriangleFilled(p1, p2, p3, color);
                break;
            }
                
            case IconStyle::Type::Arrow: {
                float angle = style.rotation;
                float len = size * 1.5f;
                ImVec2 tip(center.x + sinf(angle) * len, center.y - cosf(angle) * len);
                ImVec2 base1(center.x + sinf(angle - 0.7f) * size, center.y - cosf(angle - 0.7f) * size);
                ImVec2 base2(center.x + sinf(angle + 0.7f) * size, center.y - cosf(angle + 0.7f) * size);
                draw_list->AddTriangleFilled(tip, base1, base2, color);
                if (style.outline_thickness > 0) {
                    draw_list->AddTriangle(tip, base1, base2, outline, style.outline_thickness);
                }
                break;
            }
                
            default:
                draw_list->AddCircleFilled(center, size, color, 12);
                break;
        }
    }

    void WebRadar::AddEntity(const RadarEntity& entity) {
        state_.entities.push_back(entity);
    }

    void WebRadar::RemoveEntity(uint64_t id) {
        state_.entities.erase(
            std::remove_if(state_.entities.begin(), state_.entities.end(),
                [id](const RadarEntity& e) { return e.id == id; }),
            state_.entities.end());
    }

    void WebRadar::UpdateEntity(const RadarEntity& entity) {
        for (auto& e : state_.entities) {
            if (e.id == entity.id) {
                e = entity;
                return;
            }
        }
        AddEntity(entity);
    }

    void WebRadar::ClearEntities() {
        state_.entities.clear();
    }

    void WebRadar::SetRange(float range) {
        state_.target_range = std::clamp(range, config_.min_range, config_.max_range);
    }

    void WebRadar::ZoomIn(float factor) {
        SetRange(state_.target_range / factor);
    }

    void WebRadar::ZoomOut(float factor) {
        SetRange(state_.target_range * factor);
    }

    void WebRadar::ResetRange() {
        SetRange(config_.default_range);
    }

    void WebRadar::SetRotation(float rotation) {
        state_.target_rotation = rotation;
        if (!config_.smooth_rotation) state_.rotation = rotation;
    }

    void WebRadar::AddRotation(float delta) {
        state_.target_rotation += delta;
    }

    void WebRadar::ResetRotation() {
        state_.target_rotation = 0;
        if (!config_.smooth_rotation) state_.rotation = 0;
    }

    void WebRadar::PingEntity(uint64_t id) {
        state_.pinged_entities.push_back(id);
    }

    void WebRadar::PingPosition(const Vec3& world_pos) {
        // Create temporary ping entity
        RadarEntity ping;
        ping.id = 0xFFFFFFFF - state_.pinged_entities.size();
        ping.type = EntityType::Custom;
        ping.world_pos = world_pos;
        ping.icon = IconStyle{};
        ping.icon.type = IconStyle::Type::Cross;
        ping.icon.size = 12.0f;
        ping.icon.color = IM_COL32(255, 255, 0, 255);
        ping.active = true;
        state_.entities.push_back(ping);
    }

    void WebRadar::ClearPings() {
        state_.pinged_entities.clear();
    }

    ImVec2 WebRadar::WorldToRadar(const Vec3& world_pos) const {
        return WorldToRadarInternal(world_pos);
    }

    Vec3 WebRadar::RadarToWorld(const ImVec2& radar_pos) const {
        Vec2 relative(radar_pos.x - state_.screen_center.x, radar_pos.y - state_.screen_center.y);
        float dist = relative.Length();
        float angle = atan2f(relative.x, -relative.y) + state_.rotation;
        
        float world_dist = dist / (state_.screen_radius / state_.current_range);
        
        return Vec3(
            state_.center_pos.x + sinf(angle) * world_dist,
            state_.center_pos.y,
            state_.center_pos.z + cosf(angle) * world_dist
        );
    }

    std::string WebRadar::SerializeConfig() const {
        std::ostringstream out;
        out << config_.enabled << '|'
            << config_.follow_player << '|'
            << config_.rotate_with_player << '|'
            << config_.min_range << '|'
            << config_.max_range << '|'
            << config_.default_range << '|'
            << config_.range_step << '|'
            << config_.auto_range << '|'
            << config_.radar_size << '|'
            << config_.background_alpha << '|'
            << config_.background_color << '|'
            << config_.border_color << '|'
            << config_.show_grid << '|'
            << config_.grid_divisions << '|'
            << config_.grid_color << '|'
            << config_.grid_thickness << '|'
            << config_.show_cardinal << '|'
            << config_.cardinal_color << '|'
            << config_.show_compass << '|'
            << config_.compass_radius << '|'
            << config_.compass_color << '|'
            << config_.show_degrees << '|'
            << config_.local_player_icon.type << '|'
            << config_.local_player_icon.size << '|'
            << config_.local_player_icon.color << '|'
            << config_.local_player_color << '|'
            << config_.show_local_direction << '|'
            << config_.highlight_friends << '|'
            << config_.friend_color << '|'
            << config_.click_to_ping << '|'
            << config_.right_click_menu << '|'
            << config_.animate_entities << '|'
            << config_.entity_fade_time << '|'
            << config_.smooth_rotation << '|'
            << config_.rotation_speed << '|'
            << config_.max_entities << '|'
            << config_.updates_per_second << '|'
            << config_.cull_offscreen;
        return out.str();
    }

    bool WebRadar::DeserializeConfig(const std::string& data) {
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
        read_bool(config_.follow_player);
        read_bool(config_.rotate_with_player);
        read_float(config_.min_range);
        read_float(config_.max_range);
        read_float(config_.default_range);
        read_float(config_.range_step);
        read_bool(config_.auto_range);
        read_float(config_.radar_size);
        read_float(config_.background_alpha);
        read_uint(config_.background_color);
        read_uint(config_.border_color);
        read_bool(config_.show_grid);
        read_int(config_.grid_divisions);
        read_uint(config_.grid_color);
        read_float(config_.grid_thickness);
        read_bool(config_.show_cardinal);
        read_uint(config_.cardinal_color);
        read_bool(config_.show_compass);
        read_float(config_.compass_radius);
        read_uint(config_.compass_color);
        read_bool(config_.show_degrees);
        read_int(config_.local_player_icon.type);
        read_float(config_.local_player_icon.size);
        read_uint(config_.local_player_icon.color);
        read_uint(config_.local_player_color);
        read_bool(config_.show_local_direction);
        read_bool(config_.highlight_friends);
        read_uint(config_.friend_color);
        read_bool(config_.click_to_ping);
        read_bool(config_.right_click_menu);
        read_bool(config_.animate_entities);
        read_float(config_.entity_fade_time);
        read_bool(config_.smooth_rotation);
        read_float(config_.rotation_speed);
        read_int(config_.max_entities);
        read_int(config_.updates_per_second);
        read_bool(config_.cull_offscreen);
        
        return true;
    }

    // RadarManager implementation
    RadarManager& RadarManager::Instance() {
        static RadarManager instance;
        return instance;
    }

    WebRadar* RadarManager::CreateRadar(const std::string& name, IRadarAdapter* adapter) {
        auto radar = std::make_unique<WebRadar>();
        if (adapter) radar->SetAdapter(adapter);
        WebRadar* ptr = radar.get();
        radars_[name] = std::move(radar);
        return ptr;
    }

    WebRadar* RadarManager::GetRadar(const std::string& name) {
        auto it = radars_.find(name);
        return it != radars_.end() ? it->second.get() : nullptr;
    }

    void RadarManager::RemoveRadar(const std::string& name) {
        radars_.erase(name);
    }

    void RadarManager::ClearAll() {
        radars_.clear();
    }

    void RadarManager::UpdateAll(float dt) {
        for (auto& [name, radar] : radars_) {
            radar->Update(dt);
        }
    }

    void RadarManager::DrawAll() {
        for (auto& [name, radar] : radars_) {
            // Would need position info
        }
    }

    // Preset configurations
    RadarConfig GetDefaultRadarConfig() {
        RadarConfig cfg;
        cfg.enabled = true;
        cfg.follow_player = true;
        cfg.rotate_with_player = true;
        cfg.min_range = 50.0f;
        cfg.max_range = 1000.0f;
        cfg.default_range = 250.0f;
        cfg.range_step = 50.0f;
        cfg.auto_range = false;
        
        cfg.radar_size = 280.0f;
        cfg.background_alpha = 0.85f;
        cfg.background_color = IM_COL32(12, 12, 18, 230);
        cfg.border_color = IM_COL32(80, 80, 100, 180);
        
        cfg.show_grid = true;
        cfg.grid_divisions = 4;
        cfg.grid_color = IM_COL32(60, 70, 90, 150);
        cfg.grid_thickness = 1.0f;
        cfg.show_cardinal = true;
        cfg.cardinal_color = IM_COL32(100, 110, 130, 200);
        
        cfg.show_compass = true;
        cfg.compass_radius = 130.0f;
        cfg.compass_color = IM_COL32(180, 180, 200, 200);
        cfg.show_degrees = true;
        
        // Entity displays
        InitializeDefaultEntityDisplays(cfg);
        
        // Local player
        cfg.local_player_icon = IconStyle{};
        cfg.local_player_icon.type = IconStyle::Type::Arrow;
        cfg.local_player_icon.size = 10.0f;
        cfg.local_player_icon.color = IM_COL32(0, 255, 120, 255);
        cfg.local_player_color = IM_COL32(0, 255, 120, 255);
        cfg.show_local_direction = true;
        
        cfg.highlight_friends = true;
        cfg.friend_color = IM_COL32(0, 200, 255, 255);
        
        cfg.click_to_ping = true;
        cfg.right_click_menu = true;
        cfg.animate_entities = true;
        cfg.entity_fade_time = 0.3f;
        cfg.smooth_rotation = true;
        cfg.rotation_speed = 15.0f;
        
        cfg.max_entities = 128;
        cfg.updates_per_second = 30;
        cfg.cull_offscreen = true;
        
        return cfg;
    }

    RadarConfig GetCompetitiveRadarConfig() {
        RadarConfig cfg = GetDefaultRadarConfig();
        cfg.radar_size = 220.0f;
        cfg.default_range = 180.0f;
        cfg.show_compass = false;
        cfg.show_degrees = false;
        cfg.local_player_icon.size = 8.0f;
        cfg.max_entities = 64;
        cfg.updates_per_second = 60;
        return cfg;
    }

    RadarConfig GetSniperRadarConfig() {
        RadarConfig cfg = GetDefaultRadarConfig();
        cfg.default_range = 500.0f;
        cfg.max_range = 2000.0f;
        cfg.radar_size = 320.0f;
        cfg.show_grid = true;
        cfg.grid_divisions = 8;
        return cfg;
    }

    RadarConfig GetVehicleRadarConfig() {
        RadarConfig cfg = GetDefaultRadarConfig();
        cfg.default_range = 400.0f;
        cfg.max_range = 1500.0f;
        // Vehicle entities will be highlighted
        return cfg;
    }

    RadarConfig GetMinimalRadarConfig() {
        RadarConfig cfg = GetDefaultRadarConfig();
        cfg.radar_size = 180.0f;
        cfg.show_compass = false;
        cfg.show_grid = false;
        cfg.show_cardinal = false;
        cfg.local_player_icon.type = IconStyle::Type::Circle;
        cfg.max_entities = 32;
        return cfg;
    }

    void InitializeDefaultEntityDisplays(RadarConfig& config) {
        // Player
        config.entity_displays[static_cast<int>(EntityType::Player)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::Player)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::Player)].max_distance = 500.0f;
        config.entity_displays[static_cast<int>(EntityType::Player)].show_names = true;
        config.entity_displays[static_cast<int>(EntityType::Player)].show_distance = true;
        config.entity_displays[static_cast<int>(EntityType::Player)].show_health = true;
        config.entity_displays[static_cast<int>(EntityType::Player)].show_team_color = true;
        config.entity_displays[static_cast<int>(EntityType::Player)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::Player)].icon.type = IconStyle::Type::Circle;
        config.entity_displays[static_cast<int>(EntityType::Player)].icon.size = 7.0f;
        config.entity_displays[static_cast<int>(EntityType::Player)].icon.color = IM_COL32(255, 80, 80, 255);
        config.entity_displays[static_cast<int>(EntityType::Player)].fade_distance = 300.0f;
        
        // Vehicle
        config.entity_displays[static_cast<int>(EntityType::Vehicle)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].max_distance = 800.0f;
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].show_names = true;
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].show_distance = true;
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].icon.type = IconStyle::Type::Square;
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].icon.size = 10.0f;
        config.entity_displays[static_cast<int>(EntityType::Vehicle)].icon.color = IM_COL32(255, 200, 0, 255);
        
        // Animal
        config.entity_displays[static_cast<int>(EntityType::Animal)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::Animal)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::Animal)].max_distance = 300.0f;
        config.entity_displays[static_cast<int>(EntityType::Animal)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::Animal)].icon.type = IconStyle::Type::Triangle;
        config.entity_displays[static_cast<int>(EntityType::Animal)].icon.size = 6.0f;
        config.entity_displays[static_cast<int>(EntityType::Animal)].icon.color = IM_COL32(100, 255, 150, 255);
        
        // Resource
        config.entity_displays[static_cast<int>(EntityType::Resource)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::Resource)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::Resource)].max_distance = 200.0f;
        config.entity_displays[static_cast<int>(EntityType::Resource)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::Resource)].icon.type = IconStyle::Type::Diamond;
        config.entity_displays[static_cast<int>(EntityType::Resource)].icon.size = 5.0f;
        config.entity_displays[static_cast<int>(EntityType::Resource)].icon.color = IM_COL32(0, 255, 200, 255);
        
        // Loot
        config.entity_displays[static_cast<int>(EntityType::Loot)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::Loot)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::Loot)].max_distance = 150.0f;
        config.entity_displays[static_cast<int>(EntityType::Loot)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::Loot)].icon.type = IconStyle::Type::Cross;
        config.entity_displays[static_cast<int>(EntityType::Loot)].icon.size = 5.0f;
        config.entity_displays[static_cast<int>(EntityType::Loot)].icon.color = IM_COL32(255, 100, 255, 255);
        
        // Building
        config.entity_displays[static_cast<int>(EntityType::Building)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::Building)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::Building)].max_distance = 1000.0f;
        config.entity_displays[static_cast<int>(EntityType::Building)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::Building)].icon.type = IconStyle::Type::Square;
        config.entity_displays[static_cast<int>(EntityType::Building)].icon.size = 12.0f;
        config.entity_displays[static_cast<int>(EntityType::Building)].icon.color = IM_COL32(180, 180, 180, 200);
        
        // Dropped item
        config.entity_displays[static_cast<int>(EntityType::DroppedItem)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::DroppedItem)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::DroppedItem)].max_distance = 100.0f;
        config.entity_displays[static_cast<int>(EntityType::DroppedItem)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::DroppedItem)].icon.type = IconStyle::Type::Circle;
        config.entity_displays[static_cast<int>(EntityType::DroppedItem)].icon.size = 4.0f;
        config.entity_displays[static_cast<int>(EntityType::DroppedItem)].icon.color = IM_COL32(255, 255, 100, 255);
        
        // Projectile
        config.entity_displays[static_cast<int>(EntityType::Projectile)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::Projectile)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::Projectile)].max_distance = 300.0f;
        config.entity_displays[static_cast<int>(EntityType::Projectile)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::Projectile)].icon.type = IconStyle::Type::Arrow;
        config.entity_displays[static_cast<int>(EntityType::Projectile)].icon.size = 4.0f;
        config.entity_displays[static_cast<int>(EntityType::Projectile)].icon.color = IM_COL32(255, 100, 100, 200);
        
        // NPC
        config.entity_displays[static_cast<int>(EntityType::NPC)] = RadarConfig::EntityDisplay{};
        config.entity_displays[static_cast<int>(EntityType::NPC)].enabled = true;
        config.entity_displays[static_cast<int>(EntityType::NPC)].max_distance = 400.0f;
        config.entity_displays[static_cast<int>(EntityType::NPC)].icon = IconStyle{};
        config.entity_displays[static_cast<int>(EntityType::NPC)].icon.type = IconStyle::Type::Circle;
        config.entity_displays[static_cast<int>(EntityType::NPC)].icon.size = 6.0f;
        config.entity_displays[static_cast<int>(EntityType::NPC)].icon.color = IM_COL32(200, 150, 255, 255);
    }

    std::string SerializeRadarConfig(const RadarConfig& config) {
        // Simplified serialization
        return ""; // Would be similar to WebRadar::SerializeConfig
    }

    bool DeserializeRadarConfig(const std::string& data, RadarConfig& config) {
        // Simplified deserialization
        return true;
    }

} // namespace Gameplay::WebRadar