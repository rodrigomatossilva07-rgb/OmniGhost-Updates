[CmdletBinding()]
param([string]$ProjectDir = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$git = Get-Command git -ErrorAction SilentlyContinue
if ($null -eq $git -or -not (Test-Path -LiteralPath (Join-Path $ProjectDir '.git'))) { return }

$versionFiles = @(
    'config/release/version.txt',
    'resources/app.manifest',
    'resources/app.rc',
    'src/app_version.h'
)
$status = @(& $git.Source -C $ProjectDir status --porcelain --untracked-files=normal --ignore-submodules)
if ($LASTEXITCODE -ne 0 -or $status.Count -eq 0) { return }
$changed = @($status | ForEach-Object { ([string]$_).Substring(3).Replace('\', '/') })
$unrelated = @($changed | Where-Object { $_ -notin $versionFiles })
if ($unrelated.Count -gt 0) { return }

$version = [IO.File]::ReadAllText((Join-Path $ProjectDir 'config\release\version.txt')).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') { return }
$header = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\app_version.h'))
$resource = [IO.File]::ReadAllText((Join-Path $ProjectDir 'resources\app.rc'))
$manifest = [IO.File]::ReadAllText((Join-Path $ProjectDir 'resources\app.manifest'))
$parts = $version.Split('.')
if ($header -notmatch ('Version\[\]\s*=\s*"' + [regex]::Escape($version) + '"') -or
    $resource -notmatch "OMNIGHOST_VERSION_MAJOR\s+$($parts[0])" -or
    $resource -notmatch "OMNIGHOST_VERSION_MINOR\s+$($parts[1])" -or
    $resource -notmatch "OMNIGHOST_VERSION_PATCH\s+$($parts[2])" -or
    $manifest -notmatch ('version="' + [regex]::Escape($version) + '\.0"')) { return }

# config/release is intentionally ignored because it may also contain private
# publication material. Force-add only this explicit allowlist; no directory or
# wildcard is ever staged.
& $git.Source -C $ProjectDir add -f -- @versionFiles
if ($LASTEXITCODE -ne 0) { throw 'Falha ao preparar os ficheiros oficiais de versão.' }
$identityArgs = @()
$userName = ([string](& $git.Source -C $ProjectDir config user.name 2>$null)).Trim()
$userEmail = ([string](& $git.Source -C $ProjectDir config user.email 2>$null)).Trim()
if ([string]::IsNullOrWhiteSpace($userName)) { $identityArgs += @('-c', 'user.name=OmniGhost Release') }
if ([string]::IsNullOrWhiteSpace($userEmail)) { $identityArgs += @('-c', 'user.email=release@omnighost.local') }
& $git.Source -C $ProjectDir @identityArgs commit -m "chore(release): prepare v$version" -- @versionFiles
if ($LASTEXITCODE -ne 0) { throw "Falha ao guardar automaticamente a versão $version." }
Write-Host "[OmniGhost Version] Versão $version guardada automaticamente; working tree limpo."
