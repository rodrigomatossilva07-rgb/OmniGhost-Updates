#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <functional>
#include <chrono>
#include <optional>
#include "src/project/third_party/ImGui/imgui.h"

namespace OmniGhost::UI::Toast {

enum class ToastType : uint8_t {
    Info = 0,
    Success,
    Warning,
    Error,
    Custom // usa cor customizada
};

struct Toast {
    std::string title;
    std::string message;
    ToastType type = ToastType::Info;
    ImU32 custom_color = 0; // só usado se type == Custom
    std::string icon;       // FontAwesome codepoint (ex: "\uf0f3")
    bool pinned = false;    // não expira automaticamente
    bool centered = false;  // toast central (style "grande")
    uint64_t id = 0;        // ID único para dismiss programático
    std::chrono::steady_clock::time_point created_at;
    float age = 0.0f;       // segundos desde criação
};

struct ToastConfig {
    // Timing (seconds)
    float enter_duration = 0.34f;
    float hold_duration = 3.40f;
    float exit_duration = 0.28f;

    // Layout
    float width = 344.0f;
    float height = 56.0f;
    float gap = 10.0f;
    float margin = 18.0f;

    // Stack limits
    size_t max_toasts = 5;

    // Animation
    bool enable_animations = true;
    float stack_ease_rate = 14.0f;   // exponential easing for stack reposition
    float slide_ease_rate = 14.0f;   // slide in/out easing
};

// Inicializa sistema (chamar no startup do overlay)
void Initialize(const ToastConfig& config = {});

// Shutdown
void Shutdown();

// Adiciona toast à queue (thread-safe)
// Retorna ID do toast para dismiss posterior
[[nodiscard]] uint64_t Push(std::string_view title,
                            std::string_view message,
                            ToastType type = ToastType::Info,
                            std::string_view icon = "\uf0f3", // fa-bell
                            bool pinned = false,
                            bool centered = false,
                            ImU32 custom_color = 0);

// Adiciona toast com cor customizada (convenience)
[[nodiscard]] uint64_t PushCustom(std::string_view title,
                                  std::string_view message,
                                  ImU32 color,
                                  std::string_view icon = "\uf0f3",
                                  bool pinned = false,
                                  bool centered = false);

// Remove toast por ID (thread-safe)
bool Dismiss(uint64_t id);

// Remove todos os toasts
void DismissAll();

// Marca toast como não-pinned (começa countdown de expiração)
void Unpin(uint64_t id);

// Atualiza animações (chamar uma vez por frame no Render loop)
void Update(float delta_time);

// Renderiza todos os toasts (chamar no final do Render, sobreposto a tudo)
void Render();

// Config runtime
void SetConfig(const ToastConfig& config);
const ToastConfig& GetConfig();

// Helpers de conveniência
[[nodiscard]] uint64_t Info(std::string_view title, std::string_view msg, bool pinned = false);
[[nodiscard]] uint64_t Success(std::string_view title, std::string_view msg, bool pinned = false);
[[nodiscard]] uint64_t Warning(std::string_view title, std::string_view msg, bool pinned = false);
[[nodiscard]] uint64_t Error(std::string_view title, std::string_view msg, bool pinned = false);

// Toast centralizado (estilo "grande" no meio do ecrã)
[[nodiscard]] uint64_t Centered(std::string_view title, std::string_view msg,
                                ToastType type = ToastType::Info,
                                std::string_view icon = "\uf0f3");

// Callback opcional quando toast é dismissed (ex: auto-save config)
using DismissCallback = std::function<void(uint64_t toast_id)>;
void SetDismissCallback(DismissCallback cb);

} // namespace OmniGhost::UI::Toast