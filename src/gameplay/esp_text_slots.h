#pragma once

#include <string>
#include <vector>
#include <array>
#include <optional>
#include <cstdint>
#include <functional>
#include "src/project/third_party/ImGui/imgui.h"

namespace OmniGhost::Gameplay::ESPText {

// ===== Slot Definition =====
enum class SlotID : uint8_t {
    Name = 0,           // Player name
    Platform,           // Platform icon (PC/Console/Mobile)
    Rank,               // Rank/tier
    KDRatio,            // K/D ratio
    WinRate,            // Win rate %
    BattlePass,         // Battle pass level
    StreamSnipe,        // Stream sniper warning
    State,              // Player state (building, swimming, etc.)
    Distance,           // Distance to player
    WeaponName,         // Current weapon name
    WeaponAmmo,         // Weapon ammo
    WeaponDamage,       // Weapon damage
    Reloading,          // Reloading indicator
    COUNT               // Sempre último
};

inline constexpr size_t SLOT_COUNT = static_cast<size_t>(SlotID::COUNT);

// ===== Slot Metadata =====
struct SlotMeta {
    SlotID id;
    const char* key;          // Key interno (ex: "name")
    const char* label;        // Label para UI (ex: "Nome")
    const char* description;  // Tooltip descrição
    const char* icon;         // FontAwesome icon
    bool enabled_by_default;
    int default_anchor;       // 0=left, 1=right, 2=top, 3=bottom, 4=center
    int default_order;        // Draw order (lower = drawn first)
    int default_size;         // 0=small(11.5), 1=medium(13.5), 2=regular(15)
};

// Retorna metadata para um slot
[[nodiscard]] const SlotMeta& GetSlotMeta(SlotID id);
[[nodiscard]] const SlotMeta& GetSlotMeta(size_t index);
[[nodiscard]] const std::array<SlotMeta, SLOT_COUNT>& GetAllSlots();

// ===== Slot Configuration =====
struct SlotConfig {
    bool enabled = true;
    bool color_override_enabled = false;
    ImU32 color_override = IM_COL32(255, 255, 255, 255); // RGBA
    bool outline_enabled = true;
    float outline_width = 1.0f;
    bool background_enabled = false;
    ImU32 background_color = IM_COL32(0, 0, 0, 180);
    int anchor = 0;           // 0=left, 1=right, 2=top, 3=bottom, 4=center
    int order = 0;            // Draw order
    int size = 1;             // 0=small, 1=medium, 2=regular
    float offset_x = 0.0f;    // Offset customizado
    float offset_y = 0.0f;
    std::string format;       // Format string (ex: "{value}m", "{value} HP")
    bool show_icon = true;
};

// Configuração completa de todos os slots
struct ESPTextConfig {
    std::array<SlotConfig, SLOT_COUNT> slots;

    ESPTextConfig() {
        // Inicializa com defaults
        for (size_t i = 0; i < SLOT_COUNT; ++i) {
            const auto& meta = GetSlotMeta(static_cast<SlotID>(i));
            slots[i].enabled = meta.enabled_by_default;
            slots[i].anchor = meta.default_anchor;
            slots[i].order = meta.default_order;
            slots[i].size = meta.default_size;
        }
    }
};

// ===== Slot Renderer =====
struct SlotRenderData {
    SlotID id;
    std::string text;
    ImVec2 position;          // Posição base (anchor point)
    ImVec2 size;              // Tamanho do texto
    ImU32 color;              // Cor final (com override se enabled)
    ImU32 outline_color;
    float outline_width;
    bool has_background;
    ImU32 background_color;
    ImVec2 background_padding = ImVec2(4, 2);
    float rotation = 0.0f;
};

// Callback para obter valor do slot em runtime
using SlotValueProvider = std::function<std::optional<std::string>(SlotID, void* entity_context)>;

// Renderiza todos os slots habilitados para uma entidade
void RenderEntitySlots(ImDrawList* dl,
                       const ESPTextConfig& config,
                       const SlotValueProvider& provider,
                       void* entity_context,
                       const ImVec2& box_min,
                       const ImVec2& box_max,
                       float distance_m);

// ===== UI Helpers =====
namespace UI {

// Desenha editor completo de slots (para menu de config)
void DrawSlotEditor(const char* label, ESPTextConfig& config, const ImVec2& size = ImVec2(400, 500));

// Desenha preview dos slots
void DrawSlotPreview(const ESPTextConfig& config, const ImVec2& box_size = ImVec2(100, 200));

// Desenha configuração de um slot individual
bool DrawSlotConfig(ESPTextConfig& config, SlotID id, const ImVec2& size = ImVec2(0, 0));

// Selector de anchor (visual)
bool DrawAnchorSelector(const char* label, int& anchor);

// Selector de size tier
bool DrawSizeSelector(const char* label, int& size);

// Color picker com alpha
bool DrawColorPicker(const char* label, ImU32& color, bool show_alpha = true);

} // namespace UI

// ===== Game-Specific Extensions =====
// Cada jogo pode estender com slots próprios
namespace GameSlots {

// Fortnite slots adicionais (além dos base)
enum class FortniteSlotID : uint8_t {
    BaseOffset = static_cast<uint8_t>(SlotID::COUNT),
    BuildingMaterial = BaseOffset,
    Shield,
    Ping,
    EliminationCount,
    CreativeMode,
    COUNT
};

// CS2 slots adicionais
enum class CS2SlotID : uint8_t {
    BaseOffset = static_cast<uint8_t>(SlotID::COUNT),
    Armor,
    DefuseKit,
    ScopeZoom,
    FlashDuration,
    COUNT
};

// Retorna metadata estendida para jogo específico
[[nodiscard]] const std::vector<SlotMeta>& GetGameSlots(uint32_t game_hash);

} // namespace GameSlots

} // namespace OmniGhost::Gameplay::ESPText