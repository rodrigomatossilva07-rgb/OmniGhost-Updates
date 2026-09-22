[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$RuntimeDir,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$RuntimeDir = [IO.Path]::GetFullPath((Join-Path $RuntimeDir '.'))
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$versionPath = Join-Path $ProjectDir 'config\release\version.txt'

if (-not (Test-Path -LiteralPath $RuntimeDir -PathType Container)) {
    throw "Pasta runtime em falta para gerar o SBOM: $RuntimeDir"
}
if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    throw "Fonte de versão em falta: $versionPath"
}

$version = [IO.File]::ReadAllText($versionPath).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versão inválida para o SBOM: '$version'."
}

$runtimeFiles = @(
    Get-ChildItem -LiteralPath $RuntimeDir -Recurse -Force -File |
        Where-Object { $_.Extension.ToLowerInvariant() -in @('.exe', '.dll', '.sys') } |
        Sort-Object FullName
)
if (-not ($runtimeFiles | Where-Object Name -IEQ 'OmniGhost.exe')) {
    throw 'OmniGhost.exe não foi encontrado no runtime usado para gerar o SBOM.'
}

$components = New-Object System.Collections.Generic.List[object]
$dependencyRefs = New-Object System.Collections.Generic.List[string]
foreach ($file in $runtimeFiles) {
    $relative = $file.FullName.Substring($RuntimeDir.Length).TrimStart('\', '/').Replace('\', '/')
    $sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    $reference = "urn:omnighost:file:sha256:$sha256"
    $fileVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($file.FullName).FileVersion

    $component = [ordered]@{
        type = 'file'
        'bom-ref' = $reference
        name = $relative
        hashes = @([ordered]@{ alg = 'SHA-256'; content = $sha256 })
        properties = @(
            [ordered]@{ name = 'omnighost:relativePath'; value = $relative },
            [ordered]@{ name = 'omnighost:size'; value = [string]$file.Length }
        )
    }
    if (-not [string]::IsNullOrWhiteSpace($fileVersion)) {
        $component.version = $fileVersion
    }
    $components.Add($component)
    $dependencyRefs.Add($reference)
}

$appReference = "pkg:generic/omnighost@${version}?arch=x86_64"
$serialNumber = 'urn:uuid:' + ([guid]::NewGuid().ToString())
$componentArray = @($components | ForEach-Object { $_ })
$dependencyArray = @($dependencyRefs | ForEach-Object { $_ })
$bom = [ordered]@{
    bomFormat = 'CycloneDX'
    specVersion = '1.5'
    serialNumber = $serialNumber
    version = 1
    metadata = [ordered]@{
        timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        tools = @([ordered]@{
            vendor = 'OmniGhost'
            name = 'Generate-Sbom.ps1'
            version = '1.0.0'
        })
        component = [ordered]@{
            type = 'application'
            'bom-ref' = $appReference
            group = 'OmniGhost'
            name = 'OmniGhost'
            version = $version
            purl = $appReference
        }
        properties = @(
            [ordered]@{ name = 'omnighost:platform'; value = 'windows' },
            [ordered]@{ name = 'omnighost:architecture'; value = 'x64' }
        )
    }
    components = $componentArray
    dependencies = @([ordered]@{
        ref = $appReference
        dependsOn = $dependencyArray
    })
}

$outputDirectory = Split-Path -Parent $OutputPath
if ($outputDirectory) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
$utf8NoBom = New-Object Text.UTF8Encoding -ArgumentList $false
$json = ($bom | ConvertTo-Json -Depth 12) + [Environment]::NewLine
$null = $json | ConvertFrom-Json
[IO.File]::WriteAllText($OutputPath, $json, $utf8NoBom)
Write-Host "[OmniGhost SBOM] $($components.Count) binários inventariados em $OutputPath"
