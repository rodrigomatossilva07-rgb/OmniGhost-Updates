# Build e publicação

`config/release/version.txt` é a fonte da versão. `tools/sync_version.ps1`
atualiza os ficheiros C++/RC/manifesto derivados; a build valida-os. A build
gera `.cache/generated/<Configuration>/x64/build-metadata.json` e o header
embutido usando `git rev-parse HEAD`, a data do commit e `git status`.
Os ficheiros `SOURCE_BUILD_METADATA.json` e `SOURCE_PACKAGE_MANIFEST.json`
só são criados dentro de um arquivo de código-fonte, por
`tools/Create-SourcePackage.ps1`; não são editados no repositório.

Sequência: MSBuild valida o projeto e compila o EXE; `Publish` chama
`Test-CommercialPreflight.ps1`; `Package-Release.ps1` chama o empacotador,
que calcula os hashes, gera SBOM e manifestos e assina com o certificado real
configurado; `Validate-CommercialRelease.ps1` verifica o resultado.
O ZIP inclui `THIRD_PARTY_NOTICES.txt` junto ao EXE e o manifesto de instalação
inclui o hash de ambos.
O workflow GitHub faz a verificação DMA obrigatória antes da publicação remota.
`Release|x64` é a build local sem assinatura. Ausência de certificado é um
bloqueio de assinatura comercial, não uma obrigação de comprar licença do C++.

Preparação da versão: editar apenas `version.txt`, executar
`powershell -ExecutionPolicy Bypass -File .\tools\sync_version.ps1 -ProjectDir .`,
rever os ficheiros derivados e fazer commit com a identidade Git do responsável.
Depois da validação local, `Publish` cria a tag `v<versão>` no commit exato,
envia-a a `origin` e o workflow remoto executa o teste DMA obrigatório antes
de publicar. Os scripts não criam commits com autores fictícios nem movem tags
existentes. Um push falhado deixa a tag local para tentativa posterior.

O campo `reproducible` permanece `false` até existir uma comparação
independente de dois binários construídos do mesmo commit e ambiente.
O campo `reproducibility_prerequisites_met` só regista pré-condições; não é
prova de binários iguais. A política comercial atual exige prova antes de
empacotar. Esta verificação ainda precisa de automatização para desbloquear
uma publicação comercial.

Antes de vender, resolver também as pendências de licenças em
`docs/DEPENDENCY_LICENSE_AUDIT.md`, fornecer avisos/fontes aplicáveis e
configurar certificado de assinatura e chave pública de confiança.
