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
Write-Host "[OmniGhost Version] Versão de origem: $version; tag esperada: v$version."
if (-not $ValidateOnly) {
	& (Join-Path $PSScriptRoot 'Finalize-VersionCommit.ps1') -ProjectDir $ProjectDir
	if ($LASTEXITCODE -ne 0) { throw 'Não foi possível finalizar o incremento automático da versão.' }

	$git = Get-Command git -ErrorAction Stop
	$maximumAdvances = 100
	for ($advance = 0; $advance -lt $maximumAdvances; $advance++) {
		$currentVersion = [IO.File]::ReadAllText($versionPath).Trim()
		$currentTag = "v$currentVersion"
		$head = ([string](& $git.Source -C $ProjectDir rev-parse HEAD)).Trim()
		if ($LASTEXITCODE -ne 0 -or $head -notmatch '^[0-9a-fA-F]{40}$') {
			throw 'Não foi possível determinar o commit atual antes de preparar a versão Publish.'
		}
		$tagCommit = @(& $git.Source -C $ProjectDir rev-list -n 1 $currentTag 2>$null)
		$tagExists = $LASTEXITCODE -eq 0 -and $tagCommit.Count -gt 0 -and
			([string]$tagCommit[0]).Trim() -match '^[0-9a-fA-F]{40}$'
		if (-not $tagExists -or ([string]$tagCommit[0]).Trim() -ceq $head) {
			break
		}

		Write-Host "[OmniGhost Version] $currentTag já pertence a outro commit; a preparar automaticamente a próxima versão livre."
		& (Join-Path $PSScriptRoot 'Increment-Version.ps1') -ProjectDir $ProjectDir
		if ($LASTEXITCODE -ne 0) { throw "Falha ao avançar automaticamente além de $currentTag." }
		& (Join-Path $PSScriptRoot 'Finalize-VersionCommit.ps1') -ProjectDir $ProjectDir
		if ($LASTEXITCODE -ne 0) { throw 'Falha ao guardar o incremento automático antes da Publish.' }
		Remove-Item -LiteralPath (Join-Path $ProjectDir 'artifacts\version-backup\pending-version.json') -Force -ErrorAction SilentlyContinue
		Remove-Item -LiteralPath (Join-Path $ProjectDir 'artifacts\.version-build.lock') -Force -ErrorAction SilentlyContinue
	}
	if ($advance -ge $maximumAdvances) {
		throw "Não foi encontrada uma versão livre após $maximumAdvances incrementos automáticos."
	}
	$version = [IO.File]::ReadAllText($versionPath).Trim()
	Write-Host "[OmniGhost Version] Versão Publish selecionada automaticamente: $version"
    & (Join-Path $PSScriptRoot 'Test-CommercialPreflight.ps1') -ProjectDir $ProjectDir -AllowMissingTag -SourceOnly
}
