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
$BackupDir = Join-Path $ProjectDir 'artifacts\version-backup'
$StateFile = Join-Path $BackupDir 'pending-version.json'
$LockFile = Join-Path $ProjectDir 'artifacts\.version-build.lock'

function Write-VersionLog {
    param([string]$Message)
    Write-Host "[OmniGhost Version] $Message"
}

$State = $null
if (Test-Path -LiteralPath $StateFile -PathType Leaf) {
    try {
        $State = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
    }
    catch {
        Write-Warning "Estado de versão pendente inválido: $($_.Exception.Message)"
    }
}

$Previous = if ($State -and $State.previous) { [string]$State.previous } else { '' }
$VersionBackup = Join-Path $BackupDir 'version.txt.bak'
$HeaderBackup = Join-Path $BackupDir 'app_version.h.bak'
$ResourceBackup = Join-Path $BackupDir 'app.rc.bak'

if (-not (Test-Path -LiteralPath $VersionBackup -PathType Leaf) -and -not $Previous) {
    Write-VersionLog 'Sem estado pendente para restaurar.'
    Remove-Item -LiteralPath $LockFile -Force -ErrorAction SilentlyContinue
    return
}

Write-VersionLog 'A build falhou.'
if ($Previous) { Write-VersionLog "A restaurar a versão $Previous." }

if (Test-Path -LiteralPath $VersionBackup -PathType Leaf) {
    Copy-Item -LiteralPath $VersionBackup -Destination $VersionFile -Force
}
elseif ($Previous) {
    [System.IO.File]::WriteAllText($VersionFile, $Previous + [Environment]::NewLine,
        (New-Object System.Text.UTF8Encoding -ArgumentList $false))
}
if (Test-Path -LiteralPath $HeaderBackup -PathType Leaf) {
    Copy-Item -LiteralPath $HeaderBackup -Destination $HeaderFile -Force
}
if (Test-Path -LiteralPath $ResourceBackup -PathType Leaf) {
    Copy-Item -LiteralPath $ResourceBackup -Destination $ResourceFile -Force
}

Remove-Item -LiteralPath $StateFile -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $LockFile -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $VersionBackup -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $HeaderBackup -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $ResourceBackup -Force -ErrorAction SilentlyContinue

Write-VersionLog 'Rollback concluído.'
return
