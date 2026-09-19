#include "game_adapter.h"

namespace OmniGhost::Launcher {

std::string_view AdapterErrorMessage(AdapterErrorCode code) noexcept {
    switch (code) {
    case AdapterErrorCode::None: return {};
    case AdapterErrorCode::UnsupportedGame: return "Este jogo não está disponível nesta versão.";
    case AdapterErrorCode::AttachFailed: return "Não foi possível estabelecer ligação ao jogo.";
    case AdapterErrorCode::ValidationFailed: return "A validação da sessão do jogo falhou.";
    case AdapterErrorCode::InitializationFailed: return "Não foi possível inicializar a sessão do jogo.";
    case AdapterErrorCode::Cancelled: return "A preparação da sessão foi cancelada.";
    }
    return "Ocorreu um erro desconhecido no adapter do jogo.";
}

} // namespace OmniGhost::Launcher
