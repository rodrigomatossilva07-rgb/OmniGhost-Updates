#include "auth_page.h"

#include "../auth/local_auth_service.h"
#include "../config/app_settings.h"
#include "../config/config_manager.h"
#include "../licensing/license_service.h"
#include "../window/digital_rain.h"
#include "../window/brand_assets.h"
#include "../window/fonts.h"
#include "../window/localization.h"
#include "../window/performance_mode.h"
#include "../window/theme.h"
#include "../window/widgets.h"
#include "imgui.h"

#include <Windows.h>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <algorithm>
#include <array>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <future>
#include <string>

namespace LauncherAuth {
namespace {

float S(float value) { return CyberTheme::Px(value); }
ImVec2 Add(const ImVec2& left, const ImVec2& right) { return {left.x + right.x, left.y + right.y}; }
ImVec2 Sub(const ImVec2& left, const ImVec2& right) { return {left.x - right.x, left.y - right.y}; }

enum class Screen { Landing, Login, Register, Activate };
enum class Completion { None, Login, Register, AutoLogin, Preparing };

struct Copy {
    const char* enter;
    const char* createAccount;
    const char* exit;
    const char* email;
    const char* password;
    const char* confirmPassword;
    const char* registerAction;
    const char* back;
};

const Copy& Text() {
    // Prefer ASCII-safe UI strings here so accents never show as "?" when the
    // active font atlas lacks the glyph. Full localization can expand later.
    static constexpr std::array<Copy, 6> copy{{
        {"ENTRAR", "CRIAR CONTA", "Sair", "Utilizador", "Palavra-passe", "Confirmar palavra-passe", "REGISTAR", "VOLTAR"},
        {"SIGN IN", "CREATE ACCOUNT", "Exit", "Username", "Password", "Confirm password", "REGISTER", "BACK"},
        {"ANMELDEN", "KONTO ERSTELLEN", "Beenden", "Benutzer", "Passwort", "Passwort bestaetigen", "REGISTRIEREN", "ZURUECK"},
        {"ENTRAR", "CREAR CUENTA", "Salir", "Usuario", "Contrasena", "Confirmar contrasena", "REGISTRARSE", "VOLVER"},
        {"CONNEXION", "CREER UN COMPTE", "Quitter", "Utilisateur", "Mot de passe", "Confirmer le mot de passe", "S'INSCRIRE", "RETOUR"},
        {"ACCEDI", "CREA ACCOUNT", "Esci", "Utente", "Password", "Conferma password", "REGISTRATI", "INDIETRO"}
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(copy.size() - 1));
    return copy[static_cast<std::size_t>(index)];
}

const char* RememberMeLabel() {
    static constexpr std::array<const char*, 6> labels{{
        "Lembrar-me", "Remember me", "Angemeldet bleiben", "Recordarme", "Se souvenir de moi", "Ricordami"
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(labels.size() - 1));
    return labels[static_cast<std::size_t>(index)];
}

const char* LicenseKeyHint() {
    static constexpr std::array<const char*, 6> labels{{
        "Chave de licenca", "License key", "Lizenzschluessel", "Clave de licencia", "Cle de licence", "Chiave di licenza"
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(labels.size() - 1));
    return labels[static_cast<std::size_t>(index)];
}


Screen g_screen = Screen::Landing;
float g_transition = 1.0f;
float g_window_entry = 0.0f;
float g_language_transition = 0.0f;
bool g_initialized = false;
bool g_authenticated = false;
bool g_close_requested = false;
bool g_show_password = false;
bool g_show_confirm = false;
bool g_show_license_key = false;
bool g_language_open = false;
bool g_upgrade_mode = false;
bool g_remember_me = true;
bool g_auto_login_attempt = false;
bool g_startup_preparing = true;
float g_startup_time = 0.0f;
char g_email[320]{};
char g_password[256]{};
char g_confirm[256]{};
char g_license_key[512]{};
std::string g_error;
Completion g_completion = Completion::None;
float g_completion_time = 0.0f;

enum class RemoteOperation { Login, Register, Activate, Upgrade };
struct RemoteOperationResult {
    bool success{};
    std::string message;
    Completion completion{Completion::None};
};
std::future<RemoteOperationResult> g_remote_operation;
bool g_remote_operation_pending = false;

void SecureClear(char* value, std::size_t size) {
    if (value && size) SecureZeroMemory(value, size);
}

void SetScreen(Screen screen) {
    if (g_screen == screen) return;
    g_screen = screen;
    g_transition = 0.0f;
    g_error.clear();
}

void StartRemoteOperation(RemoteOperation operation, std::string username,
                          std::string password, std::string licenseKey);

void InitializeState() {
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    auth.Initialize();
    g_screen = Screen::Landing;
    g_startup_preparing = true;
    g_startup_time = 0.0f;
    if (OmniGhost::Licensing::IsRemoteConfigured()) {
        std::string user, pass;
        if (OmniGhost::Licensing::LoadRememberedRemoteCredentials(user, pass)) {
            g_remember_me = true;
            g_auto_login_attempt = true;
            if (user.size() < sizeof(g_email))
                std::snprintf(g_email, sizeof(g_email), "%s", user.c_str());
            // Validate against KeyAuth in the background; only enter if still valid.
            StartRemoteOperation(RemoteOperation::Login, user, pass, {});
            if (!pass.empty()) SecureZeroMemory(pass.data(), pass.size());
        }
    } else if (auth.TryAutoLogin()) {
        g_completion = Completion::AutoLogin;
        g_completion_time = 0.0f;
    }
    g_initialized = true;
}

void BeginCompletion(Completion completion) {
    g_completion = completion;
    g_completion_time = 0.0f;
    g_error.clear();
}

void StartRemoteOperation(RemoteOperation operation, std::string username,
                          std::string password, std::string licenseKey) {
    if (g_remote_operation_pending)
        return;
    g_remote_operation_pending = true;
    g_error.clear();
    g_remote_operation = std::async(std::launch::async,
        [operation, username = std::move(username), password = std::move(password), licenseKey = std::move(licenseKey)]() mutable {
            RemoteOperationResult outcome{};
            try {
                OmniGhost::Auth::LicenseResult result{};
                switch (operation) {
                case RemoteOperation::Login:
                    result = OmniGhost::Licensing::Login(username, password);
                    outcome.completion = Completion::Login;
                    break;
                case RemoteOperation::Register:
                    result = OmniGhost::Licensing::Register(username, password, licenseKey);
                    outcome.completion = Completion::Register;
                    break;
                case RemoteOperation::Activate:
                    result = OmniGhost::Licensing::ActivateKey(licenseKey);
                    outcome.completion = Completion::Login;
                    break;
                case RemoteOperation::Upgrade:
                    result = OmniGhost::Licensing::Upgrade(username, licenseKey);
                    outcome.completion = Completion::Login;
                    break;
                }
                outcome.success = result.Ok();
                outcome.message = std::move(result.userMessage);
            } catch (...) {
                outcome.success = false;
                outcome.message = "Não foi possível contactar o serviço de autenticação.";
            }
            if (!password.empty()) SecureZeroMemory(password.data(), password.size());
            if (!licenseKey.empty()) SecureZeroMemory(licenseKey.data(), licenseKey.size());
            return outcome;
        });
}

void PollRemoteOperation() {
    using namespace std::chrono_literals;
    if (!g_remote_operation_pending || !g_remote_operation.valid() ||
        g_remote_operation.wait_for(0ms) != std::future_status::ready)
        return;
    g_remote_operation_pending = false;
    try {
        RemoteOperationResult result = g_remote_operation.get();
        const bool automatic = g_auto_login_attempt;
        if (result.success) {
            g_auto_login_attempt = false;
            BeginCompletion(automatic ? Completion::AutoLogin : result.completion);
        } else {
            // Only wipe stored credentials when an automatic remember-me login failed.
            if (automatic) {
                OmniGhost::Licensing::ClearRememberedRemoteCredentials();
                g_auto_login_attempt = false;
                SetScreen(Screen::Login);
            }
            g_error = result.message.empty() ? "Não foi possível iniciar sessão." : result.message;
        }
    } catch (...) {
        if (g_auto_login_attempt) {
            OmniGhost::Licensing::ClearRememberedRemoteCredentials();
            g_auto_login_attempt = false;
            SetScreen(Screen::Login);
        }
        g_error = "Não foi possível contactar o serviço de autenticação.";
    }
}

void CenterCursor(float width) {
    const float available = ImGui::GetContentRegionAvail().x;
    if (available > width)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) * 0.5f);
}

float AnimateAuthState(ImGuiID id, bool active, float response = 13.0f) {
    ImGuiStorage* storage = ImGui::GetStateStorage();
    float value = storage->GetFloat(id, active ? 1.0f : 0.0f);
    const float target = active ? 1.0f : 0.0f;
    if (app_settings::AnimationScale() <= 0.0f) {
        value = target;
    } else {
        const float dt = (std::min)(ImGui::GetIO().DeltaTime, 0.05f);
        value += (target - value) * (1.0f - std::exp(-response * dt));
        if (std::fabs(value - target) < 0.002f) value = target;
    }
    storage->SetFloat(id, value);
    return value;
}

void DrawAuthFocusTransition(ImGuiID id, const ImVec2& position,
                             float width, float height) {
    const float focus = AnimateAuthState(id, ImGui::IsItemActive() || ImGui::IsItemFocused());
    if (focus <= 0.002f) return;
    ImGui::GetWindowDrawList()->AddRect(
        position, Add(position, ImVec2(width, height)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.08f + focus * 0.34f),
        CyberTheme::Metrics::ControlRounding, 0, S(1.f));
}

bool AuthPrimaryButton(const char* label, const ImVec2& size) {
    ImGui::PushID(label);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton("##auth_primary", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const float hover = AnimateAuthState(ImGui::GetID("##hover"), hovered, 10.0f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 end = Add(position, size);
    const ImVec4 fill = ImVec4(
        CyberTheme::Colors.Gold.x + (CyberTheme::Colors.GoldHover.x - CyberTheme::Colors.Gold.x) * hover * 0.16f,
        CyberTheme::Colors.Gold.y + (CyberTheme::Colors.GoldHover.y - CyberTheme::Colors.Gold.y) * hover * 0.16f,
        CyberTheme::Colors.Gold.z + (CyberTheme::Colors.GoldHover.z - CyberTheme::Colors.Gold.z) * hover * 0.16f,
        CyberTheme::Colors.Gold.w);
    draw->AddRectFilled(position, end, CyberTheme::U32(fill), CyberTheme::Metrics::ControlRounding);
    draw->AddRect(position, end,
        CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.20f + hover * 0.10f),
        CyberTheme::Metrics::ControlRounding, 0, S(1.f));
    if (held)
        draw->AddRectFilled(position, end, CyberTheme::SafeShadowU32(14), CyberTheme::Metrics::ControlRounding);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    draw->AddText(ImVec2(position.x + (size.x - textSize.x) * 0.5f,
                         position.y + (size.y - textSize.y) * 0.5f),
                  IM_COL32(24, 22, 18, 255), label);
    ImGui::PopID();
    return pressed;
}

void CenterBlockVertically(float blockHeight, float alignment = 0.36f) {
    // Keep the compact form slightly above the mathematical centre, like a
    // desktop launcher, while retaining enough room for the bottom action.
    const float topPad = S(22.f);
    const float bottomPad = S(16.f);
    const float available = ImGui::GetWindowHeight() - topPad - bottomPad;
    const float y = topPad + (std::max)(0.f, (available - blockHeight) * alignment);
    ImGui::SetCursorPosY(y);
}

void DrawCenteredText(const char* text, ImVec4 color, ImFont* font = nullptr) {
    if (font) ImGui::PushFont(font);
    const float width = ImGui::CalcTextSize(text).x;
    CenterCursor(width);
    ImGui::TextColored(color, "%s", text);
    if (font) ImGui::PopFont();
}

void DrawBrandSignal(const ImVec2& center) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 line = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.052f);
    for (int i = 0; i < 4; ++i) {
        const float radius = S(19.f + i * 8.f);
        draw->AddCircle(center, radius, line, 48, S(0.7f));
    }
    draw->AddLine(ImVec2(center.x - S(47.f), center.y), ImVec2(center.x - S(37.f), center.y), line, S(0.7f));
    draw->AddLine(ImVec2(center.x + S(37.f), center.y), ImVec2(center.x + S(47.f), center.y), line, S(0.7f));
}

void DrawBrand(bool prominent) {
    const float pulse = app_settings::AnimationScale() <= 0.0f
        ? 1.0f
        : 1.0f + std::sin(static_cast<float>(ImGui::GetTime()) * 0.55f) * 0.006f;
    const float markSize = S(prominent ? 31.f : 26.f) * pulse;
    const ImVec2 markStart(ImGui::GetCursorScreenPos().x + (ImGui::GetContentRegionAvail().x - markSize) * 0.5f,
                           ImGui::GetCursorScreenPos().y + S(8.f));
    DrawBrandSignal(ImVec2(markStart.x + markSize * 0.5f, markStart.y + markSize * 0.5f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(markStart, Add(markStart, ImVec2(markSize, markSize)),
        CyberTheme::U32(CyberTheme::Colors.Surface), S(8.f));
    draw->AddRect(markStart, Add(markStart, ImVec2(markSize, markSize)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.40f), S(8.f), 0, S(1.f));
    if (const ImTextureID logo = BrandAssets::GetLogoTexture()) {
        draw->AddImage(logo, Add(markStart, ImVec2(S(4.f), S(4.f))),
                       Add(markStart, ImVec2(markSize - S(4.f), markSize - S(4.f))));
    }
    ImGui::Dummy(ImVec2(0, markSize + S(7.f)));
    ImFont* title = CyberFonts::GetTitleFont();
    DrawCenteredText("OMNIGHOST", CyberTheme::Colors.GoldHover, title);
    ImGui::Dummy(ImVec2(0, S(1.f)));
    DrawCenteredText("Everywhere. Nowhere.", CyberTheme::Colors.TextDisabled);
}

enum class AuthFieldIcon { Email, Lock };

void DrawAuthFieldIcon(AuthFieldIcon icon, const ImVec2& fieldPos, float fieldHeight,
                       float fieldWidth) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsMouseHoveringRect(
        fieldPos, ImVec2(fieldPos.x + fieldWidth, fieldPos.y + fieldHeight));
    const ImU32 color = CyberTheme::U32(hovered
        ? CyberTheme::Colors.GoldHover
        : CyberTheme::Colors.TextDisabled);
    const float x = fieldPos.x + S(16.f);
    const float y = fieldPos.y + fieldHeight * 0.5f;
    const float stroke = S(1.35f);

    if (icon == AuthFieldIcon::Email) {
        const ImVec2 topLeft(x - S(7.f), y - S(5.f));
        const ImVec2 bottomRight(x + S(7.f), y + S(5.f));
        draw->AddRect(topLeft, bottomRight, color, S(2.f), 0, stroke);
        draw->AddLine(topLeft, ImVec2(x, y + S(1.f)), color, stroke);
        draw->AddLine(ImVec2(bottomRight.x, topLeft.y), ImVec2(x, y + S(1.f)), color, stroke);
        return;
    }

    draw->AddRect(ImVec2(x - S(6.f), y - S(1.f)),
                  ImVec2(x + S(6.f), y + S(7.f)), color, S(2.f), 0, stroke);
    draw->PathArcTo(ImVec2(x, y - S(1.f)), S(4.f), 3.14159265f, 6.28318530f, 14);
    draw->PathStroke(color, 0, stroke);
    draw->AddCircleFilled(ImVec2(x, y + S(3.f)), S(1.1f), color, 10);
}

bool AuthInputField(const char* id, char* buffer, std::size_t bufferSize,
                    const char* hint, float width, bool defaultFocus,
                    AuthFieldIcon icon, ImGuiInputTextFlags flags = 0) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(38.f), S(12.f)));
    const ImGuiID focusId = ImGui::GetID(id);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputTextWithHint(
        "##field", hint ? hint : "", buffer, bufferSize, flags);
    if (defaultFocus && !ImGui::IsAnyItemActive())
        ImGui::SetItemDefaultFocus();
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    ImGui::PopStyleVar();
    const float height = ImGui::GetItemRectSize().y;
    DrawAuthFocusTransition(focusId, position, width, height);
    ImGui::PopID();
    DrawAuthFieldIcon(icon, position, height, width);
    return changed;
}

bool AuthPasswordField(const char* id, char* buffer, std::size_t bufferSize,
                       bool* reveal, const char* hint, float width,
                       ImGuiInputTextFlags flags = 0) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(38.f), S(12.f)));
    const ImGuiID focusId = ImGui::GetID(id);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputTextWithHint("##field", hint, buffer, bufferSize,
        *reveal ? flags : (flags | ImGuiInputTextFlags_Password));
    const float height = ImGui::GetItemRectSize().y;
    DrawAuthFocusTransition(focusId, position, width, height);
    const ImVec2 eyeMin(position.x + width - S(38.f), position.y + S(3.f));
    ImGui::SetCursorScreenPos(eyeMin);
    const bool clicked = ImGui::InvisibleButton("##eye", ImVec2(S(32.f), height - S(6.f)));
    if (clicked) *reveal = !*reveal;
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 center(eyeMin.x + S(16.f), eyeMin.y + (height - S(6.f)) * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 eye = CyberTheme::U32(hovered ? CyberTheme::Colors.GoldHover : CyberTheme::Colors.TextDisabled);
    draw->AddCircle(center, S(6.f), eye, 16, S(1.1f));
    draw->AddCircleFilled(center, S(1.7f), eye, 10);
    if (!*reveal) draw->AddLine(Sub(center, ImVec2(S(6.f), S(6.f))), Add(center, ImVec2(S(6.f), S(6.f))), eye, S(1.2f));
    ImGui::SetCursorScreenPos(ImVec2(position.x, position.y + height));
    ImGui::PopID();
    ImGui::PopStyleVar();
    DrawAuthFieldIcon(AuthFieldIcon::Lock, position, height, width);
    return changed;
}

void DrawFieldLabel(const char* label) {
    ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.92f));
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(5.f)));
}

bool DrawTextAction(const char* label, bool centered = true) {
    const float width = ImGui::CalcTextSize(label).x;
    if (centered) CenterCursor(width);
    ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::Colors.TextDisabled);
    const bool pressed = ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(width, S(20.f)));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor();
    if (hovered) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y - S(3.f)),
            ImVec2(max.x, max.y - S(3.f)), CyberTheme::U32(CyberTheme::Colors.Gold), S(1.f));
    }
    return pressed;
}

void DrawLanguageSelector(const ImVec2& windowPos, float windowWidth) {
    constexpr int visibleLanguages = 6;
    static constexpr std::array<const char*, visibleLanguages> codes{{"PT", "EN", "DE", "ES", "FR", "IT"}};
    int selected = std::clamp(static_cast<int>(app_settings::config.language), 0,
                              visibleLanguages - 1);
    const float width = S(52.f);
    const ImVec2 buttonPos(windowPos.x + windowWidth - width - S(18.f), windowPos.y + S(17.f));
    ImGui::SetCursorScreenPos(buttonPos);
    ImGui::PushID("auth_language");
    if (ImGui::InvisibleButton("##toggle", ImVec2(width, S(27.f))))
        g_language_open = !g_language_open;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(buttonPos, Add(buttonPos, ImVec2(width, S(27.f))),
        CyberTheme::WithAlpha(CyberTheme::Colors.Surface, hovered ? 0.98f : 0.88f), S(7.f));
    draw->AddRect(buttonPos, Add(buttonPos, ImVec2(width, S(27.f))),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, hovered || g_language_open ? 0.30f : 0.13f), S(7.f));
    draw->AddText(Add(buttonPos, ImVec2(S(9.f), S(6.f))), CyberTheme::U32(CyberTheme::Colors.Text), codes[selected]);
    draw->AddTriangleFilled(Add(buttonPos, ImVec2(width - S(13.f), S(10.f))),
        Add(buttonPos, ImVec2(width - S(7.f), S(10.f))), Add(buttonPos, ImVec2(width - S(10.f), S(14.f))),
        CyberTheme::U32(CyberTheme::Colors.TextDisabled));

    const float languageTarget = g_language_open ? 1.0f : 0.0f;
    if (app_settings::AnimationScale() <= 0.0f) {
        g_language_transition = languageTarget;
    } else {
        const float dt = (std::min)(ImGui::GetIO().DeltaTime, 0.05f);
        g_language_transition += (languageTarget - g_language_transition) *
            (1.0f - std::exp(-14.0f * dt));
    }
    if (g_language_transition > 0.01f) {
        const float reveal = g_language_transition * g_language_transition *
            (3.0f - 2.0f * g_language_transition);
        const float scale = 0.98f + reveal * 0.02f;
        const ImVec2 baseSize(S(170.f), S(static_cast<float>(visibleLanguages * 36 + 10)));
        const ImVec2 popoverSize(baseSize.x * scale, baseSize.y * scale);
        const ImVec2 popoverPos(buttonPos.x + width - popoverSize.x,
                                buttonPos.y + S(35.f));
        draw->AddRectFilled(popoverPos, Add(popoverPos, popoverSize),
            CyberTheme::WithAlpha(CyberTheme::Colors.Surface, reveal), S(10.f));
        draw->AddRect(popoverPos, Add(popoverPos, popoverSize),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.20f * reveal), S(10.f));
        for (int index = 0; index < visibleLanguages; ++index) {
            const bool active = index == selected;
            const ImVec2 itemPos(popoverPos.x + S(5.f) * scale,
                popoverPos.y + S(5.f + index * 36.f) * scale);
            ImGui::SetCursorScreenPos(itemPos);
            const ImVec2 itemSize(popoverSize.x - S(10.f) * scale, S(32.f) * scale);
            bool pressed = false;
            if (g_language_open)
                pressed = ImGui::InvisibleButton(codes[index], itemSize);
            else
                ImGui::Dummy(itemSize);
            if (pressed) {
                app_settings::config.language = static_cast<app_settings::Language>(index);
                // Authentication is displayed before a game profile exists, so
                // save the global preference here as well. This keeps the
                // selected language across the next launch, including login and
                // account creation screens.
                std::string saveError;
                (void)app_settings::SaveGlobal(&saveError);
                config_manager::FlushActiveConfig();
                g_language_open = false;
            }
            const bool itemHovered = g_language_open && ImGui::IsItemHovered();
            if (active || itemHovered) {
                draw->AddRectFilled(itemPos, Add(itemPos, itemSize),
                    CyberTheme::WithAlpha(CyberTheme::Colors.Gold,
                        (active ? 0.12f : 0.06f) * reveal), S(6.f));
            }
            if (active)
                draw->AddCircleFilled(Add(itemPos, ImVec2(S(10.f), S(16.f))), S(2.5f),
                    CyberTheme::WithAlpha(CyberTheme::Colors.Gold, reveal));
            draw->AddText(Add(itemPos, ImVec2(S(20.f), S(8.f))),
                CyberTheme::WithAlpha(active ? CyberTheme::Colors.Text : CyberTheme::Colors.TextDisabled,
                                      reveal), Loc::LanguageName(index));
        }
    }
    ImGui::PopID();
}

void DrawFeedback(float contentWidth) {
    // Reserve this slot even when there is no message.  Otherwise an error
    // inserts a variable-height block below the primary button and can push
    // the Back action outside the compact authentication card.
    ImGui::Dummy(ImVec2(0, S(7.f)));
    CenterCursor(contentWidth);
    ImGui::BeginChild("##auth_feedback_slot", ImVec2(contentWidth, S(34.f)), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (!g_error.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
        ImGui::TextColored(CyberTheme::Colors.Error, "%s", g_error.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
}

void DrawLanding() {
    const Copy& text = Text();
    constexpr float baseWidth = 342.f;
    const float contentWidth = S(baseWidth);

    CenterBlockVertically(S(238.f));
    DrawBrand(true);
    ImGui::Dummy(ImVec2(0, S(21.f)));

    CenterCursor(contentWidth);
    if (AuthPrimaryButton(text.enter, ImVec2(contentWidth, S(42.f))))
        SetScreen(Screen::Login);
    ImGui::Dummy(ImVec2(0, S(6.f)));
    CenterCursor(contentWidth);
    if (CyberWidgets::GhostButton(text.createAccount, ImVec2(contentWidth, S(42.f))))
        SetScreen(Screen::Register);
    ImGui::Dummy(ImVec2(0, S(5.f)));
    if (DrawTextAction(text.exit))
        g_close_requested = true;
}

void DrawLogin() {
    const Copy& text = Text();
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const float contentWidth = S(342.f);

    // The feedback slot has a stable height, so the Back action never moves
    // outside the card when KeyAuth returns an error.
    CenterBlockVertically(S(310.f), 0.48f);
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(14.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    const bool remote = OmniGhost::Licensing::IsRemoteConfigured();
    const char* identity = text.email; // already localized (Utilizador / Username / ...)
    AuthInputField("login_email", g_email, sizeof(g_email), identity,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(7.f)));
    const bool enter = AuthPasswordField("login_password", g_password,
        sizeof(g_password), &g_show_password, text.password, contentWidth,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(0, S(6.f)));
    {
        CenterCursor(contentWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::Colors.TextDisabled);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(2.f), S(2.f)));
        ImGui::Checkbox(RememberMeLabel(), &g_remember_me);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, S(7.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool submit = AuthPrimaryButton(g_remote_operation_pending ? "A AUTENTICAR..." : text.enter,
        ImVec2(contentWidth, S(42.f))) || enter;
    ImGui::EndDisabled();
    if (submit && !g_remote_operation_pending) {
        if (remote) {
            const std::string user = g_email;
            const std::string pass = g_password;
            if (g_remember_me)
                (void)OmniGhost::Licensing::SaveRememberedRemoteCredentials(user, pass);
            else
                OmniGhost::Licensing::ClearRememberedRemoteCredentials();
            StartRemoteOperation(RemoteOperation::Login, user, pass, {});
            SecureClear(g_password, sizeof(g_password));
            ImGui::EndGroup();
            DrawFeedback(contentWidth);
            return;
        }
        const auto result = auth.Login(g_email, g_password, g_remember_me);
        if (result.Ok()) {
            SecureClear(g_password, sizeof(g_password));
            BeginCompletion(Completion::Login);
        } else {
            g_error = result.message;
        }
    }
    ImGui::EndGroup();
    DrawFeedback(contentWidth);
    ImGui::Dummy(ImVec2(0, S(2.f)));
    const std::string back = std::string("<- ") + text.back;
    if (DrawTextAction(back.c_str())) {
        SecureClear(g_password, sizeof(g_password));
        SetScreen(Screen::Landing);
    }
}

void DrawRegister() {
    const Copy& text = Text();
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const float contentWidth = S(342.f);
    const bool remote = OmniGhost::Licensing::IsRemoteConfigured();

    // Compact block so the full form (fields + remember + button + back) stays visible.
    const float blockHeight = remote ? S(354.f) : S(304.f);
    CenterBlockVertically(blockHeight, 0.48f);
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(14.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    AuthInputField("register_email", g_email, sizeof(g_email), text.email,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(6.f)));
    bool submit = AuthPasswordField("register_password", g_password, sizeof(g_password),
        &g_show_password, text.password, contentWidth,
        remote ? 0 : ImGuiInputTextFlags_EnterReturnsTrue);
    if (remote) {
        ImGui::Dummy(ImVec2(0, S(6.f)));
        submit = AuthPasswordField("register_key", g_license_key,
            sizeof(g_license_key), &g_show_license_key, LicenseKeyHint(), contentWidth,
            ImGuiInputTextFlags_EnterReturnsTrue) || submit;
    }
    ImGui::Dummy(ImVec2(0, S(6.f)));
    CenterCursor(contentWidth);
    ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::Colors.TextDisabled);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(2.f), S(2.f)));
    ImGui::Checkbox(RememberMeLabel(), &g_remember_me);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(6.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool registerPressed = AuthPrimaryButton(
        g_remote_operation_pending ? "A REGISTAR..." : text.registerAction,
        ImVec2(contentWidth, S(42.f))) || submit;
    ImGui::EndDisabled();
    if (registerPressed && !g_remote_operation_pending) {
        if (g_password[0] == '\0') {
            g_error = "Indica uma palavra-passe.";
        } else if (remote) {
            const std::string user = g_email;
            const std::string pass = g_password;
            const std::string key = g_license_key;
            if (g_remember_me)
                (void)OmniGhost::Licensing::SaveRememberedRemoteCredentials(user, pass);
            else
                OmniGhost::Licensing::ClearRememberedRemoteCredentials();
            StartRemoteOperation(RemoteOperation::Register, user, pass, key);
            SecureClear(g_password, sizeof(g_password));
            SecureClear(g_confirm, sizeof(g_confirm));
            SecureClear(g_license_key, sizeof(g_license_key));
            ImGui::EndGroup();
            DrawFeedback(contentWidth);
            return;
        } else {
            const auto result = auth.Register(g_email, g_password, g_remember_me);
            if (result.Ok()) {
                SecureClear(g_password, sizeof(g_password));
                SecureClear(g_confirm, sizeof(g_confirm));
                SecureClear(g_license_key, sizeof(g_license_key));
                BeginCompletion(Completion::Register);
            } else {
                g_error = result.message;
            }
        }
    }
    ImGui::EndGroup();
    DrawFeedback(contentWidth);
    ImGui::Dummy(ImVec2(0, S(2.f)));
    const std::string back = std::string("<- ") + text.back;
    if (DrawTextAction(back.c_str())) {
        SecureClear(g_password, sizeof(g_password));
        SecureClear(g_confirm, sizeof(g_confirm));
        SecureClear(g_license_key, sizeof(g_license_key));
        SetScreen(Screen::Landing);
    }
}

void DrawActivate() {
    const float contentWidth = S(330.f);
    CenterBlockVertically(S(298.f));
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(24.f)));
    DrawCenteredText(g_upgrade_mode ? "UPGRADE // LICENSE" : "ACTIVATE // LICENSE",
        ImGui::ColorConvertU32ToFloat4(CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.70f)));
    ImGui::Dummy(ImVec2(0, S(22.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    if (g_upgrade_mode) {
        DrawFieldLabel("Utilizador");
        AuthInputField("upgrade_username", g_email, sizeof(g_email), "Utilizador",
            contentWidth, true, AuthFieldIcon::Email);
        ImGui::Dummy(ImVec2(0, S(12.f)));
    }
    DrawFieldLabel("Chave de licença");
    const bool enter = AuthPasswordField("activate_key", g_license_key, sizeof(g_license_key),
        &g_show_license_key, "Chave de licença", contentWidth, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(0, S(18.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool activatePressed = AuthPrimaryButton(g_remote_operation_pending
        ? (g_upgrade_mode ? "A ATUALIZAR..." : "A ATIVAR...")
        : (g_upgrade_mode ? "ATUALIZAR ACESSO" : "ATIVAR"),
        ImVec2(contentWidth, S(46.f))) || enter;
    ImGui::EndDisabled();
    if (activatePressed && !g_remote_operation_pending) {
        StartRemoteOperation(g_upgrade_mode ? RemoteOperation::Upgrade : RemoteOperation::Activate,
            g_upgrade_mode ? g_email : std::string{}, {}, g_license_key);
        SecureClear(g_license_key, sizeof(g_license_key));
        ImGui::EndGroup();
        DrawFeedback(contentWidth);
        return;
    }
    ImGui::EndGroup();
    DrawFeedback(contentWidth);
    ImGui::Dummy(ImVec2(0, S(8.f)));
    if (DrawTextAction(g_upgrade_mode ? "Ativar apenas com key" : "Atualizar uma conta existente"))
        g_upgrade_mode = !g_upgrade_mode;
    ImGui::Dummy(ImVec2(0, S(12.f)));
    if (DrawTextAction("← Voltar")) {
        SecureClear(g_license_key, sizeof(g_license_key));
        g_upgrade_mode = false;
        SetScreen(Screen::Landing);
    }
}

void DrawStartupPreparation(ImDrawList* draw, const ImVec2& windowPos,
                            float windowWidth, float windowHeight) {
    const float centerY = windowPos.y + windowHeight * .43f;
    ImGui::SetCursorScreenPos(ImVec2(windowPos.x + S(28.f), centerY - S(76.f)));
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(26.f)));
    DrawCenteredText("A PREPARAR SESSÃO", CyberTheme::Colors.Text,
                     CyberFonts::GetBodyFont());
    ImGui::Dummy(ImVec2(0, S(6.f)));
    DrawCenteredText(g_remote_operation_pending
        ? "A validar a sessão guardada com segurança"
        : "A verificar as preferências da conta",
        CyberTheme::Colors.TextDisabled);

    const float lineWidth = S(126.f);
    const float phase = std::fmod(g_startup_time * 0.82f, 1.0f);
    const ImVec2 lineMin(windowPos.x + (windowWidth - lineWidth) * .5f,
                         ImGui::GetCursorScreenPos().y + S(20.f));
    draw->AddLine(lineMin, ImVec2(lineMin.x + lineWidth, lineMin.y),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, .15f), S(1.f));
    const float segment = lineWidth * .34f;
    const float start = lineMin.x + (lineWidth + segment) * phase - segment;
    draw->PushClipRect(lineMin, ImVec2(lineMin.x + lineWidth, lineMin.y + S(3.f)), true);
    draw->AddLine(ImVec2(start, lineMin.y), ImVec2(start + segment, lineMin.y),
        CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, .94f), S(1.7f));
    draw->PopClipRect();
}

} // namespace

void Reset() {
    g_initialized = false;
    g_remember_me = true;
    g_startup_preparing = true;
    g_startup_time = 0.0f;
    g_authenticated = false;
    g_close_requested = false;
    g_transition = 1.0f;
    g_window_entry = 0.0f;
    g_language_transition = 0.0f;
    g_screen = Screen::Landing;
    g_error.clear();
    g_completion = Completion::None;
    g_completion_time = 0.0f;
    g_language_open = false;
    g_show_license_key = false;
    g_upgrade_mode = false;
    g_remote_operation_pending = false;
    g_email[0] = '\0';
    SecureClear(g_password, sizeof(g_password));
    SecureClear(g_confirm, sizeof(g_confirm));
    SecureClear(g_license_key, sizeof(g_license_key));
}

bool Draw() {
    if (!g_initialized) InitializeState();
    if (g_authenticated) return true;
    PollRemoteOperation();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && g_screen != Screen::Landing) {
        SecureClear(g_password, sizeof(g_password));
        SecureClear(g_confirm, sizeof(g_confirm));
        SecureClear(g_license_key, sizeof(g_license_key));
        SetScreen(Screen::Landing);
    }

    const float delta = (std::min)(ImGui::GetIO().DeltaTime, 0.05f);
    const float animation = app_settings::AnimationScale();
    if (animation <= 0.0f)
        g_window_entry = 1.0f;
    else
        g_window_entry = (std::min)(1.0f, g_window_entry + delta / 0.19f);
    const float entryEased = g_window_entry * g_window_entry *
        (3.0f - 2.0f * g_window_entry);

    if (g_startup_preparing) {
        g_startup_time += delta;
        // Keep the branded preparation state visible long enough to avoid a
        // one-frame flash, while never delaying an outstanding network request.
        if (g_startup_time >= 0.52f && !g_remote_operation_pending)
            g_startup_preparing = false;
    }

    if (!g_startup_preparing && g_completion != Completion::None) {
        g_completion_time += delta;
        const float successDuration = animation <= 0.f ? 0.f : 0.30f * (std::max)(0.35f, animation);
        const float preparingDuration = animation <= 0.f ? 0.f : 0.38f * (std::max)(0.35f, animation);
        if ((g_completion == Completion::Login || g_completion == Completion::Register) &&
            g_completion_time >= successDuration) {
            g_completion = Completion::Preparing;
            g_completion_time = 0.0f;
        } else if (g_completion == Completion::AutoLogin) {
            g_completion = Completion::Preparing;
            g_completion_time = 0.0f;
        } else if (g_completion == Completion::Preparing &&
                   g_completion_time >= preparingDuration) {
            g_authenticated = true;
        }
    }
    if (animation <= 0.f) g_transition = 1.f;
    else g_transition = (std::min)(1.0f, g_transition + delta /
        (CyberTheme::Metrics::PageTransitionSeconds / (std::max)(0.35f, animation)));
    const float eased = g_transition * g_transition * (3.0f - 2.0f * g_transition);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled(ImVec2(0, 0), display, IM_COL32(3, 3, 4, 255));
    CyberTheme::DrawSubtleNoise(background, ImVec2(0, 0), display, 0.012f, 0x41555448u, 180);
    const PerformanceMode::State performance = PerformanceMode::Current();

    const float desiredWidth = S(468.f);
    const float desiredHeight =
        g_screen == Screen::Register ? S(530.f) :
        (g_screen == Screen::Login ? S(486.f) :
         (g_screen == Screen::Activate ? S(438.f) : S(360.f)));
    const float width = (std::max)(S(340.f), (std::min)(desiredWidth, display.x - S(20.f)));
    const float availableHeight = (std::max)(S(300.f), display.y - S(24.f));
    const float height = (std::min)(desiredHeight, availableHeight);
    const bool needsVerticalScroll = desiredHeight > availableHeight + 0.5f;
    const float entryOffset = animation > 0.f ? (1.f - entryEased) * S(5.f) : 0.f;
    const ImVec2 position((display.x - width) * 0.5f,
                          (display.y - height) * 0.5f + entryOffset);

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(30.f), S(24.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, S(1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, entryEased);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CyberTheme::WithAlpha(CyberTheme::Colors.Card, 0.985f));
    ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.16f));
    if (!needsVerticalScroll) {
        ImGui::GetIO().MouseWheel = 0.0f;
        ImGui::GetIO().MouseWheelH = 0.0f;
    }
    ImGuiWindowFlags authFlags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse;
    if (!needsVerticalScroll)
        authFlags |= ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##omnighost_auth", nullptr,
        authFlags);

    const ImVec2 windowPos = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const bool showAuthRain = !g_startup_preparing && g_completion == Completion::None &&
        (g_screen == Screen::Landing || g_screen == Screen::Login || g_screen == Screen::Register);
    if (showAuthRain) {
        // Rain belongs to the authentication card itself. Keep it deliberately
        // faint and sparse so fields and branding remain the visual hierarchy.
        draw->PushClipRect(windowPos, Add(windowPos, ImVec2(width, height)), true);
        DigitalRain::SetQualityFromEffectLevel(static_cast<int>(app_settings::config.digital_rain_level));
        DigitalRain::Draw(draw, windowPos, ImVec2(width, height), true,
            performance.effective, 0.0f, 0.58f, 0.28f, animation, false, false);
        draw->PopClipRect();
    }
    draw->AddLine(ImVec2(windowPos.x + S(24.f), windowPos.y + S(14.f)),
                  ImVec2(windowPos.x + S(76.f), windowPos.y + S(14.f)),
                  CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.22f), S(1.f));
    CyberTheme::DrawRadialAccent(draw, ImVec2(windowPos.x + width * 0.5f,
        windowPos.y + height * 0.31f), S(158.f), 0.030f);
    if (!g_startup_preparing)
        DrawLanguageSelector(windowPos, width);

    ImGui::SetCursorScreenPos(ImVec2(windowPos.x + S(24.f), windowPos.y + S(19.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, (std::max)(0.06f, eased));
    if (g_startup_preparing) {
        DrawStartupPreparation(draw, windowPos, width, height);
    } else if (g_completion == Completion::None) {
        switch (g_screen) {
        case Screen::Landing: DrawLanding(); break;
        case Screen::Login: DrawLogin(); break;
        case Screen::Register: DrawRegister(); break;
        case Screen::Activate: DrawActivate(); break;
        }
    } else {
        const bool success = g_completion == Completion::Login ||
                             g_completion == Completion::Register;
        const char* status = success
            ? (g_completion == Completion::Register ? "CONTA CRIADA" : "ACCESS GRANTED")
            : "A preparar o OmniGhost";
        const float centerY = windowPos.y + height * 0.43f;
        ImGui::SetCursorScreenPos(ImVec2(windowPos.x + S(28.f), centerY - S(58.f)));
        DrawBrand(false);
        ImGui::Dummy(ImVec2(0, S(28.f)));
        DrawCenteredText(status, success ? CyberTheme::Colors.Success : CyberTheme::Colors.Text,
                         CyberFonts::GetBodyFont());
        if (!success) {
            const float lineWidth = S(112.f);
            const float phase = std::fmod(g_completion_time * 1.8f, 1.0f);
            const ImVec2 lineMin(windowPos.x + (width - lineWidth) * 0.5f,
                                 ImGui::GetCursorScreenPos().y + S(18.f));
            draw->AddLine(lineMin, ImVec2(lineMin.x + lineWidth, lineMin.y),
                CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.16f), S(1.f));
            const float segment = lineWidth * 0.34f;
            const float start = lineMin.x + (lineWidth + segment) * phase - segment;
            draw->PushClipRect(lineMin, ImVec2(lineMin.x + lineWidth, lineMin.y + S(3.f)), true);
            draw->AddLine(ImVec2(start, lineMin.y), ImVec2(start + segment, lineMin.y),
                CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, 0.92f), S(1.6f));
            draw->PopClipRect();
        }
    }
    ImGui::PopStyleVar();

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    return g_authenticated;
}

bool ConsumeCloseRequest() {
    const bool requested = g_close_requested;
    g_close_requested = false;
    return requested;
}

} // namespace LauncherAuth
