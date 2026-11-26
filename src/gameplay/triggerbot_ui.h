#pragma once

#include "triggerbot_integration.h"
#include <string>

namespace OmniGhost::Gameplay::UI {

// Desenha configuração completa do triggerbot no menu
// Retorna true se alguma config mudou
bool DrawTriggerbotConfig(const char* label, TriggerIntegrationConfig& config, const std::string& game_prefix = "");

// Desenha versão compacta (apenas essenciais)
bool DrawTriggerbotConfigCompact(const char* label, TriggerIntegrationConfig& config);

// Desenha debug info do triggerbot
void DrawTriggerbotDebug(const TriggerIntegration& integration);

} // namespace OmniGhost::Gameplay::UI