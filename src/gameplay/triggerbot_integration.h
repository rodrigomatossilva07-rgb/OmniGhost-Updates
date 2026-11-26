#pragma once

#include "triggerbot.h"
#include <cstdint>
#include <functional>
#include <string>

namespace OmniGhost::Gameplay {

// ===== Triggerbot Integration =====
// Wrapper que conecta o triggerbot genérico com cada jogo

struct TriggerIntegrationConfig {
    // Configuração base (mapeia para TriggerbotConfig)
    bool enabled = false;
    bool visible_only = true;
    bool fire_without_aim = false;
    bool require_target_lock = false;

    int mode = 3; // 0=Instant, 1=PreFire, 2=PostFire, 3=PrePost, 4=Burst

    float pre_fire_ms = 20.0f;
    float post_fire_ms = 120.0f;
    float min_time_between_shots = 50.0f;

    float deadzone_px = 5.0f;
    float max_distance_m = 100.0f;

    uint64_t weapon_mask = ~0ULL;
    bool weapon_allowed[64] = {};

    float reaction_variance_ms = 5.0f;
    float jitter_px = 0.5f;
    bool simulate_human_reaction = true;

    int burst_shots = 3;
    float burst_interval_ms = 80.0f;
    float burst_cooldown_ms = 500.0f;

    bool ignore_knocked = true;
    bool ignore_teammates = true;
    bool ignore_npcs = false;
    float min_health_percent = 0.0f;
};

struct TriggerTargetInfo {
    uint64_t entity_id = 0;
    float screen_x = 0.0f;
    float screen_y = 0.0f;
    float distance_m = 0.0f;
    bool visible = true;
    bool knocked = false;
    bool teammate = false;
    bool npc = false;
    float health_percent = 100.0f;
    int weapon_index = 0;
};

class TriggerIntegration {
public:
    TriggerIntegration() = default;
    ~TriggerIntegration() = default;

    // Inicializa com config do jogo
    void Initialize(const TriggerIntegrationConfig& cfg);

    // Atualiza config (chamar quando menu mudar)
    void SetConfig(const TriggerIntegrationConfig& cfg);

    // Chama a cada frame no game loop
    // Retorna true se deve atirar
    bool Update(const TriggerTargetInfo& target, bool aim_key_held, bool crosshair_on_target);

    // Reset estado
    void Reset();

    // Debug
    const char* GetPhaseName() const;
    OmniGhost::Gameplay::Triggerbot::TriggerbotState::Phase GetPhase() const;
    const OmniGhost::Gameplay::Triggerbot& GetTriggerbot() const { return triggerbot_; }

    // Static helper to convert config
    static OmniGhost::Gameplay::TriggerbotConfig ConvertConfig(const TriggerIntegrationConfig& cfg);

private:
    OmniGhost::Gameplay::Triggerbot triggerbot_;
    TriggerIntegrationConfig current_config_;
};

} // namespace OmniGhost::Gameplay