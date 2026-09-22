[CmdletBinding()]
param(
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [ValidateSet('Release','Publish','Diagnostics')][string]$Configuration = 'Publish',
    [string]$OutputDir,
    [string]$Destination
)
$ErrorActionPreference = "Stop"
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if (-not $OutputDir) { $OutputDir = Join-Path $ProjectDir ".cache\symbols\$Configuration" }
$versionPath = Join-Path $ProjectDir 'version.txt'
$version = if (Test-Path -LiteralPath $versionPath -PathType Leaf) {
    ([IO.File]::ReadAllText($versionPath)).Trim()
} else {
    'unknown'
}
if ($version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { $version = 'unknown' }
if (-not $Destination) {
    $Destination = Join-Path $ProjectDir ("artifacts\symbols\{0}\OmniGhost-symbols-{0}.zip" -f $version)
}
$symbols = @(Get-ChildItem -LiteralPath $OutputDir -File -Filter *.pdb -ErrorAction SilentlyContinue)
if ($symbols.Count -eq 0) { throw "No private $Configuration PDB files found in $OutputDir. Build $Configuration|x64 first." }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Destination) | Out-Null
if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
Compress-Archive -LiteralPath $symbols.FullName -DestinationPath $Destination -CompressionLevel Optimal
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $Destination).Hash.ToLowerInvariant()
Write-Host "Symbols package (private build artifact): $Destination" -ForegroundColor Green
Write-Host "Version: $version SHA256: $sha"
