[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeDir,

    [Parameter(Mandatory = $true)]
    [string]$SbomPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$RuntimeDir = [IO.Path]::GetFullPath((Join-Path $RuntimeDir '.'))
$SbomPath = [IO.Path]::GetFullPath($SbomPath)

foreach ($required in @(
    (Join-Path $ProjectDir 'THIRD_PARTY_NOTICES.txt'),
    (Join-Path $ProjectDir 'versions.json'),
    $SbomPath
)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Inventário/licença obrigatório em falta: $required"
    }
}

$sbom = Get-Content -LiteralPath $SbomPath -Raw | ConvertFrom-Json
$version = [IO.File]::ReadAllText((Join-Path $ProjectDir 'version.txt')).Trim()
if ([string]$sbom.bomFormat -cne 'CycloneDX' -or [string]$sbom.specVersion -cne '1.5') {
    throw 'O SBOM não é CycloneDX 1.5.'
}
if ([string]$sbom.metadata.component.name -cne 'OmniGhost' -or
    [string]$sbom.metadata.component.version -cne $version) {
    throw 'O componente principal do SBOM não corresponde à build.'
}

$componentsByPath = @{}
foreach ($component in @($sbom.components)) {
    $relativeProperty = @($component.properties |
        Where-Object name -CEQ 'omnighost:relativePath' |
        Select-Object -First 1)
    if ($relativeProperty.Count -eq 1) {
        $relative = [string]$relativeProperty[0].value
        if (-not [string]::IsNullOrWhiteSpace($relative)) {
            $componentsByPath[$relative] = $component
        }
    }
}

$runtimeBinaries = @(Get-ChildItem -LiteralPath $RuntimeDir -Recurse -Force -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in @('.exe', '.dll', '.sys') })
foreach ($file in $runtimeBinaries) {
    $relative = $file.FullName.Substring($RuntimeDir.Length).TrimStart('\', '/').Replace('\', '/')
    if (-not $componentsByPath.ContainsKey($relative)) {
        throw "Binário runtime ausente do SBOM: $relative"
    }
    $expected = [string](@($componentsByPath[$relative].hashes |
        Where-Object alg -CEQ 'SHA-256' | Select-Object -First 1).content)
    $actual = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($expected -cne $actual) {
        throw "SHA-256 do SBOM não corresponde ao runtime: $relative"
    }
}

$forbidden = @(Get-ChildItem -LiteralPath $RuntimeDir -Recurse -Force -File |
    Where-Object { $_.Extension.ToLowerInvariant() -in @(
        '.pdb', '.obj', '.tlog', '.idb', '.ilk', '.exp',
        '.partial', '.pfx', '.p12', '.pem', '.key'
    ) })
if ($forbidden.Count -gt 0) {
    throw "O runtime contém ficheiros de desenvolvimento/segredos proibidos: $($forbidden[0].FullName)"
}

$offsetText = Get-Content -LiteralPath (Join-Path $ProjectDir 'Fivem\game\offsets.cpp') -Raw
if ($offsetText -match '(?i)placeholder|provisional|unverified') {
    throw 'Os offsets FiveM de release ainda contêm marcadores não validados.'
}

$gameCatalog = Get-Content -LiteralPath (Join-Path $ProjectDir 'src\launcher\launcher_data.cpp') -Raw
foreach ($game in @('Warzone', 'Valorant')) {
    $pattern = "GameId::$game,[\s\S]{0,700}?false,\s*true,"
    if ($gameCatalog -notmatch $pattern) {
        throw "$game deixou de estar marcado explicitamente como Beta no catálogo."
    }
}

$ownSourceRoots = @('src', 'Cs2', 'Fivem', 'Warzone', 'Valorant', 'DMALibrary')
foreach ($rootName in $ownSourceRoots) {
    $root = Join-Path $ProjectDir $rootName
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
    foreach ($source in Get-ChildItem -LiteralPath $root -Recurse -File -Include '*.cpp', '*.h', '*.hpp') {
        $text = Get-Content -LiteralPath $source.FullName -Raw
        if ($text -match 'catch\s*\(\s*\.\.\.\s*\)\s*\{\s*\}') {
            throw "catch (...) vazio encontrado em código próprio: $($source.FullName)"
        }
    }
}

Write-Host "[OmniGhost Audit] SBOM, hashes, maturidade dos jogos e política de runtime validados."
