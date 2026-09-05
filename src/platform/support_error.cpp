#include "support_error.h"

namespace OmniGhost::Support {

std::string_view Code(ErrorCode error) noexcept
{
    switch (error) {
    case ErrorCode::None:                   return "OG-OK-000";
    case ErrorCode::RendererUnavailable:    return "OG-UI-101";
    case ErrorCode::DmaUnavailable:         return "OG-DMA-201";
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
    case ErrorCode::InputUnavailable:       return "O dispositivo de entrada não está disponível.";
    case ErrorCode::GameNotFound:           return "O processo do jogo não foi encontrado.";
    case ErrorCode::OffsetsUnsupported:     return "A build do jogo não possui offsets validados.";
    case ErrorCode::RadarAuthentication:    return "O acesso ao radar foi recusado.";
    case ErrorCode::UpdaterFailed:          return "A atualização falhou.";
    case ErrorCode::DiagnosticExportFailed: return "A exportação do diagnóstico falhou.";
    }
    return "Ocorreu um erro não identificado.";
}

} // namespace OmniGhost::Support
