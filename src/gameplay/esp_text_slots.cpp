#include "esp_text_slots.h"
#include <algorithm>
#include "../window/theme.h"
#include "../window/icons.h"
#include "../window/fa_icons.h"

namespace OmniGhost::Gameplay::ESPText {

namespace {

// ===== Base Slot Definitions =====
const std::array<SlotMeta, SLOT_COUNT> g_base_slots = {{
    // Name
    { SlotID::Name, "name", "Nome", "Nome do jogador", ICON_FA_USER, true, 0, 6, 2 },
    // Platform
    { SlotID::Platform, "platform", "Plataforma", "Plataforma (PC/Console/Mobile)", ICON_FA_DESKTOP, true, 0, 3, 1 },
    // Rank
    { SlotID::Rank, "rank", "Rank", "Rank/tier do jogador", ICON_FA_TROPHY, true, 0, 4, 1 },
    // K/D Ratio
    { SlotID::KDRatio, "kd", "K/D", "Kill/Death ratio", ICON_FA_CHART_LINE, true, 0, 5, 1 },
    // Win Rate
    { SlotID::WinRate, "winrate", "Winrate", "Taxa de vitórias %", ICON_FA_PERCENTAGE, false, 0, 5, 1 },
    // Battle Pass
    { SlotID::BattlePass, "battlepass", "Passe de Batalha", "Nível do Battle Pass", ICON_FA_MEDAL, false, 0, 5, 1 },
    // Stream Snipe
    { SlotID::StreamSnipe, "streamsnipe", "Stream Sniper", "Alerta de stream sniper", ICON_FA_EYE_SLASH, true, 0, 5, 1 },
    // State
    { SlotID::State, "state", "Estado", "Estado do jogador (building, etc.)", ICON_FA_INFO, true, 3, 1, 1 },
    // Distance
    { SlotID::Distance, "distance", "Distância", "Distância em metros", ICON_FA_ARROWS_ALT_H, true, 1, 5, 2 },
    // Weapon Name
    { SlotID::WeaponName, "weapon", "Arma", "Nome da arma atual", ICON_FA_GUN, true, 1, 1, 1 },
    // Weapon Ammo
    { SlotID::WeaponAmmo, "ammo", "Munição", "Munição atual / total", ICON_FA_BULLET, true, 1, 5, 1 },
    // Weapon Damage
    { SlotID::WeaponDamage, "damage", "Dano", "Dano da arma", ICON_FA_BOLT, true, 1, 4, 1 },
    // Reloading
    { SlotID::Reloading, "reloading", "Recarregando", "Indicador de recarga", ICON_FA_SYNC_ALT, false, 3, 3, 1 },
}};

} // anonymous namespace

const SlotMeta& GetSlotMeta(SlotID id) {
    size_t idx = static_cast<size_t>(id);
    if (idx < SLOT_COUNT) return g_base_slots[idx];
    return g_base_slots[0];
}

const SlotMeta& GetSlotMeta(size_t index) {
    if (index < SLOT_COUNT) return g_base_slots[index];
    return g_base_slots[0];
}

const std::array<SlotMeta, SLOT_COUNT>& GetAllSlots() {
    return g_base_slots;
}

// ===== Render Helpers =====

ImVec2 MeasureText(const char* text, float font_size) {
    ImFont* font = ImGui::GetFont();
    return font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, text);
}

ImU32 GetSlotColor(const SlotConfig& slot, ImU32 default_color) {
    if (slot.color_override_enabled) return slot.color_override;
    return default_color;
}

ImVec2 CalculateAnchorPos(const ImVec2& box_min, const ImVec2& box_max, int anchor, float offset_x, float offset_y) {
    ImVec2 pos;
    switch (anchor) {
        case 0: // Left
            pos = ImVec2(box_min.x, (box_min.y + box_max.y) * 0.5f);
            break;
        case 1: // Right
            pos = ImVec2(box_max.x, (box_min.y + box_max.y) * 0.5f);
            break;
        case 2: // Top
            pos = ImVec2((box_min.x + box_max.x) * 0.5f, box_min.y);
            break;
        case 3: // Bottom
            pos = ImVec2((box_min.x + box_max.x) * 0.5f, box_max.y);
            break;
        case 4: // Center
            pos = ImVec2((box_min.x + box_max.x) * 0.5f, (box_min.y + box_max.y) * 0.5f);
            break;
        default:
            pos = ImVec2(box_min.x, box_min.y);
    }
    return ImVec2(pos.x + offset_x, pos.y + offset_y);
}

float GetFontSize(int size_tier) {
    switch (size_tier) {
        case 0: return 11.5f; // Small
        case 1: return 13.5f; // Medium
        case 2: return 15.0f; // Regular
        default: return 13.5f;
    }
}

// ===== Main Render Function =====

void RenderEntitySlots(ImDrawList* dl,
                       const ESPTextConfig& config,
                       const SlotValueProvider& provider,
                       void* entity_context,
                       const ImVec2& box_min,
                       const ImVec2& box_max,
                       float distance_m) {

    // Coleta slots habilitados e ordena por order
    struct SlotDrawInfo {
        SlotID id;
        SlotConfig config;
        SlotMeta meta;
        std::string text;
        ImVec2 pos;
        float font_size;
        int order;
    };

    std::vector<SlotDrawInfo> to_draw;
    to_draw.reserve(SLOT_COUNT);

    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        SlotID id = static_cast<SlotID>(i);
        const SlotConfig& sc = config.slots[i];
        if (!sc.enabled) continue;

        const SlotMeta& meta = GetSlotMeta(id);

        // Obtém valor
        auto value_opt = provider(id, entity_context);
        if (!value_opt) continue;

        std::string full_text = *value_opt;
        if (meta.icon && sc.show_icon) {
            full_text = std::string(meta.icon) + " " + full_text;
        }

        // Aplica format se definido
        if (!sc.format.empty()) {
            // Simple format: {value} -> actual value
            size_t pos = full_text.find("{value}");
            if (pos != std::string::npos) {
                full_text.replace(pos, 7, *value_opt);
            }
        }

        ImVec2 anchor_pos = CalculateAnchorPos(box_min, box_max, sc.anchor, sc.offset_x, sc.offset_y);
        float fsize = GetFontSize(sc.size);

        to_draw.push_back({
            id, sc, meta, full_text, anchor_pos, fsize, sc.order
        });
    }

    // Ordena por order
    std::sort(to_draw.begin(), to_draw.end(),
              [](const SlotDrawInfo& a, const SlotDrawInfo& b) {
                  return a.order < b.order;
              });

    // Render
    ImFont* font = ImGui::GetIO().Fonts->Fonts[0];
    for (auto& info : to_draw) {
        const SlotConfig& sc = info.config;
        ImU32 text_color = GetSlotColor(sc, IM_COL32(255, 255, 255, 255));
        ImU32 outline_color = IM_COL32(0, 0, 0, 255);

        ImVec2 text_size = font->CalcTextSizeA(info.font_size, FLT_MAX, 0.0f, info.text.c_str());

        // Ajusta posição baseada no anchor
        ImVec2 draw_pos = info.pos;
        switch (sc.anchor) {
            case 0: // Left - alinha à esquerda do box
                draw_pos.x -= 4.0f;
                draw_pos.y -= text_size.y * 0.5f;
                break;
            case 1: // Right - alinha à direita do box
                draw_pos.x += 4.0f + text_size.x;
                draw_pos.y -= text_size.y * 0.5f;
                break;
            case 2: // Top - acima do box
                draw_pos.x -= text_size.x * 0.5f;
                draw_pos.y -= text_size.y + 4.0f;
                break;
            case 3: // Bottom - abaixo do box
                draw_pos.x -= text_size.x * 0.5f;
                draw_pos.y += 4.0f;
                break;
            case 4: // Center
                draw_pos.x -= text_size.x * 0.5f;
                draw_pos.y -= text_size.y * 0.5f;
                break;
        }

        // Background
        if (sc.background_enabled) {
            ImVec2 bg_min = ImVec2(draw_pos.x - sc.background_padding.x, draw_pos.y - sc.background_padding.y);
            ImVec2 bg_max = ImVec2(draw_pos.x + text_size.x + sc.background_padding.x,
                                   draw_pos.y + text_size.y + sc.background_padding.y);
            dl->AddRectFilled(bg_min, bg_max, sc.background_color, 3.0f);
        }

        // Outline
        if (sc.outline_enabled && sc.outline_width > 0) {
            for (int ox = -1; ox <= 1; ++ox) {
                for (int oy = -1; oy <= 1; ++oy) {
                    if (ox == 0 && oy == 0) continue;
                    dl->AddText(font, info.font_size,
                                ImVec2(draw_pos.x + ox * sc.outline_width, draw_pos.y + oy * sc.outline_width),
                                outline_color, info.text.c_str());
                }
            }
        }

        // Main text
        dl->AddText(font, info.font_size, draw_pos, text_color, info.text.c_str());
    }
}

// ===== UI Implementation =====

namespace UI {

bool DrawAnchorSelector(const char* label, int& anchor) {
    const char* anchors[] = { "Left", "Right", "Top", "Bottom", "Center" };
    const char* icons[] = { ICON_FA_ARROW_LEFT, ICON_FA_ARROW_RIGHT, ICON_FA_ARROW_UP, ICON_FA_ARROW_DOWN, ICON_FA_ARROWS_ALT };
    bool changed = false;

    if (ImGui::BeginCombo(label, anchors[anchor])) {
        for (int i = 0; i < 5; ++i) {
            bool selected = (anchor == i);
            if (ImGui::Selectable((std::string(icons[i]) + " " + anchors[i]).c_str(), selected)) {
                anchor = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawSizeSelector(const char* label, int& size) {
    const char* sizes[] = { "Small (11.5)", "Medium (13.5)", "Regular (15)" };
    bool changed = false;

    if (ImGui::BeginCombo(label, sizes[size])) {
        for (int i = 0; i < 3; ++i) {
            if (ImGui::Selectable(sizes[i], size == i)) {
                size = i;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool DrawColorPicker(const char* label, ImU32& color, bool show_alpha) {
    float col[4] = {
        (color & 0xFF) / 255.0f,
        ((color >> 8) & 0xFF) / 255.0f,
        ((color >> 16) & 0xFF) / 255.0f,
        ((color >> 24) & 0xFF) / 255.0f
    };

    ImGuiColorEditFlags flags = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_PickerHueWheel;
    if (show_alpha) flags |= ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;
    else flags |= ImGuiColorEditFlags_NoAlpha;

    bool changed = ImGui::ColorEdit4(label, col, flags);
    if (changed) {
        color = IM_COL32(static_cast<int>(col[0] * 255), static_cast<int>(col[1] * 255),
                         static_cast<int>(col[2] * 255), static_cast<int>(col[3] * 255));
    }
    return changed;
}

bool DrawSlotConfig(ESPTextConfig& config, SlotID id, const ImVec2&) {
    const SlotMeta& meta = GetSlotMeta(id);
    SlotConfig& sc = config.slots[static_cast<size_t>(id)];
    bool any_changed = false;

    ImGui::PushID(static_cast<int>(id));

    // Header com toggle
    bool enabled = sc.enabled;
    if (ImGui::Checkbox(std::string("##enable_").append(meta.key).c_str(), &enabled)) {
        sc.enabled = enabled;
        any_changed = true;
    }
    ImGui::SameLine();

    ImGui::Text("%s %s", meta.icon ? meta.icon : "", meta.label);
    if (ImGui::IsItemHovered() && meta.description) {
        ImGui::SetTooltip("%s", meta.description);
    }

    if (!enabled) {
        ImGui::PopID();
        return any_changed;
    }

    ImGui::Indent(16.0f);

    // Anchor
    any_changed |= DrawAnchorSelector("Anchor", sc.anchor);
    ImGui::SameLine();

    // Order
    int order = sc.order;
    if (ImGui::DragInt("Order", &order, 0.1f, -100, 100)) {
        sc.order = order;
        any_changed = true;
    }

    // Size
    any_changed |= DrawSizeSelector("Size", sc.size);

    // Color Override
    bool color_override = sc.color_override_enabled;
    if (ImGui::Checkbox("Color Override", &color_override)) {
        sc.color_override_enabled = color_override;
        any_changed = true;
    }
    if (color_override) {
        ImGui::SameLine();
        any_changed |= DrawColorPicker("##color", sc.color_override, true);
    }

    // Outline
    bool outline = sc.outline_enabled;
    if (ImGui::Checkbox("Outline", &outline)) {
        sc.outline_enabled = outline;
        any_changed = true;
    }
    if (outline) {
        ImGui::SameLine();
        float w = sc.outline_width;
        if (ImGui::DragFloat("Width", &w, 0.1f, 0.5f, 5.0f)) {
            sc.outline_width = w;
            any_changed = true;
        }
    }

    // Background
    bool bg = sc.background_enabled;
    if (ImGui::Checkbox("Background", &bg)) {
        sc.background_enabled = bg;
        any_changed = true;
    }
    if (bg) {
        ImGui::SameLine();
        any_changed |= DrawColorPicker("##bg", sc.background_color, true);
    }

    // Offsets
    ImGui::DragFloat2("Offset", &sc.offset_x, 0.5f);

    // Format string
    char fmt_buf[64];
    strncpy(fmt_buf, sc.format.c_str(), sizeof(fmt_buf) - 1);
    fmt_buf[sizeof(fmt_buf) - 1] = 0;
    if (ImGui::InputText("Format", fmt_buf, sizeof(fmt_buf))) {
        sc.format = fmt_buf;
        any_changed = true;
    }

    // Show icon
    bool show_icon = sc.show_icon;
    if (ImGui::Checkbox("Show Icon", &show_icon)) {
        sc.show_icon = show_icon;
        any_changed = true;
    }

    ImGui::Unindent(16.0f);
    ImGui::Separator();

    ImGui::PopID();
    return any_changed;
}

void DrawSlotEditor(const char* label, ESPTextConfig& config, const ImVec2& size) {
    ImGui::BeginChild(label, size, true);

    // Toolbar
    if (ImGui::Button(ICON_FA_EYE " Toggle All")) {
        bool any_enabled = false;
        for (auto& sc : config.slots) if (sc.enabled) { any_enabled = true; break; }
        for (auto& sc : config.slots) sc.enabled = !any_enabled;
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_UNDO " Reset Defaults")) {
        config = ESPTextConfig();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu slots)", SLOT_COUNT);

    ImGui::Separator();

    // Lista de slots
    for (size_t i = 0; i < SLOT_COUNT; ++i) {
        SlotID id = static_cast<SlotID>(i);
        DrawSlotConfig(config, id);
    }

    ImGui::EndChild();
}

void DrawSlotPreview(const ESPTextConfig& config, const ImVec2& box_size) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    // Mock box
    ImVec2 box_min = pos;
    ImVec2 box_max = ImVec2(pos.x + box_size.x, pos.y + box_size.y);

    dl->AddRect(box_min, box_max, IM_COL32(255, 180, 0, 255), 0, 0, 2.0f);

    // Mock provider que retorna valores de exemplo
    auto mock_provider = [](SlotID id, void*) -> std::optional<std::string> {
        switch (id) {
            case SlotID::Name: return "PlayerName";
            case SlotID::Platform: return "PC";
            case SlotID::Rank: return "Diamond III";
            case SlotID::KDRatio: return "2.45";
            case SlotID::WinRate: return "68%";
            case SlotID::BattlePass: return "Lv. 156";
            case SlotID::StreamSnipe: return "SUSPECT";
            case SlotID::State: return "Building";
            case SlotID::Distance: return "124m";
            case SlotID::WeaponName: return "SCAR";
            case SlotID::WeaponAmmo: return "30/120";
            case SlotID::WeaponDamage: return "36";
            case SlotID::Reloading: return "RELOADING";
            default: return std::nullopt;
        }
    };

    RenderEntitySlots(dl, config, mock_provider, nullptr, box_min, box_max, 124.0f);

    ImGui::Dummy(box_size);
}

} // namespace UI

// ===== Game-Specific Slots =====

namespace GameSlots {

const std::vector<SlotMeta> g_fortnite_slots = {
    { static_cast<SlotID>(FortniteSlotID::BuildingMaterial), "mats", "Materiais", "Madeira/Tijolo/Metal", ICON_FA_HAMMER, true, 3, 2, 1 },
    { static_cast<SlotID>(FortniteSlotID::Shield), "shield", "Escudo", "Vida de escudo", ICON_FA_SHIELD_ALT, true, 1, 2, 1 },
    { static_cast<SlotID>(FortniteSlotID::Ping), "ping", "Ping", "Latência do jogador", ICON_FA_WIFI, false, 1, 6, 0 },
    { static_cast<SlotID>(FortniteSlotID::EliminationCount), "elims", "Elims", "Eliminações na partida", ICON_FA_SKULL, true, 1, 3, 1 },
    { static_cast<SlotID>(FortniteSlotID::CreativeMode), "creative", "Criativo", "Indicador modo criativo", ICON_FA_PAINT_BRUSH, false, 3, 4, 0 },
};

const std::vector<SlotMeta> g_cs2_slots = {
    { static_cast<SlotID>(CS2SlotID::Armor), "armor", "Armadura", "Kevlar + Helmet", ICON_FA_SHIELD_ALT, true, 1, 2, 1 },
    { static_cast<SlotID>(CS2SlotID::DefuseKit), "defuse", "Kit Desarme", "Possui defuse kit", ICON_FA_BOMB, true, 3, 4, 0 },
    { static_cast<SlotID>(CS2SlotID::ScopeZoom), "zoom", "Zoom", "Nível de zoom da sniper", ICON_FA_SEARCH, false, 1, 5, 0 },
    { static_cast<SlotID>(CS2SlotID::FlashDuration), "flash", "Flash", "Tempo restante de flash", ICON_FA_EYE, false, 3, 5, 0 },
};

const std::vector<SlotMeta>& GetGameSlots(uint32_t game_hash) {
    // Hash simples baseado no nome do jogo
    // Em produção, usar enum ActiveGame
    if (game_hash == 0x466E6974) return g_fortnite_slots; // "Fort" hash
    if (game_hash == 0x435332) return g_cs2_slots;       // "CS2" hash
    static const std::vector<SlotMeta> empty;
    return empty;
}

} // namespace GameSlots

} // namespace OmniGhost::Gameplay::ESPText
