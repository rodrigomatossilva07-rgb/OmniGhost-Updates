#include "../widgets.h"
#include "../theme.h"
#include "../localization.h"
#include "global_search.h"
#include "gameplay/unified_aim.h"
#include "gameplay/web_radar.h"
#include "gameplay/sound_esp.h"
#include "gameplay/spectator_list.h"
#include "gameplay/triggerbot.h"
#include "gameplay/recoil_control.h"
#include "gameplay/prediction.h"
#include "gameplay/visibility.h"
#include "gameplay/bone_system.h"
#include "gameplay/smooth_curves.h"
#include "gameplay/movement_assist.h"
#include "gameplay/entity_cache.h"
#include "gameplay/profile_manager.h"
#include "gameplay/offset_manager.h"
#include "gameplay/resolution.h"
#include "gameplay/game_adapter.h"
#include "../config/app_settings.h"
#include "Fivem/fivem_radar.h"
#include "Fivem/fivem_radar_config.h"
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace CyberWidgets {

// ============================================================================
// Unified Aim Page
// ============================================================================

void DrawUnifiedAim() {
    auto& aimbot = Gameplay::UnifiedAim::GetUnifiedAimbot();
    auto& config = aimbot.GetMutableConfig();
    const auto& state = aimbot.GetState();
    
    BeginCard("Aimbot Unificado - Configuração Principal");
    
    // Enable/Disable
    ToggleSwitch(Loc::TrID("aim.enabled"), &config.enabled);
    ToggleSwitch("Apenas ao mirar", &config.only_when_aiming);
    
    // Key binds
    ImGui::Text("Tecla de mira (VK): %d", config.aim_bind);
    if (CyberButton("Rebind", ImVec2(100, 30))) {
        // Would open key capture
    }
    
    CardGap();
    SectionTitle("Targeting");
    
    // FOV
    SliderFloat("FOV", &config.targeting.fov, 0.5f, 180.0f, "%.1f°");
    SliderFloat("FOV Mínimo", &config.targeting.fov_min, 0.5f, 10.0f, "%.1f°");
    ToggleSwitch("FOV Dinâmico", &config.targeting.dynamic_fov);
    
    // Distance
    SliderFloat("Distância Máxima", &config.targeting.max_distance, 10.0f, 1000.0f, "%.0fm");
    SliderFloat("Distância Mínima", &config.targeting.min_distance, 0.0f, 50.0f, "%.0fm");
    
    // Hitbox
    const char* hitbox_names[] = {"Head", "Neck", "Head > Neck", "Upper Body", "Torso", "Center Mass", "Pelvis", "Closest Bone", "Custom"};
    int hb = static_cast<int>(config.targeting.hitbox_preset);
    if (Combo("Hitbox", &hb, hitbox_names, 9)) {
        config.targeting.hitbox_preset = static_cast<Gameplay::BoneSystem::HitboxPreset>(hb);
    }
    
    ToggleSwitch("Troca Dinâmica de Osso", &config.targeting.dynamic_bone_switch);
    SliderFloat("Dist. Troca", &config.targeting.bone_switch_distance, 10.0f, 200.0f, "%.0fm");
    ToggleSwitch("Apenas Osso Visível", &config.targeting.visible_bone_only);
    
    // Priority
    const char* priority_names[] = {"Crosshair Dist", "Distance", "Threat", "Health", "Velocity", "Exposure", "Custom"};
    int pri = static_cast<int>(config.targeting.priority);
    if (Combo("Prioridade", &pri, priority_names, 7)) {
        config.targeting.priority = static_cast<Gameplay::UnifiedAim::UnifiedConfig::Targeting::Priority>(pri);
    }
    
    ToggleSwitch("Ignorar Team", &config.targeting.ignore_teammates);
    ToggleSwitch("Ignorar Amigos", &config.targeting.ignore_friends);
    ToggleSwitch("Ignorar Mortos", &config.targeting.ignore_dead);
    
    EndCard();
    
    CardGap();
    BeginCard("Suavização e Movimento");
    
    SliderFloat("Suavização", &config.smoothing.smooth, 0.0f, 100.0f, "%.1f");
    SliderFloat("Mínimo", &config.smoothing.smooth_min, 0.0f, 10.0f, "%.1f");
    SliderFloat("Máximo", &config.smoothing.smooth_max, 10.0f, 200.0f, "%.1f");
    
    ToggleSwitch("Adaptativa", &config.smoothing.adaptive);
    ToggleSwitch("Baseada em Distância", &config.smoothing.distance_based);
    ToggleSwitch("Baseada em Velocidade", &config.smoothing.velocity_based);
    ToggleSwitch("Baseada em Ângulo", &config.smoothing.angle_based);
    ToggleSwitch("Baseada em FOV", &config.smoothing.fov_based);
    
    // Curve type
    const char* curve_names[] = {"Linear", "EaseInQuad", "EaseOutQuad", "EaseInOutQuad", "EaseInCubic", "EaseOutCubic", "EaseInOutCubic", "EaseInQuart", "EaseOutQuart", "EaseInOutQuart", "EaseInQuint", "EaseOutQuint", "EaseInOutQuint", "EaseInSine", "EaseOutSine", "EaseInOutSine", "EaseInExpo", "EaseOutExpo", "EaseInOutExpo", "EaseInCirc", "EaseOutCirc", "EaseInOutCirc", "EaseInBack", "EaseOutBack", "EaseInOutBack", "EaseInElastic", "EaseOutElastic", "EaseInOutElastic", "EaseInBounce", "EaseOutBounce", "EaseInOutBounce", "Bezier", "CatmullRom", "Custom"};
    int ct = static_cast<int>(config.smoothing.curve_type);
    if (Combo("Curva", &ct, curve_names, 33)) {
        config.smoothing.curve_type = static_cast<Gameplay::SmoothCurves::CurveType>(ct);
    }
    
    // Humanizer
    ToggleSwitch("Humanizar", &config.smoothing.humanize);
    SliderFloat("Micro Jitter", &config.smoothing.humanizer.micro_jitter, 0.0f, 1.0f, "%.3f");
    SliderFloat("Variância Reação", &config.smoothing.humanizer.reaction_variance, 0.0f, 0.1f, "%.4f");
    SliderFloat("Chance Overshoot", &config.smoothing.humanizer.overshoot_chance, 0.0f, 0.2f, "%.3f");
    SliderFloat("Quantidade Overshoot", &config.smoothing.humanizer.overshoot_amount, 0.0f, 1.0f, "%.2f");
    
    EndCard();
    
    CardGap();
    BeginCard("Predição");
    
    ToggleSwitch("Predição Ativada", &config.prediction.enabled);
    ToggleSwitch("Predição de Velocidade", &config.prediction.velocity_prediction);
    ToggleSwitch("Predição de Aceleração", &config.prediction.acceleration_prediction);
    ToggleSwitch("Predição de Trajetória", &config.prediction.trajectory_prediction);
    ToggleSwitch("Predição de Strafe", &config.prediction.strafe_prediction);
    ToggleSwitch("Predição Pulo/Queda", &config.prediction.jump_fall_prediction);
    
    SliderFloat("Tempo Predição", &config.prediction.prediction_time, 0.0f, 0.2f, "%.3fs");
    SliderFloat("Tempo Máx.", &config.prediction.max_prediction_time, 0.0f, 0.5f, "%.3fs");
    ToggleSwitch("Tempo Adaptativo", &config.prediction.adaptive_time);
    
    ToggleSwitch("Compensação Latência", &config.prediction.latency_compensation);
    SliderFloat("Latência Local (ms)", &config.prediction.local_latency_ms, 0.0f, 100.0f, "%.1fms");
    ToggleSwitch("Latência Adaptativa", &config.prediction.adaptive_latency);
    
    ToggleSwitch("Escalonamento por Distância", &config.prediction.distance_scaling);
    SliderFloat("Dist. Perto", &config.prediction.close_range, 10.0f, 100.0f, "%.0fm");
    SliderFloat("Dist. Média", &config.prediction.mid_range, 50.0f, 200.0f, "%.0fm");
    SliderFloat("Dist. Longe", &config.prediction.far_range, 100.0f, 500.0f, "%.0fm");
    SliderFloat("Escala Perto", &config.prediction.close_scale, 0.1f, 2.0f, "%.2f");
    SliderFloat("Escala Média", &config.prediction.mid_scale, 0.1f, 1.0f, "%.2f");
    SliderFloat("Escala Longe", &config.prediction.far_scale, 0.05f, 0.5f, "%.2f");
    
    EndCard();
}

// ============================================================================
// Web Radar Page
// ============================================================================

void DrawWebRadar() {
    static Gameplay::WebRadar::RadarConfig radar_config = Gameplay::WebRadar::GetDefaultRadarConfig();
    static Gameplay::WebRadar::RadarManager radar_manager;
    static Gameplay::WebRadar::IRadarAdapter* current_adapter = nullptr;
    
    BeginCard("Web Radar - Configuração");
    
    ToggleSwitch("Radar Ativado", &radar_config.enabled);
    ToggleSwitch("Seguir Jogador", &radar_config.follow_player);
    ToggleSwitch("Rotacionar com Jogador", &radar_config.rotate_with_player);
    
    SliderFloat("Raio Mínimo", &radar_config.min_range, 10.0f, 500.0f, "%.0fm");
    SliderFloat("Raio Máximo", &radar_config.max_range, 100.0f, 5000.0f, "%.0fm");
    SliderFloat("Raio Padrão", &radar_config.default_range, 50.0f, 2000.0f, "%.0fm");
    SliderFloat("Passo Zoom", &radar_config.range_step, 10.0f, 200.0f, "%.0fm");
    ToggleSwitch("Auto Range", &radar_config.auto_range);
    
    SliderFloat("Tamanho Radar", &radar_config.radar_size, 150.0f, 500.0f, "%.0fpx");
    
    CardGap();
    SectionTitle("Entidades");
    
    for (int i = 0; i < 10; ++i) {
        auto& display = radar_config.entity_displays[i];
        const char* type_names[] = {"Player", "Vehicle", "Animal", "Item", "Projectile", "Building", "NPC", "Resource", "Dropped Item", "Custom"};
        ImGui::Text("%s:", type_names[i]);
        ImGui::SameLine();
        ToggleSwitch("##enable", &display.enabled);
        ImGui::SameLine();
        SliderFloat("Dist. Máx.", &display.max_distance, 50.0f, 2000.0f, "%.0fm");
        ImGui::SameLine();
        ToggleSwitch("Nomes", &display.show_names);
        ImGui::SameLine();
        ToggleSwitch("Dist.", &display.show_distance);
        ImGui::SameLine();
        ToggleSwitch("HP", &display.show_health);
        ImGui::SameLine();
        ToggleSwitch("Cor Time", &display.show_team_color);
        ImGui::SameLine();
        ToggleSwitch("Apenas Visível", &display.only_visible);
    }
    
    EndCard();
}

// ============================================================================
// Sound ESP Page
// ============================================================================

void DrawSoundESP() {
    static Gameplay::SoundESP::SoundESPConfig config = Gameplay::SoundESP::GetLegitSoundESPConfig();
    
    BeginCard("Sound ESP - Direcional");
    
    ToggleSwitch("Ativado", &config.enabled);
    ToggleSwitch("Apenas ao Mirar", &config.only_when_aiming);
    
    CardGap();
    SectionTitle("Tipos de Som");
    
    ToggleSwitch("Passos", &config.visual.show_footsteps);
    ToggleSwitch("Tiros", &config.visual.show_gunshots);
    ToggleSwitch("Explosões", &config.visual.show_explosions);
    ToggleSwitch("Recarregar", &config.visual.show_reloads);
    ToggleSwitch("Voz", &config.visual.show_voice);
    
    CardGap();
    SectionTitle("Indicadores Direcionais");
    
    ToggleSwitch("Ativado", &config.directional.enabled);
    ToggleSwitch("No Radar", &config.directional.show_on_radar);
    ToggleSwitch("Na Tela", &config.directional.show_on_screen);
    ToggleSwitch("3D", &config.directional.show_3d);
    
    SliderFloat("Tamanho", &config.directional.indicator_size, 10.0f, 100.0f, "%.0fpx");
    SliderFloat("Margem Borda", &config.directional.screen_edge_margin, 20.0f, 200.0f, "%.0fpx");
    SliderFloat("Dist. Fade", &config.directional.fade_distance, 10.0f, 500.0f, "%.0fm");
    ToggleSwitch("Mostrar Distância", &config.directional.show_distance);
    
    EndCard();
}

// ============================================================================
// Spectator List Page
// ============================================================================

void DrawSpectatorList() {
    auto& manager = Gameplay::SpectatorList::SpectatorManager::Instance();
    auto& config = manager.GetConfig();
    
    BeginCard("Lista de Espectadores");
    
    ToggleSwitch("Ativado", &config.enabled);
    ToggleSwitch("Mostrar no HUD", &config.show_on_hud);
    ToggleSwitch("Mostrar no Menu", &config.show_in_menu);
    
    CardGap();
    SectionTitle("Exibição");
    
    ToggleSwitch("Nomes", &config.display.show_names);
    ToggleSwitch("Alvo", &config.display.show_target);
    ToggleSwitch("Modo", &config.display.show_mode);
    ToggleSwitch("Ping", &config.display.show_ping);
    ToggleSwitch("País", &config.display.show_country);
    ToggleSwitch("Plataforma", &config.display.show_platform);
    ToggleSwitch("Duração", &config.display.show_duration);
    ToggleSwitch("Só quem me assiste", &config.display.only_when_spectating_me);
    ToggleSwitch("Destacar Amigos", &config.display.highlight_friends);
    ToggleSwitch("Destacar Streamers", &config.display.highlight_streamers);
    ToggleSwitch("Destacar Admins", &config.display.highlight_admins);
    
    CardGap();
    SectionTitle("Alertas");
    
    ToggleSwitch("Novo Espectador", &config.alerts.notify_new_spectator);
    ToggleSwitch("Me Assiste", &config.alerts.notify_spectating_me);
    ToggleSwitch("Streamer", &config.alerts.notify_streamer);
    ToggleSwitch("Admin", &config.alerts.notify_admin);
    ToggleSwitch("Som", &config.alerts.sound_alert);
    SliderFloat("Duração Alerta", &config.alerts.alert_duration, 1.0f, 30.0f, "%.1fs");
    
    EndCard();
    
    // Draw current spectators
    CardGap();
    BeginCard("Espectadores Atuais");
    
    auto& specs = Gameplay::SpectatorList::SpectatorManager::Instance().GetSpectators();
    if (specs.empty()) {
        TextLine("Nenhum espectador detectado", TextTone::Secondary);
    } else {
        for (const auto& spec : specs) {
            char line[256];
            snprintf(line, sizeof(line), "%s%s%s -> %s", 
                spec.is_streamer ? "★ " : "",
                spec.is_admin ? "◆ " : "",
                spec.name.c_str(),
                spec.target_name.empty() ? "?" : spec.target_name.c_str());
            TextLine(line, TextTone::Primary);
        }
    }
    EndCard();
}

// ============================================================================
// Triggerbot Page
// ============================================================================

void DrawTriggerbot() {
    auto& manager = Gameplay::Triggerbot::TriggerbotManager::Instance();
    auto& config = manager.GetMutableConfig();
    
    BeginCard("Triggerbot");
    
    ToggleSwitch("Ativado", &config.enabled);
    
    CardGap();
    SectionTitle("Condições (AND - todas devem ser verdadeiras)");
    
    // Add condition UI
    static int new_condition_type = 0;
    const char* condition_names[] = {
        "Sempre", "Tecla Segurada", "Tecla Toggle", "Ao Mirar", "Ao Usar Mira",
        "Agachado", "Movendo", "Em Pé", "No Ar", "No Chão",
        "Alvo Visível", "Alvo no FOV", "Distância do Alvo", "Vida do Alvo",
        "Tipo de Arma", "Munição", "Customizado"
    };
    
    Combo("Nova Condição", &new_condition_type, condition_names, 16);
    if (CyberButton("Adicionar", ImVec2(100, 30))) {
        // Would add condition
    }
    
    // List conditions
    for (size_t i = 0; i < config.conditions.size(); ++i) {
        auto& cond = config.conditions[i];
        ImGui::PushID((int)i);
        ImGui::Text("%s", "Condition");
        ImGui::SameLine();
        if (CyberButton("Remover", ImVec2(80, 25))) {
            config.conditions.erase(config.conditions.begin() + i);
        }
        ImGui::PopID();
    }
    
    CardGap();
    SectionTitle("Ações");
    
    for (size_t i = 0; i < config.actions.size(); ++i) {
        auto& action = config.actions[i];
        ImGui::PushID((int)i);
        const char* action_names[] = {"Clique Esquerdo", "Clique Direito", "Clique Meio", "Tecla", "Customizado"};
        int act = static_cast<int>(action.type);
        Combo("##action", &act, action_names, 5);
        ImGui::SameLine();
        if (CyberButton("Remover", ImVec2(80, 25))) {
            config.actions.erase(config.actions.begin() + i);
        }
        ImGui::PopID();
    }
    
    if (CyberButton("Adicionar Ação", ImVec2(150, 30))) {
        config.actions.push_back({Gameplay::Triggerbot::TriggerAction::Type::LeftClick, 0, 0.0f, 0.0f, true});
    }
    
    EndCard();
}

// ============================================================================
// Recoil Control Page
// ============================================================================

void DrawRecoilControl() {
    auto& manager = Gameplay::RecoilControl::RecoilController::Instance();
    auto& config = manager.GetConfig();
    
    BeginCard("Controle de Recuo");
    
    ToggleSwitch("Ativado", &config.enabled);
    ToggleSwitch("Só ao Mirar", &config.only_when_aiming);
    ToggleSwitch("Requer Ataque", &config.require_attack_held);
    
    Combo("Modo", reinterpret_cast<int*>(&config.mode), 
          const_cast<const char**>(static_cast<const char* const*>([]{
              static const char* names[] = {"Off", "Simple", "Pattern", "Hybrid", "Adaptive"};
              return names;
          }())), 5);
    
    SliderFloat("Vertical Global", &config.global_vertical, 0.0f, 2.0f, "%.2f");
    SliderFloat("Horizontal Global", &config.global_horizontal, 0.0f, 2.0f, "%.2f");
    
    ToggleSwitch("Desativar no Reload", &config.disable_on_reload);
    ToggleSwitch("Desativar no Troca de Arma", &config.disable_on_weapon_swap);
    ToggleSwitch("Aprender Padrões", &config.learn_patterns);
    SliderInt("Mín. Tiros p/ Aprender", &config.min_shots_to_learn, 10, 100);
    SliderFloat("Confiança Padrão", &config.pattern_confidence_threshold, 0.5f, 1.0f, "%.2f");
    
    CardGap();
    SectionTitle("Per-Arma");
    
    // Would show per-weapon configs
    TextLine("Configurações por arma disponíveis no Profile Manager", TextTone::Secondary);
    
    EndCard();
}

// ============================================================================
// Prediction Page
// ============================================================================

void DrawPrediction() {
    auto& manager = Gameplay::Prediction::PredictionManager::Instance();
    auto& config = manager.GetMutableConfig();
    
    BeginCard("Predição e Lag Compensation");
    
    ToggleSwitch("Ativado", &config.enabled);
    ToggleSwitch("Predição Velocidade", &config.velocity_prediction);
    ToggleSwitch("Predição Aceleração", &config.acceleration_prediction);
    
    SliderFloat("Tempo Predição", &config.prediction_time, 0.0f, 0.2f, "%.3fs");
    SliderFloat("Tempo Máx.", &config.max_prediction_time, 0.0f, 0.5f, "%.3fs");
    ToggleSwitch("Tempo Adaptativo", &config.adaptive_time);
    
    ToggleSwitch("Compensação Latência", &config.latency_compensation);
    SliderFloat("Latência Local (ms)", &config.local_latency_ms, 0.0f, 100.0f, "%.1fms");
    SliderFloat("Latência Servidor (ms)", &config.server_latency_ms, 0.0f, 200.0f, "%.1fms");
    ToggleSwitch("Latência Adaptativa", &config.adaptive_latency);
    
    ToggleSwitch("Escalonamento Distância", &config.distance_scaling);
    SliderFloat("Perto (m)", &config.close_range, 10.0f, 100.0f, "%.0fm");
    SliderFloat("Médio (m)", &config.mid_range, 50.0f, 300.0f, "%.0fm");
    SliderFloat("Longe (m)", &config.far_range, 100.0f, 1000.0f, "%.0fm");
    SliderFloat("Escala Perto", &config.close_scale, 0.1f, 2.0f, "%.2f");
    SliderFloat("Escala Média", &config.mid_scale, 0.1f, 1.0f, "%.2f");
    SliderFloat("Escala Longe", &config.far_scale, 0.05f, 0.5f, "%.2f");
    
    ToggleSwitch("Predição Vertical", &config.predict_vertical);
    SliderFloat("Gravidade", &config.gravity, 5.0f, 20.0f, "%.1f");
    SliderFloat("Máx. Vert.", &config.max_vertical_prediction, 0.0f, 10.0f, "%.1fm");
    
    EndCard();
}

// ============================================================================
// Visibility Page
// ============================================================================

void DrawVisibility() {
    auto& manager = Gameplay::Visibility::VisibilityManager::Instance();
    auto& config = manager.GetMutableConfig();
    
    BeginCard("Verificação de Visibilidade");
    
    const char* methods[] = {"None", "Raycast", "MultiPoint", "Bresenham3D", "HardwareOcclusion", "Hybrid"};
    int method = static_cast<int>(config.method);
    Combo("Método", &method, methods, 6);
    config.method = static_cast<Gameplay::Visibility::CheckMethod>(method);
    
    // MultiPoint config
    if (config.method == Gameplay::Visibility::CheckMethod::MultiPoint || config.method == Gameplay::Visibility::CheckMethod::Hybrid) {
        CardGap();
        ToggleSwitch("Cabeça", &config.multi_point.check_head);
        ToggleSwitch("Pescoço", &config.multi_point.check_neck);
        ToggleSwitch("Peito", &config.multi_point.check_chest);
        ToggleSwitch("Pelve", &config.multi_point.check_pelvis);
        ToggleSwitch("Membros", &config.multi_point.check_limbs);
        SliderFloat("Mín. Pontos Visíveis", &config.multi_point.min_visible_points, 0.0f, 1.0f, "%.2f");
        SliderInt("Máx. Pontos", &config.multi_point.max_points, 1, 16);
    }
    
    CardGap();
    ToggleSwitch("Ignorar Fumaça", &config.raycast.ignore_smoke);
    ToggleSwitch("Ignorar Vidro", &config.raycast.ignore_glass);
    ToggleSwitch("Ignorar Grades", &config.raycast.ignore_grates);
    ToggleSwitch("Ignorar Folhagem", &config.raycast.ignore_foliage);
    
    CardGap();
    ToggleSwitch("Cache", &config.enable_caching);
    SliderFloat("Duração Cache (s)", &config.cache_duration, 0.01f, 1.0f, "%.3fs");
    SliderInt("Máx. Entradas", &config.max_cache_entries, 100, 10000);
    
    ToggleSwitch("Penetração", &config.check_penetration);
    SliderFloat("Prof. Máx.", &config.max_penetration_depth, 0.0f, 100.0f, "%.1fm");
    SliderInt("Máx. Contagens", &config.max_penetration_count, 0, 5);
    
    EndCard();
}

// ============================================================================
// Bone System Page
// ============================================================================

void DrawBoneSystem() {
    BeginCard("Sistema de Ossos Unificado");
    
    TextLine("Sistema unificado de seleção de ossos/hitboxes para todos os jogos", TextTone::Secondary);
    
    CardGap();
    SectionTitle("Presets de Hitbox");
    
    const char* presets[] = {"Head", "Neck", "Head > Neck", "Upper Body", "Torso", "Center Mass", "Pelvis", "Closest Bone", "Custom"};
    static int preset = 2; // HeadNeck
    Combo("Preset", &preset, presets, 9);
    
    CardGap();
    SectionTitle("Prioridades Personalizadas");
    
    static int custom_bones[8] = {0, 1, 7, 8, 2, 3, 4, 5};
    for (int i = 0; i < 8; ++i) {
        ImGui::PushID(i);
        const char* bones[] = {"Head", "Neck", "Jaw", "LeftEye", "RightEye", "SpineRoot", "SpineLower", "SpineMid", "SpineUpper", "SpineNeck", 
                              "LeftClavicle", "LeftShoulder", "LeftElbow", "LeftWrist", "LeftHand", "LeftThumb", "LeftFingers",
                              "RightClavicle", "RightShoulder", "RightElbow", "RightWrist", "RightHand", "RightThumb", "RightFingers",
                              "LeftHip", "LeftKnee", "LeftAnkle", "LeftFoot", "LeftToes",
                              "RightHip", "RightKnee", "RightAnkle", "RightFoot", "RightToes",
                              "Pelvis", "Root"};
        Combo(("Bone " + std::to_string(i)).c_str(), &custom_bones[i], bones, 36);
        ImGui::PopID();
    }
    
    CardGap();
    ToggleSwitch("Troca Dinâmica", &config.smoothing.dynamic_bone_switch);
    SliderFloat("Dist. Troca", &config.smoothing.bone_switch_distance, 10.0f, 200.0f, "%.0fm");
    
    EndCard();
}

// ============================================================================
// Smooth Curves Page
// ============================================================================

void DrawSmoothCurves() {
    static Gameplay::SmoothCurves::SmoothCurveConfig config = Gameplay::SmoothCurves::SmoothCurveEvaluator::LegitConfig();
    
    BeginCard("Curvas de Suavização");
    
    const char* curve_names[] = {"Linear", "EaseInQuad", "EaseOutQuad", "EaseInOutQuad", "EaseInCubic", "EaseOutCubic", "EaseInOutCubic", "EaseInQuart", "EaseOutQuart", "EaseInOutQuart", "EaseInQuint", "EaseOutQuint", "EaseInOutQuint", "EaseInSine", "EaseOutSine", "EaseInOutSine", "EaseInExpo", "EaseOutExpo", "EaseInOutExpo", "EaseInCirc", "EaseOutCirc", "EaseInOutCirc", "EaseInBack", "EaseOutBack", "EaseInOutBack", "EaseInElastic", "EaseOutElastic", "EaseInOutElastic", "EaseInBounce", "EaseOutBounce", "EaseInOutBounce", "Bezier", "CatmullRom", "Custom"};
    int ct = static_cast<int>(config.type);
    Combo("Tipo", &ct, curve_names, 33);
    config.type = static_cast<Gameplay::SmoothCurves::CurveType>(ct);
    
    if (config.type == Gameplay::SmoothCurves::CurveType::Bezier) {
        CardGap();
        SectionTitle("Editor Bezier");
        // Would draw bezier editor
        TextLine("Editor visual de curva Bezier (clique para editar)", TextTone::Secondary);
    }
    
    if (config.type == Gameplay::SmoothCurves::CurveType::CatmullRom) {
        CardGap();
        SectionTitle("Editor Spline");
        TextLine("Editor de spline Catmull-Rom multi-ponto", TextTone::Secondary);
    }
    
    ToggleSwitch("Curvas por Distância", &config.use_distance_curves);
    if (config.use_distance_curves) {
        CardGap();
        SectionTitle("Curvas por Distância");
        for (int i = 0; i < 4; ++i) {
            ImGui::PushID(i);
            float max_d = config.distance_curves[i].max_distance;
            int ct2 = static_cast<int>(config.distance_curves[i].type);
            ImGui::Text("Até %.0fm:", max_d);
            ImGui::SameLine();
            Combo(("##type" + std::to_string(i)).c_str(), &ct2, curve_names, 33);
            ImGui::PopID();
        }
    }
    
    CardGap();
    ToggleSwitch("Ajuste Dinâmico", &config.dynamic_adjustment);
    SliderFloat("Fator Velocidade", &config.velocity_factor, 0.0f, 0.2f, "%.4f");
    SliderFloat("Fator Erro", &config.error_factor, 0.0f, 0.2f, "%.4f");
    SliderFloat("Fator Distância", &config.distance_factor, 0.0f, 0.1f, "%.5f");
    
    EndCard();
}

// ============================================================================
// Recoil Patterns Page
// ============================================================================

void DrawRecoilPatterns() {
    auto& db = Gameplay::RecoilControl::RecoilPatternDatabase::Instance();
    
    BeginCard("Padrões de Recuo - Banco de Dados");
    
    TextLine("Gerencie padrões de recuo por arma", TextTone::Secondary);
    
    // Search
    static char search[64] = "";
    InputField("##pattern_search", search, sizeof(search), "Buscar arma...");
    
    // Filter by weapon class
    const char* classes[] = {"Todos", "Pistol", "SMG", "Rifle", "Sniper", "Shotgun", "LMG", "Marksman", "Heavy", "Special", "Melee", "Grenade"};
    static int class_filter = 0;
    Combo("Classe", &class_filter, classes, 12);
    
    // List patterns
    auto& db_instance = Gameplay::RecoilControl::RecoilPatternDatabase::Instance();
    auto patterns = db_instance.GetAllPatterns();
    
    BeginSurfaceList("##pattern_list", 300.0f);
    for (const auto& [hash, pattern] : patterns) {
        ImGui::PushID(hash.c_str());
        
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float row_h = 40.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        
        dl->AddRectFilled(pos, ImVec2(pos.x + ImGui::GetContentRegionAvail().x, pos.y + row_h),
            IM_COL32(30, 30, 40, 255), 4.0f);
        
        dl->AddText(ImVec2(pos.x + 10, pos.y + 10), IM_COL32(212, 175, 55, 255), pattern.weapon_name.c_str());
        
        char info[128];
        snprintf(info, sizeof(info), "Class: %s | Vert: %.1f | Horiz: %.1f | Shots: %d",
            Gameplay::RecoilControl::GetWeaponClassName(pattern.weapon_class),
            pattern.vertical_recoil, pattern.horizontal_spread, pattern.shots_in_pattern);
        dl->AddText(ImVec2(pos.x + 10, pos.y + 24), IM_COL32(180, 180, 180, 255), info);
        
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, row_h + 4));
        ImGui::PopID();
    }
    EndSurfaceList();
    
    EndCard();
}

// ============================================================================
// Entity Cache Page
// ============================================================================

void DrawEntityCache() {
    auto& cache = Gameplay::EntityCache::EntityCache::Instance();
    
    BeginCard("Cache de Entidades");
    
    auto stats = cache.GetStats();
    TextLineF(TextTone::Primary, "Total: %zu | Ativas: %zu | Stale: %zu | Memória: %.2f MB",
        stats.total_entities, stats.active_entities, stats.stale_entities, stats.memory_usage_mb);
    
    CardGap();
    SectionTitle("Configuração");
    
    SliderInt("Máx. Entidades", &config.max_entities, 100, 10000);
    SliderFloat("Idade Máx. (s)", &config.max_age, 0.1f, 10.0f, "%.1fs");
    SliderFloat("Intervalo Limpeza (s)", &config.cleanup_interval, 0.1f, 10.0f, "%.1fs");
    ToggleSwitch("Interpolação", &config.enable_interpolation);
    SliderFloat("Tempo Interpolação", &config.interpolation_time, 0.01f, 1.0f, "%.2fs");
    ToggleSwitch("Predição", &config.enable_prediction);
    SliderFloat("Tempo Predição", &config.prediction_time, 0.01f, 0.5f, "%.2fs");
    ToggleSwitch("Thread-Safe", &config.thread_safe);
    SliderInt("Máx. Histórico", &config.max_history_per_entity, 1, 100);
    SliderFloat("Threshold Pos.", &config.position_threshold, 0.01f, 1.0f, "%.2f");
    ToggleSwitch("Comprimir Histórico", &config.compress_history);
    
    CardGap();
    if (CyberButton("Limpar Cache", ImVec2(150, 32))) {
        cache.Clear();
    }
    ImGui::SameLine();
    if (CyberButton("Limpeza Forçada", ImVec2(150, 32))) {
        cache.Cleanup();
    }
    
    EndCard();
}

// ============================================================================
// Profile Manager Page
// ============================================================================

void DrawProfileManager() {
    auto& pm = Gameplay::ProfileManager::ProfileManager::Instance();
    
    BeginCard("Gerenciador de Perfis");
    
    // Game selector
    const char* games[] = {"FiveM", "CS2", "Rust", "Warzone", "Valorant", "Fortnite", "Apex"};
    static int game_sel = 1; // CS2
    Combo("Jogo", &game_sel, games, 7);
    
    auto profiles = pm.GetProfiles(static_cast<Gameplay::ProfileManager::GameType>(game_sel));
    
    if (profiles.empty()) {
        TextLine("Nenhum perfil para este jogo", TextTone::Secondary);
    } else {
        for (auto* profile : profiles) {
            ImGui::PushID(profile->id.c_str());
            bool is_active = pm.GetActiveProfile(static_cast<Gameplay::ProfileManager::GameType>(game_sel)) == profile;
            
            if (ImGui::Selectable(profile->name.c_str(), is_active, ImGuiSelectableFlags_SpanAllColumns)) {
                pm.SetActiveProfile(static_cast<Gameplay::ProfileManager::GameType>(game_sel), profile->id);
            }
            
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "[%s]", 
                Gameplay::ProfileManager::GetProfileTypeName(profile->type));
            
            if (is_active) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.83f, 0.69f, 0.22f, 1.0f), "● ATIVO");
            }
            ImGui::PopID();
        }
    }
    
    CardGap();
    if (CyberButton("Novo Perfil", ImVec2(120, 32))) {
        // Would open create dialog
    }
    ImGui::SameLine();
    if (CyberButton("Importar", ImVec2(100, 32))) {
        // Would open file dialog
    }
    ImGui::SameLine();
    if (CyberButton("Exportar Ativo", ImVec2(140, 32))) {
        // Would export
    }
    
    EndCard();
}

// ============================================================================
// Offset Manager Page
// ============================================================================

void DrawOffsetManager() {
    BeginCard("Gerenciador de Offsets");
    
    TextLine("Gerenciamento automático de offsets com hot-reload", TextTone::Secondary);
    
    CardGap();
    SectionTitle("Configuração");
    
    static Gameplay::Offsets::UpdateConfig config;
    ToggleSwitch("Auto Update", &config.auto_update);
    ToggleSwitch("Hot Reload", &config.hot_reload);
    ToggleSwitch("Validar no Início", &config.validate_on_startup);
    ToggleSwitch("Validar Periodicamente", &config.validate_periodically);
    SliderInt("Intervalo (min)", &config.validation_interval_minutes, 5, 120);
    
    ToggleSwitch("Scan Assinatura", &config.use_signature_scan);
    ToggleSwitch("Cadeia Ponteiros", &config.use_pointer_chain);
    ToggleSwitch("Relativo", &config.use_relative);
    SliderInt("Threads Scan", &config.max_scan_threads, 1, 8);
    SliderInt("Timeout Scan (ms)", &config.scan_timeout_ms, 1000, 30000);
    
    ToggleSwitch("Fallback Offsets", &config.use_fallback_offsets);
    ToggleSwitch("Offsets Comunidade", &config.use_community_offsets);
    ToggleSwitch("Offsets Cacheados", &config.use_cached_offsets);
    ToggleSwitch("Cross Validate", &config.cross_validate);
    SliderFloat("Confiança Mín.", &config.min_confidence, 0.0f, 1.0f, "%.2f");
    ToggleSwitch("Múltiplos Matches", &config.require_multiple_matches);
    
    CardGap();
    if (CyberButton("Atualizar Agora", ImVec2(150, 32))) {
        // Would trigger update
    }
    ImGui::SameLine();
    if (CyberButton("Hot Reload", ImVec2(150, 32))) {
        // Would trigger hot reload
    }
    ImGui::SameLine();
    if (CyberButton("Exportar", ImVec2(120, 32))) {
        // Export offsets
    }
    ImGui::SameLine();
    if (CyberButton("Importar", ImVec2(120, 32))) {
        // Import offsets
    }
    
    EndCard();
}

// ============================================================================
// Resolution Page
// ============================================================================

void DrawResolution() {
    auto& rm = Gameplay::Resolution::ResolutionManager::Instance();
    
    BeginCard("Resolução e Multi-Monitor");
    
    auto monitors = rm.EnumerateMonitors();
    TextLineF(TextTone::Primary, "Monitores detectados: %d", (int)monitors.size());
    
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        char buf[256];
        snprintf(buf, sizeof(buf), "Monitor %zu: %dx%d @ %.1fHz DPI: %.2fx %s",
            i + 1, m.native_resolution.width, m.native_resolution.height,
            m.max_refresh_rate, m.dpi_scale, m.is_primary ? "(Primário)" : "");
        TextLine(buf, TextTone::Primary);
    }
    
    CardGap();
    SectionTitle("World-to-Screen");
    
    auto& wts = rm.GetMutableWorldToScreenConfig();
    ToggleSwitch("Corrigir Aspecto", &wts.correct_aspect);
    ToggleSwitch("Corrigir FOV", &wts.correct_fov);
    ToggleSwitch("Compensar Borderless", &wts.compensate_borderless);
    SliderFloat("Offset X", &wts.x_offset, -500.0f, 500.0f, "%.1f");
    SliderFloat("Offset Y", &wts.y_offset, -500.0f, 500.0f, "%.1f");
    SliderFloat("Escala X", &wts.scale_x, 0.5f, 2.0f, "%.2f");
    SliderFloat("Escala Y", &wts.scale_y, 0.5f, 2.0f, "%.2f");
    
    EndCard();
}

// ============================================================================
// Game Adapter Page
// ============================================================================

void DrawGameAdapter() {
    auto& factory = Gameplay::GameAdapter::AdapterFactory::Instance();
    auto& manager = Gameplay::GameAdapter::AdapterManager::Instance();
    
    BeginCard("Adaptadores de Jogo");
    
    // Current game
    Gameplay::GameAdapter::GameType current = manager.GetCurrentGame();
    const char* game_names[] = {"Desconhecido", "FiveM", "CS2", "Rust", "Warzone", "Valorant", "Fortnite", "Apex"};
    const char* current_name = (current >= 0 && current < 8) ? game_names[static_cast<int>(current)] : "Nenhum";
    
    TextLineF(TextTone::Primary, "Jogo Atual: %s", current_name);
    
    // Available games
    auto available = manager.GetAvailableGames();
    if (!available.empty()) {
        TextLine("Jogos Detectados:", TextTone::Secondary);
        for (auto g : available) {
            const char* name = (g >= 0 && g < 8) ? game_names[static_cast<int>(g)] : "Desconhecido";
            bool is_current = (g == current);
            if (ImGui::Selectable(name, is_current)) {
                manager.AttachToGame(g);
            }
        }
    } else {
        TextLine("Nenhum jogo suportado em execução", TextTone::Warning);
    }
    
    CardGap();
    if (CyberButton("Auto Attach", ImVec2(150, 32))) {
        manager.AutoAttach();
    }
    ImGui::SameLine();
    if (CyberButton("Auto Detach", ImVec2(150, 32))) {
        manager.AutoDetach();
    }
    
    CardGap();
    SectionTitle("Lançador de Jogos");
    
    static char exe_path[260] = "";
    static char work_dir[260] = "";
    static char args[260] = "";
    static char steam_id[64] = "";
    
    InputField("##exe", exe_path, sizeof(exe_path), "Caminho do executável");
    InputField("##dir", work_dir, sizeof(work_dir), "Diretório de trabalho");
    InputField("##args", args, sizeof(args), "Argumentos");
    InputField("##steam", steam_id, sizeof(steam_id), "Steam App ID");
    
    if (CyberButton("Lançar", ImVec2(100, 32))) {
        Gameplay::GameAdapter::GameLauncher::LaunchConfig lc;
        lc.executable_path = exe_path;
        lc.working_directory = work_dir;
        lc.arguments = args;
        lc.steam_app_id = steam_id;
        lc.wait_for_attach = true;
        Gameplay::GameAdapter::GameLauncher::Instance().LaunchGame(lc);
    }
    ImGui::SameLine();
    if (CyberButton("Via Steam", ImVec2(120, 32))) {
        Gameplay::GameAdapter::GameLauncher::Instance().LaunchViaSteam(steam_id, args);
    }
    
EndCard();
}

} // namespace CyberWidgets
