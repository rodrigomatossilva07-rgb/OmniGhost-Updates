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

$configPath = Join-Path $ProjectDir 'config\release\release-publish.json'
$versionPath = Join-Path $ProjectDir 'config\release\version.txt'
$metadataPath = Join-Path $ProjectDir '.cache\generated\Publish\x64\build-metadata.json'

foreach ($requiredPath in @($configPath, $versionPath, $metadataPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Ficheiro obrigatório em falta para Publish: $requiredPath"
    }
}

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
            Write-Warning '[OmniGhost Publish] Source sujo (alterações por commit). A tag aponta ao HEAD commit; ficheiros não commitados não entram no GitHub.'
        }
        $expectedTag = "v$version"
        $headTags = @(& $git.Source -C $ProjectDir tag --points-at HEAD)
        if ($LASTEXITCODE -ne 0) {
            throw 'Não foi possível listar tags Git no HEAD.'
        }
        if ($config.requireTagRelease -eq $true -and $expectedTag -notin $headTags) {
            # Tag must follow version.txt. Create it on HEAD when missing so a
            # manual Publish can still upload to GitHub without a pre-made tag.
            $existingTagCommit = @(& $git.Source -C $ProjectDir rev-parse -q --verify ("refs/tags/{0}" -f $expectedTag) 2>$null)
            if ($LASTEXITCODE -eq 0 -and $existingTagCommit) {
                # Tag exists on another commit (e.g., from a previous failed build).
                # Since version.txt has already been incremented by Prepare-PublishVersion,
                # we can safely move the local tag to HEAD.
                Write-Host ("[OmniGhost Publish] Tag {0} existe noutro commit; a mover para HEAD atual..." -f $expectedTag)
                & $git.Source -C $ProjectDir tag -d $expectedTag
                if ($LASTEXITCODE -ne 0) {
                    throw ("Falha ao remover a tag local antiga {0}." -f $expectedTag)
                }
            }
            Write-Host ("[OmniGhost Publish] A criar tag {0} a partir de version.txt no HEAD..." -f $expectedTag)
            & $git.Source -C $ProjectDir tag -a $expectedTag -m ("OmniGhost {0}" -f $version)
            if ($LASTEXITCODE -ne 0) {
                throw ("Falha ao criar a tag local {0}." -f $expectedTag)
            }
            $pushOk = $false
            try {
                & $git.Source -C $ProjectDir push origin $expectedTag 2>&1 | Out-Host
                if ($LASTEXITCODE -eq 0) { $pushOk = $true }
            } catch {
                $pushOk = $false
            }
            if (-not $pushOk) {
                Write-Warning ("[OmniGhost Publish] Tag {0} criada localmente, mas o push para origin falhou. A release GitHub pode ainda funcionar se o gh criar a tag." -f $expectedTag)
            } else {
                Write-Host ("[OmniGhost Publish] Tag {0} enviada para origin." -f $expectedTag)
            }
        }
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
    Write-Warning '[OmniGhost Publish] Metadados não comprovam build limpa/reproduzível; a publicação GitHub continua na mesma.'
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

if ($script:SkipRemoteUpload) {
    Write-Host '[OmniGhost Publish] Packaging local concluído. Upload GitHub omitido (tag/source não elegíveis para release remoto).'
    Write-Host ("[OmniGhost Publish] Para publicar no GitHub: git tag v{0} && git push --tags, depois rebuild Publish." -f $version)
}
else {
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

    # Only a fully public, latest release may discard local build outputs.
    # A failed upload, preflight, draft, or skipped remote upload deliberately
    # leaves every artifact intact for diagnosis and retry.
    if (-not $ValidateOnly -and -not $script:SkipRemoteUpload -and $config.draft -ne $true -and
        $config.cleanWorkspaceAfterVerifiedPublish -eq $true) {
        $workspaceCleanup = Join-Path $ProjectDir 'tools\Cleanup-PublishWorkspace.ps1'
        if (-not (Test-Path -LiteralPath $workspaceCleanup -PathType Leaf)) {
            throw "Limpeza pós-publicação configurada, mas o script não existe: $workspaceCleanup"
        }
        & $workspaceCleanup -ProjectDir $ProjectDir
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "A Release já foi publicada, mas a limpeza pós-publicação terminou com o código $LASTEXITCODE."
        }
    }
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
