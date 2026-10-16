#include "support_error.h"

namespace OmniGhost::Support {

std::string_view Code(ErrorCode error) noexcept
{
    switch (error) {
    case ErrorCode::None:                   return "OG-OK-000";
    case ErrorCode::RendererUnavailable:    return "OG-UI-101";
    case ErrorCode::DmaUnavailable:         return "OG-DMA-201";
    case ErrorCode::DmaOpenFailed:          return "OG-DMA-202";
    case ErrorCode::ProcessInventoryUnavailable: return "OG-DMA-203";
    case ErrorCode::MemoryReadUnavailable:  return "OG-DMA-204";
    case ErrorCode::GameDataUnverified:     return "OG-GAME-403";
    case ErrorCode::RuntimeDependenciesInvalid: return "OG-DMA-205";
    case ErrorCode::GameInitializationFailed: return "OG-GAME-404";
    case ErrorCode::GameModuleUnavailable: return "OG-GAME-405";
    case ErrorCode::GameAdapterUnavailable: return "OG-GAME-406";
    case ErrorCode::LaunchCancelled: return "OG-GAME-407";
    case ErrorCode::InputUnavailable:       return "OG-INP-301";
    case ErrorCode::GameNotFound:           return "OG-GAME-401";
    case ErrorCode::OffsetsUnsupported:     return "OG-OFF-402";
    case ErrorCode::RadarAuthentication:    return "OG-RAD-501";
    case ErrorCode::UpdaterFailed:          return "OG-UPD-601";
    case ErrorCode::DiagnosticExportFailed: return "OG-SUP-701";
    }
    return "OG-UNK-999";
}

std::string_view Message(ErrorCode error) noexcept
{
    switch (error) {
    case ErrorCode::None:                   return "Nenhum problema detetado.";
    case ErrorCode::RendererUnavailable:    return "O renderizador não está disponível.";
    case ErrorCode::DmaUnavailable:         return "O dispositivo DMA não está disponível.";
    case ErrorCode::DmaOpenFailed:          return "O DMA foi detetado, mas a sessão de leitura não abriu.";
    case ErrorCode::ProcessInventoryUnavailable: return "Não foi possível consultar os processos no PC do jogo.";
    case ErrorCode::MemoryReadUnavailable:  return "O processo foi encontrado, mas a memória não pôde ser lida.";
    case ErrorCode::GameDataUnverified:     return "Não foi possível validar os dados lidos do jogo.";
    case ErrorCode::RuntimeDependenciesInvalid: return "Os componentes necessários para o DMA falharam a verificação.";
    case ErrorCode::GameInitializationFailed: return "A inicialização do menu do jogo falhou.";
    case ErrorCode::GameModuleUnavailable: return "Um módulo necessário do jogo ainda não está disponível.";
    case ErrorCode::GameAdapterUnavailable: return "O adapter deste jogo não está disponível nesta versão.";
    case ErrorCode::LaunchCancelled: return "O arranque do menu foi cancelado.";
    case ErrorCode::InputUnavailable:       return "O dispositivo de entrada não está disponível.";
    case ErrorCode::GameNotFound:           return "O processo do jogo não foi encontrado.";
    case ErrorCode::OffsetsUnsupported:     return "A build do jogo não possui offsets validados.";
    case ErrorCode::RadarAuthentication:    return "O acesso ao radar foi recusado.";
    case ErrorCode::UpdaterFailed:          return "A atualização falhou.";
    case ErrorCode::DiagnosticExportFailed: return "A exportação do diagnóstico falhou.";
    }
    return "Ocorreu um erro não identificado.";
}

}
