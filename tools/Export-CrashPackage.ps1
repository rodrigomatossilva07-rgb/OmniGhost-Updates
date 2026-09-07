[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [Parameter(Mandatory = $true)][string]$DumpPath,
    [Parameter(Mandatory = $true)][switch]$IncludeMemoryDump,
    [string]$OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$DumpPath = [IO.Path]::GetFullPath($DumpPath)
$localData = [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'OmniGhost'))
$crashRoot = [IO.Path]::GetFullPath((Join-Path $localData 'crash-dumps')).TrimEnd('\') + '\'
if (-not $IncludeMemoryDump) { throw 'A inclusão do dump exige confirmação explícita com -IncludeMemoryDump.' }
if (-not $DumpPath.StartsWith($crashRoot, [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetExtension($DumpPath) -cne '.dmp' -or
    -not (Test-Path -LiteralPath $DumpPath -PathType Leaf)) {
    throw 'Seleciona um .dmp existente dentro de %LOCALAPPDATA%\OmniGhost\crash-dumps.'
}
$dump = Get-Item -LiteralPath $DumpPath
if ($dump.Length -le 0 -or $dump.Length -gt 512MB) { throw 'O dump está vazio ou excede o limite de 512 MiB.' }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $localData 'support' }
$OutputDirectory = [IO.Path]::GetFullPath((Join-Path $OutputDirectory '.'))
[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$stage = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost-Crash-' + [guid]::NewGuid().ToString('N'))
try {
    [IO.Directory]::CreateDirectory($stage) | Out-Null
    Copy-Item -LiteralPath $DumpPath -Destination (Join-Path $stage $dump.Name) -Force
    $version = ([IO.File]::ReadAllText((Join-Path $ProjectDir 'version.txt'))).Trim()
    $metadata = [ordered]@{
        schema = 1
        appVersion = $version
        dumpFile = $dump.Name
        dumpSha256 = (Get-FileHash -LiteralPath $DumpPath -Algorithm SHA256).Hash.ToLowerInvariant()
        dumpSize = $dump.Length
        exportedUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        privacyNotice = 'A memory dump can contain user data. Share only after explicit review and consent.'
    }
    $buildMetadata = Join-Path $ProjectDir '.cache\generated\Publish\x64\build-metadata.json'
    if (-not (Test-Path -LiteralPath $buildMetadata -PathType Leaf)) {
        $buildMetadata = Join-Path $ProjectDir '.cache\build-metadata.json'
    }
    if (Test-Path -LiteralPath $buildMetadata -PathType Leaf) {
        $metadata.build = Get-Content -LiteralPath $buildMetadata -Raw | ConvertFrom-Json
    }
    $metadata | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $stage 'crash-metadata.json') -Encoding UTF8
    $output = Join-Path $OutputDirectory ("OmniGhost-Crash-$version-$((Get-Date).ToString('yyyyMMdd-HHmmss')).zip")
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $output -CompressionLevel Optimal -Force
    if (-not (Test-Path -LiteralPath $output -PathType Leaf) -or (Get-Item $output).Length -le 0) {
        throw 'O pacote de crash não foi criado corretamente.'
    }
    Write-Host "[OmniGhost Crash] Pacote criado com consentimento explícito: $output"
}
finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
