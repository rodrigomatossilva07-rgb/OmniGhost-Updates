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
. (Join-Path $PSScriptRoot 'ReleaseOutput.ps1')
$versionPhaseTimer = [Diagnostics.Stopwatch]::StartNew()
if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')) }
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$versionPath = Join-Path $ProjectDir 'src\project\config\release\version.txt'
$configPath = Join-Path $ProjectDir 'src\project\config\release\release-publish.json'
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




	$git = Get-Command git -ErrorAction Stop
	$status = @(& $git.Source -C $ProjectDir status --porcelain --untracked-files=normal --ignore-submodules)
	if ($LASTEXITCODE -ne 0) { throw 'Não foi possível consultar o estado Git antes da Publish.' }
	$untracked = @($status | Where-Object { ([string]$_).StartsWith('?? ') })
	if ($untracked.Count -gt 0) {
		$archiveExt = @('.zip', '.7z', '.rar', '.tar', '.gz', '.tgz', '.bz2', '.xz')
		$toAdd = @()
		$ignoredArchives = @()
		foreach ($line in $untracked) {
			$pathPart = ([string]$line).Substring(3).Trim()
			if ([string]::IsNullOrWhiteSpace($pathPart)) { continue }

			if ($pathPart.StartsWith('"') -and $pathPart.EndsWith('"') -and $pathPart.Length -ge 2) {
				$pathPart = $pathPart.Substring(1, $pathPart.Length - 2)
			}
			$ext = [IO.Path]::GetExtension($pathPart).ToLowerInvariant()
			if ($archiveExt -contains $ext) {
				$ignoredArchives += $pathPart
				continue
			}
			$toAdd += $pathPart
		}
		if ($ignoredArchives.Count -gt 0) {
			Write-Host ('[OmniGhost Publish] A ignorar ficheiros arquivo não rastreados: ' + ($ignoredArchives -join ', '))
		}
		if ($toAdd.Count -gt 0) {
			Write-Host ('[OmniGhost Publish] A aceitar ficheiros novos: ' + ($toAdd -join ', '))
			& $git.Source -C $ProjectDir add -- $toAdd
			if ($LASTEXITCODE -ne 0) { throw 'Não foi possível adicionar ficheiros novos ao índice Git antes da Publish.' }

			$status = @(& $git.Source -C $ProjectDir status --porcelain --untracked-files=normal --ignore-submodules)
			if ($LASTEXITCODE -ne 0) { throw 'Não foi possível reconsultar o estado Git após aceitar ficheiros novos.' }
		}
	}
	$trackedChanges = @($status | Where-Object { -not ([string]$_).StartsWith('?? ') })
	if ($trackedChanges.Count -gt 0) {
		& $git.Source -C $ProjectDir add -u -- .
		if ($LASTEXITCODE -ne 0) { throw 'Não foi possível preparar as alterações rastreadas para Publish.' }
		$identityArgs = @()
		$userName = ([string](& $git.Source -C $ProjectDir config user.name 2>$null)).Trim()
		$userEmail = ([string](& $git.Source -C $ProjectDir config user.email 2>$null)).Trim()
		if ([string]::IsNullOrWhiteSpace($userName)) { $identityArgs += @('-c', 'user.name=OmniGhost Publish') }
		if ([string]::IsNullOrWhiteSpace($userEmail)) { $identityArgs += @('-c', 'user.email=publish@omnighost.local') }
		& $git.Source -C $ProjectDir @identityArgs commit -m "chore(publish): capture tracked source changes"
		if ($LASTEXITCODE -ne 0) { throw 'Não foi possível guardar automaticamente as alterações rastreadas para Publish.' }
		Write-Host '[OmniGhost Publish] Alterações rastreadas guardadas automaticamente num commit local.'
	}

	& (Join-Path $PSScriptRoot 'Finalize-VersionCommit.ps1') -ProjectDir $ProjectDir
	if ($LASTEXITCODE -ne 0) { throw 'Não foi possível finalizar o incremento automático da versão.' }

	$maximumAdvances = 100
	for ($advance = 0; $advance -lt $maximumAdvances; $advance++) {
		$currentVersion = [IO.File]::ReadAllText($versionPath).Trim()
		$currentTag = "v$currentVersion"
		$head = ([string](& $git.Source -C $ProjectDir rev-parse HEAD)).Trim()
		if ($LASTEXITCODE -ne 0 -or $head -notmatch '^[0-9a-fA-F]{40}$') {
			throw 'Não foi possível determinar o commit atual antes de preparar a versão Publish.'
		}



		$matchingTags = @(& $git.Source -C $ProjectDir tag --list $currentTag)
		if ($LASTEXITCODE -ne 0) { throw "Não foi possível consultar a tag $currentTag." }
		$tagExists = $matchingTags.Count -gt 0
		$tagCommit = @()
		if ($tagExists) {
			$tagCommit = @(& $git.Source -C $ProjectDir rev-list -n 1 $currentTag)
			if ($LASTEXITCODE -ne 0 -or $tagCommit.Count -eq 0 -or
				([string]$tagCommit[0]).Trim() -notmatch '^[0-9a-fA-F]{40}$') {
				throw "A tag $currentTag existe, mas o commit associado não pôde ser validado."
			}
		}
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
Write-Host "[OmniGhost Version] Validação concluída em $(Format-OmniDuration $versionPhaseTimer.Elapsed)."
