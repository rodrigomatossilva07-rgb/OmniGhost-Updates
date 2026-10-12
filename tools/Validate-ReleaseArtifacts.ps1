[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [Parameter(Mandatory = $true)][string]$Version,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
. (Join-Path $PSScriptRoot 'ReleaseTrustTools.ps1')

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$ReleaseDirectory = [IO.Path]::GetFullPath((Join-Path $ReleaseDirectory '.'))
if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versão OmniGhost inválida: '$Version'."
}

$required = @(
    'OmniGhost.zip', 'update.json', 'update.json.sig', 'install-manifest.sha256', 'install-manifest.sha256.sig',
    'SBOM.cdx.json', 'changelog.json', 'release-notes.md', 'release-meta.json', 'SHA256SUMS.txt'
)
foreach ($name in $required) {
    $path = Join-Path $ReleaseDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0) {
        throw "Artefacto obrigatório ausente ou vazio: $name"
    }
}

$sumsPath = Join-Path $ReleaseDirectory 'SHA256SUMS.txt'
$expectedNames = @('OmniGhost.zip','update.json','update.json.sig',
    'install-manifest.sha256','install-manifest.sha256.sig',
    'SBOM.cdx.json','changelog.json','release-notes.md','release-meta.json')
$seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
foreach ($line in [IO.File]::ReadAllLines($sumsPath)) {
    if ($line -cnotmatch '^([0-9A-F]{64})  ([A-Za-z0-9._-]+)$') { throw 'SHA256SUMS.txt contém uma linha inválida.' }
    $name = $Matches[2]
    if ($name -notin $expectedNames -or -not $seen.Add($name)) { throw "SHA256SUMS.txt contém um artefacto inesperado ou repetido: $name" }
    $actual = (Get-FileHash -LiteralPath (Join-Path $ReleaseDirectory $name) -Algorithm SHA256).Hash
    if ($actual -cne $Matches[1]) { throw "[HASH] SHA-256 incorreto: $name" }
}
foreach ($name in $expectedNames) {
    if (-not $seen.Contains($name)) { throw "SHA256SUMS.txt omite: $name" }
}
if (-not $Quiet) { Write-Host "[HASH] SHA256SUMS.txt validado: $($seen.Count) artefactos." }

$policy = [IO.File]::ReadAllText((Join-Path $ProjectDir 'config\release\release-publish.json'), [Text.Encoding]::UTF8) | ConvertFrom-Json
if ($policy.requireAuthenticode -ne $true -or $policy.requireManifestSignature -ne $true) {
    throw 'Release validation requires executable and manifest authenticity.'
}
$trustedCertificate = Get-ReleasePublicCertificate -Policy $policy -HeaderPath (Join-Path $ProjectDir 'src\updater\release_trust.h')
try {
    foreach ($name in @('update.json', 'install-manifest.sha256')) {
        Assert-ReleaseManifestSignature -InputPath (Join-Path $ReleaseDirectory $name) -SignaturePath (Join-Path $ReleaseDirectory ($name + '.sig')) -Certificate $trustedCertificate
    }
} finally { $trustedCertificate.Dispose() }

$zipPath = Join-Path $ReleaseDirectory 'OmniGhost.zip'
$manifest = Get-Content -LiteralPath (Join-Path $ReleaseDirectory 'update.json') -Raw | ConvertFrom-Json
if ([string]$manifest.version -cne $Version -or [string]$manifest.channel -cne 'stable') {
    throw 'update.json não corresponde à versão/canal da release.'
}
$package = @($manifest.packages | Where-Object {
    [string]$_.platform -ceq 'windows' -and [string]$_.architecture -ceq 'x64'
} | Select-Object -First 1)
if ($package.Count -ne 1) { throw 'update.json não contém exatamente um pacote Windows x64 utilizável.' }
$actualSize = (Get-Item -LiteralPath $zipPath).Length
$actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ([int64]$package[0].size -ne $actualSize -or [string]$package[0].sha256 -cne $actualHash) {
    throw 'O tamanho/SHA-256 de OmniGhost.zip não corresponde ao update.json.'
}

$archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
$validationDirectory = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost-Authenticode-' + [Guid]::NewGuid().ToString('N'))
$validationExecutable = Join-Path $validationDirectory 'OmniGhost.exe'
try {
    $names = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
	if ($names.Count -ne 1 -or $names[0] -cne 'OmniGhost.exe') {
		throw 'O ZIP de cliente tem de conter exclusivamente OmniGhost.exe na raiz.'
	}
    $forbidden = @($names | Where-Object {
        $_ -match '(^|/)(fortnite_offsets|warzone_offsets)\.json$' -or
        $_ -match '\.(pdb|obj|ilk|tlog|pfx|p12|pem|key)$' -or
        $_ -match '(^|/)\.git(/|$)'
    })
    if ($forbidden.Count -gt 0) { throw "O ZIP contém conteúdo proibido: $($forbidden[0])" }
    foreach ($entry in $archive.Entries) {
        $name = $entry.FullName.Replace('\','/')
        if ($name.StartsWith('/') -or $name -match '^[A-Za-z]:' -or @($name.Split('/')) -contains '..') {
            throw "Entrada ZIP insegura: $name"
        }
    }
    [void](New-Item -ItemType Directory -Path $validationDirectory)
    [IO.Compression.ZipFileExtensions]::ExtractToFile($archive.Entries[0], $validationExecutable)
    $signature = Get-AuthenticodeSignature -LiteralPath $validationExecutable
    if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
        $signature.SignerCertificate.Thumbprint -cne [string]$policy.signingCertificateThumbprint -or
        $signature.SignerCertificate.Subject -cne [string]$policy.expectedPublisherSubject) {
        throw 'The packaged executable has no valid Authenticode signature from the pinned publisher.'
    }
    $installLines = @([IO.File]::ReadAllLines((Join-Path $ReleaseDirectory 'install-manifest.sha256')) | Where-Object { $_.Length -gt 0 })
    if ($installLines.Count -ne 1 -or $installLines[0] -cnotmatch '^([0-9a-f]{64})\t([0-9]+)\tOmniGhost\.exe$') {
        throw 'The authenticated install manifest must describe exactly the single packaged executable.'
    }
    $installedHash = (Get-FileHash -LiteralPath $validationExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($installedHash -cne $Matches[1] -or (Get-Item -LiteralPath $validationExecutable).Length -ne [int64]$Matches[2]) {
        throw 'The signed installation manifest does not match the packaged executable.'
    }
}
finally {
    $archive.Dispose()
    Remove-Item -LiteralPath $validationExecutable -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $validationDirectory -PathType Container) { Remove-Item -LiteralPath $validationDirectory -Force }
}

$runtimeDir = Join-Path $ProjectDir 'build\Publish'
if (Test-Path -LiteralPath $runtimeDir -PathType Container) {
    & (Join-Path $ProjectDir 'tools\Audit-ReleaseDependencies.ps1') `
        -ProjectDir $ProjectDir `
        -RuntimeDir $runtimeDir `
        -SbomPath (Join-Path $ReleaseDirectory 'SBOM.cdx.json') `
        -Quiet:$Quiet
}
else {
    $sbom = Get-Content -LiteralPath (Join-Path $ReleaseDirectory 'SBOM.cdx.json') -Raw | ConvertFrom-Json
    if ([string]$sbom.bomFormat -cne 'CycloneDX' -or
        [string]$sbom.metadata.component.version -cne $Version) {
        throw 'O SBOM descarregado não corresponde à release.'
    }
}

if (-not $Quiet) {
    Write-Host "[OmniGhost Release] Artefactos $Version validados: ZIP, manifesto, SHA-256, caminhos e SBOM."
}
