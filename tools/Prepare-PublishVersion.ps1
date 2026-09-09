[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    # Optional: accepted for MSBuild/runner compatibility and ignored.
    # Some runners pass -Configuration to every tool script.
    [Parameter(Mandatory = $false)]
    [string]$Configuration = '',

    [Parameter(Mandatory = $false)]
    [string]$Platform = '',

    # Safe preflight used by local diagnostics/CI. It validates only the local
    # Publish configuration and never queries GitHub or changes the version.
    [Parameter(Mandatory = $false)]
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
if ($Configuration) {
    Write-Host "[OmniGhost Version] Configuration=$Configuration (informational only)"
}

Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$versionPath = Join-Path $ProjectDir 'version.txt'
$configPath = Join-Path $ProjectDir 'release-publish.json'
$pendingPath = Join-Path $ProjectDir 'artifacts\version-backup\pending-version.json'
$lockPath = Join-Path $ProjectDir 'artifacts\.version-build.lock'

if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "version.txt nao encontrado: $versionPath"
}
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
    throw "release-publish.json nao encontrado: $configPath"
}

$version = ([IO.File]::ReadAllText($versionPath)).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versao invalida para Publish: '$version'."
}

$publishConfig = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
# Under StrictMode a missing property throws before we can give the user a
# useful Publish configuration error. Resolve it explicitly instead.
$repositoryProperty = $publishConfig.PSObject.Properties['repository']
$repository = if ($null -eq $repositoryProperty) { '' } else { ([string]$repositoryProperty.Value).Trim() }
if ($repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
    throw 'Repositorio de publicacao invalido em release-publish.json.'
}
Write-Host "[OmniGhost Version] Publish repository=$repository"
if ($ValidateOnly) {
    Write-Host '[OmniGhost Version] Local Publish configuration validation passed.'
    return
}

$gh = Get-Command gh -ErrorAction Stop
$tag = "v$version"
$previousErrorPreference = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $output = @(& $gh.Source release view $tag --repo $repository --json tagName 2>&1)
    $exitCode = $LASTEXITCODE
}
finally {
    $ErrorActionPreference = $previousErrorPreference
}

if ($exitCode -ne 0) {
    $detail = ($output -join [Environment]::NewLine)
    if ($detail -match '(?i)release not found|HTTP 404|Not Found') {
        Write-Host "[OmniGhost Version] $tag ainda nao existe em $repository; a build mantem $version."
        return
    }
    throw "Nao foi possivel confirmar a versao publicada no GitHub. $detail"
}

# A release da versão atual confirma qualquer incremento pendente anterior. A
# limpeza permite que Increment-Version avance uma única vez para a próxima
# versão, sem saltar novamente quando uma compilação falha e é repetida.
if (Test-Path -LiteralPath $pendingPath -PathType Leaf) {
    try {
        $pending = Get-Content -LiteralPath $pendingPath -Raw | ConvertFrom-Json
        if ([string]$pending.next -eq $version) {
            Remove-Item -LiteralPath (Split-Path -Parent $pendingPath) -Recurse -Force
            Remove-Item -LiteralPath $lockPath -Force -ErrorAction SilentlyContinue
            Write-Host "[OmniGhost Version] Incremento $version confirmado pela release remota."
        }
    }
    catch {
        throw "O estado pendente da versao nao pode ser validado: $($_.Exception.Message)"
    }
}

Write-Host "[OmniGhost Version] $tag ja esta publicada; a preparar a proxima versao."
& (Join-Path $ProjectDir 'tools\Increment-Version.ps1') -ProjectDir $ProjectDir
if ($LASTEXITCODE -ne 0) {
    throw "O incremento da versao terminou com o codigo $LASTEXITCODE."
}

$next = ([IO.File]::ReadAllText($versionPath)).Trim()
if ($next -eq $version) {
    throw "A versao publicada $version nao foi incrementada."
}
Write-Host "[OmniGhost Version] Publish vai compilar a versao $next."
