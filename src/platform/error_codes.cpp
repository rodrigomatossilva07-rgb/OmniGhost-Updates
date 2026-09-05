#include "error_codes.h"
#include <array>

namespace OmniGhost::Platform {

namespace {

struct ErrorEntry {
    OmniGhost::Platform::ErrorCode code;
    std::string_view message;
    bool retryable;
    bool user_actionable;
    std::string_view subsystem;
};

constexpr std::array<ErrorEntry, 88> kErrorTable = {{
    // Generic
    {OmniGhost::Platform::ErrorCode::None, "OK", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::Unknown, "Erro desconhecido", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::NotImplemented, "Funcionalidade nao implementada", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::InvalidArgument, "Argumento invalido", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::NotSupported, "Nao suportado", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::OutOfMemory, "Memoria insuficiente", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::Timeout, "Tempo excedido", true, true, "core"},
    {OmniGhost::Platform::ErrorCode::Cancelled, "Operacao cancelada", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::AlreadyExists, "Ja existe", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::NotFound, "Nao encontrado", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::AccessDenied, "Acesso negado", false, true, "core"},
    {OmniGhost::Platform::ErrorCode::InvalidState, "Estado invalido", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::CorruptedData, "Dados corrompidos", false, false, "core"},
    {OmniGhost::Platform::ErrorCode::VersionMismatch, "Versao incompativel", false, true, "core"},
    {OmniGhost::Platform::ErrorCode::NetworkError, "Erro de rede", true, true, "network"},
    {OmniGhost::Platform::ErrorCode::DiskError, "Erro de disco", false, false, "filesystem"},
    {OmniGhost::Platform::ErrorCode::PermissionDenied, "Permissao negada", false, true, "core"},
    {OmniGhost::Platform::ErrorCode::IntegrityCheckFailed, "Falha na verificacao de integridade", false, false, "core"},

    // DMA / Hardware
    {OmniGhost::Platform::ErrorCode::DmaDeviceNotFound, "Dispositivo DMA nao encontrado", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaDeviceOpenFailed, "Falha ao abrir dispositivo DMA", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaDeviceDataPathFailed, "Caminho de dados DMA sem resposta", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaVmmInitFailed, "Falha ao inicializar sessao VMM", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaVmmSessionRecoveryFailed, "Sessao VMM perdida", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaPluginInitFailed, "Plugins VMM falharam", true, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaProcessNotFound, "Jogo nao encontrado", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaProcInfoGenerating, "Gerando informacoes de processo...", true, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaProcInfoTimeout, "ProcInfo excedeu o tempo", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaProcInfoStuck, "ProcInfo preso em 0%", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaProcInfoCancelled, "ProcInfo cancelado", false, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaMappingUnavailable, "Mapeamento de memoria indisponivel", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaModuleValidationFailed, "Aguardando validacao de modulo", true, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaRuntimeUnavailable, "Jogo anexado; aguardando runtime", true, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaOperationSuperseded, "Operacao cancelada/substituida", false, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaDependencyMismatch, "Dependencias DMA invalidas", false, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaBackendBusy, "Motor ocupado", true, false, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaFpgaLost, "FPGA perdida", true, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaInvalidConfiguration, "Configuracao DMA invalida", false, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaDriverVersionMismatch, "Versao de driver incompativel", false, true, "dma"},
    {OmniGhost::Platform::ErrorCode::DmaFirmwareTooOld, "Firmware muito antigo", false, true, "dma"},

    // Authentication / License
    {OmniGhost::Platform::ErrorCode::AuthInvalidCredentials, "Credenciais invalidas", false, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthExpired, "Licenca expirada", false, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthRevoked, "Licenca revogada", false, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthNetworkError, "Erro de rede na autenticacao", true, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthServerUnavailable, "Servidor indisponivel", true, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthInvalidKey, "Chave invalida", false, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthHwidMismatch, "Hardware nao corresponde a licenca", false, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthEntitlementMissing, "Produto nao licenciado", false, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthTrialExpired, "Trial expirado", false, true, "auth"},
    {OmniGhost::Platform::ErrorCode::AuthConcurrentLimit, "Limite de sessoes excedido", false, true, "auth"},

    // Offsets / Game
    {OmniGhost::Platform::ErrorCode::OffsetsMissing, "Offsets em falta", false, true, "offsets"},
    {OmniGhost::Platform::ErrorCode::OffsetsOutdated, "Offsets desatualizados", false, true, "offsets"},
    {OmniGhost::Platform::ErrorCode::OffsetsValidationFailed, "Validacao de offsets falhou", true, true, "offsets"},
    {OmniGhost::Platform::ErrorCode::OffsetsNotLoaded, "Offsets nao carregados", false, false, "offsets"},
    {OmniGhost::Platform::ErrorCode::GameNotRunning, "Jogo nao esta em execucao", true, true, "game"},
    {OmniGhost::Platform::ErrorCode::GameAttachFailed, "Falha ao anexar ao jogo", true, true, "game"},
    {OmniGhost::Platform::ErrorCode::GameProcessLost, "Processo do jogo perdido", true, false, "game"},
    {OmniGhost::Platform::ErrorCode::GameModuleNotFound, "Modulo do jogo nao encontrado", true, true, "game"},
    {OmniGhost::Platform::ErrorCode::GameOffsetsOutdated, "Offsets do jogo desatualizados", false, true, "offsets"},
    {OmniGhost::Platform::ErrorCode::GameUnsupportedBuild, "Build do jogo nao suportado", false, true, "game"},

    // Updater
    {OmniGhost::Platform::ErrorCode::UpdateCheckFailed, "Falha ao verificar atualizacoes", true, true, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateDownloadFailed, "Falha ao baixar atualizacao", true, true, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateIntegrityFailed, "Integridade da atualizacao falhou", false, false, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateApplyFailed, "Falha ao aplicar atualizacao", false, false, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateRollbackFailed, "Falha no rollback", false, false, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateNoSpace, "Espaco em disco insuficiente", false, true, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateLocked, "Atualizacao bloqueada", false, false, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateManifestInvalid, "Manifesto invalido", false, false, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateSignatureInvalid, "Assinatura invalida", false, false, "updater"},
    {OmniGhost::Platform::ErrorCode::UpdateVersionIncompatible, "Versao incompativel", false, true, "updater"},

    // Configuration
    {OmniGhost::Platform::ErrorCode::ConfigLoadFailed, "Falha ao carregar configuracoes", false, false, "config"},
    {OmniGhost::Platform::ErrorCode::ConfigSaveFailed, "Falha ao salvar configuracoes", false, false, "config"},
    {OmniGhost::Platform::ErrorCode::ConfigCorrupted, "Configuracao corrompida", false, true, "config"},
    {OmniGhost::Platform::ErrorCode::ConfigSchemaMismatch, "Schema de configuracao incompativel", false, true, "config"},
    {OmniGhost::Platform::ErrorCode::ConfigMigrationFailed, "Migracao de configuracao falhou", false, false, "config"},

    // Network
    {OmniGhost::Platform::ErrorCode::NetworkDnsFailed, "Falha na resolucao DNS", true, false, "network"},
    {OmniGhost::Platform::ErrorCode::NetworkConnectionFailed, "Falha na conexao", true, true, "network"},
    {OmniGhost::Platform::ErrorCode::NetworkTimeout, "Tempo de conexao excedido", true, true, "network"},
    {OmniGhost::Platform::ErrorCode::NetworkSslError, "Erro SSL/TLS", false, false, "network"},
    {OmniGhost::Platform::ErrorCode::NetworkRateLimited, "Taxa de requisicoes excedida", true, true, "network"},

    // Filesystem
    {OmniGhost::Platform::ErrorCode::FsNotFound, "Ficheiro nao encontrado", false, true, "filesystem"},
    {OmniGhost::Platform::ErrorCode::FsAccessDenied, "Acesso negado ao ficheiro", false, true, "filesystem"},
    {OmniGhost::Platform::ErrorCode::FsFull, "Disco cheio", false, true, "filesystem"},
    {OmniGhost::Platform::ErrorCode::FsCorrupted, "Ficheiro corrompido", false, false, "filesystem"},
    {OmniGhost::Platform::ErrorCode::FsLocked, "Ficheiro bloqueado", true, true, "filesystem"},

    // UI / Rendering
    {OmniGhost::Platform::ErrorCode::UiRendererInitFailed, "Falha ao inicializar renderer", false, false, "ui"},
    {OmniGhost::Platform::ErrorCode::UiFontLoadFailed, "Falha ao carregar fonte", false, false, "ui"},
    {OmniGhost::Platform::ErrorCode::UiTextureLoadFailed, "Falha ao carregar textura", false, false, "ui"},
    {OmniGhost::Platform::ErrorCode::UiLayoutError, "Erro de layout", false, false, "ui"},
}};

std::string_view ErrorMessage(OmniGhost::Platform::ErrorCode code) noexcept {
    for (const auto& entry : kErrorTable) {
        if (entry.code == code) return entry.message;
    }
    return "Erro desconhecido";
}

bool IsRetryable(OmniGhost::Platform::ErrorCode code) noexcept {
    for (const auto& entry : kErrorTable) {
        if (entry.code == code) return entry.retryable;
    }
    return false;
}

bool IsUserActionable(OmniGhost::Platform::ErrorCode code) noexcept {
    for (const auto& entry : kErrorTable) {
        if (entry.code == code) return entry.user_actionable;
    }
    return false;
}

std::string_view ErrorSubsystem(OmniGhost::Platform::ErrorCode code) noexcept {
    for (const auto& entry : kErrorTable) {
        if (entry.code == code) return entry.subsystem;
    }
    return "core";
}

} // namespace OmniGhost::Platform