#include "../widgets.h"
#include "../theme.h"
#include "../localization.h"
#include "../../Fivem/object_esp.h"
#include "../../Fivem/object_esp_config.h"
#include <string>
#include <vector>

namespace CyberWidgets {

void DrawFivemObjectESP() {
    using namespace object_esp;
    auto& manager = GetObjectESPManager();
    auto& esp_config = manager.GetMutableConfig();
    const auto& state = manager.GetScannerState();
    const auto& scan_results = manager.GetFilteredResults();
    auto whitelist = manager.GetFilteredWhitelist();
    
    BeginCard("Object ESP - FiveM");
    
    // Main toggle
    ToggleSwitch("Ativar Object ESP", &esp_config.enabled);
    
    if (!esp_config.enabled) {
        TextLine("Object ESP desativado. Ative para configurar.", TextTone::Secondary);
        EndCard();
        return;
    }
    
    CardGap();
    SectionTitle("Scanner");
    
    // Scan controls
    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 120);
    ImGui::Text("Raio (m):");
    ImGui::NextColumn();
    SliderFloat("##scan_radius", &esp_config.scan_radius, 50.0f, 2000.0f, "%.0fm");
    ImGui::NextColumn();
    
ImGui::Text("Intervalo (ms):");
    ImGui::NextColumn();
    InputInt("##scan_interval", &esp_config.scan_interval_ms);
    ImGui::NextColumn();
    
    ImGui::Text("Auto-scan:");
    ImGui::NextColumn();
    ToggleSwitch("##auto_scan", &esp_config.auto_scan);
    ImGui::NextColumn();
    ImGui::Columns(1);
    
    // Scan button
    if (state.scanning) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Scanning... %.0f%%", state.scan_progress * 100.0f);
    } else if (state.scan_complete) {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Scan completo: %d modelos, %d objetos", 
            state.unique_models_found, state.total_objects_found);
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
    
    if (CyberButton("Limpar Resultados", ImVec2(120, 30))) {
        object_esp::GetObjectESPManager().ClearScanResults();
    }
    
    CardGap();
    SectionTitle("Resultados do Scan");
    
    // Search filter
    ImGui::Text("Filtrar:");
    ImGui::SameLine();
    static char search_buf[256] = "";
    ImGui::InputText("##obj_search", search_buf, sizeof(search_buf));
    object_esp::GetObjectESPManager().SetSearchQuery(search_buf);
    
    // Category filter
    ImGui::SameLine();
    ImGui::Text("Categoria:");
    ImGui::SameLine();
    static int cat_idx = 0;
    const char* cat_names[] = {"Todas", "Loot", "Missao", "Interacao", "Policial", "Medico",
        "Veiculo", "Crafting", "Oficina", "Trabalho", "Container", "Custom", "Outros"};
    if (Combo("##cat_filter", &cat_idx, cat_names, 13)) {
        object_esp::GetObjectESPManager().SetCategoryFilter(static_cast<ObjectCategory>(cat_idx));
    }
    
    // Scan results table
    if (!scan_results.empty()) {
        ImGui::Separator();
        BeginSurfaceList("##scan_results", 200.0f);
        
        ImGui::TableSetupColumn("Modelo", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("Distancia", ImGuiTableColumnFlags_WidthFixed, 80);
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
        ImGui::Columns(4, "whitelist_cols", false);
        ImGui::SetColumnWidth(0, 180); // Name
        ImGui::SetColumnWidth(1, 100); // Category
        ImGui::SetColumnWidth(2, 80);  // Distance
        ImGui::SetColumnWidth(3, 120); // Actions
        
        ImGui::Text("Modelo");
        ImGui::NextColumn();
        ImGui::Text("Categoria");
        ImGui::NextColumn();
        ImGui::Text("Dist Max");
        ImGui::NextColumn();
        ImGui::Text("Acoes");
        ImGui::NextColumn();
        ImGui::Separator();
        
        for (auto& entry : whitelist) {
            ImGui::PushID(entry.model.c_str());
            
            ImGui::Text("%s", entry.display_name.c_str());
            ImGui::NextColumn();
            ImGui::Text("%s", ObjectCategoryToString(entry.category));
            ImGui::NextColumn();
            
            ImGui::SetNextItemWidth(70);
            SliderFloat("##dist", &entry.max_distance, 10.0f, 2000.0f, "%.0fm");
            ImGui::NextColumn();
            
            if (entry.enabled) {
                if (CyberButton("Desativar", ImVec2(80, 25))) {
                    object_esp::GetObjectESPManager().ToggleWhitelistEntry(entry.model, false);
                }
            } else {
                if (CyberButton("Ativar", ImVec2(80, 25))) {
                    object_esp::GetObjectESPManager().ToggleWhitelistEntry(entry.model, true);
                }
            }
            ImGui::SameLine();
            if (CyberButton("Remover", ImVec2(70, 25))) {
                object_esp::GetObjectESPManager().RemoveFromWhitelist(entry.model);
            }
            ImGui::SameLine();
            if (CyberButton("Config", ImVec2(60, 25))) {
                // Open inspector
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
    if (CyberButton("Salvar Configuracao", ImVec2(150, 32))) {
        object_esp::GetObjectESPManager().SaveAll();
    }
    ImGui::SameLine();
    if (CyberButton("Carregar Configuracao", ImVec2(150, 32))) {
        object_esp::GetObjectESPManager().LoadAll();
    }
    
    EndCard();
}

} // namespace CyberWidgets