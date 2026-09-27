[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$ProjectDir = '',

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

if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $ProjectDir 'build\Publish'
}
$BuildDir = [IO.Path]::GetFullPath((Join-Path $BuildDir '.'))

# Downloads contains only convenience copies made during packaging. Canonical
# release assets live in artifacts\release and are never removed here.
try {

$configPath = Join-Path $ProjectDir 'config\release\release-publish.json'
$versionPath = Join-Path $ProjectDir 'config\release\version.txt'
$metadataPath = Join-Path $ProjectDir '.cache\generated\Publish\x64\build-metadata.json'

foreach ($requiredPath in @($configPath, $versionPath, $metadataPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Ficheiro obrigatório em falta para Publish: $requiredPath"
    }
}

& (Join-Path $PSScriptRoot 'Test-CommercialPreflight.ps1') -ProjectDir $ProjectDir -AllowMissingTag

$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ($config.enabled -ne $true) {
    throw 'A publicação está desativada em config\release\release-publish.json.'
}
if ($config.publishAfterBuild -ne $true -or
    $config.requireExplicitPublishCommand -ne $false -or
    $config.requireConfirmation -ne $false) {
    throw 'Política inválida: Publish deve publicar automaticamente, sem afetar Release.'
}
if (-not $ExplicitConfirmed) {
    throw 'Publicação direta bloqueada. O pedido tem de vir do target Publish ou do wrapper de recuperação autorizado.'
}
if ([string]$config.releaseChannel -cne 'stable') {
    throw 'A configuração Publish apenas pode publicar no canal stable.'
}
if ($config.requireAuthenticode -ne $true -or $config.requireManifestSignature -ne $true) {
    throw 'Publish requer Authenticode e assinatura do manifesto.'
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
            throw ("Release validation failed: working tree dirty. Faz commit das alterações antes de Publish:`n" + ($status -join "`n"))
        }
        $expectedTag = "v$version"
        $headTags = @(& $git.Source -C $ProjectDir tag --points-at HEAD)
        if ($LASTEXITCODE -ne 0) {
            throw 'Não foi possível listar tags Git no HEAD.'
        }
        # A new tag is created only after package and signatures pass below.
    }
}

if (-not (Get-Variable -Name SkipRemoteUpload -Scope Script -ErrorAction SilentlyContinue)) {
    $script:SkipRemoteUpload = $false
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
    throw 'Release validation failed: metadata indica árvore dirty ou build não reproduzível. Consulta dirty_files em build-metadata.json.'
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
    & (Join-Path $ProjectDir 'tools\Package-Release.ps1') -ProjectDir $ProjectDir -BuildDir $BuildDir -Mode Commercial -AllowMissingTag
    if ($LASTEXITCODE -ne 0) { throw "O packaging terminou com o código $LASTEXITCODE." }
}

if ([string]::IsNullOrWhiteSpace($ReleaseDirectory)) {
    $ReleaseDirectory = Join-Path $ProjectDir 'artifacts\release'
}
$ReleaseDirectory = [IO.Path]::GetFullPath((Join-Path $ReleaseDirectory '.'))

& (Join-Path $ProjectDir 'tools\Validate-CommercialRelease.ps1') `
    -ProjectDir $ProjectDir -ReleaseDirectory $ReleaseDirectory -Version $version
& (Join-Path $ProjectDir 'tools\Validate-ReleaseArtifacts.ps1') `
    -ProjectDir $ProjectDir -ReleaseDirectory $ReleaseDirectory -Version $version
Write-Host '[OmniGhost Publish] Pacote comercial local validado. A publicação remota depende do workflow de build, assinatura e integridade.'
if (-not $ValidateOnly -and -not $SkipRemoteUpload -and -not $usingApprovedSourceSnapshot) {
    $git = Get-Command git -ErrorAction Stop
    $tag = "v$version"
    $headTags = @(& $git.Source -C $ProjectDir tag --points-at HEAD)
    if ($LASTEXITCODE -ne 0) { throw 'Não foi possível verificar as tags no HEAD após o packaging.' }
    if ($tag -notin $headTags) {
        & $git.Source -C $ProjectDir tag -a $tag -m "OmniGhost $version"
        if ($LASTEXITCODE -ne 0) { throw "Falha ao criar a tag local $tag." }
    }
    & $git.Source -C $ProjectDir push origin $tag
    if ($LASTEXITCODE -ne 0) {
        throw "O pacote local foi validado, mas o push da tag $tag falhou. Corrige a ligação/autenticação e repete Publish; não movas a tag."
    }
    Write-Host "[OmniGhost Publish] Tag $tag enviada. O workflow GitHub publica após validar build, assinatura e artefactos."
}
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
