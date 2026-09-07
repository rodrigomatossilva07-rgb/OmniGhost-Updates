#include "../../widgets.h"
#include "../../theme.h"
#include "../../localization.h"
#include "../../Rust/rust_config.h"
#include "../../Rust/rust_game.h"
#include "imgui.h"

void DrawRustWorld() {
    auto& c = Rust::config;

    CyberWidgets::BeginCardRow(2);
    CyberWidgets::BeginCard("Configurações de ESP de Mundo");
    CyberWidgets::ToggleSwitch("Ativar ESP de Mundo", &c.world_esp);
    ImGui::TextUnformatted("Tecla de Toggle");
    ImGui::SameLine(180.f);
    ImGui::TextDisabled(c.world_toggle_key > 0 ? "definida" : "NENHUM");
    ImGui::TextUnformatted("Tecla de Ciclo de Distância");
    ImGui::SameLine(180.f);
    ImGui::TextDisabled(c.world_distance_cycle_key > 0 ? "definida" : "NENHUM");
    ImGui::SliderInt("Distância máxima", &c.world_max_distance, 50, 1000, "%d m");
    CyberWidgets::ToggleSwitch("Mostrar nome", &c.world_show_name);
    CyberWidgets::ToggleSwitch("Mostrar distância", &c.world_show_distance);
    ImGui::TextDisabled("Runtime: world=%d  list=%s",
        Rust::runtime.world_count, Rust::runtime.list_ok ? "OK" : "wait");
    CyberWidgets::EndCard();

    CyberWidgets::NextCardColumn();
    CyberWidgets::BeginCard("Categorias");
    CyberWidgets::ToggleSwitch("Ores", &c.ore_esp);
    CyberWidgets::ToggleSwitch("Crates", &c.crate_esp);
    CyberWidgets::ToggleSwitch("Stash", &c.stash_esp);
    CyberWidgets::ToggleSwitch("Tool cupboard (TC)", &c.tc_esp);
    CyberWidgets::ToggleSwitch("Turrets / traps", &c.turret_esp);
    CyberWidgets::ToggleSwitch("Veículos", &c.vehicle_esp);
    CyberWidgets::ToggleSwitch("Animais", &c.animal_esp);
    CyberWidgets::ToggleSwitch("AirDrop / Heli", &c.airdrop_esp);
    CyberWidgets::ToggleSwitch("Collectables", &c.collectables_esp);
    CyberWidgets::ToggleSwitch("Item drops", &c.item_drops_esp);
    CyberWidgets::ToggleSwitch("Corpos / bodybags", &c.corpse_esp);
    CyberWidgets::EndCard();
    CyberWidgets::EndCardRow();

    CyberWidgets::BeginCard("Itens do mundo (filtro fino)");
    ImGui::TextDisabled("Só desenha se a categoria pai estiver ligada OU se 'ESP de Mundo' estiver ativo.");
    if (ImGui::BeginTable("##world_items", 3, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("AirDrop", &c.w_airdrop);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Attack Heli", &c.w_attackheli);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Bradley", &c.w_bradley);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Barrel", &c.w_barrel);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Oil barrel", &c.w_oil_barrel);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Basic crate", &c.w_basiccrate);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Elite crate", &c.w_elite_crate);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Military crate", &c.w_military_crate);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch(Loc::Tr("rust.world.hackable_crate"), &c.w_hackable_crate);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Locked crate", &c.w_locked_crate);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Buried stash", &c.w_buriedstash);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Tool cupboard", &c.w_toolcupboard);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Auto turret", &c.w_autoturret);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Shotgun trap", &c.w_shotguntrap);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Flame turret", &c.w_flameturret);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Sulfur ore", &c.w_sulfur);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Metal ore", &c.w_metal_ore);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Stone ore", &c.w_stone_ore);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Hemp", &c.w_hemp);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Blueberry", &c.w_blueberry);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Wood pile", &c.w_wood_pile);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Diesel", &c.w_diesel);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Minicopter", &c.w_minicopter);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Scrap heli", &c.w_scrapheli);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Rowboat", &c.w_rowboat);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("RHIB", &c.w_rhib);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Bike", &c.w_bike);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Horse", &c.w_horse);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Bear", &c.w_bear);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Boar", &c.w_boar);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Wolf", &c.w_wolf);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Stag", &c.w_stag);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Chicken", &c.w_chicken);

        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Body bag", &c.w_bodybag);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Coffin", &c.w_coffin);
        ImGui::TableNextColumn(); CyberWidgets::ToggleSwitch("Blueprint frag", &c.w_blueprint);
        ImGui::EndTable();
    }
    CyberWidgets::EndCard();

    CyberWidgets::BeginCard("NOTA");
    ImGui::TextWrapped(
        "Offsets atualizados (BN 0x115B1A70, MC 0x115BB318, IL2CPP 0x11A88F30) + decrypt "
        "client_entities/entity_list do dump atual. O ESP de mundo percorre a lista BaseNetworkable "
        "(até 1024 slots) e classifica pelo nome da classe Il2Cpp. Com lista legível, world=N no status.");
    CyberWidgets::EndCard();
}
