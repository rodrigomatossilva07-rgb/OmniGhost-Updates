[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$VersionFile = Join-Path $ProjectDir 'config\release\version.txt'
$HeaderFile = Join-Path $ProjectDir 'src\app_version.h'
$ResourceFile = Join-Path $ProjectDir 'resources\app.rc'
$IconFile = Join-Path $ProjectDir 'resources\OmniGhost.ico'

function Write-MetadataLog {
    param([string]$Message)
    Write-Host "[OmniGhost Metadata] $Message"
}

function Test-VersionFormat {
    param([string]$Version)
    return $Version -match '^(0|[1-9]\d*)\.[0-9]\.[0-9]$'
}

function Require-SourceFile {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$DisplayName)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$DisplayName está em falta. A build é read-only e não reconstrói ficheiros de source automaticamente."
    }
}

function Get-VersionFromHeader {
    $Header = [System.IO.File]::ReadAllText($HeaderFile)
    $Match = [regex]::Match(
        $Header,
        'inline\s+constexpr\s+char\s+Version\[\]\s*=\s*"(?<version>[^"]+)"\s*;')
    if (-not $Match.Success) {
        throw "Não foi possível obter OmniGhost::Version de $HeaderFile"
    }
    $Detected = $Match.Groups['version'].Value.Trim()
    if (-not (Test-VersionFormat -Version $Detected)) {
        throw "Versão inválida em app_version.h: '$Detected'"
    }
    return $Detected
}

function Get-VersionFromResource {
    $Resource = [System.IO.File]::ReadAllText($ResourceFile)
    $MajorMatch = [regex]::Match($Resource, '(?m)^#define\s+OMNIGHOST_VERSION_MAJOR\s+(?<value>\d+)\s*$')
    $MinorMatch = [regex]::Match($Resource, '(?m)^#define\s+OMNIGHOST_VERSION_MINOR\s+(?<value>\d+)\s*$')
    $PatchMatch = [regex]::Match($Resource, '(?m)^#define\s+OMNIGHOST_VERSION_PATCH\s+(?<value>\d+)\s*$')
    $RevisionMatch = [regex]::Match($Resource, '(?m)^#define\s+OMNIGHOST_VERSION_REVISION\s+(?<value>\d+)\s*$')
    if (-not ($MajorMatch.Success -and $MinorMatch.Success -and $PatchMatch.Success -and $RevisionMatch.Success)) {
        throw "Não foi possível obter a versão de $ResourceFile"
    }
    if ($RevisionMatch.Groups['value'].Value -ne '0') {
        throw "OMNIGHOST_VERSION_REVISION deve permanecer 0 para a versão SemVer de source."
    }
    $Detected = '{0}.{1}.{2}' -f $MajorMatch.Groups['value'].Value, $MinorMatch.Groups['value'].Value, $PatchMatch.Groups['value'].Value
    if (-not (Test-VersionFormat -Version $Detected)) {
        throw "Versão inválida em app.rc: '$Detected'"
    }
    return $Detected
}

# Normal Build/Release must be source-read-only. Missing or divergent version/resource
# files are engineering errors; they are never repaired, bumped or rewritten here.
Require-SourceFile -Path $VersionFile -DisplayName 'version.txt'
Require-SourceFile -Path $HeaderFile -DisplayName 'src\app_version.h'
Require-SourceFile -Path $ResourceFile -DisplayName 'resources\app.rc'
Require-SourceFile -Path $IconFile -DisplayName 'resources\OmniGhost.ico'

$Version = ([System.IO.File]::ReadAllText($VersionFile)).Trim()
if (-not (Test-VersionFormat -Version $Version)) {
    throw "version.txt inválido: '$Version'. MINOR e PATCH têm de estar entre 0 e 9."
}

$HeaderVersion = Get-VersionFromHeader
$ResourceVersion = Get-VersionFromResource
if (($HeaderVersion -ne $Version) -or ($ResourceVersion -ne $Version)) {
    Write-MetadataLog "Versões dessincronizadas (version.txt=$Version, app_version.h=$HeaderVersion, app.rc=$ResourceVersion). A sincronizar a partir de version.txt…"
    $SyncScript = Join-Path $ProjectDir 'tools\sync_version.ps1'
    if (-not (Test-Path -LiteralPath $SyncScript -PathType Leaf)) {
        throw "Ferramenta de sincronização em falta: $SyncScript"
    }
    & $SyncScript -ProjectDir $ProjectDir
    if ($LASTEXITCODE -ne 0) {
        throw "Falha ao sincronizar versão a partir de version.txt."
    }
    $HeaderVersion = Get-VersionFromHeader
    $ResourceVersion = Get-VersionFromResource
    if (($HeaderVersion -ne $Version) -or ($ResourceVersion -ne $Version)) {
        throw "Após sincronização ainda há divergência: version.txt=$Version, app_version.h=$HeaderVersion, app.rc=$ResourceVersion."
    }
    Write-MetadataLog "Versão sincronizada com sucesso: $Version"
} else {
    Write-MetadataLog "Validação concluída: versão $Version sincronizada."
}
exit 0
