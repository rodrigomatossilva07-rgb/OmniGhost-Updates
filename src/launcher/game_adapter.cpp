#include "game_adapter.h"

namespace OmniGhost::Launcher {

std::string_view AdapterErrorMessage(AdapterErrorCode code) noexcept {
    switch (code) {
    case AdapterErrorCode::None: return {};
    case AdapterErrorCode::UnsupportedGame: return "Este jogo não está disponível nesta versão.";
    case AdapterErrorCode::AttachFailed: return "Não foi possível estabelecer ligação ao jogo.";
    case AdapterErrorCode::DeviceOpenFailed: return "Não foi possível abrir uma sessão de leitura DMA.";
    case AdapterErrorCode::ProcessNotFound: return "O processo do jogo não foi confirmado.";
    case AdapterErrorCode::ProcessBindFailed: return "O processo foi encontrado, mas a memória não pôde ser validada.";
    case AdapterErrorCode::ModuleNotFound: return "Um módulo necessário do jogo não foi encontrado.";
    case AdapterErrorCode::OffsetsMissing: return "Os offsets necessários estão em falta.";
    case AdapterErrorCode::UnsupportedBuild: return "Esta versão do jogo ainda não é suportada.";
    case AdapterErrorCode::GameDataUnverified: return "A leitura dos dados do jogo não passou na validação.";
    case AdapterErrorCode::UnexpectedException: return "Ocorreu uma falha inesperada ao abrir o menu.";
    case AdapterErrorCode::ValidationFailed: return "A validação da sessão do jogo falhou.";
    case AdapterErrorCode::InitializationFailed: return "Não foi possível inicializar a sessão do jogo.";
    case AdapterErrorCode::Cancelled: return "A preparação da sessão foi cancelada.";
    }
    return "Ocorreu um erro desconhecido no adapter do jogo.";
}

} // namespace OmniGhost::Launcher
