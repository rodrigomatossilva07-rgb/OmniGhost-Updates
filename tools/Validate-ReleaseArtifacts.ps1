[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [Parameter(Mandatory = $true)][string]$ReleaseDirectory,
    [Parameter(Mandatory = $true)][string]$Version
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$ReleaseDirectory = [IO.Path]::GetFullPath((Join-Path $ReleaseDirectory '.'))
if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versão OmniGhost inválida: '$Version'."
}

$required = @(
    'OmniGhost.zip', 'update.json', 'install-manifest.sha256',
    'SBOM.cdx.json', 'changelog.json', 'release-notes.md', 'release-meta.json'
)
foreach ($name in $required) {
    $path = Join-Path $ReleaseDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -le 0) {
        throw "Artefacto obrigatório ausente ou vazio: $name"
    }
}

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
try {
    $names = @($archive.Entries | ForEach-Object { $_.FullName.Replace('\','/') })
    if ('OmniGhost.exe' -notin $names) { throw 'OmniGhost.exe não existe na raiz do ZIP.' }
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
}
finally { $archive.Dispose() }

$runtimeDir = Join-Path $ProjectDir 'build\Publish'
if (Test-Path -LiteralPath $runtimeDir -PathType Container) {
    & (Join-Path $ProjectDir 'tools\Audit-ReleaseDependencies.ps1') `
        -ProjectDir $ProjectDir `
        -RuntimeDir $runtimeDir `
        -SbomPath (Join-Path $ReleaseDirectory 'SBOM.cdx.json')
}
else {
    $sbom = Get-Content -LiteralPath (Join-Path $ReleaseDirectory 'SBOM.cdx.json') -Raw | ConvertFrom-Json
    if ([string]$sbom.bomFormat -cne 'CycloneDX' -or
        [string]$sbom.metadata.component.version -cne $Version) {
        throw 'O SBOM descarregado não corresponde à release.'
    }
}

Write-Host "[OmniGhost Release] Artefactos $Version validados: ZIP, manifesto, SHA-256, caminhos e SBOM."
