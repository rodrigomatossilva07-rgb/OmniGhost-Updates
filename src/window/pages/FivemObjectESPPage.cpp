#include "../widgets.h"
#include "../theme.h"
#include "../localization.h"
#include "../../Fivem/object_esp/object_esp.h"
#include "../../Fivem/object_esp/object_esp_config.h"
#include <string>
#include <vector>
#include <algorithm>

namespace CyberWidgets {

static void DrawObjectInspector() {
    using namespace object_esp;
    auto& manager = GetObjectESPManager();
    const auto* inspector = manager.GetInspectorData();
    if (!inspector || !manager.IsInspectorOpen()) return;
    
    bool inspector_open = manager.IsInspectorOpen();
    ImGui::SetNextWindowSize(ImVec2(400, 550), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Object Inspector", &inspector_open, ImGuiWindowFlags_NoCollapse)) {
        if (!inspector_open) manager.CloseInspector();
        ImGui::Text("Model: %s", inspector->model.c_str());
        ImGui::Separator();
        
        // Custom display name editor
        static char display_name_buf[256] = "";
        if (ImGui::InputText("Display Name", display_name_buf, sizeof(display_name_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
            manager.SetCustomDisplayName(inspector->model, display_name_buf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Name")) {
            manager.SetCustomDisplayName(inspector->model, display_name_buf);
        }
        
        if (!inspector->display_name.empty() && inspector->display_name != inspector->model) {
            ImGui::Text("Current Display Name: %s", inspector->display_name.c_str());
        }
        
        ImGui::Separator();
        
        ImGui::Text("Hash: 0x%08X (%u)", inspector->hash, inspector->hash);
        ImGui::Text("Distance: %.1fm", inspector->distance);
        ImGui::Text("Position: X=%.1f Y=%.1f Z=%.1f", 
            inspector->position.x, inspector->position.y, inspector->position.z);
        
        if (inspector->networked) {
            ImGui::Text("Network ID: %u", inspector->network_id);
            ImGui::Text("Entity Handle: 0x%llX", (unsigned long long)inspector->entity_handle);
            ImGui::Text("Networked: Yes");
        }
        
        if (!inspector->extra_props.empty()) {
            ImGui::Separator();
            ImGui::Text("Extra Properties:");
            for (const auto& prop : inspector->extra_props) {
                ImGui::Text("  %s: %s", prop.first.c_str(), prop.second.c_str());
            }
        }
        
        ImGui::Separator();
        if (CyberButton("Close", ImVec2(80, 30))) {
            manager.CloseInspector();
        }
    }
    ImGui::End();
}

void DrawFivemObjectESP() {
    using namespace object_esp;
    auto& manager = GetObjectESPManager();
    Config& esp_config = manager.GetMutableConfig();
    const auto& state = manager.GetScannerState();
    const auto& scan_results = manager.GetFilteredResults();
    auto whitelist = manager.GetFilteredWhitelist();
    
    BeginCard("Object ESP");
    
    if (!manager.IsInitialized())
        manager.Initialize();

    // Draw inspector if open
    DrawObjectInspector();

    if (!manager.HasValidatedDiscoverySource()) {
        TextLine("Object pool nao validado — entra no servidor FiveM (build b3258+).", TextTone::Warning);
        TextLine("Quando a sessao FiveM estiver OK, usa 'Escanear' para descobrir props.", TextTone::Secondary);
    }
    
    // Main toggle - simple toggle like ESP
    ToggleSwitch("Ativar Object ESP", &esp_config.enabled);
    
    if (!esp_config.enabled) {
        TextLine("Object ESP desativado. Ative para configurar.", TextTone::Secondary);
        EndCard();
        return;
    }

    CardGap();
    SectionTitle("Scanner");
    
    // Scan controls
    ImGui::Columns(3, "scan_controls", false);
    ImGui::SetColumnWidth(0, 100);
    ImGui::SetColumnWidth(1, 180);
    ImGui::Text("Raio (m):");
    ImGui::NextColumn();
    ImGui::SetNextItemWidth(ImGui::GetColumnWidth() * 0.5f);
    SliderFloat("##scan_radius", &esp_config.scan_radius, 50.0f, 2000.0f, "%.0fm");
    ImGui::NextColumn();
    
    ImGui::Text("Intervalo (ms):");
    ImGui::NextColumn();
    ImGui::SetNextItemWidth(ImGui::GetColumnWidth() * 0.5f);
    SliderInt("##scan_interval", &esp_config.scan_interval_ms, 100, 60000, "%d ms");
    ImGui::NextColumn();
    
    ImGui::Text("Auto-scan:");
    ImGui::NextColumn();
    ToggleSwitch("##auto_scan", &esp_config.auto_scan);
    ImGui::NextColumn();
    ImGui::Columns(1);
    
    // Scan button and status
    if (state.scanning) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Scanning... %.0f%%", state.scan_progress * 100.0f);
    } else if (state.scan_complete) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Scan completo: %d modelos, %d objetos", 
            state.unique_models_found, state.total_objects_found);
        if (state.error_message.empty() == false) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Erro: %s", state.error_message.c_str());
        }
    } else {
        ImGui::Text("Pronto para escanear");
    }
    
    ImGui::SameLine();
    if (!state.scanning) {
        if (CyberButton("Escanear Agora", ImVec2(120, 30))) {
            object_esp::GetObjectESPManager().StartScan(esp_config.scan_radius);
        }
    } else {
        if (CyberButton("Parar Scan", ImVec2(120, 30))) {
            object_esp::GetObjectESPManager().StopScan();
        }
    }
    
    ImGui::SameLine();
    if (CyberButton("Limpar Resultados", ImVec2(120, 30))) {
        object_esp::GetObjectESPManager().ClearScanResults();
    }
    
    CardGap();
    SectionTitle("Resultados do Scan");
    
    // Search and category filter
    ImGui::Columns(3, "scan_filters", false);
    ImGui::SetColumnWidth(0, 180);
    ImGui::SetColumnWidth(1, 150);
    ImGui::Text("Filtrar:");
    ImGui::NextColumn();
    static char search_buf[256] = "";
    ImGui::InputText("##obj_search", search_buf, sizeof(search_buf));
    object_esp::GetObjectESPManager().SetSearchQuery(search_buf);
    ImGui::NextColumn();
    
    ImGui::Text("Categoria:");
    ImGui::NextColumn();
    static int cat_idx = 0;
    const char* cat_names[] = {"Todas", "Loot", "Missao", "Interacao", "Policial", "Medico",
        "Veiculo", "Crafting", "Oficina", "Trabalho", "Container", "Custom", "Outros"};
    if (Combo("##cat_filter", &cat_idx, cat_names, 13)) {
        object_esp::GetObjectESPManager().SetCategoryFilter(static_cast<ObjectCategory>(cat_idx));
    }
    ImGui::NextColumn();
    ImGui::Columns(1);
    
    // Scan results table
    if (!scan_results.empty()) {
        ImGui::Separator();
        BeginSurfaceList("##scan_results", 250.0f);
        
        ImGui::TableSetupColumn("Modelo", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Distancia", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Custom", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Acao", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();
        
        for (const auto& result : scan_results) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", result.model.c_str());
            
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d", result.count);
            
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.0fm", result.nearest_distance);
            
            ImGui::TableSetColumnIndex(3);
            if (result.is_custom) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "CUSTOM");
            } else {
                ImGui::Text("-");
            }
            
            ImGui::TableSetColumnIndex(4);
            bool in_whitelist = std::any_of(
                object_esp::GetObjectESPManager().GetWhitelist().begin(),
                object_esp::GetObjectESPManager().GetWhitelist().end(),
                [&result](const WhitelistEntry& e) { return e.model == result.model; });
            
            if (!in_whitelist) {
                if (CyberButton(("Adicionar##" + result.model).c_str(), ImVec2(80, 25))) {
                    object_esp::GetObjectESPManager().AddToWhitelist(
                        result.model, result.hash, result.model, result.category, result.is_custom);
                }
            } else {
                TextLine("Ja adicionado", TextTone::Success);
            }
        }
        EndSurfaceList();
    } else {
        TextLine("Nenhum objeto encontrado. Clique em 'Escanear Agora'.", TextTone::Secondary);
    }
    
    CardGap();
    SectionTitle("Objetos Rastreados (Whitelist)");
    
    if (!whitelist.empty()) {
        ImGui::Columns(5, "whitelist_cols", false);
        ImGui::SetColumnWidth(0, 160); // Name
        ImGui::SetColumnWidth(1, 100); // Category
        ImGui::SetColumnWidth(2, 80);  // Distance
        ImGui::SetColumnWidth(3, 80);  // Display options
        ImGui::SetColumnWidth(4, 140); // Actions
        
        ImGui::Text("Modelo / Nome");
        ImGui::NextColumn();
        ImGui::Text("Categoria");
        ImGui::NextColumn();
        ImGui::Text("Dist Max");
        ImGui::NextColumn();
        ImGui::Text("Exibir");
        ImGui::NextColumn();
        ImGui::Text("Acoes");
        ImGui::NextColumn();
        ImGui::Separator();
        
        for (auto& entry : whitelist) {
            ImGui::PushID(entry.model.c_str());
            
            // Name with custom display name support
            std::string display = entry.display_name.empty() ? entry.model : entry.display_name;
            ImGui::Text("%s", display.c_str());
            if (entry.is_custom) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), " [CUSTOM]");
            }
            ImGui::NextColumn();
            ImGui::Text("%s", ObjectCategoryToString(entry.category));
            ImGui::NextColumn();
            
            ImGui::SetNextItemWidth(70);
            SliderFloat("##dist", &entry.max_distance, 10.0f, 5000.0f, "%.0fm");
            ImGui::NextColumn();
            
            // Display toggles
            ImGui::Columns(4, "display_opts", false);
            ImGui::SetColumnWidth(0, 30);
            ImGui::SetColumnWidth(1, 30);
            ImGui::SetColumnWidth(2, 30);
            ImGui::SetColumnWidth(3, 30);
            ImGui::PushID("disp");
            ToggleSwitch("##name", &entry.show_name); ImGui::SameLine(); ImGui::Text("N");
            ImGui::NextColumn();
            ToggleSwitch("##dist", &entry.show_distance); ImGui::SameLine(); ImGui::Text("D");
            ImGui::NextColumn();
            ToggleSwitch("##cat", &entry.show_category); ImGui::SameLine(); ImGui::Text("C");
            ImGui::NextColumn();
            ToggleSwitch("##box", &entry.show_box); ImGui::SameLine(); ImGui::Text("B");
            ImGui::NextColumn();
            ImGui::PopID();
            ImGui::Columns(1);
            ImGui::NextColumn();
            
            // Actions
            if (entry.enabled) {
                if (CyberButton("Desativar", ImVec2(70, 25))) {
                    object_esp::GetObjectESPManager().ToggleWhitelistEntry(entry.model, false);
                }
            } else {
                if (CyberButton("Ativar", ImVec2(70, 25))) {
                    object_esp::GetObjectESPManager().ToggleWhitelistEntry(entry.model, true);
                }
            }
            ImGui::SameLine();
            if (CyberButton("Remover", ImVec2(70, 25))) {
                object_esp::GetObjectESPManager().RemoveFromWhitelist(entry.model);
            }
            ImGui::SameLine();
            if (CyberButton("Config", ImVec2(60, 25))) {
                object_esp::GetObjectESPManager().OpenInspector(entry.model);
            }
            
            ImGui::NextColumn();
            ImGui::PopID();
        }
        ImGui::Columns(1);
    } else {
        TextLine("Nenhum objeto na whitelist. Adicione objetos do scan acima.", TextTone::Secondary);
    }
    
    CardGap();
    SectionTitle("Configuracoes de Exibicao");
    
    ImGui::Columns(2, nullptr, false);
    ToggleSwitch("Mostrar Nome", &esp_config.show_name);
    ImGui::NextColumn();
    ToggleSwitch("Mostrar Distancia", &esp_config.show_distance);
    ImGui::NextColumn();
    ToggleSwitch("Mostrar Categoria", &esp_config.show_category);
    ImGui::NextColumn();
    ToggleSwitch("Mostrar Box", &esp_config.show_box);
    ImGui::NextColumn();
    ToggleSwitch("Mostrar Marker", &esp_config.show_marker);
    ImGui::NextColumn();
    ToggleSwitch("Culling por Distancia", &esp_config.distance_culling);
    ImGui::NextColumn();
    ToggleSwitch("Culling Frustum", &esp_config.frustum_culling);
    ImGui::NextColumn();
    ImGui::Columns(1);
    
    ImGui::Separator();
    SliderFloat("Escala Texto", &esp_config.text_scale, 0.5f, 2.5f, "%.1f");
    SliderFloat("Espessura Box", &esp_config.box_thickness, 1.0f, 5.0f, "%.1f");
    SliderFloat("Distancia Maxima", &esp_config.max_distance, 50.0f, 5000.0f, "%.0fm");
    
    CardGap();
    SectionTitle("Persistencia");
    
    ImGui::Columns(2, "persist", false);
    if (CyberButton("Salvar Tudo", ImVec2(-1, 32))) {
        object_esp::GetObjectESPManager().SaveAll();
    }
    ImGui::NextColumn();
    if (CyberButton("Carregar Tudo", ImVec2(-1, 32))) {
        object_esp::GetObjectESPManager().LoadAll();
    }
    ImGui::Columns(1);
    
    // Stats
    CardGap();
    const auto& stats = manager.GetStats();
    ImGui::Text("Estatisticas: Scanned=%d | Tracked=%d | Rendered=%d | ScanTime=%.1fms | UpdateTime=%.1fms", 
        stats.total_scanned, stats.tracked_objects, stats.rendered_objects, 
        stats.last_scan_time_ms, stats.update_time_ms);
    
    EndCard();
}

} // namespace CyberWidgets