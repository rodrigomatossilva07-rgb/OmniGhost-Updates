[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),

    [Parameter(Mandatory = $false)]
    [string]$BuildDir = '',

    [Parameter(Mandatory = $false)]
    [string]$ReleaseDirectory = '',

    [switch]$SkipPackaging,
    [switch]$ValidateOnly,
    [switch]$ExplicitConfirmed
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ProjectDir 'build\Publish'
}
$BuildDir = [IO.Path]::GetFullPath((Join-Path $BuildDir '.'))

# Downloads contains only convenience copies made during packaging. Canonical
# release assets live in artifacts\release and are never removed here.
try {

$configPath = Join-Path $ProjectDir 'release-publish.json'
$versionPath = Join-Path $ProjectDir 'version.txt'
$metadataPath = Join-Path $ProjectDir '.cache\generated\Publish\x64\build-metadata.json'

foreach ($requiredPath in @($configPath, $versionPath, $metadataPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Ficheiro obrigatório em falta para Publish: $requiredPath"
    }
}

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($config.enabled -ne $true) {
    throw 'A publicação está desativada em release-publish.json.'
}
if ($config.publishAfterBuild -ne $true -or
    $config.requireExplicitPublishCommand -ne $false -or
    $config.requireConfirmation -ne $false) {
    throw 'Política inválida: Publish deve publicar automaticamente, sem afetar Release ou Tester.'
}
if (-not $ExplicitConfirmed) {
    throw 'Publicação direta bloqueada. O pedido tem de vir do target Publish ou do wrapper de recuperação autorizado.'
}
if ([string]$config.releaseChannel -cne 'stable') {
    throw 'A configuração Publish apenas pode publicar no canal stable.'
}
if (($config.requireAuthenticode -eq $true) -xor ($config.requireManifestSignature -eq $true)) {
    throw 'Authenticode e assinatura do manifesto devem ser ativados ou desativados em conjunto.'
}

$version = ([IO.File]::ReadAllText($versionPath)).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versão inválida para Publish: '$version'."
}

$usingApprovedSourceSnapshot = $false
if ($config.requireTagRelease -eq $true -or $config.requireCleanReproducibleBuild -eq $true) {
    if (-not (Test-Path -LiteralPath (Join-Path $ProjectDir '.git'))) {
        if ($config.allowDeterministicSourceSnapshot -ne $true) {
            throw 'Publicação recusada: esta cópia não contém o repositório Git e a publicação de snapshots não está autorizada.'
        }
        $usingApprovedSourceSnapshot = $true
        Write-Warning '[OmniGhost Publish] Snapshot sem .git: será usada a identidade determinística registada nos metadados da build.'
    }
    else {
        $git = Get-Command git -ErrorAction Stop
        $status = @(& $git.Source -C $ProjectDir status --porcelain --untracked-files=normal)
        if ($LASTEXITCODE -ne 0) { throw 'Não foi possível validar o estado Git.' }
        if ($config.requireCleanReproducibleBuild -eq $true -and $status.Count -gt 0) {
            throw 'Publicação recusada: o source contém alterações por commit ou ficheiros não rastreados.'
        }
        $expectedTag = "v$version"
        $headTags = @(& $git.Source -C $ProjectDir tag --points-at HEAD)
        if ($LASTEXITCODE -ne 0 -or $config.requireTagRelease -eq $true -and $expectedTag -notin $headTags) {
            throw "Publicação recusada: HEAD tem de estar marcado exatamente com $expectedTag."
        }
    }
}

$metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
if ([string]$metadata.configuration -cne 'Publish' -or
    [string]$metadata.architecture -cne 'x64' -or
    [string]$metadata.release_channel -cne 'stable' -or
    [string]$metadata.app_version -cne $version) {
    throw 'Os metadados não pertencem a uma build Publish|x64 stable da versão atual.'
}
if ($usingApprovedSourceSnapshot -and
    ([string]$metadata.source_provenance -notmatch '^source-' -or
     [string]$metadata.commit_id -notmatch '^source-tree-[0-9a-f]{16}$')) {
    throw 'Publicação recusada: o snapshot não possui uma identidade determinística válida.'
}
if ($config.requireCleanReproducibleBuild -eq $true -and -not $usingApprovedSourceSnapshot -and
    ($metadata.reproducible -ne $true -or $metadata.dirty -eq $true)) {
    throw 'Publicação recusada: os metadados não comprovam uma build limpa e reproduzível.'
}

$expectedBuildDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir 'build\Publish'))
if (-not $BuildDir.TrimEnd('\').Equals($expectedBuildDir.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Publish recusado: o output tem de ser build\Publish. Recebido: $BuildDir"
}
$executablePath = Join-Path $BuildDir 'OmniGhost.exe'
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "OmniGhost.exe não foi encontrado no output Publish: $executablePath"
}

if (-not $SkipPackaging) {
    $mode = if ($config.requireAuthenticode -eq $true) { 'Commercial' } else { 'Publish' }
    Write-Host "[OmniGhost Publish] Packaging da versão $version (modo $mode)..."
    & (Join-Path $ProjectDir 'tools\Package-Release.ps1') -ProjectDir $ProjectDir -BuildDir $BuildDir -Mode $mode
    if ($LASTEXITCODE -ne 0) { throw "O packaging terminou com o código $LASTEXITCODE." }
}

if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $ProjectDir 'artifacts\release'
}
$ReleaseDirectory = [IO.Path]::GetFullPath((Join-Path $ReleaseDirectory '.'))

$operationMessage = if ($ValidateOnly) {
    '[OmniGhost Publish] A validar autenticação e assets sem alterar o GitHub...'
} else {
    '[OmniGhost Publish] A enviar e validar a Release no GitHub...'
}
Write-Host $operationMessage

& (Join-Path $ProjectDir 'tools\publish_github_release.ps1') `
    -ProjectDir $ProjectDir `
    -Version $version `
    -ReleaseDirectory $ReleaseDirectory `
    -InternalConfirmed `
    -ValidateOnly:$ValidateOnly

if ($LASTEXITCODE -ne 0) { throw "A publicação terminou com o código $LASTEXITCODE." }

$completionMessage = if ($ValidateOnly) {
    '[OmniGhost Publish] Preflight concluído. Nenhuma alteração remota foi efetuada.'
} else {
    "[OmniGhost Publish] Versão $version publicada com sucesso."
}
Write-Host $completionMessage
}
finally {
    $cleanupScript = Join-Path $ProjectDir 'tools\Cleanup-ReleaseCopies.ps1'
    if (Test-Path -LiteralPath $cleanupScript -PathType Leaf) {
        & $cleanupScript
        if ($LASTEXITCODE -ne 0) {
            Write-Warning '[OmniGhost Publish] Algumas cópias temporárias continuaram bloqueadas; consulta o aviso de limpeza acima.'
        }
    }
}
