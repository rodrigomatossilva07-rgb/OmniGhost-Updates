[CmdletBinding()]
param(
    [Parameter(Mandatory = $false)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $false)]
    [string]$Tag
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
    $ProjectDir = Split-Path -Parent $PSScriptRoot
}
$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))

$ConfigPath = Join-Path $ProjectDir 'config\release\release-publish.json'
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "config\release\release-publish.json não encontrado: $ConfigPath"
}
$Config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
$Repository = if ($Config.repository) {
    [string]$Config.repository
} else {
    'rodrigomatossilva07-rgb/OmniGhost-Updates'
}

if ([string]::IsNullOrWhiteSpace($Tag)) {
    $VersionPath = Join-Path $ProjectDir 'config\release\version.txt'
    if (-not (Test-Path -LiteralPath $VersionPath -PathType Leaf)) {
        throw "version.txt não encontrado: $VersionPath"
    }
    $Version = [System.IO.File]::ReadAllText($VersionPath).Trim()
    if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
        throw "Versão inválida em version.txt: '$Version'"
    }
    $Tag = "v$Version"
}
elseif ($Tag -notmatch '^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') {
    throw "Tag inválida: '$Tag'"
}
elseif (-not $Tag.StartsWith('v', [System.StringComparison]::OrdinalIgnoreCase)) {
    $Tag = "v$Tag"
}

function Find-Gh {
    $Command = Get-Command gh.exe -ErrorAction SilentlyContinue
    if (-not $Command) { $Command = Get-Command gh -ErrorAction SilentlyContinue }
    if ($Command) { return $Command.Source }

    $Candidates = @()
    if ($env:ProgramFiles) { $Candidates += Join-Path $env:ProgramFiles 'GitHub CLI\gh.exe' }
    if ($env:LOCALAPPDATA) {
        $Candidates += Join-Path $env:LOCALAPPDATA 'Programs\GitHub CLI\gh.exe'
        $Candidates += Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links\gh.exe'
    }
    foreach ($Candidate in $Candidates) {
        if (Test-Path -LiteralPath $Candidate -PathType Leaf) {
            return [System.IO.Path]::GetFullPath($Candidate)
        }
    }
    return $null
}

function Invoke-GhCapture {
    param([string]$Gh, [string[]]$Arguments)

    $Previous = $ErrorActionPreference
    $Captured = @()
    $ExitCode = 1
    try {
        $ErrorActionPreference = 'Continue'
        $Captured = @(& $Gh @Arguments 2>&1)
        $ExitCode = [int]$LASTEXITCODE
    }
    finally {
        $ErrorActionPreference = $Previous
    }

    return [pscustomobject]@{
        ExitCode = $ExitCode
        Output = (($Captured | ForEach-Object { [string]$_ }) -join [Environment]::NewLine).Trim()
    }
}

$Gh = Find-Gh
if (-not $Gh) {
    throw 'GitHub CLI não encontrada. Instala com: winget install --id GitHub.cli -e'
}

$Auth = Invoke-GhCapture -Gh $Gh -Arguments @('auth', 'status')
if ($Auth.ExitCode -ne 0) {
    throw "GitHub CLI sem autenticação válida. Executa 'gh auth login'. Detalhes: $($Auth.Output)"
}

$Release = Invoke-GhCapture -Gh $Gh -Arguments @(
    'release', 'view', $Tag,
    '--repo', $Repository,
    '--json', 'tagName,isDraft,isPrerelease,url'
)
if ($Release.ExitCode -ne 0) {
    throw "A Release $Tag não foi encontrada: $($Release.Output)"
}
$ReleaseInfo = $Release.Output | ConvertFrom-Json
if ([bool]$ReleaseInfo.isDraft) {
    throw "A Release $Tag ainda é draft e não pode ser marcada como Latest pública."
}
if ([bool]$ReleaseInfo.isPrerelease) {
    throw "A Release $Tag está marcada como prerelease."
}

Write-Host "[OmniGhost GitHub] A marcar $Tag como Latest..."
$Edit = Invoke-GhCapture -Gh $Gh -Arguments @(
    'release', 'edit', $Tag,
    '--repo', $Repository,
    '--latest'
)
if ($Edit.ExitCode -ne 0) {
    throw "gh release edit falhou: $($Edit.Output)"
}

$Observed = ''
for ($Attempt = 1; $Attempt -le 10; $Attempt++) {
    $Latest = Invoke-GhCapture -Gh $Gh -Arguments @(
        'api', "repos/$Repository/releases/latest",
        '--jq', '.tag_name'
    )
    if ($Latest.ExitCode -eq 0) {
        $Observed = ([string]$Latest.Output).Trim()
        if ($Observed -eq $Tag) {
            Write-Host "[OmniGhost GitHub] Latest confirmado: $Tag"
            Write-Host "[OmniGhost GitHub] URL: $($ReleaseInfo.url)"
            exit 0
        }
    }
    else {
        $Observed = $Latest.Output
    }
    Start-Sleep -Milliseconds (500 * $Attempt)
}

throw "A API Latest devolveu '$Observed' em vez de '$Tag'."
