#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <chrono>
#include <random>
#include <functional>

namespace OmniGhost::Gameplay {

// ===== Triggerbot =====
// Baseado no crisis-fortnite triggerbot com humanização avançada

enum class TriggerMode : uint8_t {
    Instant = 0,        // Dispara imediatamente ao entrar no hitbox
    PreFire = 1,        // Pre-fire delay (antecipa movimento)
    PostFire = 2,       // Post-fire delay (mantém disparo)
    PrePost = 3,        // Ambos
    Burst = 4,          // Rajada controlada
};

struct TriggerbotConfig {
    bool enabled = false;
    bool visible_only = true;         // Só atira se visível
    bool fire_without_aim = false;    // Dispara mesmo sem segurar aim key
    bool require_target_lock = false; // Requer target travado (aimbot)

    TriggerMode mode = TriggerMode::PrePost;

    // Timing (milliseconds)
    float pre_fire_ms = 20.0f;        // Delay antes de atirar (antecipação)
    float post_fire_ms = 120.0f;      // Delay depois de atirar (controle recuo)
    float min_time_between_shots = 50.0f; // Cooldown mínimo entre tiros

    // Hitbox / Deadzone
    float deadzone_px = 5.0f;         // Raio morto ao redor do crosshair (pixels)
    float max_distance_m = 100.0f;    // Distância máxima

    // Weapon filtering
    using WeaponMask = uint64_t;      // Bitmask de armas permitidas
    WeaponMask weapon_mask = ~WeaponMask(0); // Todas por padrão
    std::array<bool, 64> weapon_allowed{};   // Array alternativo por índice

    // Humanização
    float reaction_variance_ms = 5.0f;     // Variância aleatória no reaction time
    float jitter_px = 0.5f;                // Jitter no crosshair check
    bool simulate_human_reaction = true;   // Simula tempo de reação humano

    // Burst mode
    int burst_shots = 3;                   // Tiros por rajada
    float burst_interval_ms = 80.0f;       // Intervalo entre tiros na rajada
    float burst_cooldown_ms = 500.0f;      // Cooldown após rajada

    // Filtros de alvo
    bool ignore_knocked = true;
    bool ignore_teammates = true;
    bool ignore_npcs = false;
    float min_health_percent = 0.0f;       // Ignora alvos com vida menor

    // Visual/Debug
    bool show_debug = false;
    bool show_crosshair_overlay = false;
};

struct TriggerbotState {
    enum class Phase : uint8_t {
        Idle = 0,
        PreFire,      // Aguardando pre-fire delay
        Firing,       // Atirando
        PostFire,     // Aguardando post-fire delay
        BurstCooldown,// Cooldown entre rajadas
        Cooldown      // Cooldown geral
    };

    Phase phase = Phase::Idle;
    uint64_t target_entity = 0;
    std::chrono::steady_clock::time_point phase_start;
    std::chrono::steady_clock::time_point last_shot;
    int shots_in_burst = 0;
    float accumulated_pre_fire = 0.0f;
    bool waiting_for_aim_release = false;
};

class Triggerbot {
public:
    Triggerbot() = default;
    explicit Triggerbot(const TriggerbotConfig& cfg) : config_(cfg) {}

    void SetConfig(const TriggerbotConfig& cfg) { config_ = cfg; }
    const TriggerbotConfig& GetConfig() const { return config_; }

    // Chama a cada frame com info do alvo atual
    // Retorna true se deve disparar neste frame
    bool Update(bool crosshair_on_target,
                uint64_t target_entity_id,
                float target_distance,
                bool target_visible,
                int target_weapon_index,
                bool target_knocked,
                bool target_teammate,
                bool target_npc,
                float target_health_percent,
                bool aim_key_held);

    // Força reset do estado
    void Reset();

    // Estado atual (para debug/UI)
    const TriggerbotState& GetState() const { return state_; }
    const char* GetPhaseName() const;

    // Configuração de weapon mask por nome/índice
    void SetWeaponAllowed(int weapon_index, bool allowed);
    void SetWeaponMask(TriggerbotConfig::WeaponMask mask);
    bool IsWeaponAllowed(int weapon_index) const;

private:
    TriggerbotConfig config_;
    TriggerbotState state_;
    std::mt19937 rng_{ static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count()) };

    bool CheckTargetValid(uint64_t entity_id, float distance, bool visible,
                          int weapon_idx, bool knocked, bool teammate, bool npc, float hp_pct) const;
    float GetRandomizedDelay(float base_ms) const;
    void TransitionTo(TriggerbotState::Phase new_phase);
};

} // namespace OmniGhost::Gameplay