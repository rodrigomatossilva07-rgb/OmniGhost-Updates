#include "auth_page.h"

#include "../app_version.h"
#include "../config/app_settings.h"
#include "../config/config_manager.h"
#include "../licensing/license_service.h"
#include "../window/animations.h"
#include "../window/brand_assets.h"
#include "../window/fonts.h"
#include "../window/localization.h"
#include "../window/theme.h"
#include "../window/widgets.h"
#include "../window/ui_feedback.h"
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
#include <exception>
#include <future>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace LauncherAuth {
namespace {





std::mutex g_auth_stage_mutex;

void WriteAuthStage(const char* stage) noexcept {
    const std::lock_guard<std::mutex> lock(g_auth_stage_mutex);
    (void)stage;
}

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

struct AuthUiCopy {
    const char* welcomeTitle;
    const char* welcomeSubtitle;
    const char* createTitle;
    const char* createSubtitle;
    const char* preparingTitle;
    const char* restoringSession;
    const char* finishing;
    const char* authenticating;
    const char* registering;
    const char* accountCreated;
    const char* accessGranted;
    const char* user;
    const char* licenseKey;
    const char* upgradeLicense;
    const char* activateLicense;
    const char* updating;
    const char* activating;
    const char* updateAccess;
    const char* activate;
    const char* activateOnly;
    const char* updateExisting;
    const char* serviceUnavailable;
    const char* signInFailed;
    const char* enterPassword;
    const char* passwordTooShort;
    const char* enterEmailLicense;
    const char* invalidCredentials;
    const char* noActiveLicense;
    const char* rateLimited;
    const char* sessionExpired;
};

const AuthUiCopy& UiText() {
    static constexpr std::array<AuthUiCopy, 6> copy{{
        {"Bem-vindo de volta.", "O teu acesso começa aqui.", "Cria a tua conta.", "Ativa o teu acesso OmniGhost.", "A preparar sessão", "A verificar a sessão guardada…", "A finalizar…", "A autenticar…", "A registar…", "Conta criada", "Acesso autorizado", "Utilizador", "Chave de licença", "ATUALIZAR LICENÇA", "ATIVAR LICENÇA", "A atualizar…", "A ativar…", "ATUALIZAR ACESSO", "ATIVAR", "Ativar apenas com chave", "Atualizar uma conta existente", "Não foi possível contactar o serviço de autenticação.", "Não foi possível iniciar sessão.", "Indica uma palavra-passe.", "A palavra-passe deve ter pelo menos 8 caracteres.", "Indica um email válido e uma chave de licença.", "Email ou palavra-passe incorretos.", "A conta não possui uma licença ativa.", "Demasiadas tentativas. Tenta novamente mais tarde.", "A sessão terminou. Inicia sessão novamente."},
        {"Welcome back.", "Your access starts here.", "Create your account.", "Activate your OmniGhost access.", "Preparing session", "Checking your saved session…", "Finalizing…", "Authenticating…", "Creating account…", "Account created", "Access granted", "User", "License key", "UPGRADE LICENSE", "ACTIVATE LICENSE", "Updating…", "Activating…", "UPDATE ACCESS", "ACTIVATE", "Activate with key only", "Update an existing account", "The authentication service could not be reached.", "Unable to sign in.", "Enter your password.", "The password must be at least 8 characters long.", "Enter a valid email and license key.", "Incorrect email or password.", "This account has no active license.", "Too many attempts. Try again later.", "Your session has expired. Sign in again."},
        {"Willkommen zurück.", "Dein Zugang beginnt hier.", "Erstelle dein Konto.", "Aktiviere deinen OmniGhost-Zugang.", "Sitzung wird vorbereitet", "Gespeicherte Sitzung wird geprüft…", "Wird abgeschlossen…", "Authentifizierung…", "Konto wird erstellt…", "Konto erstellt", "Zugriff gewährt", "Benutzer", "Lizenzschlüssel", "LIZENZ AKTUALISIEREN", "LIZENZ AKTIVIEREN", "Wird aktualisiert…", "Wird aktiviert…", "ZUGANG AKTUALISIEREN", "AKTIVIEREN", "Nur mit Schlüssel aktivieren", "Bestehendes Konto aktualisieren", "Der Authentifizierungsdienst ist nicht erreichbar.", "Anmeldung nicht möglich.", "Gib dein Passwort ein.", "Das Passwort muss mindestens 8 Zeichen lang sein.", "Gib eine gültige E-Mail-Adresse und einen Lizenzschlüssel ein.", "E-Mail-Adresse oder Passwort ist falsch.", "Dieses Konto besitzt keine aktive Lizenz.", "Zu viele Versuche. Versuche es später erneut.", "Deine Sitzung ist abgelaufen. Melde dich erneut an."},
        {"Te damos la bienvenida.", "Tu acceso comienza aquí.", "Crea tu cuenta.", "Activa tu acceso a OmniGhost.", "Preparando la sesión", "Comprobando la sesión guardada…", "Finalizando…", "Autenticando…", "Creando la cuenta…", "Cuenta creada", "Acceso autorizado", "Usuario", "Clave de licencia", "ACTUALIZAR LICENCIA", "ACTIVAR LICENCIA", "Actualizando…", "Activando…", "ACTUALIZAR ACCESO", "ACTIVAR", "Activar solo con clave", "Actualizar una cuenta existente", "No se ha podido contactar con el servicio de autenticación.", "No se ha podido iniciar sesión.", "Introduce tu contraseña.", "La contraseña debe tener al menos 8 caracteres.", "Introduce un correo válido y una clave de licencia.", "El correo o la contraseña son incorrectos.", "Esta cuenta no tiene una licencia activa.", "Demasiados intentos. Inténtalo más tarde.", "Tu sesión ha caducado. Inicia sesión de nuevo."},
        {"Bon retour.", "Votre accès commence ici.", "Créez votre compte.", "Activez votre accès OmniGhost.", "Préparation de la session", "Vérification de la session enregistrée…", "Finalisation…", "Authentification…", "Création du compte…", "Compte créé", "Accès autorisé", "Utilisateur", "Clé de licence", "METTRE À JOUR LA LICENCE", "ACTIVER LA LICENCE", "Mise à jour…", "Activation…", "METTRE À JOUR L’ACCÈS", "ACTIVER", "Activer uniquement avec une clé", "Mettre à jour un compte existant", "Impossible de contacter le service d’authentification.", "Impossible de se connecter.", "Saisissez votre mot de passe.", "Le mot de passe doit comporter au moins 8 caractères.", "Saisissez une adresse e-mail valide et une clé de licence.", "L’adresse e-mail ou le mot de passe est incorrect.", "Ce compte ne possède aucune licence active.", "Trop de tentatives. Réessayez plus tard.", "Votre session a expiré. Reconnectez-vous."},
        {"Bentornato.", "Il tuo accesso inizia qui.", "Crea il tuo account.", "Attiva il tuo accesso OmniGhost.", "Preparazione della sessione", "Verifica della sessione salvata…", "Finalizzazione…", "Autenticazione…", "Creazione dell’account…", "Account creato", "Accesso autorizzato", "Utente", "Chiave di licenza", "AGGIORNA LICENZA", "ATTIVA LICENZA", "Aggiornamento…", "Attivazione…", "AGGIORNA ACCESSO", "ATTIVA", "Attiva solo con la chiave", "Aggiorna un account esistente", "Impossibile contattare il servizio di autenticazione.", "Impossibile accedere.", "Inserisci la password.", "La password deve contenere almeno 8 caratteri.", "Inserisci un indirizzo e-mail valido e una chiave di licenza.", "E-mail o password non corretti.", "Questo account non dispone di una licenza attiva.", "Troppi tentativi. Riprova più tardi.", "La sessione è scaduta. Accedi di nuovo."}
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(copy.size() - 1));
    return copy[static_cast<std::size_t>(index)];
}

const char* ReadableStatus(const char* value) {


    static thread_local std::string text;
    text = value ? value : "";
    constexpr std::string_view ellipsis = "\xE2\x80\xA6";
    for (std::size_t at = text.find(ellipsis); at != std::string::npos;
         at = text.find(ellipsis, at + 3)) {
        text.replace(at, ellipsis.size(), "...");
    }
    return text.c_str();
}

const Copy& Text() {


    static constexpr std::array<Copy, 6> copy{{
        {"ENTRAR", "CRIAR CONTA", "Sair", "Email", "Palavra-passe", "Confirmar palavra-passe", "REGISTAR", "VOLTAR"},
        {"SIGN IN", "CREATE ACCOUNT", "Exit", "Email", "Password", "Confirm password", "REGISTER", "BACK"},
        {"ANMELDEN", "KONTO ERSTELLEN", "Beenden", "E-Mail", "Passwort", "Passwort bestätigen", "REGISTRIEREN", "ZURÜCK"},
        {"INICIAR SESIÓN", "CREAR CUENTA", "Salir", "Correo electrónico", "Contraseña", "Confirmar contraseña", "REGISTRARSE", "VOLVER"},
        {"CONNEXION", "CRÉER UN COMPTE", "Quitter", "E-mail", "Mot de passe", "Confirmer le mot de passe", "S’INSCRIRE", "RETOUR"},
        {"ACCEDI", "CREA ACCOUNT", "Esci", "E-mail", "Password", "Conferma password", "REGISTRATI", "INDIETRO"}
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(copy.size() - 1));
    return copy[static_cast<std::size_t>(index)];
}

const char* LicenseKeyHint() {
    static constexpr std::array<const char*, 6> labels{{
        "Chave de licença", "License key", "Lizenzschlüssel", "Clave de licencia", "Clé de licence", "Chiave di licenza"
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(labels.size() - 1));
    return labels[static_cast<std::size_t>(index)];
}

const char* CredentialsHint() {
    static constexpr std::array<const char*, 6> labels{{
        "Indica um email válido e a palavra-passe.",
        "Enter a valid email and password.",
        "Gib eine gültige E-Mail-Adresse und dein Passwort ein.",
        "Introduce un correo válido y la contraseña.",
        "Saisissez une adresse e-mail valide et le mot de passe.",
        "Inserisci un indirizzo e-mail valido e la password."
    }};
    const int index = std::clamp(static_cast<int>(app_settings::config.language), 0,
                                 static_cast<int>(labels.size() - 1));
    return labels[static_cast<std::size_t>(index)];
}

const char* BackToSignInLabel() {
    static constexpr std::array<const char*, 6> labels{{
        "Voltar ao início de sessão", "Back to Sign In", "Zurück zur Anmeldung",
        "Volver al inicio de sesión", "Retour à la connexion", "Torna all'accesso"
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

enum class RemoteOperation {
    WebsiteLogin, WebsiteRegister, WebsiteRestore,
    Activate, Upgrade
};
struct RemoteOperationResult {
    bool success{};
    std::string message;
    Completion completion{Completion::None};
    std::string diagnosticCode;
    bool websiteAccount{};
    std::uint64_t generation{};
};
std::future<RemoteOperationResult> g_remote_operation;
std::vector<std::future<RemoteOperationResult>> g_draining_operations;
std::uint64_t g_operation_generation = 0;
std::uint64_t g_pending_operation_generation = 0;
bool g_remote_operation_pending = false;

std::string LocalizedRemoteError(std::string_view code, const std::string& fallback) {
    const AuthUiCopy& ui = UiText();
    if (code == "auth.unavailable" || code == "auth.not_configured" ||
        code == "website.unavailable")
        return ui.serviceUnavailable;
    if (code == "auth.invalid_input" || code == "website.invalid_input")
        return ui.invalidCredentials;
    if (code == "website.invalid_registration")
        return ui.enterEmailLicense;
    if (code == "website.no_active_license" || code == "website.invalid_licenses")
        return ui.noActiveLicense;
    if (code == "website.rate_limited" || code == "auth.locked_out")
        return ui.rateLimited;
    if (code == "website.no_session" || code == "website.session_forbidden" ||
        code == "website.session_unauthorized" || code == "auth.session_missing")
        return ui.sessionExpired;
    if (code == "website.rejected")
        return ui.invalidCredentials;
    if (app_settings::config.language == app_settings::Language::PT && !fallback.empty())
        return fallback;
    return ui.signInFailed;
}

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
    g_screen = Screen::Landing;
    g_startup_preparing = true;
    g_startup_time = 0.0f;
    g_auto_login_attempt = true;
    StartRemoteOperation(RemoteOperation::WebsiteRestore, {}, {}, {});
    g_initialized = true;
}

void BeginCompletion(Completion completion) {
    g_completion = completion;
    g_completion_time = 0.0f;
    g_error.clear();
    if (completion == Completion::Login || completion == Completion::Register)
        OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Success);
}

void StartRemoteOperation(RemoteOperation operation, std::string username,
                          std::string password, std::string licenseKey) {
    if (g_remote_operation_pending)
        return;
    g_remote_operation_pending = true;
    WriteAuthStage("auth-operation-started");
    g_error.clear();
    if (g_remote_operation.valid())
        g_draining_operations.push_back(std::move(g_remote_operation));
    const std::uint64_t generation = g_operation_generation;
    g_pending_operation_generation = generation;
    const std::string contactError = UiText().serviceUnavailable;
    g_remote_operation = std::async(std::launch::async,
        [operation, generation, username = std::move(username), password = std::move(password),
         licenseKey = std::move(licenseKey), contactError]() mutable {
            RemoteOperationResult outcome{};
            try {
                OmniGhost::Auth::LicenseResult result{};
                switch (operation) {
                case RemoteOperation::WebsiteLogin:
                    result = OmniGhost::Licensing::LoginWebsite(username, password);
                    outcome.completion = Completion::Login;
                    outcome.websiteAccount = true;
                    break;
                case RemoteOperation::WebsiteRegister:
                    result = OmniGhost::Licensing::RegisterWebsite(username, password, licenseKey);
                    outcome.completion = Completion::Register;
                    outcome.websiteAccount = true;
                    break;
                case RemoteOperation::WebsiteRestore:
                    result = OmniGhost::Licensing::RestoreWebsiteSession();
                    outcome.completion = Completion::AutoLogin;
                    outcome.websiteAccount = true;
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
                WriteAuthStage(result.Ok() ? "auth-service-success" : "auth-service-rejected");
                outcome.success = result.Ok();
                outcome.message = std::move(result.userMessage);
                outcome.diagnosticCode = std::move(result.diagnosticCode);
                outcome.generation = generation;



            } catch (...) {
                WriteAuthStage("auth-service-exception");
                outcome.success = false;
                outcome.message = contactError;
            }
            if (!password.empty()) SecureZeroMemory(password.data(), password.size());
            if (!licenseKey.empty()) SecureZeroMemory(licenseKey.data(), licenseKey.size());
            return outcome;
        });
}

void PollRemoteOperation() {
    using namespace std::chrono_literals;
    for (std::size_t i = 0; i < g_draining_operations.size();) {
        if (g_draining_operations[i].valid() &&
            g_draining_operations[i].wait_for(0ms) == std::future_status::ready) {
            try {
                (void)g_draining_operations[i].get();
            } catch (...) {
                OutputDebugStringA("[OmniGhost][Auth] background authentication cleanup failed\n");
            }
            g_draining_operations.erase(g_draining_operations.begin() +
                                        static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    if (!g_remote_operation_pending || !g_remote_operation.valid() ||
        g_remote_operation.wait_for(0ms) != std::future_status::ready)
        return;
    g_remote_operation_pending = false;
    const std::uint64_t completedGeneration = g_pending_operation_generation;
    try {
        RemoteOperationResult result = g_remote_operation.get();
        if (completedGeneration != g_operation_generation)
            return;
        WriteAuthStage(result.success ? "auth-result-success" : "auth-result-failure");
        const bool automatic = g_auto_login_attempt;
        if (result.success) {
            g_auto_login_attempt = false;
            BeginCompletion(automatic ? Completion::AutoLogin : result.completion);
        } else {

            if (automatic) {
                g_auto_login_attempt = false;
                SetScreen(Screen::Login);
            }
            if (result.diagnosticCode != "website.no_saved_session")
                g_error = LocalizedRemoteError(result.diagnosticCode, result.message);
        }
    } catch (...) {
        if (completedGeneration != g_operation_generation)
            return;
        WriteAuthStage("auth-result-exception");
        if (g_auto_login_attempt) {
            OmniGhost::Licensing::ClearRememberedRemoteCredentials();
            g_auto_login_attempt = false;
            SetScreen(Screen::Login);
        }
            g_error = UiText().serviceUnavailable;
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
    if (hover > 0.01f && app_settings::MotionEnabled() &&
        app_settings::config.animation_intensity == app_settings::EffectLevel::Full) {
        const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.62f, 1.0f);
        const float sheenX = position.x - S(42.f) + (size.x + S(84.f)) * phase;
        draw->PushClipRect(position, end, true);
        draw->AddRectFilledMultiColor(
            ImVec2(sheenX - S(30.f), position.y), ImVec2(sheenX + S(30.f), end.y),
            IM_COL32(255, 255, 255, 0), IM_COL32(255, 248, 211, static_cast<int>(22 * hover)),
            IM_COL32(255, 248, 211, static_cast<int>(22 * hover)), IM_COL32(255, 255, 255, 0));
        draw->PopClipRect();
    }
    if (held)
        draw->AddRectFilled(position, end, CyberTheme::SafeShadowU32(14), CyberTheme::Metrics::ControlRounding);
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float nudge = CyberAnimations::HoverNudge(ImGui::GetID("##auth_primary_nudge"), hovered);
    draw->AddText(ImVec2(position.x + (size.x - textSize.x) * 0.5f + nudge,
                         position.y + (size.y - textSize.y) * 0.5f),
                  IM_COL32(24, 22, 18, 255), label);
    ImGui::PopID();
    return pressed;
}

void CenterBlockVertically(float blockHeight, float alignment = 0.36f) {


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



    ImGui::GetWindowDrawList()->AddCircleFilled(
        center, S(42.f), CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.018f), 48);
}

void DrawBrand(bool prominent) {
    (void)prominent;
    const float markSize = S(54.f);
    const ImVec2 markStart(ImGui::GetCursorScreenPos().x + (ImGui::GetContentRegionAvail().x - markSize) * 0.5f,
                           ImGui::GetCursorScreenPos().y + S(8.f));
    DrawBrandSignal(ImVec2(markStart.x + markSize * 0.5f, markStart.y + markSize * 0.5f));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(markStart, Add(markStart, ImVec2(markSize, markSize)),
        CyberTheme::U32(CyberTheme::Colors.Surface), S(8.f));
    draw->AddRect(markStart, Add(markStart, ImVec2(markSize, markSize)),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.40f), S(8.f), 0, S(1.f));
    if (const ImTextureID logo = BrandAssets::GetLogoTexture()) {
        draw->AddImage(logo, Add(markStart, ImVec2(S(5.f), S(5.f))),
                       Add(markStart, ImVec2(markSize - S(5.f), markSize - S(5.f))));
    }
    ImGui::Dummy(ImVec2(0, markSize + S(12.f)));
    ImFont* title = CyberFonts::GetTitleFont();
    DrawCenteredText("OMNIGHOST", CyberTheme::Colors.GoldHover, title);
    ImGui::Dummy(ImVec2(0, S(1.f)));
    DrawCenteredText("Everywhere. Nowhere.", CyberTheme::Colors.TextDisabled);
}

void DrawAuthFooter(const ImVec2& windowPos, float windowWidth, float windowHeight) {
    const std::string version = std::string("Version ") + OmniGhost::Version;
    const char* copyright = "© 2026 OMNIGHOST. All rights reserved.";
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 muted = CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.78f);
    const ImU32 faint = CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.58f);
    const ImVec2 copyrightSize = ImGui::CalcTextSize(copyright);
    const ImVec2 versionSize = ImGui::CalcTextSize(version.c_str());
    const float footerY = windowPos.y + windowHeight - S(43.f);
    draw->AddText(ImVec2(windowPos.x + (windowWidth - copyrightSize.x) * 0.5f, footerY),
                  muted, copyright);
    draw->AddText(ImVec2(windowPos.x + (windowWidth - versionSize.x) * 0.5f,
                         footerY + S(17.f)), faint, version.c_str());
}

void DrawPageHeading(const char* title, const char* subtitle) {
    ImGui::Dummy(ImVec2(0, S(18.f)));
    DrawCenteredText(title, CyberTheme::Colors.Text, CyberFonts::GetTitleFont());
    if (subtitle && subtitle[0] != '\0') {
        ImGui::Dummy(ImVec2(0, S(3.f)));
        DrawCenteredText(subtitle, ImGui::ColorConvertU32ToFloat4(
            CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, 0.92f)));
        ImGui::Dummy(ImVec2(0, S(22.f)));
    } else {
        ImGui::Dummy(ImVec2(0, S(22.f)));
    }
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
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(40.f), S(14.f)));
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

void DrawLucideVisibilityIcon(ImDrawList* draw, const ImVec2& center,
                              bool visible, ImU32 color) {
    if (!draw)
        return;



    const float halfWidth = S(9.0f);
    const float curveHeight = S(6.0f);
    const float stroke = S(1.9f);
    const ImVec2 left(center.x - halfWidth, center.y);
    const ImVec2 right(center.x + halfWidth, center.y);

    draw->PathClear();
    draw->PathLineTo(left);
    draw->PathBezierCubicCurveTo(
        ImVec2(center.x - S(4.8f), center.y - curveHeight),
        ImVec2(center.x + S(4.8f), center.y - curveHeight), right, 16);
    draw->PathBezierCubicCurveTo(
        ImVec2(center.x + S(4.8f), center.y + curveHeight),
        ImVec2(center.x - S(4.8f), center.y + curveHeight), left, 16);
    draw->PathStroke(color, ImDrawFlags_Closed, stroke);
    draw->AddCircle(center, S(3.0f), color, 20, stroke);

    if (!visible) {

        draw->AddLine(ImVec2(center.x - S(7.2f), center.y - S(7.2f)),
                      ImVec2(center.x + S(7.2f), center.y + S(7.2f)),
                      color, stroke);
    }
}

bool AuthPasswordField(const char* id, char* buffer, std::size_t bufferSize,
                       bool* reveal, const char* hint, float width,
                       ImGuiInputTextFlags flags = 0) {
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(40.f), S(14.f)));
    const ImGuiID focusId = ImGui::GetID(id);
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    ImGui::SetNextItemAllowOverlap();
    const bool changed = ImGui::InputTextWithHint("##field", hint, buffer, bufferSize,
        *reveal ? flags : (flags | ImGuiInputTextFlags_Password));
    const float height = ImGui::GetItemRectSize().y;
    DrawAuthFocusTransition(focusId, position, width, height);
    const float hitSize = S(36.f);
    const ImVec2 eyeMin(position.x + width - hitSize - S(6.f),
                        position.y + (height - hitSize) * 0.5f);
    ImGui::SetCursorScreenPos(eyeMin);
    ImGui::PushID("eye");
    const bool clicked = ImGui::InvisibleButton("##eye", ImVec2(hitSize, hitSize));
    if (clicked) *reveal = !*reveal;
    const bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    const float hover = AnimateAuthState(ImGui::GetID("##eye_hover"), hovered, 14.0f);
    ImGui::PopID();
    const ImVec2 center(eyeMin.x + hitSize * 0.5f, eyeMin.y + hitSize * 0.5f);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 normal(166.0f / 255.0f, 166.0f / 255.0f, 173.0f / 255.0f, 1.0f);
    const ImVec4 hoverColor(215.0f / 255.0f, 179.0f / 255.0f, 58.0f / 255.0f, 1.0f);
    const ImVec4 iconColor(
        normal.x + (hoverColor.x - normal.x) * hover,
        normal.y + (hoverColor.y - normal.y) * hover,
        normal.z + (hoverColor.z - normal.z) * hover,
        1.0f);
    DrawLucideVisibilityIcon(draw, center, *reveal, CyberTheme::U32(iconColor));
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

bool DrawBackAction(const char* label) {
    const ImVec2 textSize = ImGui::CalcTextSize(label);
    const float arrowWidth = S(17.f);
    const ImVec2 size(textSize.x + arrowWidth, S(24.f));
    CenterCursor(size.x);
    const ImVec2 position = ImGui::GetCursorScreenPos();
    ImGui::PushID("auth_back_to_sign_in");
    const bool pressed = ImGui::InvisibleButton("##back", size);
    const bool hovered = ImGui::IsItemHovered();
    const float hover = AnimateAuthState(ImGui::GetID("##back_hover"), hovered, 11.f);
    ImGui::PopID();

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec4 color(
        CyberTheme::Colors.TextDisabled.x +
            (CyberTheme::Colors.GoldHover.x - CyberTheme::Colors.TextDisabled.x) * hover,
        CyberTheme::Colors.TextDisabled.y +
            (CyberTheme::Colors.GoldHover.y - CyberTheme::Colors.TextDisabled.y) * hover,
        CyberTheme::Colors.TextDisabled.z +
            (CyberTheme::Colors.GoldHover.z - CyberTheme::Colors.TextDisabled.z) * hover,
        1.f);
    const ImU32 packed = CyberTheme::U32(color);
    const ImVec2 arrowTip(position.x + S(2.f), position.y + size.y * 0.5f);
    const ImVec2 arrowEnd(position.x + S(12.f), arrowTip.y);
    draw->AddLine(arrowTip, arrowEnd, packed, S(1.25f));
    draw->AddLine(arrowTip, Add(arrowTip, ImVec2(S(4.f), -S(4.f))), packed, S(1.25f));
    draw->AddLine(arrowTip, Add(arrowTip, ImVec2(S(4.f), S(4.f))), packed, S(1.25f));
    draw->AddText(ImVec2(position.x + arrowWidth, position.y + (size.y - textSize.y) * 0.5f),
                  packed, label);
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

void DrawErrorToast(const ImVec2& windowPos, float windowWidth) {
    if (g_error.empty())
        return;

    static std::string lastError;
    if (lastError != g_error) {
        lastError = g_error;
        OmniGhost::UiFeedback::Play(OmniGhost::UiFeedback::SoundEvent::Error);
    }

    const float toastWidth = (std::min)(S(410.f), windowWidth - S(126.f));
    const float toastHeight = S(43.f);
    const ImVec2 position(windowPos.x + (windowWidth - toastWidth) * 0.5f,
                          windowPos.y + S(18.f));
    const ImVec2 end = Add(position, ImVec2(toastWidth, toastHeight));
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(position, end, IM_COL32(25, 15, 17, 248), S(9.f));
    draw->AddRect(position, end, IM_COL32(205, 69, 79, 116), S(9.f), 0, S(1.f));
    draw->AddCircleFilled(Add(position, ImVec2(S(18.f), toastHeight * 0.5f)),
                          S(3.f), IM_COL32(238, 86, 96, 255), 12);

    const ImVec2 textPos = Add(position, ImVec2(S(31.f), S(9.f)));
    draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), textPos,
                  IM_COL32(240, 220, 222, 255), g_error.c_str(), nullptr,
                  toastWidth - S(62.f));

    const ImVec2 closePos(end.x - S(31.f), position.y + S(6.f));
    ImGui::SetCursorScreenPos(closePos);
    ImGui::PushID("auth_error_toast");
    if (ImGui::InvisibleButton("##dismiss", ImVec2(S(25.f), S(30.f))))
        g_error.clear();
    const bool closeHovered = ImGui::IsItemHovered();
    ImGui::PopID();
    const ImU32 closeColor = closeHovered ? IM_COL32(245, 225, 227, 255)
                                          : IM_COL32(162, 139, 143, 255);
    const ImVec2 closeCenter(closePos.x + S(12.f), closePos.y + S(15.f));
    draw->AddLine(Sub(closeCenter, ImVec2(S(4.f), S(4.f))),
                  Add(closeCenter, ImVec2(S(4.f), S(4.f))), closeColor, S(1.2f));
    draw->AddLine(Add(closeCenter, ImVec2(-S(4.f), S(4.f))),
                  Add(closeCenter, ImVec2(S(4.f), -S(4.f))), closeColor, S(1.2f));
}

void DrawLanding() {
    const Copy& text = Text();
    constexpr float baseWidth = 342.f;
    const float contentWidth = S(baseWidth);

    CenterBlockVertically(S(270.f), 0.42f);
    DrawBrand(true);
    ImGui::Dummy(ImVec2(0, S(27.f)));

    CenterCursor(contentWidth);
    if (AuthPrimaryButton(text.enter, ImVec2(contentWidth, S(42.f))))
        SetScreen(Screen::Login);
    ImGui::Dummy(ImVec2(0, S(9.f)));
    CenterCursor(contentWidth);
    if (CyberWidgets::GhostButton(text.createAccount, ImVec2(contentWidth, S(42.f))))
        SetScreen(Screen::Register);
    ImGui::Dummy(ImVec2(0, S(9.f)));
    if (DrawTextAction(text.exit))
        g_close_requested = true;
}

void DrawLogin() {
    const Copy& text = Text();
    const AuthUiCopy& ui = UiText();


    const float contentWidth = S(420.f);





    ImGui::SetCursorPosY(S(62.f));
    DrawBrand(false);
    DrawPageHeading(ui.welcomeTitle, "");

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    AuthInputField("login_email", g_email, sizeof(g_email), text.email,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    const bool enter = AuthPasswordField("login_password", g_password,
        sizeof(g_password), &g_show_password,
        text.password, contentWidth,
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(0, S(12.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool submit = AuthPrimaryButton(g_remote_operation_pending ? ReadableStatus(ui.authenticating) : text.enter,
        ImVec2(contentWidth, S(46.f))) || enter;
    ImGui::EndDisabled();
    if (submit && !g_remote_operation_pending) {
        if (std::strchr(g_email, '@') == nullptr || g_password[0] == '\0') {
            g_error = CredentialsHint();
        } else {
            StartRemoteOperation(RemoteOperation::WebsiteLogin, g_email, g_password, {});
            SecureClear(g_password, sizeof(g_password));
            ImGui::EndGroup();
            return;
        }
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, S(12.f)));
    CenterCursor(contentWidth);
    if (CyberWidgets::Button(text.createAccount, CyberWidgets::ButtonStyle::Secondary,
            ImVec2(contentWidth, S(46.f))))
        SetScreen(Screen::Register);
}

void DrawRegister() {
    const Copy& text = Text();
    const AuthUiCopy& ui = UiText();

    const float contentWidth = S(420.f);


    ImGui::SetCursorPosY(S(62.f));
    DrawBrand(false);
    DrawPageHeading(ui.createTitle, "");

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    AuthInputField("register_email", g_email, sizeof(g_email), text.email,
                   contentWidth, true, AuthFieldIcon::Email);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    bool submit = AuthPasswordField("register_password", g_password, sizeof(g_password),
        &g_show_password, text.password,
        contentWidth, 0);
    ImGui::Dummy(ImVec2(0, S(10.f)));
    submit = AuthPasswordField("register_key", g_license_key,
        sizeof(g_license_key), &g_show_license_key, LicenseKeyHint(), contentWidth,
        ImGuiInputTextFlags_EnterReturnsTrue) || submit;
    ImGui::Dummy(ImVec2(0, S(14.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool registerPressed = AuthPrimaryButton(
        g_remote_operation_pending ? ReadableStatus(ui.registering) : text.registerAction,
        ImVec2(contentWidth, S(46.f))) || submit;
    ImGui::EndDisabled();
    if (registerPressed && !g_remote_operation_pending) {
        if (g_password[0] == '\0') {
            g_error = ui.enterPassword;
        } else if (std::strlen(g_password) < 8) {
            g_error = ui.passwordTooShort;
        } else if (std::strchr(g_email, '@') == nullptr || g_license_key[0] == '\0') {
            g_error = ui.enterEmailLicense;
        } else {
            StartRemoteOperation(RemoteOperation::WebsiteRegister, g_email,
                                 g_password, g_license_key);
            SecureClear(g_password, sizeof(g_password));
            SecureClear(g_confirm, sizeof(g_confirm));
            SecureClear(g_license_key, sizeof(g_license_key));
            ImGui::EndGroup();
            return;
        }
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, S(12.f)));
    if (DrawBackAction(BackToSignInLabel())) {
        SecureClear(g_password, sizeof(g_password));
        SecureClear(g_confirm, sizeof(g_confirm));
        SecureClear(g_license_key, sizeof(g_license_key));
        SetScreen(Screen::Login);
    }
}

void DrawActivate() {
    const Copy& text = Text();
    const AuthUiCopy& ui = UiText();
    const float contentWidth = S(330.f);
    CenterBlockVertically(S(298.f));
    DrawBrand(false);
    ImGui::Dummy(ImVec2(0, S(24.f)));
    DrawCenteredText(g_upgrade_mode ? ui.upgradeLicense : ui.activateLicense,
        ImGui::ColorConvertU32ToFloat4(CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.70f)));
    ImGui::Dummy(ImVec2(0, S(22.f)));

    CenterCursor(contentWidth);
    ImGui::BeginGroup();
    if (g_upgrade_mode) {
        DrawFieldLabel(ui.user);
        AuthInputField("upgrade_username", g_email, sizeof(g_email), ui.user,
            contentWidth, true, AuthFieldIcon::Email);
        ImGui::Dummy(ImVec2(0, S(12.f)));
    }
    DrawFieldLabel(ui.licenseKey);
    const bool enter = AuthPasswordField("activate_key", g_license_key, sizeof(g_license_key),
        &g_show_license_key, ui.licenseKey, contentWidth, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::Dummy(ImVec2(0, S(18.f)));
    ImGui::BeginDisabled(g_remote_operation_pending);
    const bool activatePressed = AuthPrimaryButton(g_remote_operation_pending
        ? (g_upgrade_mode ? ui.updating : ui.activating)
        : (g_upgrade_mode ? ui.updateAccess : ui.activate),
        ImVec2(contentWidth, S(46.f))) || enter;
    ImGui::EndDisabled();
    if (activatePressed && !g_remote_operation_pending) {
        StartRemoteOperation(g_upgrade_mode ? RemoteOperation::Upgrade : RemoteOperation::Activate,
            g_upgrade_mode ? g_email : std::string{}, {}, g_license_key);
        SecureClear(g_license_key, sizeof(g_license_key));
        ImGui::EndGroup();
        return;
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, S(8.f)));
    if (DrawTextAction(g_upgrade_mode ? ui.activateOnly : ui.updateExisting))
        g_upgrade_mode = !g_upgrade_mode;
    ImGui::Dummy(ImVec2(0, S(12.f)));
    const std::string back = std::string("← ") + text.back;
    if (DrawTextAction(back.c_str())) {
        SecureClear(g_license_key, sizeof(g_license_key));
        g_upgrade_mode = false;
        SetScreen(Screen::Landing);
    }
}

void DrawStartupPreparation(ImDrawList* draw, const ImVec2& windowPos,
                            float windowWidth, float                 ) {
    const AuthUiCopy& ui = UiText();
    const float centerX = windowPos.x + windowWidth * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(windowPos.x + S(32.f), windowPos.y + S(70.f)));
    DrawBrand(false);

    ImGui::SetCursorScreenPos(ImVec2(windowPos.x + S(32.f), windowPos.y + S(292.f)));
    DrawCenteredText(ui.preparingTitle, CyberTheme::Colors.Text,
                     CyberFonts::GetTitleFont());
    ImGui::Dummy(ImVec2(0, S(10.f)));
    DrawCenteredText(ReadableStatus(g_remote_operation_pending ? ui.restoringSession : ui.finishing),
                     CyberTheme::Colors.TextDisabled);

    const float lineWidth = S(188.f);
    const float phase = std::fmod(g_startup_time * 0.72f, 1.0f);
    const ImVec2 lineMin(centerX - lineWidth * 0.5f,
                         ImGui::GetCursorScreenPos().y + S(30.f));
    draw->AddLine(lineMin, ImVec2(lineMin.x + lineWidth, lineMin.y),
        CyberTheme::WithAlpha(CyberTheme::Colors.TextDisabled, .16f), S(1.f));
    const float segment = lineWidth * .24f;
    const float start = lineMin.x + (lineWidth + segment) * phase - segment;
    draw->PushClipRect(lineMin, ImVec2(lineMin.x + lineWidth, lineMin.y + S(4.f)), true);
    draw->AddLine(ImVec2(start, lineMin.y), ImVec2(start + segment, lineMin.y),
        CyberTheme::WithAlpha(CyberTheme::Colors.GoldHover, .94f), S(2.f));
    draw->PopClipRect();



    const float dotY = lineMin.y + S(24.f);
    for (int index = 0; index < 3; ++index) {
        const float wave = app_settings::AnimationScale() <= 0.f ? 1.f :
            0.30f + 0.70f * (0.5f + 0.5f * std::sin(g_startup_time * 4.2f - index * 0.9f));
        draw->AddCircleFilled(ImVec2(centerX + S((index - 1) * 16.f), dotY), S(2.2f),
            CyberTheme::WithAlpha(CyberTheme::Colors.Gold, wave), 12);
    }
}

}

void Reset() {









    using namespace std::chrono_literals;
    if (g_remote_operation.valid()) {
        if (g_remote_operation.wait_for(0ms) == std::future_status::ready) {
            try {
                (void)g_remote_operation.get();
            } catch (const std::exception&) {
                WriteAuthStage("auth-reset-result-exception");
            } catch (...) {
                WriteAuthStage("auth-reset-result-unknown-exception");
            }
        } else {
            ++g_operation_generation;
            g_draining_operations.push_back(std::move(g_remote_operation));
        }
    }
    g_initialized = false;
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
        SetScreen(g_screen == Screen::Register ? Screen::Login : Screen::Landing);
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
        if (!g_remote_operation_pending)
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
            WriteAuthStage("auth-ui-complete");
            g_authenticated = true;
        }
    }
    if (animation <= 0.f) g_transition = 1.f;
    else g_transition = (std::min)(1.0f, g_transition + delta /
        (CyberTheme::Metrics::PageTransitionSeconds * (0.80f + 0.20f * animation)));
    const float eased = g_transition * g_transition * (3.0f - 2.0f * g_transition);

    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled(ImVec2(0, 0), display, IM_COL32(3, 3, 4, 255));
    CyberTheme::DrawSubtleNoise(background, ImVec2(0, 0), display, 0.012f, 0x41555448u, 180);



    const ImVec2 glowCenter(display.x * 0.5f, display.y * 0.30f);
    background->AddCircleFilled(glowCenter, S(190.f),
        CyberTheme::WithAlpha(CyberTheme::Colors.Gold, 0.008f), 64);





    const float desiredWidth = S(564.f);
    const float desiredHeight = S(624.f);
    const float width = (std::max)(S(340.f), (std::min)(desiredWidth, display.x - S(20.f)));
    const float availableHeight = (std::max)(S(300.f), display.y - S(24.f));
    const float height = (std::min)(desiredHeight, availableHeight);
    const bool needsVerticalScroll = desiredHeight > availableHeight + 0.5f;
    const float entryOffset = animation > 0.f ? (1.f - entryEased) * S(5.f) : 0.f;
    const ImVec2 position((display.x - width) * 0.5f,
                          (display.y - height) * 0.5f + entryOffset);

    ImGui::SetNextWindowPos(position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(34.f), S(28.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, CyberTheme::Radius::Lg);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, entryEased);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.043f, 0.043f, 0.051f, 0.985f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16f, 0.16f, 0.18f, 0.0f));
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
    draw->AddRectFilledMultiColor(
        windowPos, Add(windowPos, ImVec2(width, S(180.f))),
        IM_COL32(18, 18, 20, 38), IM_COL32(18, 18, 20, 38),
        IM_COL32(11, 11, 13, 0), IM_COL32(11, 11, 13, 0));
    if (!g_startup_preparing)
        DrawLanguageSelector(windowPos, width);

    const float pageSlide = animation > 0.f ? (1.f - eased) * S(12.f) : 0.f;
    ImGui::SetCursorScreenPos(ImVec2(windowPos.x + S(24.f) + pageSlide, windowPos.y + S(19.f)));
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
        const AuthUiCopy& ui = UiText();
        const char* status = success
            ? (g_completion == Completion::Register ? ui.accountCreated : ui.accessGranted)
            : ReadableStatus(ui.finishing);
        const float centerY = windowPos.y + height * 0.43f;
        if (success) {
            const float duration = (std::max)(0.12f, 0.30f * (std::max)(0.35f, animation));
            const float checkProgress = animation <= 0.f ? 1.f
                : (std::min)(1.f, g_completion_time / duration);
            const ImVec2 checkCenter(windowPos.x + width * 0.5f, centerY - S(74.f));
            const ImVec2 checkA(checkCenter.x - S(12.f), checkCenter.y);
            const ImVec2 checkB(checkCenter.x - S(3.f), checkCenter.y + S(9.f));
            const ImVec2 checkC(checkCenter.x + S(15.f), checkCenter.y - S(11.f));
            draw->AddCircle(checkCenter, S(23.f),
                CyberTheme::WithAlpha(CyberTheme::Colors.Success, 0.30f), 32, S(1.2f));
            if (checkProgress <= 0.42f) {
                const float segmentProgress = checkProgress / 0.42f;
                draw->AddLine(checkA,
                    ImVec2(checkA.x + (checkB.x - checkA.x) * segmentProgress,
                           checkA.y + (checkB.y - checkA.y) * segmentProgress),
                    CyberTheme::U32(CyberTheme::Colors.Success), S(2.4f));
            } else {
                draw->AddLine(checkA, checkB, CyberTheme::U32(CyberTheme::Colors.Success), S(2.4f));
                const float segmentProgress = (checkProgress - 0.42f) / 0.58f;
                draw->AddLine(checkB,
                    ImVec2(checkB.x + (checkC.x - checkB.x) * segmentProgress,
                           checkB.y + (checkC.y - checkB.y) * segmentProgress),
                    CyberTheme::U32(CyberTheme::Colors.Success), S(2.4f));
            }
        }
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
    DrawAuthFooter(windowPos, width, height);
    DrawErrorToast(windowPos, width);
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

}
