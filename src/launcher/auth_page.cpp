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
        if (result.success) {
            g_auto_login_attempt = false;
            BeginCompletion(result.completion);
        } else {
            g_error = result.message.empty() ? "Nao foi possivel iniciar sessao." : result.message;
            // Only wipe stored credentials when an automatic remember-me login failed.
            if (g_auto_login_attempt) {
                OmniGhost::Licensing::ClearRememberedRemoteCredentials();
                g_auto_login_attempt = false;
            }
        }
    } catch (...) {
        g_error = "Não foi possível contactar o serviço de autenticação.";
    }
}

void CenterCursor(float width) {
    const float available = ImGui::GetContentRegionAvail().x;
    if (available > width)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) * 0.5f);
}

void CenterBlockVertically(float blockHeight) {
    // Center the form block inside the card, leaving room for language chip / padding.
    const float topPad = S(28.f);
    const float bottomPad = S(20.f);
    const float available = ImGui::GetWindowHeight() - topPad - bottomPad;
    const float y = topPad + (std::max)(0.f, (available - blockHeight) * 0.5f);
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
    const ImU32 line = CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.075f);
    for (int i = 0; i < 4; ++i) {
        const float radius = S(31.f + i * 13.f);
        draw->AddCircle(center, radius, line, 48, S(0.7f));
    }
    draw->AddLine(ImVec2(center.x - S(76.f), center.y), ImVec2(center.x - S(60.f), center.y), line, S(0.8f));
    draw->AddLine(ImVec2(center.x + S(60.f), center.y), ImVec2(center.x + S(76.f), center.y), line, S(0.8f));
}

void DrawBrand(bool prominent) {
    const float markSize = S(prominent ? 48.f : 40.f);
    const ImVec2 markStart(ImGui::GetCursorScreenPos().x + (ImGui::GetContentRegionAvail().x - markSize) * 0.5f,
                           ImGui::GetCursorScreenPos().y + S(8.f));
    DrawBrandSignal(ImVec2(markStart.x + markSize * 0.5f, markStart.y + markSize * 0.5f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(markStart, Add(markStart, ImVec2(markSize, markSize)),
        CyberTheme::U32(CyberTheme::Colors.Surface), S(12.f));
    draw->AddRect(markStart, Add(markStart, ImVec2(markSize, markSize)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.48f), S(12.f), 0, S(1.f));
    if (const ImTextureID logo = BrandAssets::GetLogoTexture()) {
        draw->AddImage(logo, Add(markStart, ImVec2(S(7.f), S(7.f))),
                       Add(markStart, ImVec2(markSize - S(7.f), markSize - S(7.f))));
    }
    ImGui::Dummy(ImVec2(0, markSize + S(14.f)));
    ImFont* title = CyberFonts::GetTitleFont();
    DrawCenteredText("OMNIGHOST", CyberTheme::Colors.GoldHover, title);
    ImGui::Dummy(ImVec2(0, S(3.f)));
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
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(40.f), S(13.f)));
    const bool changed = CyberWidgets::InputField(
        id, buffer, bufferSize, hint, flags, width, defaultFocus);
    ImGui::PopStyleVar();
    const float height = ImGui::GetItemRectSize().y;
    DrawAuthFieldIcon(icon, position, height, width);
    return changed;
}

bool AuthPasswordField(const char* id, char* buffer, std::size_t bufferSize,
                       bool* reveal, const char* hint, float width,
                       ImGuiInputTextFlags flags = 0) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(40.f), S(13.f)));
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    const bool changed = ImGui::InputTextWithHint("##field", hint, buffer, bufferSize,
        *reveal ? flags : (flags | ImGuiInputTextFlags_Password));
    const float height = ImGui::GetItemRectSize().y;
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
    const bool pressed = ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(width, S(24.f)));
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
    const float width = S(58.f);
    const ImVec2 buttonPos(windowPos.x + windowWidth - width - S(24.f), windowPos.y + S(22.f));
    ImGui::SetCursorScreenPos(buttonPos);
    ImGui::PushID("auth_language");
    if (ImGui::InvisibleButton("##toggle", ImVec2(width, S(30.f))))
        g_language_open = !g_language_open;
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(buttonPos, Add(buttonPos, ImVec2(width, S(30.f))),
        CyberTheme::WithAlpha(CyberTheme::Colors.Surface, hovered ? 0.98f : 0.88f), S(8.f));
    draw->AddRect(buttonPos, Add(buttonPos, ImVec2(width, S(30.f))),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, hovered || g_language_open ? 0.34f : 0.16f), S(8.f));
    draw->AddText(Add(buttonPos, ImVec2(S(11.f), S(8.f))), CyberTheme::U32(CyberTheme::Colors.Text), codes[selected]);
    draw->AddTriangleFilled(Add(buttonPos, ImVec2(width - S(14.f), S(12.f))),
        Add(buttonPos, ImVec2(width - S(7.f), S(12.f))), Add(buttonPos, ImVec2(width - S(10.5f), S(17.f))),
        CyberTheme::U32(CyberTheme::Colors.TextDisabled));

    if (g_language_open) {
        const ImVec2 popoverPos(buttonPos.x - S(112.f), buttonPos.y + S(38.f));
        const ImVec2 popoverSize(S(170.f), S(static_cast<float>(visibleLanguages * 36 + 10)));
        draw->AddRectFilled(popoverPos, Add(popoverPos, popoverSize),
            CyberTheme::U32(CyberTheme::Colors.Surface), S(10.f));
        draw->AddRect(popoverPos, Add(popoverPos, popoverSize),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.20f), S(10.f));
        for (int index = 0; index < visibleLanguages; ++index) {
            const bool active = index == selected;
            const ImVec2 itemPos(popoverPos.x + S(5.f), popoverPos.y + S(5.f + index * 36.f));
            ImGui::SetCursorScreenPos(itemPos);
            if (ImGui::InvisibleButton(codes[index], ImVec2(popoverSize.x - S(10.f), S(32.f)))) {
                app_settings::config.language = static_cast<app_settings::Language>(index);
                config_manager::FlushActiveConfig();
                g_language_open = false;
            }
            const bool itemHovered = ImGui::IsItemHovered();
            if (active || itemHovered) {
                draw->AddRectFilled(itemPos, Add(itemPos, ImVec2(popoverSize.x - S(10.f), S(32.f))),
                    CyberTheme::WithAlpha(CyberTheme::Colors.Gold, active ? 0.12f : 0.06f), S(6.f));
            }
            if (active)
                draw->AddCircleFilled(Add(itemPos, ImVec2(S(10.f), S(16.f))), S(2.5f), CyberTheme::U32(CyberTheme::Colors.Gold));
            draw->AddText(Add(itemPos, ImVec2(S(20.f), S(8.f))), CyberTheme::U32(active ? CyberTheme::Colors.Text : CyberTheme::Colors.TextDisabled), Loc::LanguageName(index));
        }
    }
    ImGui::PopID();
}

void DrawFeedback(float contentWidth) {
    if (g_error.empty()) return;
    ImGui::Dummy(ImVec2(0, S(10.f)));
    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + contentWidth);
    ImGui::TextColored(CyberTheme::Colors.Error, "%s", g_error.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
}

void DrawLanding() {
    const Copy& text = Text();
    constexpr float baseWidth = 330.f;
    const float contentWidth = S(baseWidth);

    CenterBlockVertically(S(304.f));
    DrawBrand(true);
    ImGui::Dummy(ImVec2(0, S(42.f)));

    CenterCursor(contentWidth);
    if (CyberWidgets::GoldButton(text.enter, ImVec2(contentWidth, S(46.f))))
        SetScreen(Screen::Login);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    CenterCursor(contentWidth);
    if (CyberWidgets::GhostButton(text.createAccount, ImVec2(contentWidth, S(46.f))))
        SetScreen(Screen::Register);
    ImGui::Dummy(ImVec2(0, S(12.f)));
    if (DrawTextAction(text.exit))
        g_close_requested = true;
}

void DrawLogin() {
    const Copy& text = Text();
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const float contentWidth = S(330.f);

    CenterBlockVertically(S(320.f));
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(20.f)));
    DrawCenteredText("ACCESS // AUTHENTICATION", ImGui::ColorConvertU32ToFloat4(CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.70f)));
    ImGui::Dummy(ImVec2(0, S(22.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    const bool remote = OmniGhost::Licensing::IsRemoteConfigured();
    const char* identity = text.email; // already localized (Utilizador / Username / ...)
    AuthInputField("login_email", g_email, sizeof(g_email), identity,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(13.f)));
    const bool enter = AuthPasswordField("login_password", g_password,
        sizeof(g_password), &g_show_password, text.password, contentWidth,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(0, S(12.f)));
    {
        CenterCursor(contentWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::Colors.TextDisabled);
        ImGui::Checkbox(RememberMeLabel(), &g_remember_me);
        ImGui::PopStyleColor();
    }
    ImGui::Dummy(ImVec2(0, S(14.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool submit = CyberWidgets::GoldButton(g_remote_operation_pending ? "A AUTENTICAR..." : text.enter,
        ImVec2(contentWidth, S(46.f))) || enter;
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
    ImGui::Dummy(ImVec2(0, S(12.f)));
    const std::string back = std::string("<- ") + text.back;
    if (DrawTextAction(back.c_str())) {
        SecureClear(g_password, sizeof(g_password));
        SetScreen(Screen::Landing);
    }
}

void DrawRegister() {
    const Copy& text = Text();
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const float contentWidth = S(330.f);
    const bool remote = OmniGhost::Licensing::IsRemoteConfigured();

    // Compact block so the full form (fields + remember + button + back) stays visible.
    const float blockHeight = remote ? S(360.f) : S(300.f);
    CenterBlockVertically(blockHeight);
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(12.f)));
    DrawCenteredText("CREATE // IDENTITY",
        ImGui::ColorConvertU32ToFloat4(CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.70f)));
    ImGui::Dummy(ImVec2(0, S(14.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    AuthInputField("register_email", g_email, sizeof(g_email), text.email,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    bool submit = AuthPasswordField("register_password", g_password, sizeof(g_password),
        &g_show_password, text.password, contentWidth,
        remote ? 0 : ImGuiInputTextFlags_EnterReturnsTrue);
    if (remote) {
        ImGui::Dummy(ImVec2(0, S(10.f)));
        submit = AuthPasswordField("register_key", g_license_key,
            sizeof(g_license_key), &g_show_license_key, LicenseKeyHint(), contentWidth,
            ImGuiInputTextFlags_EnterReturnsTrue) || submit;
    }
    ImGui::Dummy(ImVec2(0, S(10.f)));
    CenterCursor(contentWidth);
    ImGui::PushStyleColor(ImGuiCol_Text, CyberTheme::Colors.TextDisabled);
    ImGui::Checkbox(RememberMeLabel(), &g_remember_me);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, S(12.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool registerPressed = CyberWidgets::GoldButton(
        g_remote_operation_pending ? "A REGISTAR..." : text.registerAction,
        ImVec2(contentWidth, S(46.f))) || submit;
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
    ImGui::Dummy(ImVec2(0, S(8.f)));
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
    const bool activatePressed = CyberWidgets::GoldButton(g_remote_operation_pending
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

} // namespace

void Reset() {
    g_initialized = false;
    g_remember_me = true;
    g_authenticated = false;
    g_close_requested = false;
    g_transition = 1.0f;
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

    if (g_completion != Completion::None) {
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
    DigitalRain::Draw(background, ImVec2(0, 0), display, true, performance.effective, 0.0f,
        app_settings::DigitalRainOpacity() * 0.08f,
        app_settings::DigitalRainDensity() * 0.16f, animation, true);

    const float desiredWidth = S(480.f);
    const float desiredHeight =
        g_screen == Screen::Register ? S(560.f) :
        (g_screen == Screen::Login ? S(520.f) :
         (g_screen == Screen::Activate ? S(500.f) : S(500.f)));
    const float width = (std::max)(S(360.f), (std::min)(desiredWidth, display.x - S(40.f)));
    // Prefer fitting the form on screen over a tall card that clips the bottom.
    const float height = (std::max)(S(400.f), (std::min)(desiredHeight, display.y - S(48.f)));
    const float slide = animation > 0.f ? (1.f - eased) * S(14.f) : 0.f;
    const ImVec2 position((display.x - width) * 0.5f + slide, (display.y - height) * 0.5f);

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(40.f), S(34.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, S(1.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CyberTheme::WithAlpha(CyberTheme::Colors.Card, 0.985f));
    ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.24f));
    ImGui::GetIO().MouseWheel = 0.0f;
    ImGui::GetIO().MouseWheelH = 0.0f;
    ImGui::Begin("##omnighost_auth", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 windowPos = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(windowPos.x + S(28.f), windowPos.y + S(16.f)),
                  ImVec2(windowPos.x + S(92.f), windowPos.y + S(16.f)),
                  CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.30f), S(1.f));
    CyberTheme::DrawRadialAccent(draw, ImVec2(windowPos.x + width * 0.5f,
        windowPos.y + height * 0.32f), S(210.f), 0.045f);
    DrawLanguageSelector(windowPos, width);

    ImGui::SetCursorScreenPos(ImVec2(windowPos.x + S(28.f), windowPos.y + S(24.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, (std::max)(0.12f, eased));
    if (g_completion == Completion::None) {
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
            ? (g_completion == Completion::Register ? "✓ CONTA CRIADA" : "✓ AUTENTICADO")
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
    ImGui::PopStyleVar(3);
    return g_authenticated;
}

bool ConsumeCloseRequest() {
    const bool requested = g_close_requested;
    g_close_requested = false;
    return requested;
}

} // namespace LauncherAuth
