[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$VersionFile = Join-Path $ProjectDir 'version.txt'
$HeaderFile = Join-Path $ProjectDir 'src\app_version.h'
$ResourceFile = Join-Path $ProjectDir 'resources\app.rc'
$BackupDir = Join-Path $ProjectDir 'artifacts\version-backup'
$StateFile = Join-Path $BackupDir 'pending-version.json'
$LockFile = Join-Path $ProjectDir 'artifacts\.version-build.lock'
$Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false

function Write-VersionLog {
    param([string]$Message)
    Write-Host "[OmniGhost Version] $Message"
}

function Test-OmniGhostVersionFormat {
    param([string]$Version)
    # OmniGhost uses decimal release slots: MINOR and PATCH are always 0..9.
    return $Version -match '^(0|[1-9]\d*)\.[0-9]\.[0-9]$'
}

function Get-NextOmniGhostVersion {
    param([Parameter(Mandatory = $true)][string]$Current)

    if (-not (Test-OmniGhostVersionFormat -Version $Current)) {
        throw "Versão inválida para a regra OmniGhost: '$Current'. Usa MAJOR.MINOR.PATCH."
    }

    $Parts = $Current.Split('.')
    $Major = [int]$Parts[0]
    $Minor = [int]$Parts[1]
    $Patch = [int]$Parts[2]

    # OmniGhost rule: PATCH and MINOR are 0..9.
    # Examples: 2.4.9 -> 2.5.0 and 2.9.9 -> 3.0.0.
    if ($Patch -ge 9) {
        $Patch = 0
        $Minor++
        if ($Minor -ge 10) {
            $Minor = 0
            $Major++
        }
    } else {
        $Patch++
    }

    return '{0}.{1}.{2}' -f $Major, $Minor, $Patch
}

function Write-TextAtomic {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )

    $Directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Force -Path $Directory | Out-Null
    $Temporary = Join-Path $Directory ((Split-Path -Leaf $Path) + '.tmp-' + [guid]::NewGuid().ToString('N'))
    [System.IO.File]::WriteAllText($Temporary, $Text, $Utf8NoBom)
    $Check = [System.IO.File]::ReadAllText($Temporary)
    if ($Check -cne $Text) {
        Remove-Item -LiteralPath $Temporary -Force -ErrorAction SilentlyContinue
        throw "A validação da escrita temporária falhou: $Path"
    }
    if (Test-Path -LiteralPath $Path) {
        Remove-Item -LiteralPath $Path -Force
    }
    Move-Item -LiteralPath $Temporary -Destination $Path -Force
}

function Invoke-VersionSync {
    $SyncScript = Join-Path $ProjectDir 'tools\sync_version.ps1'
    if (-not (Test-Path -LiteralPath $SyncScript -PathType Leaf)) {
        throw "Script de sincronização não encontrado: $SyncScript"
    }
    & $SyncScript -ProjectDir $ProjectDir
}

function Remove-StateAndLock {
    Remove-Item -LiteralPath $StateFile -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $LockFile -Force -ErrorAction SilentlyContinue
}

function Restore-Backups {
    $VersionBackup = Join-Path $BackupDir 'version.txt.bak'
    $HeaderBackup = Join-Path $BackupDir 'app_version.h.bak'
    $ResourceBackup = Join-Path $BackupDir 'app.rc.bak'

    if (Test-Path -LiteralPath $VersionBackup -PathType Leaf) {
        Copy-Item -LiteralPath $VersionBackup -Destination $VersionFile -Force
    }
    if (Test-Path -LiteralPath $HeaderBackup -PathType Leaf) {
        Copy-Item -LiteralPath $HeaderBackup -Destination $HeaderFile -Force
    }
    if (Test-Path -LiteralPath $ResourceBackup -PathType Leaf) {
        Copy-Item -LiteralPath $ResourceBackup -Destination $ResourceFile -Force
    }
}

if (-not (Test-Path -LiteralPath $VersionFile -PathType Leaf)) {
    throw "version.txt não encontrado em $VersionFile"
}
if (-not (Test-Path -LiteralPath $HeaderFile -PathType Leaf)) {
    throw "app_version.h não encontrado em $HeaderFile"
}
if (-not (Test-Path -LiteralPath $ResourceFile -PathType Leaf)) {
    throw "app.rc não encontrado em $ResourceFile"
}

$Current = (Get-Content -LiteralPath $VersionFile -Raw).Trim()
if (-not (Test-OmniGhostVersionFormat -Version $Current)) {
    throw "version.txt inválido. Esperado MAJOR.MINOR.PATCH. Valor: '$Current'"
}

# If MSBuild invokes this target twice in the same build, reuse the pending
# version instead of failing because the lock already exists.
if (Test-Path -LiteralPath $StateFile -PathType Leaf) {
    try {
        $Pending = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
        if ($Pending.confirmed -eq $false -and [string]$Pending.next -eq $Current) {
            Write-VersionLog "A versão $Current já foi preparada nesta build; incremento duplicado ignorado."
            Invoke-VersionSync
            return
        }
        if ($Pending.confirmed -eq $true) {
            Remove-StateAndLock
        }
    }
    catch {
        Write-VersionLog "Estado pendente inválido detetado; a limpar antes de continuar."
        Remove-StateAndLock
    }
}

$Next = Get-NextOmniGhostVersion -Current $Current
Write-VersionLog "Versão atual: $Current"
Write-VersionLog "Próxima versão: $Next"

if ($DryRun) {
    Write-VersionLog 'DryRun — nenhuma alteração escrita.'
    return
}

New-Item -ItemType Directory -Force -Path $BackupDir | Out-Null

# A lock without state is left by an interrupted attempt and can be discarded.
if (Test-Path -LiteralPath $LockFile) {
    Write-VersionLog "Lock sem estado reutilizável detetado; a remover."
    Remove-Item -LiteralPath $LockFile -Force -ErrorAction SilentlyContinue
}

$LockStream = $null
try {
    $LockStream = [System.IO.File]::Open(
        $LockFile,
        [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None)
    $LockText = "pid=$PID`r`nstartedAt=$((Get-Date).ToUniversalTime().ToString('o'))`r`n"
    $LockBytes = $Utf8NoBom.GetBytes($LockText)
    $LockStream.Write($LockBytes, 0, $LockBytes.Length)
    $LockStream.Flush()
    $LockStream.Dispose()
    $LockStream = $null

    Copy-Item -LiteralPath $VersionFile -Destination (Join-Path $BackupDir 'version.txt.bak') -Force
    Copy-Item -LiteralPath $HeaderFile -Destination (Join-Path $BackupDir 'app_version.h.bak') -Force
    Copy-Item -LiteralPath $ResourceFile -Destination (Join-Path $BackupDir 'app.rc.bak') -Force
    Write-VersionLog 'Backup criado.'

    $State = [ordered]@{
        previous = $Current
        next = $Next
        startedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        confirmed = $false
    }
    Write-TextAtomic -Path $StateFile -Text (($State | ConvertTo-Json -Depth 4) + [Environment]::NewLine)

    Write-TextAtomic -Path $VersionFile -Text ($Next + [Environment]::NewLine)
    Write-VersionLog "version.txt atualizado para $Next"

    Invoke-VersionSync
    Write-VersionLog "Recursos internos atualizados para $Next"

    # Commit version bump to keep git tree clean for Publish validation
    try {
        $git = Get-Command git -ErrorAction Stop
        & $git.Source -C $ProjectDir add -A
        & $git.Source -C $ProjectDir -c "user.name=OmniGhost Build" -c "user.email=build@omnighost.local" commit -m "chore: prepare version $Next"
        Write-VersionLog "Version bump committed to git"
    }
    catch {
        Write-Warning "Could not commit version bump (git may not be available or no changes): $($_.Exception.Message)"
    }

    return
}
catch {
    if ($LockStream) { $LockStream.Dispose() }
    Write-VersionLog "Erro durante o incremento: $($_.Exception.Message)"
    try {
        Restore-Backups
        Write-VersionLog "A restaurar a versão $Current."
    }
    catch {
        Write-Warning "Não foi possível restaurar todos os backups: $($_.Exception.Message)"
    }
    Remove-StateAndLock
    throw
}
