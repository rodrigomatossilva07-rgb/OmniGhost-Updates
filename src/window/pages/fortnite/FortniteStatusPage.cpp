#include "../../widgets.h"
#include "../../Fortnite/fortnite_game.h"
#include "imgui.h"

void DrawFortniteStatus()
{
    using namespace CyberWidgets;
    ImGui::PushID("fortnite_status");

    // Beta/Experimental indicator
    InlineMessage("Fortnite support is in Beta — LocalPlayer ESP / diagnostic only.", TextTone::Warning, "fn_status_beta_warning");

    BeginCard("Estado Fortnite");
    StatusBadge("DMA / Attach", Fortnite::runtime.attached);
    StatusBadge("Chain GEngine→LP", Fortnite::runtime.chain_ok);
    StatusBadge("Camera", Fortnite::runtime.camera_ok);
    StatusBadge("Local Pawn", Fortnite::runtime.local_pawn_ok);
    StatusBadge("WorldToScreen", Fortnite::runtime.w2s_ok);
    StatusBadge("GI match", Fortnite::runtime.gi_match);
    KeyValueRow("Processo", Fortnite::runtime.process_name.c_str());
    KeyValueRow("Estado", Fortnite::StatusText());
    TextLineF(TextTone::Secondary, "PID: %u", Fortnite::runtime.pid);
    TextLineF(TextTone::Secondary, "Base: 0x%llX", (unsigned long long)Fortnite::runtime.base);
    TextLineF(TextTone::Secondary, "Frames: %llu", (unsigned long long)Fortnite::runtime.frames);
    if (GoldButton("Dump diagnostico (logs.txt)", ImVec2(-1.f, 32.f)))
        Fortnite::FlushDiagnosticsToLog();
    EndCard();

    BeginCard("Pointer chain (hex)");
    TextLineF(TextTone::Secondary, "GEngine: 0x%llX", (unsigned long long)Fortnite::runtime.engine);
    TextLineF(TextTone::Secondary, "Viewport: 0x%llX", (unsigned long long)Fortnite::runtime.viewport);
    TextLineF(TextTone::Secondary, "UWorld: 0x%llX", (unsigned long long)Fortnite::runtime.world);
    TextLineF(TextTone::Secondary, "GameInstance: 0x%llX", (unsigned long long)Fortnite::runtime.game_instance);
    TextLineF(TextTone::Secondary, "LocalPlayer: 0x%llX", (unsigned long long)Fortnite::runtime.local_player);
    TextLineF(TextTone::Secondary, "PlayerController: 0x%llX", (unsigned long long)Fortnite::runtime.player_controller);
    if (Fortnite::runtime.local_pawn_ok)
        TextLineF(TextTone::Secondary, "LocalPawn: 0x%llX", (unsigned long long)Fortnite::runtime.local_pawn);
    else
        TextLine("LocalPawn: NOT SPAWNED", TextTone::Secondary);
    TextLineF(TextTone::Secondary, "CameraManager: 0x%llX", (unsigned long long)Fortnite::runtime.camera_manager);
    EndCard();

    BeginCard("Camera / Pawn");
    if (Fortnite::runtime.camera_ok) {
        TextLineF(TextTone::Secondary, "Cam XYZ: %.2f %.2f %.2f",
            Fortnite::runtime.cam_loc.x, Fortnite::runtime.cam_loc.y, Fortnite::runtime.cam_loc.z);
        TextLineF(TextTone::Secondary, "Pitch/Yaw/Roll: %.2f %.2f %.2f",
            Fortnite::runtime.cam_rot.pitch, Fortnite::runtime.cam_rot.yaw, Fortnite::runtime.cam_rot.roll);
        TextLineF(TextTone::Secondary, "FOV: %.2f", Fortnite::runtime.cam_fov);
    } else {
        TextLine("Camera: INVALID", TextTone::Secondary);
    }
    if (Fortnite::runtime.local_pawn_ok) {
        TextLineF(TextTone::Secondary, "Pawn XYZ: %.2f %.2f %.2f",
            Fortnite::runtime.local_pos.x, Fortnite::runtime.local_pos.y, Fortnite::runtime.local_pos.z);
        TextLineF(TextTone::Secondary, "Dist camera: %.1f", Fortnite::runtime.distance_to_cam);
        TextLineF(TextTone::Secondary, "Screen: %.0f, %.0f  W2S=%s",
            Fortnite::runtime.screen_x, Fortnite::runtime.screen_y,
            Fortnite::runtime.w2s_ok ? "PASS" : "FAIL");
    }
    EndCard();

    BeginCard("Build");
    TextLineF(TextTone::Secondary, "Expected: %s-CL-%s", Fortnite::offsets.build, Fortnite::offsets.cl);
    TextLineF(TextTone::Secondary, "GEngine RVA: 0x%llX", (unsigned long long)Fortnite::offsets.gengine);
    EndCard();

    ImGui::PopID();
}
