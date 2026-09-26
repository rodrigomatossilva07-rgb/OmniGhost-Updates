[CmdletBinding()]
param(
    [string]$ProjectDir = '',
    [string]$Configuration = '',
    [string]$Platform = '',
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch {}
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$versionPath = Join-Path $ProjectDir 'config\release\version.txt'
$configPath = Join-Path $ProjectDir 'config\release\release-publish.json'
if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) { throw "Versão em falta: $versionPath" }
if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) { throw "Configuração em falta: $configPath" }
$version = [IO.File]::ReadAllText($versionPath).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') { throw "Versão inválida em version.txt: $version" }
$config = Get-Content -LiteralPath $configPath -Raw | ConvertFrom-Json
if ([string]$config.repository -notmatch '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
    throw 'Repositório de publicação inválido em release-publish.json.'
}
Write-Host "[OmniGhost Version] Versão de origem: $version; tag esperada: v$version. A build não altera a versão nem o Git."
if (-not $ValidateOnly) {
    & (Join-Path $PSScriptRoot 'Test-CommercialPreflight.ps1') -ProjectDir $ProjectDir -AllowMissingTag -SourceOnly
}
