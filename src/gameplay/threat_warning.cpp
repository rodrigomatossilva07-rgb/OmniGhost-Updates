#include "threat_warning.h"
#include <algorithm>
#include <cmath>

namespace OmniGhost::Gameplay {

void ThreatWarningSystem::Update(const ImVec2& crosshair_pos, float fov_degrees,
                                 const std::vector<ThreatTarget>& targets) {
    active_threats_.clear();

    for (const auto& t : targets) {
        // Filtros básicos
        if (t.distance_m > config_.threat_distance) continue;
        if (t.angle_to_crosshair > config_.threat_fov) continue;
        if (!t.is_visible) continue;

        // Calcula nível de ameaça (0-1)
        float dist_factor = 1.0f - std::clamp(t.distance_m / config_.threat_distance, 0.0f, 1.0f);
        float angle_factor = 1.0f - std::clamp(t.angle_to_crosshair / config_.threat_fov, 0.0f, 1.0f);
        float visibility_factor = t.is_visible ? 1.0f : 0.3f;
        float look_factor = t.looking_at_local ? 1.5f : 1.0f;

        ThreatTarget threat = t;
        threat.threat_level = std::clamp((dist_factor * 0.5f + angle_factor * 0.3f + visibility_factor * 0.2f) * look_factor, 0.0f, 1.0f);

        active_threats_.push_back(threat);

        // Trigger pulse animation for new threats
        if (config_.pulse_on_new_threat) {
            auto it = std::find_if(threat_anims_.begin(), threat_anims_.end(),
                                   [t](const ThreatAnim& a) { return a.entity_id == t.entity_id; });
            if (it == threat_anims_.end()) {
                threat_anims_.push_back({ t.entity_id, config_.pulse_duration });
            } else {
                it->pulse_timer = config_.pulse_duration;
            }
        }
    }

    // Ordena por threat_level (maior primeiro)
    std::sort(active_threats_.begin(), active_threats_.end(),
              [](const ThreatTarget& a, const ThreatTarget& b) {
                  return a.threat_level > b.threat_level;
              });

    // Limpa animações de threats que não estão mais ativos
    threat_anims_.erase(
        std::remove_if(threat_anims_.begin(), threat_anims_.end(),
                       [this](const ThreatAnim& anim) {
                           return std::none_of(active_threats_.begin(), active_threats_.end(),
                                               [&anim](const ThreatTarget& t) { return t.entity_id == anim.entity_id; });
                       }),
        threat_anims_.end());
}

void ThreatWarningSystem::UpdateAnimations(float dt) {
    for (auto& anim : threat_anims_) {
        anim.pulse_timer = std::max(0.0f, anim.pulse_timer - dt);
    }
}

ImU32 ThreatWarningSystem::GetThreatColor(float distance) const {
    if (distance < 20.0f) return config_.color_close;
    if (distance < 40.0f) return config_.color_medium;
    return config_.color_far;
}

void ThreatWarningSystem::Render(ImDrawList* dl, const ImVec2& crosshair_pos) {
    if (!config_.enabled || active_threats_.empty()) return;

    float dt = ImGui::GetIO().DeltaTime;
    UpdateAnimations(dt);

    for (const auto& threat : active_threats_) {
        ImU32 color = GetThreatColor(threat.distance_m);

        // Encontra animação de pulse
        float pulse = 1.0f;
        auto anim_it = std::find_if(threat_anims_.begin(), threat_anims_.end(),
                                    [&threat](const ThreatAnim& a) { return a.entity_id == threat.entity_id; });
        if (anim_it != threat_anims_.end() && anim_it->pulse_timer > 0) {
            float t = 1.0f - anim_it->pulse_timer / config_.pulse_duration;
            pulse = 1.0f + 0.3f * std::sin(t * 3.14159f * 4.0f); // 4 pulses
        }

        // Calcula direção do crosshair para o target
        ImVec2 dir = ImVec2(threat.screen_pos.x - crosshair_pos.x,
                            threat.screen_pos.y - crosshair_pos.y);
        float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y);
        if (dist < 0.001f) continue;

        dir.x /= dist;
        dir.y /= dist;

        // ===== Desenha Seta =====
        if (config_.show_arrow) {
            float arrow_dist = config_.arrow_distance_from_crosshair * pulse;
            float arrow_size = config_.arrow_size * pulse;

            ImVec2 arrow_base = ImVec2(crosshair_pos.x + dir.x * arrow_dist,
                                       crosshair_pos.y + dir.y * arrow_dist);

            // Perpendicular para as asas da seta
            ImVec2 perp(-dir.y, dir.x);
            float wing = arrow_size * 0.5f;

            ImVec2 p1 = ImVec2(arrow_base.x + dir.x * arrow_size, arrow_base.y + dir.y * arrow_size);
            ImVec2 p2 = ImVec2(arrow_base.x - dir.x * arrow_size * 0.6f + perp.x * wing,
                               arrow_base.y - dir.y * arrow_size * 0.6f + perp.y * wing);
            ImVec2 p3 = ImVec2(arrow_base.x - dir.x * arrow_size * 0.6f - perp.x * wing,
                               arrow_base.y - dir.y * arrow_size * 0.6f - perp.y * wing);

            // Seta preenchida
            dl->AddTriangleFilled(p1, p2, p3, color);

            // Outline
            dl->AddTriangle(p1, p2, p3, IM_COL32(0, 0, 0, 200), 2.0f);

            // Linha conectando ao crosshair (opcional, sutil)
            dl->AddLine(crosshair_pos, arrow_base, IM_COL32((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, 80), 1.0f);
        }

        // ===== Desenha Arco no FOV =====
        if (config_.show_arc) {
            float angle_rad = threat.angle_to_crosshair * 3.14159f / 180.0f;
            float arc_radius = config_.arc_radius;
            float thickness = config_.arc_thickness;

            // Centro do arco é o crosshair
            // Desenha arco indicando direção da ameaça
            int segments = 16;
            float start_angle = -angle_rad - 0.3f;
            float end_angle = -angle_rad + 0.3f;

            ImVec2 prev = ImVec2(crosshair_pos.x + std::cos(start_angle) * arc_radius,
                                 crosshair_pos.y + std::sin(start_angle) * arc_radius);

            for (int i = 1; i <= segments; ++i) {
                float t = static_cast<float>(i) / segments;
                float ang = start_angle + (end_angle - start_angle) * t;
                ImVec2 curr = ImVec2(crosshair_pos.x + std::cos(ang) * arc_radius,
                                     crosshair_pos.y + std::sin(ang) * arc_radius);
                dl->AddLine(prev, curr, config_.color_arc, thickness);
                prev = curr;
            }

            // Marcador no final do arco (ponta)
            ImVec2 marker = ImVec2(crosshair_pos.x + std::cos(-angle_rad) * (arc_radius + 8),
                                   crosshair_pos.y + std::sin(-angle_rad) * (arc_radius + 8));
            dl->AddCircleFilled(marker, 4.0f, color);
            dl->AddCircle(marker, 4.0f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
        }

        // ===== Distância =====
        if (config_.show_distance) {
            char dist_str[16];
            snprintf(dist_str, sizeof(dist_str), "%.0fm", threat.distance_m);

            ImVec2 text_pos = ImVec2(threat.screen_pos.x, threat.screen_pos.y - 20.0f);
            ImVec2 text_size = ImGui::CalcTextSize(dist_str);
            text_pos.x -= text_size.x * 0.5f;

            // Background
            dl->AddRectFilled(ImVec2(text_pos.x - 2, text_pos.y - 1),
                              ImVec2(text_pos.x + text_size.x + 2, text_pos.y + text_size.y + 1),
                              IM_COL32(0, 0, 0, 180), 3.0f);

            dl->AddText(text_pos, color, dist_str);
        }
    }
}

std::optional<ThreatWarningSystem::ThreatTarget> ThreatWarningSystem::GetHighestThreat() const {
    if (active_threats_.empty()) return std::nullopt;
    return active_threats_.front();
}

} // namespace OmniGhost::Gameplay