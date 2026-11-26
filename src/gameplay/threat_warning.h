#pragma once

#include <cstdint>
#include <optional>
#include <vector>
#include <algorithm>
#include <cmath>
#include "src/project/third_party/ImGui/imgui.h"

namespace OmniGhost::Gameplay {

// ===== Threat Warning System =====
// Desenha indicador visual no crosshair/hud para inimigos perigosos
// (perto + no FOV + olhando para você)

struct ThreatConfig {
    bool enabled = false;
    bool show_arrow = true;           // Seta apontando para ameaça
    bool show_arc = true;             // Arco no FOV indicando direção
    bool show_distance = true;        // Mostra distância
    bool show_only_when_aiming = false; // Só mostra quando segurando aim key

    float threat_distance = 60.0f;    // Distância máxima para considerar ameaça (metros)
    float threat_fov = 30.0f;         // FOV angular para considerar ameaça (graus)
    float aim_indicator_angle = 8.0f; // Ângulo máximo para indicador de mira

    // Visual
    float arrow_size = 24.0f;
    float arrow_distance_from_crosshair = 40.0f;
    float arc_radius = 60.0f;
    float arc_thickness = 3.0f;
    ImU32 color_close = IM_COL32(255, 50, 50, 255);    // < 20m
    ImU32 color_medium = IM_COL32(255, 180, 0, 255);   // 20-40m
    ImU32 color_far = IM_COL32(255, 255, 50, 255);     // > 40m
    ImU32 color_arc = IM_COL32(255, 100, 100, 180);

    // Animation
    bool pulse_on_new_threat = true;
    float pulse_duration = 0.5f;
};

struct ThreatTarget {
    uint64_t entity_id = 0;
    ImVec2 screen_pos;           // Posição na tela
    float distance_m = 0.0f;
    float angle_to_crosshair = 0.0f; // Graus
    bool is_visible = true;
    bool looking_at_local = false;
    float threat_level = 0.0f;   // 0.0 - 1.0
};

class ThreatWarningSystem {
public:
    ThreatWarningSystem() = default;
    explicit ThreatWarningSystem(const ThreatConfig& cfg) : config_(cfg) {}

    void SetConfig(const ThreatConfig& cfg) { config_ = cfg; }
    const ThreatConfig& GetConfig() const { return config_; }

    // Atualiza com lista de jogadores (chamar a cada frame)
    void Update(const ImVec2& crosshair_pos, float fov_degrees,
                const std::vector<ThreatTarget>& targets);

    // Renderiza indicadores (chamar no render loop, sobreposto ao jogo)
    void Render(ImDrawList* dl, const ImVec2& crosshair_pos);

    // Obtém ameaça mais perigosa
    std::optional<ThreatTarget> GetHighestThreat() const;

    // Stats
    int GetActiveThreatCount() const { return static_cast<int>(active_threats_.size()); }

private:
    ThreatConfig config_;
    std::vector<ThreatTarget> active_threats_;
    struct ThreatAnim {
        uint64_t entity_id = 0;
        float pulse_timer = 0.0f;
    };
    std::vector<ThreatAnim> threat_anims_;

    ImU32 GetThreatColor(float distance) const;
    void UpdateAnimations(float dt);
};

} // namespace OmniGhost::Gameplay