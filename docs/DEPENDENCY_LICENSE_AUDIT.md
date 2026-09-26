# Auditoria de dependências para distribuição

Estado em 2026-09-24. Esta tabela distingue a licença do código de uma eventual
subscrição do serviço e assinala versões que não estão provadas pelos ficheiros
locais. A ausência de uma licença no pacote não é prova de licença permissiva.

| Componente usado | Versão identificada | Licença declarada a montante | Aviso/atribuição | Código-fonte a disponibilizar | Estado |
|---|---|---|---|---|---|
| MemProcFS (`vmm.dll`, `vmm.lib`, plugins) | Binário detetado 5.18.11.250 | AGPL-3.0; alguns plugins têm licenças distintas | Sim | Avaliar obrigações AGPL para a distribuição e ligação concreta | **Pendente:** obter licença aplicável a cada binário e plano de conformidade; não foi identificada alternativa permissiva compatível com este adaptador DMA. |
| LeechCore (`leechcore.dll`, `.lib`, plugins) | Binário detetado 2.23.3.103 | GPL-3.0 no repositório principal; plugins podem diferir | Sim | Avaliar obrigações GPL para a distribuição e ligação concreta | **Pendente:** revisão jurídica/técnica ou autorização alternativa documentada. |
| FTDI D3XX (`FTD3XXWU.dll`, `FTD3XX.dll`) | 1.4.0.1 / 1.3.0.10 | Termos de redistribuição FTDI, não MIT | Preservar informação de licença FTDI | Não confirmado nos termos locais | **Pendente:** arquivar os termos incluídos nos pacotes exatos. |
| Plugins DMA (`vmmyara.dll`, `tinylz4.dll`, `leechcore_device_*.dll`, `leechcore_driver.dll`) | Manifesto local não regista versões individuais | Licenças por plugin; não inferir GPL único | Pendente | Pendente | **Pendente:** mapear hash de cada DLL ao respetivo código, licença e avisos. |
| DLL de depuração (`dbghelp.dll`, `symsrv.dll`, `pdbcrust.dll`) | Sem versão registada no inventário | Microsoft/terceiros conforme o binário exato | Pendente | Pendente | **Pendente:** documentar proveniência e termos de redistribuição de cada DLL. |
| KeyAuth C++ SDK e serviço | SDK sem tag/commit registado; API 1.3 | Elastic License 2.0 declarada pelo fornecedor | Sim | Consultar termos ELv2; não assumir que é MIT | **Bloqueio comercial:** plano gratuito do serviço limitado a 10 utilizadores por aplicação; migrar para backend próprio ou confirmar contrato/plano antes de escalar. |
| Dear ImGui | `1.89.8 WIP` nos headers | MIT | Sim | Não | Guardar texto MIT no material distribuído. |
| cloudflared | 2026.8.2 | Apache-2.0 | Sim, incluindo NOTICE aplicável | Não, salvo modificações sujeitas aos termos | Preservar licença e avisos do binário oficial. |
| libcurl (`keyauth/x64/lib/curl`) | Header 8.10.1-DEV; `.lib` sem versão provada | curl | Sim | Não | Provar que o `.lib` corresponde ao header e incluir aviso. |
| libsodium (`keyauth/x64/lib/libsodium`) | Header 1.0.20; `.lib` sem versão provada | ISC a montante | Sim | Não | Provar que o `.lib` corresponde ao header. |
| nlohmann/json (`keyauth`) | Header 3.9.1 | MIT a montante | Sim | Não | Confirmar se esta cópia foi modificada. |
| zlib (`keyauth/x64/lib/curl/zlib.lib`) | Sem versão provada | zlib a montante | Aviso na distribuição de fonte; reconhecimento no produto apreciado | Não | Determinar versão do `.lib` exato. |
| Nayuki QR Code (`keyauth/x64/lib/QRCode/qrcode.*`) | Cópia sem tag/commit | MIT no cabeçalho local | Sim | Não | Conservar o aviso. |
| Nayuki Tiny PNG Output (`keyauth/x64/lib/QRCode/pngout.*`) | Cópia sem tag/commit | LGPL-3.0-ou-posterior no cabeçalho local | Sim | Sim, e avaliar direito de relink da biblioteca estática | **Bloqueio:** `dumpbin` encontra `TinyPngOut` dentro de `library_x64.lib`; textos COPYING referidos não estão no diretório. |
| CS2 WebRadar (`src/games/Cs2/radar_webapp`) | Cópia de `clauadv/cs2_webradar`, commit não registado | GPL-3.0 declarado no `NOTICE.txt` local | Sim | Avaliar obrigação GPL e fornecer fonte correspondente quando distribuído | **Pendente:** provar versão/origem e preservar licença completa. |
| Mapas/imagens CS2 e outros recursos | Várias cópias locais | Por ficheiro; dois mapas contêm ficheiros `LICENSE` | Por licença | Por licença | Fazer correspondência entre cada recurso distribuído e o titular/licença; mudanças locais recentes ainda não verificadas. |
| Visual Studio/MSVC + Windows SDK | Ferramenta local 14.51; SDK 10.0.26100.0 | Termos Microsoft, não licença open source | Conforme redistribuíveis | Não aplicável | Visual Studio Community permite apps pagas para indivíduo; organizações têm limites de elegibilidade. |
| Microsoft VC Runtime embutido (`vcruntime140.dll`) | Versão do ficheiro ainda não registada nesta auditoria | Termos de redistribuição Microsoft | Conforme pacote redistribuível | Não aplicável | Conferir que a cópia embutida provém do pacote MSVC elegível. |

Fontes primárias: [MemProcFS](https://github.com/ufrisk/MemProcFS),
[LeechCore](https://github.com/ufrisk/LeechCore),
[FTDI D3XX](https://ftdichip.com/drivers/d3xx-drivers/),
[KeyAuth SDK](https://github.com/KeyAuth/KeyAuth-CPP-Example),
[preços KeyAuth](https://keyauth.cc/pricing/),
[ImGui](https://github.com/ocornut/imgui),
[cloudflared](https://github.com/cloudflare/cloudflared),
[curl](https://curl.se/docs/copyright.html),
[libsodium](https://github.com/jedisct1/libsodium),
[QR Code generator](https://github.com/nayuki/QR-Code-generator),
[nlohmann/json](https://github.com/nlohmann/json),
[zlib](https://www.zlib.net/zlib_license.html),
[Visual Studio Community](https://visualstudio.microsoft.com/vs/pricing/).

Uma licença GPL/AGPL não exige, por si só, uma taxa comercial. Pode, porém,
impor obrigações incompatíveis com a distribuição pretendida do binário fechado.
O inventário acima não constitui autorização de venda; falta prova de licença
para vários binários empacotados e avaliação da combinação/linkagem.
