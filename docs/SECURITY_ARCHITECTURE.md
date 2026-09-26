# OmniGhost: controlo defensivo de integridade e sessão

## Fluxo anterior

O launcher autenticava a conta via `LicenseGateway`/KeyAuth e comparava as subscrições devolvidas com o ID do jogo antes de abrir o adaptador. O `Publish` assinava o EXE exterior, criava um inventário SHA-256 assinado dos ficheiros instalados e um `update.json` assinado. O cliente verificava esse inventário apenas quando o utilizador iniciava manualmente a verificação de instalação. A payload privada e as DLLs embebidas eram verificadas no bootstrap, mas não havia uma verificação obrigatória da cadeia completa antes de entrar no launcher. A sessão autenticada podia permanecer válida indefinidamente na memória.

`src/platform/security.h` continha placeholders não usados que prometiam hashes sem os calcular e terminavam o processo ao detetar um debugger ou uma pausa de execução. Foram removidos; não eram uma proteção fiável.

## Fluxo atual

Numa build `Publish`, o core exige um EXE exterior assinado pelo publisher configurado em `release_trust.h`. Compara o SHA-256 da própria payload com o recurso RCDATA 3001 do EXE assinado, valida a assinatura do inventário de instalação, confere os ficheiros e valida as DLLs críticas (`vmm.dll`, `leechcore.dll`, `FTD3XXWU.dll`) contra o manifesto embebido. A falha confirmada bloqueia as funcionalidades protegidas localmente e produz um evento `SEC-*` sem segredo ou dados pessoais. Uma build `Release` local mantém o fluxo de desenvolvimento utilizável. O core volta a testar a autorização antes de abrir um jogo e durante a sessão do jogo.

Após login, registo ou ativação, um worker revalida a sessão KeyAuth a cada 15 minutos. Um resultado remoto válido renova a janela local de 20 minutos. Falhas de rede não são classificadas como ataque, mas a autorização deixa de valer se a janela expirar. Revogação/expiração confirmada remove a autorização. Não há banimento permanente no cliente; decisões dessa natureza pertencem ao KeyAuth/operador.

O logout invalida sempre a autorização local e tenta terminar a sessão KeyAuth no servidor. A lista de produtos e o nome da conta são mantidos em cache no gateway para que uma revalidação de rede nunca segure o bloqueio usado pela thread de renderização.

Os eventos `SEC-001` a `SEC-004` representam falhas de confiança da release, assinatura, manifesto e ficheiros críticos. `SEC-005` indica sessão inválida; `SEC-006` indisponibilidade da revalidação; `SEC-007` recuperação. `SEC-008` e `SEC-009` estão reservados para observações de debugger e módulo desconhecido e nunca causam banimento por si. `SEC-010` indica que a consulta da revogação Authenticode não está disponível: o arranque fica pendente, sem registar adulteração nem banir. Os logs usam apenas ID/severidade/ação, sem licença, credenciais, HWID ou lista de processos. Não há envio automático de telemetria de segurança.

## Ordem de Publish

Compilação do core → incorporação da payload e runtime no bootstrap → assinatura Authenticode do EXE exterior → cálculo dos hashes dos ficheiros finais → assinatura do inventário de instalação e `update.json` → validação das assinaturas, hashes e conteúdo do ZIP → teste de integração DMA obrigatório no runner → publicação. Nenhum binário assinado é modificado após a assinatura. A chave privada e o certificado não pertencem ao repositório.

## Validação manual numa máquina limpa

1. Configurar o certificado real e a âncora pública com `tools/Configure-ReleaseTrust.ps1`; fazer commit da âncora pública e construir a partir de uma árvore limpa.
2. Criar o pacote comercial e executar `tools/Validate-CommercialRelease.ps1` sobre os artefactos finais.
3. Confirmar `signtool verify /pa /v OmniGhost.exe` e abrir o EXE assinado; autenticar e iniciar uma sessão autorizada.
4. Em cópias isoladas do ZIP, alterar um byte do EXE, de uma DLL embebida ou de `install-manifest.sha256`; a validação do pacote ou o arranque deve falhar. Não testar sobre uma instalação real.
5. Simular indisponibilidade temporária de rede e confirmar que não há banimento, que a sessão expira após o prazo local e que um novo login válido recupera o acesso.

## Limites

O cliente corre numa máquina controlada pelo utilizador e não consegue garantir inviolabilidade. Não existe backend próprio de análise de eventos, revisão administrativa ou banimento; o KeyAuth continua a ser a autoridade de licenças. A build comercial e os testes de tampering in vivo exigem o certificado real, o runner DMA e a conta KeyAuth configurada. Debuggers, VMs e DLLs desconhecidas não são tratados como fraude. O SHA-256 das DLLs verifica os ficheiros em disco, não é um detetor genérico de hooks em memória. O bootstrap repara ficheiros privados danificados a partir da sua payload assinada antes de o core iniciar, pelo que um ficheiro previamente alterado pode ser substituído em vez de gerar uma rejeição visível.
