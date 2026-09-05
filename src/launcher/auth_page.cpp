#include "auth_page.h"

#include "../auth/local_auth_service.h"
#include "../config/app_settings.h"
#include "../config/config_manager.h"
#include "../window/digital_rain.h"
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
#include <cmath>
#include <cstring>
#include <string>

namespace LauncherAuth {
namespace {

float S(float value) { return CyberTheme::Px(value); }

enum class Screen { Landing, Login, Register };
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
    static constexpr std::array<Copy, 6> copy{{
        {"ENTRAR", "CRIAR CONTA", "Sair", "Email", "Palavra-passe", "Confirmar palavra-passe", "REGISTAR", "VOLTAR"},
        {"SIGN IN", "CREATE ACCOUNT", "Exit", "Email", "Password", "Confirm password", "REGISTER", "BACK"},
        {"ANMELDEN", "KONTO ERSTELLEN", "Beenden", "E-Mail", "Passwort", "Passwort bestätigen", "REGISTRIEREN", "ZURÜCK"},
        {"ENTRAR", "CREAR CUENTA", "Salir", "Correo electrónico", "Contraseña", "Confirmar contraseña", "REGISTRARSE", "VOLVER"},
        {"CONNEXION", "CRÉER UN COMPTE", "Quitter", "E-mail", "Mot de passe", "Confirmer le mot de passe", "S'INSCRIRE", "RETOUR"},
        {"ACCEDI", "CREA ACCOUNT", "Esci", "Email", "Password", "Conferma password", "REGISTRATI", "INDIETRO"}
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(copy.size() - 1));
    return copy[static_cast<std::size_t>(index)];
}

Screen g_screen = Screen::Landing;
float g_transition = 1.0f;
bool g_initialized = false;
bool g_authenticated = false;
bool g_close_requested = false;
bool g_show_password = false;
bool g_show_confirm = false;
char g_email[320]{};
char g_password[256]{};
char g_confirm[256]{};
std::string g_error;
Completion g_completion = Completion::None;
float g_completion_time = 0.0f;

void SecureClear(char* value, std::size_t size) {
    if (value && size) SecureZeroMemory(value, size);
}

void SetScreen(Screen screen) {
    if (g_screen == screen) return;
    g_screen = screen;
    g_transition = 0.0f;
    g_error.clear();
}

void InitializeState() {
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    auth.Initialize();
    if (auth.TryAutoLogin()) {
        g_completion = Completion::AutoLogin;
        g_completion_time = 0.0f;
    }
    else
        g_screen = Screen::Landing;
    g_initialized = true;
}

void BeginCompletion(Completion completion) {
    g_completion = completion;
    g_completion_time = 0.0f;
    g_error.clear();
}

void CenterCursor(float width) {
    const float available = ImGui::GetContentRegionAvail().x;
    if (available > width)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (available - width) * 0.5f);
}

void CenterBlockVertically(float blockHeight) {
    const float available = ImGui::GetWindowHeight() - S(48.f);
    ImGui::SetCursorPosY(S(24.f) + (std::max)(0.f, (available - blockHeight) * 0.5f));
}

void DrawCenteredText(const char* text, ImVec4 color, ImFont* font = nullptr) {
    if (font) ImGui::PushFont(font);
    const float width = ImGui::CalcTextSize(text).x;
    CenterCursor(width);
    ImGui::TextColored(color, "%s", text);
    if (font) ImGui::PopFont();
}

void DrawBrand(bool prominent) {
    ImFont* title = CyberFonts::GetTitleFont();
    ImGui::Dummy(ImVec2(0, prominent ? S(6.f) : S(2.f)));
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
    const float height = ImGui::GetFrameHeight();
    const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(40.f), framePadding.y));
    const bool changed = CyberWidgets::InputField(
        id, buffer, bufferSize, hint, flags, width, defaultFocus);
    ImGui::PopStyleVar();
    DrawAuthFieldIcon(icon, position, height, width);
    return changed;
}

bool AuthPasswordField(const char* id, char* buffer, std::size_t bufferSize,
                       bool* reveal, const char* hint, float width,
                       ImGuiInputTextFlags flags = 0) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 framePadding = ImGui::GetStyle().FramePadding;
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(40.f), framePadding.y));
    const bool changed = CyberWidgets::PasswordField(
        id, buffer, bufferSize, reveal, hint, flags, width, false);
    ImGui::PopStyleVar();
    DrawAuthFieldIcon(AuthFieldIcon::Lock, position, height, width);
    return changed;
}

void DrawLanguageSelector(const ImVec2& windowPos, float windowWidth) {
    constexpr int visibleLanguages = 5;
    int selected = std::clamp(static_cast<int>(app_settings::config.language), 0,
                              visibleLanguages - 1);
    const float width = S(94.f);
    ImGui::SetCursorScreenPos(ImVec2(windowPos.x + windowWidth - width - S(8.f),
                                    windowPos.y + S(9.f)));
    ImGui::SetNextItemWidth(width);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, CyberTheme::WithAlpha(CyberTheme::Colors.Surface, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Border, 0.80f));
    if (ImGui::BeginCombo("##auth_language", Loc::LanguageName(selected),
                          ImGuiComboFlags_HeightRegular)) {
        for (int index = 0; index < visibleLanguages; ++index) {
            const bool active = index == selected;
            if (ImGui::Selectable(Loc::LanguageName(index), active)) {
                app_settings::config.language = static_cast<app_settings::Language>(index);
                config_manager::FlushActiveConfig();
            }
            if (active) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);
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

    CenterBlockVertically(S(236.f));
    DrawBrand(true);
    ImGui::Dummy(ImVec2(0, S(46.f)));

    CenterCursor(contentWidth);
    if (CyberWidgets::GoldButton(text.enter, ImVec2(contentWidth, S(46.f))))
        SetScreen(Screen::Login);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    CenterCursor(contentWidth);
    if (CyberWidgets::GhostButton(text.createAccount, ImVec2(contentWidth, S(46.f))))
        SetScreen(Screen::Register);
    ImGui::Dummy(ImVec2(0, S(16.f)));
    const float exitWidth = ImGui::CalcTextSize(text.exit).x + S(30.f);
    CenterCursor(exitWidth);
    if (CyberWidgets::GhostButton(text.exit, ImVec2(exitWidth, S(30.f))))
        g_close_requested = true;
}

void DrawLogin() {
    const Copy& text = Text();
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const float contentWidth = S(330.f);

    CenterBlockVertically(S(268.f));
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(34.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    AuthInputField("login_email", g_email, sizeof(g_email), text.email,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    const bool enter = AuthPasswordField("login_password", g_password,
        sizeof(g_password), &g_show_password, text.password, contentWidth,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(0, S(18.f)));
    if (CyberWidgets::GoldButton(text.enter, ImVec2(contentWidth, S(46.f))) || enter) {
        const auto result = auth.Login(g_email, g_password, true);
        if (result.Ok()) {
            SecureClear(g_password, sizeof(g_password));
            BeginCompletion(Completion::Login);
        } else {
            g_error = result.message;
        }
    }
    ImGui::EndGroup();
    DrawFeedback(contentWidth);
    ImGui::Dummy(ImVec2(0, S(14.f)));
    const float backWidth = ImGui::CalcTextSize(text.back).x + S(34.f);
    CenterCursor(backWidth);
    if (CyberWidgets::GhostButton(text.back, ImVec2(backWidth, S(30.f)))) {
        SecureClear(g_password, sizeof(g_password));
        SetScreen(Screen::Landing);
    }
}

void DrawRegister() {
    const Copy& text = Text();
    auto& auth = OmniGhost::Auth::LocalAuthService::Instance();
    const float contentWidth = S(330.f);

    CenterBlockVertically(S(326.f));
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(22.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    AuthInputField("register_email", g_email, sizeof(g_email), text.email,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(9.f)));
    AuthPasswordField("register_password", g_password, sizeof(g_password),
        &g_show_password, text.password, contentWidth);
    ImGui::Dummy(ImVec2(0, S(9.f)));
    const bool enter = AuthPasswordField("register_confirm", g_confirm,
        sizeof(g_confirm), &g_show_confirm, text.confirmPassword, contentWidth,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(0, S(17.f)));
    if (CyberWidgets::GoldButton(text.registerAction, ImVec2(contentWidth, S(46.f))) || enter) {
        if (std::strcmp(g_password, g_confirm) != 0) {
            g_error = "As palavras-passe não coincidem.";
        } else {
            const auto result = auth.Register(g_email, g_password, true);
            if (result.Ok()) {
                SecureClear(g_password, sizeof(g_password));
                SecureClear(g_confirm, sizeof(g_confirm));
                BeginCompletion(Completion::Register);
            } else {
                g_error = result.message;
            }
        }
    }
    ImGui::EndGroup();
    DrawFeedback(contentWidth);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    const float backWidth = ImGui::CalcTextSize(text.back).x + S(34.f);
    CenterCursor(backWidth);
    if (CyberWidgets::GhostButton(text.back, ImVec2(backWidth, S(30.f)))) {
        SecureClear(g_password, sizeof(g_password));
        SecureClear(g_confirm, sizeof(g_confirm));
        SetScreen(Screen::Landing);
    }
}

} // namespace

void Reset() {
    g_initialized = false;
    g_authenticated = false;
    g_close_requested = false;
    g_transition = 1.0f;
    g_screen = Screen::Landing;
    g_error.clear();
    g_completion = Completion::None;
    g_completion_time = 0.0f;
    g_email[0] = '\0';
    SecureClear(g_password, sizeof(g_password));
    SecureClear(g_confirm, sizeof(g_confirm));
}

bool Draw() {
    if (!g_initialized) InitializeState();
    if (g_authenticated) return true;

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && g_screen != Screen::Landing) {
        SecureClear(g_password, sizeof(g_password));
        SecureClear(g_confirm, sizeof(g_confirm));
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
        app_settings::DigitalRainOpacity() * 0.22f,
        app_settings::DigitalRainDensity() * 0.45f, animation, true);

    const float width = (std::max)(S(360.f), display.x - S(24.f));
    const float height = (std::max)(S(430.f), display.y - S(24.f));
    const float slide = animation > 0.f ? (1.f - eased) * S(14.f) : 0.f;
    const ImVec2 position((display.x - width) * 0.5f + slide, (display.y - height) * 0.5f);

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(28.f), S(24.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, S(1.f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, CyberTheme::WithAlpha(CyberTheme::Colors.Card, 0.985f));
    ImGui::PushStyleColor(ImGuiCol_Border, CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.24f));
    ImGui::Begin("##omnighost_auth", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);

    const ImVec2 windowPos = ImGui::GetWindowPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddLine(ImVec2(windowPos.x + S(24.f), windowPos.y + S(12.f)),
                  ImVec2(windowPos.x + width - S(164.f), windowPos.y + S(12.f)),
                  CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.18f), S(1.f));
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
